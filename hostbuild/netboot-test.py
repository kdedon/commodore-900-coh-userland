#!/usr/bin/env python3
"""netboot-test.py -- does the network come up by itself at boot?

    python3 netboot-test.py [dist]

Cold-boots a dist, takes it to multi-user with a Ctrl-D, and then pings it.
Nothing is typed at the guest: if /etc/rc.net did its job, the rendezvous pipe
exists, the inet daemon is running, ip0 has an address and slip owns /dev/tty51 -- and an
ICMP Echo Request put on the wire comes back.

That is the whole point of the test.  Every other net harness here brings the
stack up by typing the commands, which proves the software works but says
nothing about whether a booted machine is on the network.  This one only watches
the wire, so it can only pass if the boot did the work.
"""
import sys
import time

from slipwire import (Guest, SlipDecoder, describe, echo_request,
                      is_echo_reply, send_packet, wire_drain, wire_recv)

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"


def main():
    with Guest(DIST, "netboot") as g:
        g.boot()
        g.multiuser()

        dec = SlipDecoder()
        wire_drain()                    # discard anything buffered
        seen, passed = [], 0

        # the inet daemon is a 143 KB binary initialising ten protocol layers on a 6 MHz
        # machine, and rc.net starts it in the background, so the wire is silent
        # for minutes after the login prompt appears.  Ping patiently rather than
        # concluding early: a retry costs one packet, and giving up too soon
        # would report "the boot does not configure the network" for a boot that
        # merely had not finished.
        for seq in range(1, 9):
            send_packet(echo_request(seq))
            got = None
            for _ in range(40):
                time.sleep(1)
                for pkt in dec.feed(wire_drain()):
                    seen.append(describe(pkt))
                    if is_echo_reply(pkt, seq):
                        got = pkt
                if got:
                    break
            if got:
                passed += 1
                print("seq %d: %s  ECHO-REPLY-OK" % (seq, describe(got)))
                if passed == 3:
                    break
            else:
                print("seq %d: no reply (stack still starting?)" % seq)

        if seen:
            print("--- every frame the guest sent ---")
            for d in seen:
                print("  " + d)
        else:
            print("--- the guest sent no SLIP frames at all ---")

        g.pump()
        print("--- guest console ---")
        print("\n".join(g.text().splitlines()[-12:]))

        print("--- /tmp/rc.net.log (what the boot actually did) ---")
        for l in g.rcnetlog():
            print("    " + l)

        print("=== boot-time network: %d/3 replies %s"
              % (passed, "PASS" if passed >= 3 else "FAIL"))
        return 0 if passed >= 3 else 1


if __name__ == "__main__":
    sys.exit(main())
