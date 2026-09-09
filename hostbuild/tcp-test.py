#!/usr/bin/env python3
"""tcp-test.py -- end-to-end TCP: a host echo server on the other end of the wire.

    python3 tcp-test.py [dist] [echoclient|devtcp]

Cold-boots a dist, brings the stack up, attaches `slip' to /dev/tty51, and then
serves TCP port 7 as 10.0.0.1 while the guest runs

    /bin/echoclient 10.0.0.1 7

echoclient is the libsocket exerciser: socket(), connect(), write(), read(),
close().  A pass therefore proves the whole client path -- the BSD veneer, the
daemon control channel, the TCP layer, IP, psip, slip and the serial line --
carries a real connection in both directions.

Unlike ICMP, which the stack answers from inside the daemon, this is the guest
INITIATING: the three-way handshake, the data segments and the close all come
from the C900, and the host only has to be a correct peer.  So the host end is a
real (if minimal) TCP: one connection, no retransmission of its own, but proper
sequence and acknowledgement numbers and checksums, and it drops anything that
does not check out rather than papering over it.

The wire, the framing and the boot sequence are slipwire.py.
"""
import sys
import time

from slipwire import (GUEST, PEER, Guest, SlipDecoder, Tcp, TCP_ACK, TCP_FIN,
                      TCP_PSH, TCP_RST, TCP_SYN, describe, echo_request, ip4,
                      is_echo_reply, send_packet, stack_setup, tcp_flagstr,
                      tcp_seg, typeline, wire_drain, wire_recv, wire_report,
                      slipstat_report, wire_hex)

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"
PORT = 7
ISN = 0x00010000

# Which guest client to run.  Two of them reach the same stack by different
# APIs, and one host-side TCP serves both: `echoclient' calls socket()/connect()
# (the BSD veneer), `devtcp' opens /dev/tcp and drives NWIO* ioctls (the shim the
# Minix net clients use).  Whichever passes, the transport underneath is the same
# -- so a disagreement between them is in the library, not on the wire.
CLIENTS = {
    "echoclient": (b"hello\n", "echo: hello"),
    "devtcp":     (b"devtcp-hello\n", "PASS devtcp:"),
}
CLIENT = sys.argv[2] if len(sys.argv) > 2 else "echoclient"
if CLIENT not in CLIENTS:
    sys.exit("tcp-test.py: unknown client %r (have %s)"
             % (CLIENT, ", ".join(sorted(CLIENTS))))
MESSAGE, VERDICT = CLIENTS[CLIENT]


class TcpEcho(object):
    """A one-connection TCP echo server: whatever arrives is sent straight back.

    Returns the packets to put on the wire for each packet fed in, so the caller
    keeps control of pacing (the wire is one byte at a time -- see slipwire)."""

    def __init__(self, port):
        self.port = port
        self.state = "LISTEN"
        self.peer_port = 0
        self.snd_nxt = ISN
        self.rcv_nxt = 0
        self.got = b""
        self.echoed = b""
        self.log = []

    def note(self, s):
        self.log.append(s)

    def seg(self, flags, payload=b""):
        p = tcp_seg(self.port, self.peer_port, self.snd_nxt, self.rcv_nxt,
                    flags, payload)
        self.note("  host -> guest %s seq %u ack %u len %d"
                  % (tcp_flagstr(flags), self.snd_nxt, self.rcv_nxt,
                     len(payload)))
        return p

    def input(self, pkt):
        if len(pkt) < 40 or pkt[9] != 6:
            return []
        if bytes(pkt[12:16]) != ip4(GUEST) or bytes(pkt[16:20]) != ip4(PEER):
            return []
        t = Tcp(pkt)
        if t.dport != self.port:
            return []
        if not t.cksum_ok:
            self.note("  dropped: bad TCP checksum")
            return []
        self.note("  guest -> host %s seq %u ack %u len %d"
                  % (tcp_flagstr(t.flags), t.seq, t.ack, len(t.data)))

        if t.flags & TCP_RST:
            self.state = "RESET"
            return []

        if self.state == "LISTEN":
            if not (t.flags & TCP_SYN):
                return []
            self.peer_port = t.sport
            self.rcv_nxt = (t.seq + 1) & 0xFFFFFFFF
            out = [self.seg(TCP_SYN | TCP_ACK)]
            self.snd_nxt = (self.snd_nxt + 1) & 0xFFFFFFFF
            self.state = "SYN_RCVD"
            return out

        if t.sport != self.peer_port:
            return []

        if self.state == "SYN_RCVD":
            if t.flags & TCP_ACK:
                self.state = "ESTABLISHED"
            else:
                return []

        out = []
        # Data.  Only in-order data is taken; a retransmission of something
        # already accepted is answered with a duplicate ack, not echoed twice.
        if t.data:
            if t.seq == self.rcv_nxt:
                self.got += t.data
                self.rcv_nxt = (self.rcv_nxt + len(t.data)) & 0xFFFFFFFF
                self.echoed += t.data
                out.append(self.seg(TCP_PSH | TCP_ACK, t.data))
                self.snd_nxt = (self.snd_nxt + len(t.data)) & 0xFFFFFFFF
            else:
                self.note("  out of order (want %u, got %u): bare ack"
                          % (self.rcv_nxt, t.seq))
                out.append(self.seg(TCP_ACK))

        if t.flags & TCP_FIN and self.state in ("ESTABLISHED", "SYN_RCVD"):
            self.rcv_nxt = (self.rcv_nxt + 1) & 0xFFFFFFFF
            out.append(self.seg(TCP_FIN | TCP_ACK))
            self.snd_nxt = (self.snd_nxt + 1) & 0xFFFFFFFF
            self.state = "LAST_ACK"
        elif self.state == "LAST_ACK" and t.flags & TCP_ACK:
            self.state = "CLOSED"
        return out


