#!/usr/bin/env python3
"""disco-test.py -- time hunt's driver discovery on the SIMULATOR.

    python3 disco-test.py [dist]

hunt looks for a game driver by sending one UDP datagram and waiting for the
reply with a single one-second poll.  Under the instruction-level emulator the
answer lands in the first second every time; on the simulator `hunt -S' finds
nothing at all, six probes running.  Those are the same binaries and the same
commands, so the question is whether the simulator is merely slower than that
one-second window -- which is a fact about the machine, not a bug in hunt.

`discotime' is the measurement, and it is the same binary in both places, so
the two numbers are directly comparable.  This is the short harness that runs
it here: boot, bring up the stack, start the driver, measure three times.

Cheap on purpose -- it needs no game, no curses and no paced input, so it is
about ten minutes rather than the half hour hunt-test.py takes.
"""
import sys
import time

from hunt_common import Player, net_setup

try:
    sys.stdout.reconfigure(line_buffering=True)
except AttributeError:
    pass

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"

HUNTD = "/usr/games/lib/huntd"


VERDICTS = ("PASS -- inside", "SLOW -- hunt's", "no reply in")


def await_any(g, mark, timeout=600):
    """Wait for whichever verdict discotime reaches first.

    Sequentially -- expect(A) then expect(B) then expect(C) -- is wrong
    here.  discotime's window is 30 GUEST seconds, and a
    guest second on the simulator is several wall seconds, so a run that says
    "no reply" says it long after any per-marker timeout has expired; the
    harness then declares silence for a program that was still counting.  One
    loop, all three markers, one generous deadline.
    """
    for _ in range(timeout):
        time.sleep(1)
        g.pump()
        tail = g.text()[mark:]
        for v in VERDICTS:
            if v in tail:
                print("ok: %s" % v)
                return v
    return "(no verdict in %d s of wall clock)" % timeout


def main():
    with Player(DIST, "disco") as g:
        g.boot()
        g.setup(net_setup())
        g.run("%s &" % HUNTD, pause=10)

        # Three times, because the interesting answer may be "sometimes".  A
        # single slow reply could be the driver still starting; three of them
        # spread over a couple of minutes cannot be.
        seen = []
        for i in range(3):
            mark = len(g.text())
            g.run("/bin/discotime 30", pause=2)
            seen.append(await_any(g, mark))

        print("--- guest console ---")
        print("\n".join(g.text().splitlines()[-25:]))
        print("--- three measurements ---")
        for i, s in enumerate(seen):
            print("  %d: %s" % (i + 1, s))
        return 0


if __name__ == "__main__":
    sys.exit(main())
