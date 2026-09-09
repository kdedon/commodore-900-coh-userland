"""peerfirst-test.py -- ONE guest, a host TCP peer, and the exchange talk(1) does.

    python3 peerfirst-test.py [--dist NAME] [--no-psh] [--keep] [--trace]

WHY THIS EXISTS.  talk(1)'s data phase is a three-byte write followed by a
three-byte read at BOTH ends at once (the edit characters), and it did not
complete: the bytes reached the far stack -- which ACKed them -- and never
reached the far program (FINDINGS T-52).  Every instrument that had been aimed
at that was two emulator guests, two ntalk services and a curses program, which is
about twenty minutes per answer and three layers between the question and the
evidence.

This is the same question with one guest and no talk daemon: the HOST is the peer, so
the whole exchange is visible as segments, and the host can be made to speak
FIRST -- which is what makes talk different from every other client here.
`echoclient' writes and then reads, so its peer's data always arrives while a
read is already outstanding.  talk's peer writes at the same moment, so the data
can be sitting in the stack's receive queue BEFORE the program asks for it, and
that ordering is what nothing else exercises.

    --peer-first (the default) the host sends its payload the instant the
                 connection is established, before the guest has written
                 anything -- talk's ordering.
    --guest-first the host waits for the guest's data and answers it -- every
                 other client's ordering, and the control: if this fails too,
                 the fault is not the ordering.

PASS is that the guest's `read' returns the host's payload, byte for byte, in
BOTH orderings.  The payload is a nonce generated per run and never typed at the
guest, so it cannot come from the harness echoing itself.

THE MUTATION CASE, and it needs no rebuild: --no-psh sends the same payload with
the PSH flag cleared.  This stack's `tcp_fd_read' only copies data out to a
short read when PSH is set, the connection is closing, or 4 KB have piled up --
so with PSH cleared a small segment must NOT be delivered, and a run that still
reports the nonce is not measuring delivery.  `make peerfirst-negative'.

The guest is not modified and no image is built: it boots a COPY of
hostbuild/build/<dist>.bin, brings its stack up the way twohost.py does, and
runs /bin/echoclient.  One boot, so this is minutes rather than tens of them.
"""
import os
import select
import shutil
import socket
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
OS = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(OS, "hostbuild"))

from slipwire import (GUEST, PEER, SlipDecoder, TCP_ACK, TCP_FIN,  # noqa: E402
                      TCP_PSH, TCP_RST, TCP_SYN, Tcp, ip4, slip_encode,
                      tcp_flagstr, tcp_seg)

import twohost                                                    # noqa: E402
from twohost import Guest, boot, ensure_stack, say                 # noqa: E402

PORT = 7
ISN = 0x00020000
# echoclient's message, so the guest end needs no argument and no new binary.
GUEST_MSG = b"hello\n"


