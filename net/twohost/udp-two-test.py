"""udp-two-test.py -- does a UDP datagram cross the wire between two C900s?

    python3 udp-two-test.py [--cut] [--image PATH] [--keep]

WHY THIS EXISTS SEPARATELY FROM talk-test.py.  `twohost.py' proves ICMP and TCP
between two machines and nothing else; every UDP test in the tree is either
loopback on one machine (`udpecho', `udppoll', `sntpsrv') or has the HOST's
Python at the far end (a host peer over slipwire).  So when the talk daemon's first
two-machine run reported `[No response to are ring]' -- the client sent an
ANNOUNCE and no reply came back -- there was no way to tell a defect in the
daemon from a UDP path that has never carried a datagram between two guests.

This is that missing step, and it uses programs that were already in the tree
rather than new ones: B runs `/bin/sntpsrv', which answers 123/udp with a
KNOWN timestamp, and A runs `/etc/sntp' against B.  A pass is A's clock being
set from B, which can only happen if a request datagram crossed the wire and a
reply came back.  It is deliberately not ntalk: if this fails, the talk service is
not the
suspect.

AND IT SENDS AFTER A RECONFIGURATION, deliberately.  FINDINGS T-51: udp and tcp
each took a copy of the interface address when their port started and never
re-read it, so an `ifconfig' on a running machine left the UDP send path
computing its pseudo-header checksum over an address the datagram did not carry
-- BAD-UDP-CKSUM on the wire, and every datagram dropped by the far end.  The
run therefore renumbers BOTH machines away and back again before a single
datagram is sent (reconfigure()), which puts a stale copy on both ends if one is
being kept: A's request is checksummed by A and checked by B, and B's reply by B
and checked by A, so the one criterion below catches it at either end.

WHAT THIS DOES NOT COVER.  The other consumer of that address decides
NWUO_EN_LOC versus NWUO_EN_BROAD for an ARRIVING datagram, and sntp and sntpsrv
cannot see it: libsocket asks for NWUO_EN_LOC|NWUO_EN_BROAD, so it is handed the
datagram either way.  Only a program that asks for NWUO_DI_BROAD notices, and
`talk' is the one that does -- talk-test.py is where that consumer is measured.
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import twohost as T                                           # noqa: E402
from talk_common import readdress, setup                      # noqa: E402

# A scratch address per guest, used only to move off the real one and back.  On
# the same /24 as the real addresses, so the route and the netmask the machine
# ends with are the ones it started with and this test changes one thing.
SCRATCH = {"A": "10.0.0.11", "B": "10.0.0.12"}


def reconfigure(g, addr):
    """Renumber a running guest away from `addr' and back to it.

    Away and back, rather than to a new address and left there, so that the rest
    of the run addresses the machines by the names /etc/hosts already holds and
    a failure cannot be a wrong destination.  What it leaves behind is a stack
    that has been reconfigured twice since its ports started -- which is the
    whole condition under test.
    """
    return (readdress(g, SCRATCH[g.name], T.MASK) and
            readdress(g, addr, T.MASK))


def run(cut, image, keep):
    st = setup("udp", image, cut, keep)
    if not isinstance(st, dict):
        return st
    A, B, work, wire, guests, why = (st["A"], st["B"], st["work"], st["wire"],
                                     st["guests"], st["why"])
    try:
        # Renumber both machines on a running stack, before anything is sent.
        # ICMP has already crossed the wire by here (setup()), so a failure after
        # this point is about the reconfiguration and not about the link.
        for g, addr in ((A, T.ADDR_A), (B, T.ADDR_B)):
            if not reconfigure(g, addr):
                why.append("%s could not be renumbered on a running stack" %
                           g.name)
        if why:
            return T.report(False, why, work, wire, guests, keep)

        # B answers three requests rather than one: sntp is started with -r 2,
        # so a lost first datagram costs a retry and not the run.
        B.line("/bin/sntpsrv 3 &")
        B.expect("# ", 120, "(sntpsrv start)")
        time.sleep(5)

        m = A.mark()
        A.line("/etc/sntp -t 10 -r 3 %s" % T.ADDR_B)
        A.expect("# ", 300, "(sntp finished)")
        out = A.since(m)
        T.say("A: sntp ->\n%s" % out[-800:])
        mB = B.mark()
        B.pump(10)
        T.say("B: sntpsrv ->\n%s" % B.since(mB)[-800:])

        # The criterion is one of sntp's own success lines and NOT the absence of
        # an error: the first version of this test asked for `sntp:' in the
        # output and passed on `sntp: no server answered', which is the failure
        # spelled with the string the test was looking for.  `says' comes from
        # `%s: %s says %s' (sntp.c:382) and is printed only after a reply has
        # been parsed, checked and accepted.
        ok = (" says " in out) and ("no server answered" not in out)
        if not ok:
            why.append("no SNTP reply from %s after both machines were "
                       "renumbered -- either UDP does not cross the wire at "
                       "all, or a reconfigured stack is still building "
                       "pseudo-header checksums out of an address it no longer "
                       "has (FINDINGS T-51; the wire log says BAD-UDP-CKSUM "
                       "when it is the second)" % T.ADDR_B)
        return T.report(ok, why, work, wire, guests, keep)
    finally:
        for g in guests.values():
            g.stop()


def main(argv):
    opts = [a for a in argv[1:] if a.startswith("--")]
    image = None
    for o in opts:
        if o.startswith("--image="):
            image = o.split("=", 1)[1]
    return run("--cut" in opts, image, "--keep" in opts)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