def main():
    with Guest(DIST, "tcp") as g:
        g.boot()
        # No psipping control here: the ICMP path is slip-test.py's job, and
        # running it would leave a second reader on the psip interface.
        g.setup(stack_setup(control=False))

        dec = SlipDecoder()
        wire_drain()                    # discard anything buffered
        srv = TcpEcho(PORT)
        seen = []
        print("--- the wire, before anything is sent ---")
        wire_report("after slip start")
        slipstat_report("after slip start")

        mark = len(g.text())
        # In the BACKGROUND, so the shell stays usable.  Run in the foreground
        # it blocks in connect(), and then everything typed afterwards --
        # including the `sync' that commits slip's trace to the disk -- goes to
        # the client's stdin instead of the shell.  The first run of this test
        # lost its trace exactly that way.
        typeline("/bin/%s %s %d &" % (CLIENT, PEER, PORT))

        # Serve the connection.  The guest drives; the host answers whatever
        # arrives and watches the console for the client's own verdict.
        #
        # The host retransmits its last segment when the guest goes quiet.  A
        # real TCP does this anyway, but here it also separates two explanations
        # that look identical from the outside: a reply the guest never received,
        # and a reply it received and rejected.  If a resend gets the exchange
        # moving, the first frame was lost on the wire; if four resends change
        # nothing, the guest is dropping what it gets.
        deadline = time.time() + 300
        last_out, last_out_at, resends = None, 0.0, 0
        done = False
        while time.time() < deadline and not done:
            data = wire_drain()
            for pkt in dec.feed(data):
                seen.append(describe(pkt))
                for reply in srv.input(pkt):
                    wire_report("before send")
                    t0 = time.time()
                    send_packet(reply)
                    srv.note("  (that send took %.0fs)" % (time.time() - t0))
                    wire_report("after send")
                    slipstat_report("after send")
                    last_out, last_out_at, resends = reply, time.time(), 0
            g.pump()
            out = g.text()[mark:]
            if "echo: " in out or "failed" in out:
                # Let the close finish before tearing the wire down.
                if srv.state not in ("CLOSED", "LAST_ACK"):
                    time.sleep(3)
                    continue
                done = True
            now = time.time()
            if (last_out is not None and now - last_out_at > 20 and resends < 4
                    and srv.state not in ("CLOSED", "LAST_ACK")):
                resends += 1
                srv.note("  (no answer for 20s: resend #%d)" % resends)
                print("no answer for 20s in state %s: resending (#%d)"
                      % (srv.state, resends))
                send_packet(last_out)
                last_out_at = time.time()
            if not data:
                time.sleep(1)

        g.pump()
        out = g.text()[mark:]

        # Is the inbound path still alive at all?  An ICMP echo goes down the
        # same wire, through the same slip, into the same stack, but stops at
        # the icmp layer instead of tcp.  A reply here says the host's segments
        # ARE reaching the guest and TCP is dropping them; no reply says slip's
        # receive side stopped after it transmitted, and TCP never saw anything.
        # Without this the two are indistinguishable, and they need opposite
        # fixes.
        wire_report("at the end")
        slipstat_report("at the end")
        print("--- is the inbound path alive? (ICMP over the same wire) ---")
        send_packet(echo_request(99))
        icmp_ok = False
        for _ in range(20):
            time.sleep(1)
            for pkt in dec.feed(wire_drain()):
                seen.append(describe(pkt))
                if is_echo_reply(pkt, 99):
                    icmp_ok = True
            if icmp_ok:
                break
        print("    ICMP echo after the TCP attempt: %s"
              % ("REPLIED -- inbound works, TCP dropped the segment"
                 if icmp_ok else "no reply -- the inbound path is dead"))

        print("--- the first bytes the guest put on the wire ---")
        print("    " + wire_hex())
        print("--- every frame the guest sent ---")
        for d in seen:
            print("  " + d)
        print("--- the conversation ---")
        for l in srv.log:
            print(l)
        print("--- guest console (everything since the client started) ---")
        print("\n".join(out.splitlines()[-30:]))

        # slip's own count of what it decoded off the wire.  This is the
        # difference between "the host's reply never reached the guest" and
        # "it reached slip, which injected it, and the stack dropped it" --
        # from outside, those two look exactly the same.
        print("--- slip trace (from the image) ---")
        for l in g.read_file("/slip.trace", lines=40):
            print("    " + l)

        echoed_ok = srv.echoed == MESSAGE
        console_ok = VERDICT in out
        closed_ok = srv.state in ("LAST_ACK", "CLOSED")
        print("host received %r from the guest (%s)"
              % (srv.echoed, "as sent" if echoed_ok else "EXPECTED %r" % MESSAGE))
        print("guest printed %r: %s" % (VERDICT, "yes" if console_ok else "NO"))
        print("connection closed cleanly:    %s (state %s)"
              % ("yes" if closed_ok else "NO", srv.state))

        ok = echoed_ok and console_ok and closed_ok
        print("=== TCP (%s): %s" % (CLIENT, "PASS" if ok else "FAIL"))
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