class Peer(object):
    """A one-connection TCP peer on the guest's serial line, at 10.0.0.1.

    It is deliberately minimal -- no retransmission, no window management, one
    connection -- because everything it must get right is sequence numbers and
    checksums, and the thing under test is the guest's receive path.
    """

    def __init__(self, sockpath, payload, peer_first=True, psh=True,
                 trace=False, connect_after=0, locport=PORT):
        self.sockpath, self.payload = sockpath, payload
        self.peer_first, self.psh, self.trace = peer_first, psh, trace
        # --listen: WE open the connection, and not before `connect_after'
        # seconds have passed, so the guest's first listen is interrupted by its
        # own alarm.  Zero means the guest connects to us.
        self.connect_after, self.locport = connect_after, locport
        self.started = None
        self.dec = SlipDecoder()
        self.state = "LISTEN"
        self.peer_port = 0
        self.snd_nxt = ISN
        self.rcv_nxt = 0
        self.sent_payload = False
        self.got = b""              # what the guest sent us
        self.log = []
        self.stop = threading.Event()
        self.arm = threading.Event()        # the guest is listening: start the clock
        self.armed_at = 0.0
        self.conn = None

    def note(self, s):
        self.log.append(s)

    def send(self, pkt):
        self.conn.sendall(bytes(slip_encode(pkt)))

    def seg(self, flags, payload=b""):
        pkt = tcp_seg(PORT, self.peer_port, self.snd_nxt, self.rcv_nxt, flags,
                      payload)
        self.note("host -> guest %s seq %u ack %u len %d"
                  % (tcp_flagstr(flags), self.snd_nxt, self.rcv_nxt,
                     len(payload)))
        self.send(pkt)
        self.snd_nxt = (self.snd_nxt + len(payload)) & 0xFFFFFFFF

    def push_payload(self):
        """Our three bytes.  PSH unless the mutation cleared it."""
        if self.sent_payload:
            return
        self.sent_payload = True
        flags = TCP_ACK | (TCP_PSH if self.psh else 0)
        self.seg(flags, self.payload)
        self.note("host: payload sent%s" % ("" if self.psh else " WITHOUT PSH"))

    def initiate(self):
        """Open the connection ourselves -- the guest is the one listening.

        Deliberately LATE: the guest's first listen has to be ended by its own
        alarm, because an interrupted-and-abandoned listen is the thing under
        test.  If we knocked at once, the first listen would succeed and the run
        would prove nothing -- which the guest end reports as a FAIL rather than
        letting it pass.
        """
        self.peer_port = self.locport
        self.rcv_nxt = 0
        self.seg(TCP_SYN)
        self.snd_nxt = (self.snd_nxt + 1) & 0xFFFFFFFF
        self.state = "SYN_SENT"
        self.note("host: SYN sent to the guest's port %d" % self.locport)

    def input(self, pkt):
        if len(pkt) < 40 or pkt[9] != 6:
            return
        if bytes(pkt[12:16]) != ip4(GUEST) or bytes(pkt[16:20]) != ip4(PEER):
            return
        t = Tcp(pkt)
        if t.dport != PORT:
            return
        if not t.cksum_ok:
            self.note("dropped: bad TCP checksum")
            return
        self.note("guest -> host %s seq %u ack %u len %d"
                  % (tcp_flagstr(t.flags), t.seq, t.ack, len(t.data)))

        if t.flags & TCP_RST:
            self.state = "RESET"
            return

        if self.state == "SYN_SENT":
            if (t.flags & (TCP_SYN | TCP_ACK)) != (TCP_SYN | TCP_ACK):
                return
            self.rcv_nxt = (t.seq + 1) & 0xFFFFFFFF
            self.state = "ESTABLISHED"
            self.seg(TCP_ACK)
            if self.peer_first:
                self.push_payload()
            return

        if self.state == "LISTEN":
            if not (t.flags & TCP_SYN):
                return
            self.peer_port = t.sport
            self.rcv_nxt = (t.seq + 1) & 0xFFFFFFFF
            self.seg(TCP_SYN | TCP_ACK)
            self.snd_nxt = (self.snd_nxt + 1) & 0xFFFFFFFF
            self.state = "SYN_RCVD"
            return

        if t.sport != self.peer_port:
            return

        if self.state == "SYN_RCVD":
            if not (t.flags & TCP_ACK):
                return
            self.state = "ESTABLISHED"
            if self.peer_first:
                self.push_payload()

        if t.data and t.seq == self.rcv_nxt:
            self.got += t.data
            self.rcv_nxt = (self.rcv_nxt + len(t.data)) & 0xFFFFFFFF
            self.seg(TCP_ACK)
            if not self.peer_first:
                self.push_payload()
        elif t.data:
            # A retransmission or something out of order: acknowledge what we
            # do have rather than stay silent, which is what a real peer does.
            self.seg(TCP_ACK)

        if t.flags & TCP_FIN and t.seq == self.rcv_nxt:
            self.rcv_nxt = (self.rcv_nxt + 1) & 0xFFFFFFFF
            self.seg(TCP_ACK | TCP_FIN)
            self.snd_nxt = (self.snd_nxt + 1) & 0xFFFFFFFF
            self.state = "CLOSED"

    def serve(self):
        if os.path.exists(self.sockpath):
            os.unlink(self.sockpath)
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        srv.bind(self.sockpath)
        srv.listen(1)
        self.ready = True
        while not self.stop.is_set():
            r, _, _ = select.select([srv], [], [], 0.5)
            if r:
                self.conn, _ = srv.accept()
                break
        srv.close()
        if self.conn is None:
            return
        self.conn.setblocking(False)
        while not self.stop.is_set():
            r, _, _ = select.select([self.conn], [], [], 0.5)
            if (self.connect_after and self.state == "LISTEN"
                    and self.arm.is_set()
                    and time.time() - self.armed_at >= self.connect_after):
                self.initiate()
            if not r:
                continue
            try:
                data = self.conn.recv(4096)
            except OSError:
                break
            if not data:
                break
            if self.trace and data:
                self.note("wire rx %s" % data.hex())
            for pkt in self.dec.feed(data):
                try:
                    self.input(bytes(pkt))
                except Exception as e:        # a peer bug must not hang the run
                    self.note("peer error: %r" % e)


"""The guest's local port for --listen.  Any port the stack is not already
using; 4711 is what talk's dataport looks like (LP_SEL picks an ephemeral)."""
LISTEN_PORT = 4711
# The guest's alarm is 5 of ITS seconds and the emulator does not run at wall
# speed, so the host waits considerably longer before knocking.  If it knocked
# too early the guest's first listen would succeed and the guest reports that as
# a FAIL rather than passing on a run that tested nothing.
CONNECT_DELAY = 25


