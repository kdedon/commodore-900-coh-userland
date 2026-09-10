#!/usr/bin/env python3
"""ping-test.py -- does ping(1) on the guest get an answer back?

    python3 ping-test.py [dist]

Cold-boots a dist, brings the stack up, starts `slip' on /dev/tty51, and then
acts as 10.0.0.1: the machine at the far end of the serial line, ANSWERING the
guest's ICMP Echo Requests with Echo Replies.

That is the opposite direction from slip-test.py, and it is the direction that
tests this program.  slip-test has the host send the requests and the guest's
stack reply, which exercises the daemon's inbound path and nothing in userland.
Here the guest composes the packet, writes it to /dev/ip through libsocket's
shim, and has to recognise the answer -- so a pass says the raw-IP half of that
shim works in both directions, and a request that goes out but is never matched
to its reply fails rather than passing quietly.

The reply is built FROM the request that arrived: source and destination
swapped, type set to Echo Reply, both checksums recomputed, and everything else
-- identifier, sequence number, payload -- copied.  A responder that made up its
own identifier would be answering a question the guest did not ask, and ping is
required to ignore exactly that.

Then it pings an address on the same subnet that this responder does NOT claim,
which tests the other half of the program: the alarm that ends a read no reply
will ever complete.  Without it one lost packet hangs ping forever, and from the
console that is indistinguishable from a stack that stopped.

The wire, the framing, the boot and the console are hostbuild/slipwire.py.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "hostbuild"))

from slipwire import (Guest, SlipDecoder, cksum, describe,   # noqa: E402
                      ip4, send_packet, stack_setup, typeline, wire_drain)

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"

PEER = "10.0.0.1"
GUEST = "10.0.0.2"
# On the guest's subnet, so its packets take the same route out, but not an
# address this responder claims -- so they go unanswered.
SILENT = "10.0.0.5"
COUNT = 3
WAIT = 5

# How long to serve the wire before giving up, in seconds of host time.  The
# guest is far slower than the host, and ping's own timeout is COUNT * WAIT of
# the GUEST's seconds, so this has to be generous or the harness quits while the
# program it is testing is still running.
SERVE = 420


def echo_reply(pkt):
    """The Echo Reply to `pkt', or None if it is not an Echo Request for us."""
    if len(pkt) < 28 or pkt[9] != 1:
        return None
    hl = (pkt[0] & 0x0F) * 4
    if len(pkt) < hl + 8 or pkt[hl] != 8:
        return None
    if bytes(pkt[16:20]) != ip4(PEER):
        return None
    ip = bytearray(pkt[:hl])
    ic = bytearray(pkt[hl:])
    ip[12:16], ip[16:20] = pkt[16:20], pkt[12:16]
    ip[10:12] = b"\0\0"
    ip[10:12] = cksum(ip).to_bytes(2, "big")
    ic[0] = 0
    ic[2:4] = b"\0\0"
    ic[2:4] = cksum(ic).to_bytes(2, "big")
    return bytes(ip + ic)


def seq_of(pkt):
    hl = (pkt[0] & 0x0F) * 4
    return (pkt[hl + 6] << 8) | pkt[hl + 7]


def run_ping(g, dec, args):
    """Type a ping command and serve the wire until it prints its summary.

    Returns (what the guest printed, the sequence numbers answered)."""
    mark = len(g.text())
    typeline("/bin/ping %s" % args)
    answered, seen = [], []
    t = 0
    while t < SERVE:
        time.sleep(1)
        t += 1
        for pkt in dec.feed(wire_drain()):
            seen.append(describe(pkt))
            rep = echo_reply(pkt)
            if rep is None:
                continue
            answered.append(seq_of(pkt))
            print("    seq %d: request in, reply out" % seq_of(pkt))
            send_packet(rep)
        g.pump()
        if "packets transmitted" in g.text()[mark:]:
            break
    # The summary is two lines and the console arrives in pieces, so the last
    # of it is still in flight when the first line appears.  Reading here
    # truncated the count the verdict is taken from.
    for _ in range(5):
        time.sleep(1)
        g.pump()
    out = g.text()[mark:]
    for line in out.splitlines():
        if line.strip():
            print("    | " + line)
    if seen:
        for d in seen:
            print("    wire: " + d)
    else:
        print("    (the guest sent no SLIP frames at all)")
    return out, answered


def main():
    with Guest(DIST, "ping") as g:
        g.boot()
        g.setup(stack_setup())

        dec = SlipDecoder()
        wire_drain()                    # discard anything buffered

        # An address that answers.
        print("--- ping %s, which this host answers ---" % PEER)
        out, answered = run_ping(g, dec, "-c %d -w %d %s"
                                 % (COUNT, WAIT, PEER))
        # The guest's own count is the verdict: the host knows it answered, but
        # only the guest can say the answer arrived AND matched the request it
        # was waiting for.  Anything less is a request that went out.
        alive = ("%d packets received" % COUNT) in out and len(answered) == COUNT

        # An address on the same subnet that nothing answers.  The packets
        # leave by the same route; what is being tested is the other half of
        # the program -- the alarm that ends a read no reply will ever finish.
        # Without it a single lost packet is a hang, which from the console
        # looks the same as a stack that stopped.
        print("--- ping %s, which nothing answers ---" % SILENT)
        lost, ignored = run_ping(g, dec, "-c 1 -w 3 %s" % SILENT)
        times_out = ("no reply" in lost
                     and "0 packets received" in lost
                     and not ignored)

        print("=== ping: %d/%d answered, guest agrees %s; timeout %s -- %s"
              % (len(answered), COUNT, "yes" if alive else "NO",
                 "ok" if times_out else "NOT REACHED",
                 "PASS" if alive and times_out else "FAIL"))
        return 0 if alive and times_out else 1


if __name__ == "__main__":
    sys.exit(main())
