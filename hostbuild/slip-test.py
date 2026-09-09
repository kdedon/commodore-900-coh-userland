#!/usr/bin/env python3
"""slip-test.py -- end-to-end SLIP: a host IP peer on the other end of the wire.

    python3 slip-test.py [dist]

Cold-boots a dist, brings the stack up inside the guest, starts `slip' on
/dev/tty51 (SCC 0 channel A), and then acts as the machine on the far end of
that serial line -- 10.0.0.1 sending ICMP Echo Requests to the guest's 10.0.0.2
and checking the replies.

The wire, the framing, the ICMP and the boot sequence are slipwire.py; this file
is the test itself.  For the same path exercised by a machine that configured
ITSELF at boot, see netboot-test.py.
"""
import sys
import time

from slipwire import (Guest, SlipDecoder, describe, echo_request,
                      is_echo_reply, send_packet, stack_setup, wire_drain,
                      wire_recv)

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"


def main():
    with Guest(DIST, "slip") as g:
        g.boot()
        g.setup(stack_setup())

        # ---- act as 10.0.0.1 ----
        dec = SlipDecoder()
        wire_drain()                    # discard anything buffered
        passed, seen = 0, []
        for seq in (1, 2, 3):
            send_packet(echo_request(seq))
            got = None
            for _ in range(15):
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
            else:
                print("seq %d: no reply" % seq)

        if seen:
            print("--- every frame the guest sent ---")
            for d in seen:
                print("  " + d)
        else:
            print("--- the guest sent no SLIP frames at all ---")
            g.pump()
            print("\n".join(g.text().splitlines()[-8:]))

        print("--- slip trace (from the image) ---")
        for l in g.read_file("/slip.trace"):
            print("    " + l)

        print("=== SLIP: %d/3 replies %s"
              % (passed, "PASS" if passed == 3 else "FAIL"))
        return 0 if passed == 3 else 1


if __name__ == "__main__":
    sys.exit(main())