def run(dist, peer_first, psh, keep, trace, listen=False):
    work = os.path.join(os.environ.get("TMPDIR", "/tmp"),
                        "c900-peerfirst.%d" % os.getpid())
    os.makedirs(work)
    say("workdir %s" % work)

    src = twohost.dist_image(dist)
    if not src:
        return 2
    if not twohost.EMU or not os.path.exists(twohost.EMU):
        say("no emulator; see twohost.py for where it is looked for")
        return 2

    img = os.path.join(work, "G.bin")
    subprocess.call(["cp", "--reflink=auto", src, img])

    # The nonce is what proves delivery.  Three bytes, because three is the
    # length talk sends and the length nothing else here reads.
    nonce = ("%03d" % (os.getpid() % 1000)).encode()

    sock = os.path.join(work, "wire.sock")
    peer = Peer(sock, nonce, peer_first=peer_first, psh=psh, trace=trace,
                connect_after=CONNECT_DELAY if listen else 0,
                locport=LISTEN_PORT)
    th = threading.Thread(target=peer.serve, daemon=True)
    th.start()
    for _ in range(100):
        if os.path.exists(sock):
            break
        time.sleep(0.1)

    ok, why = False, []
    g = None
    try:
        g = Guest("G", img, sock, work, trace)
        if not boot(g):
            why.append("the guest did not reach a root shell")
        elif not ensure_stack(g, GUEST):
            why.append("the guest's network did not come up")
        elif listen:
            m = g.mark()
            # `sh -c ...; echo' so that a program which vanishes becomes a
            # number rather than a silence (T-52's own note).
            g.line("sh -c '/bin/devtcp %s %d listen; echo DEVTCP_EXIT=$?'"
                   % (PEER, LISTEN_PORT))
            peer.armed_at = time.time()
            peer.arm.set()
            got = g.expect("DEVTCP_EXIT=", 600, "(devtcp listen mode)")
            g.expect("# ", 120)
            out = g.since(m)
            say("--- the guest said ---\n%s" % out[-1200:])
            if not got:
                why.append("devtcp never finished -- the read did not return")
            elif "DEVTCP_EXIT=0" not in out:
                why.append("devtcp reported failure")
            elif "PASS devtcp-listen" not in out:
                why.append("devtcp exited 0 without its own PASS")
            elif nonce.decode() not in out:
                why.append("the guest read something other than the nonce %s"
                           % nonce.decode())
            if peer.got != b"abc":
                why.append("the host peer got %r, wanted %r" % (peer.got,
                                                               b"abc"))
            ok = not why
        else:
            m = g.mark()
            g.line("/bin/echoclient %s %d" % (PEER, PORT))
            got_read = g.expect("echoclient: read returned", 420,
                                "(the guest's read)")
            g.expect("# ", 120)
            out = g.since(m)
            say("--- the guest said ---\n%s" % out[-800:])
            if not got_read:
                why.append("the guest's read never returned -- delivery")
            elif nonce.decode() not in out:
                why.append("the guest's read returned without the nonce %s"
                           % nonce.decode())
            if peer.got != GUEST_MSG:
                why.append("the host peer got %r, wanted %r"
                           % (peer.got, GUEST_MSG))
            ok = not why
    finally:
        peer.stop.set()
        if g is not None:
            g.stop()
        th.join(timeout=5)

    say("--- the wire, as the host peer saw it ---")
    for l in peer.log[:40]:
        say("  " + l)
    for f in why:
        say("!! " + f)
    say("=== peerfirst (%s%s%s): %s"
        % ("guest listens, interrupted" if listen else
           ("peer-first" if peer_first else "guest-first"),
           "" if psh else ", NO PSH", "", "PASS" if ok else "FAIL"))
    if keep:
        say("(kept %s)" % work)
    else:
        shutil.rmtree(work, ignore_errors=True)
    return 0 if ok else 1


def main(argv):
    opts = [a for a in argv[1:] if a.startswith("--")]
    dist = "coherent3-full-test"
    for o in opts:
        if o.startswith("--dist="):
            dist = o.split("=", 1)[1]
    peer_first = "--guest-first" not in opts
    psh = "--no-psh" not in opts
    rc = run(dist, peer_first, psh, "--keep" in opts, "--trace" in opts,
             listen="--listen" in opts)
    if not psh:
        # The mutation passes when the test FAILS: a segment with no PSH must
        # not be delivered to a short read on this stack, so a run that still
        # saw the nonce was not measuring delivery at all.
        say("=== no-PSH control: %s"
            % ("PASS (the un-pushed segment was not delivered)" if rc
               else "FAIL (an un-pushed segment was delivered -- the "
                    "delivery assertion proves nothing)"))
        return 0 if rc else 1
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
