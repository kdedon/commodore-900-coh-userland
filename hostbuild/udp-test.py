#!/usr/bin/env python3
"""udp-test.py -- end-to-end UDP: datagrams across the serial line.

    python3 udp-test.py [dist]

Cold-boots a dist, brings the stack up, attaches `slip' to /dev/tty51, runs

    /bin/udpserver 7 &

on the guest, and then sends it UDP datagrams as 10.0.0.1 and checks they come
back.  Loopback (net/test/udpecho.c, both ends on the guest) does not cover
this; it is the same path across the wire, which is what hunt(6) needs.

The HOST speaks first, unlike tcp-test.py.  UDP retransmits nothing, so a
datagram the serial line drops is gone -- and the end that can say it again must
be the one that starts.  A guest blocked in recvfrom() cannot.  So the host
resends on a timer until an echo comes back, and reports how many attempts it
took: one is a clean line, four is a lossy one, and neither is a broken stack.

Two things are checked beyond "bytes arrived":

  * the echo returns to the port the datagram came FROM, which is only possible
    if recvfrom() on the guest reported the sender correctly -- the whole point
    of per-datagram addressing; and
  * the guest's own console line naming that sender agrees with the host.

The wire, the framing and the boot sequence are slipwire.py.
"""
import sys
import time

from slipwire import (GUEST, PEER, Guest, SlipDecoder, Udp, describe,
                      echo_request, ip4, is_echo_reply, send_packet,
                      slipstat_report, stack_setup, typeline, udp_dgram,
                      wire_drain, wire_hex, wire_report)

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"
PORT = 7                        # the guest's udpserver
HOST_PORT = 7100                # ours, which the echo must come back to
MESSAGE = b"udp-over-slip"
TRIES = 4
QUIET = 25.0                    # seconds of silence before resending


def main():
    with Guest(DIST, "udp") as g:
        g.boot()
        # No psipping control: it would leave a second reader on the psip
        # interface, and the ICMP probe at the end covers the same ground.
        g.setup(stack_setup(control=False))

        dec = SlipDecoder()
        wire_drain()                    # discard anything buffered
        seen, log = [], []

        mark = len(g.text())
        # Background, so the shell stays usable: run in the foreground it blocks
        # in recvfrom() and everything typed afterwards -- including the `sync'
        # that commits slip's trace -- would go to its stdin instead.
        typeline("/bin/udpserver %d 4 &" % PORT)
        for _ in range(20):
            time.sleep(1)
            g.pump()
            if "udpserver: listening" in g.text()[mark:]:
                break
        if "udpserver: listening" not in g.text()[mark:]:
            # Not fatal: it may simply be slow to print.  Say so rather than
            # letting a later silence be blamed on the wire.
            print("NOTE: udpserver has not said it is listening yet")

        print("--- the wire, before anything is sent ---")
        wire_report("after slip start")
        slipstat_report("after slip start")

        echoed, attempts = None, 0
        deadline = time.time() + 420
        last_at = 0.0
        while time.time() < deadline and echoed is None:
            if time.time() - last_at > QUIET:
                if attempts >= TRIES:
                    break
                attempts += 1
                log.append("  host -> guest udp %d->%d len %d (attempt %d)"
                           % (HOST_PORT, PORT, len(MESSAGE), attempts))
                print("sending datagram, attempt %d" % attempts)
                t0 = time.time()
                send_packet(udp_dgram(HOST_PORT, PORT, MESSAGE))
                log.append("  (that send took %.0fs)" % (time.time() - t0))
                wire_report("after send")
                last_at = time.time()

            for pkt in dec.feed(wire_drain()):
                seen.append(describe(pkt))
                if pkt[9] != 17 or len(pkt) < 28:
                    continue
                if bytes(pkt[12:16]) != ip4(GUEST) \
                        or bytes(pkt[16:20]) != ip4(PEER):
                    continue
                u = Udp(pkt)
                log.append("  guest -> host udp %d->%d len %d%s"
                           % (u.sport, u.dport, len(u.data),
                              " TRUNCATED" if u.truncated else ""))
                if not u.cksum_ok:
                    log.append("  dropped: bad UDP checksum")
                    continue
                if u.dport != HOST_PORT:
                    log.append("  ignored: not addressed to our port %d"
                               % HOST_PORT)
                    continue
                echoed = u
                break
            g.pump()
            time.sleep(1)

        g.pump()
        out = g.text()[mark:]

        # Is the inbound path still alive?  An ICMP echo goes down the same
        # wire, through the same slip, into the same stack, but stops at the
        # icmp layer instead of udp.  A reply says the datagrams ARE arriving
        # and UDP dropped them; no reply says nothing reached the guest at all.
        # Those need opposite fixes and are otherwise indistinguishable.
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
        print("    ICMP echo after the UDP attempt: %s"
              % ("REPLIED -- inbound works, so UDP dropped the datagram"
                 if icmp_ok else "no reply -- the inbound path is dead"))

        print("--- the first bytes the guest put on the wire ---")
        print("    " + wire_hex())
        print("--- every frame the guest sent ---")
        for d in seen:
            print("  " + d)
        print("--- the conversation ---")
        for l in log:
            print(l)
        print("--- guest console (everything since udpserver started) ---")
        print("\n".join(out.splitlines()[-30:]))

        data_ok = echoed is not None and echoed.data == MESSAGE
        # The guest sends its echo back to whatever recvfrom() reported, so the
        # source port landing on ours is the addressing check, not a formality.
        port_ok = echoed is not None and echoed.sport == PORT
        console_ok = "from %s port %d" % (PEER, HOST_PORT) in out

        print("host got back %r (%s)"
              % (echoed.data if echoed else None,
                 "as sent" if data_ok else "EXPECTED %r" % MESSAGE))
        print("echo came from the guest's port %d:  %s"
              % (PORT, "yes" if port_ok else "NO"))
        print("guest named the right sender:        %s"
              % ("yes" if console_ok else "NO"))
        print("attempts needed:                     %d" % attempts)

        ok = data_ok and port_ok and console_ok
        print("=== UDP over the wire: %s" % ("PASS" if ok else "FAIL"))
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
