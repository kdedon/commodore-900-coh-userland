#!/usr/bin/env python3
"""tickrate-test.py -- is the simulator's tick in proportion to its CPU?

    python3 tickrate-test.py [dist]

`tickrate' counts CPU work between two of the guest's OWN clock samples, so
the number it prints involves no host clock at all.  On a machine whose 100 Hz
tick and CPU are in their correct proportion, that number is a property of the
port -- so the instruction-level emulator and the simulator have to agree, and
where they do not, the difference is the emulation rather than COHERENT.

This is the measurement a wall-clock one cannot make: host slowdown and host
load both inflate wall time, so dividing one unknown by another is not a
measurement.

Emulator baseline: 156 kloop/guest-s, identical at 5 s and 10 s.  A LARGER
number here means the tick is slow relative to the CPU: more work fitted into
what the guest was told was one second.  A number near 156 means the tick is
honest and the simulator is simply slow, which would retire the question.

No network, no paced input -- boot and three runs.
"""
import sys

from hunt_common import Player

try:
    sys.stdout.reconfigure(line_buffering=True)
except AttributeError:
    pass

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"

EMULATOR_BASELINE = 156		# kloop/guest-s under the instruction emulator


def main():
    with Player(DIST, "tickrate") as g:
        g.boot()
        for secs in (5, 5, 10):
            mark = len(g.text())
            g.run("/bin/tickrate %d" % secs, pause=2)
            if not g.done(mark, timeout=1800):
                print("    tickrate %d never finished" % secs)
        print("--- for comparison ---")
        print("  emulator: %d kloop/guest-s (5 s and 10 s agree)"
              % EMULATOR_BASELINE)
        return 0


if __name__ == "__main__":
    sys.exit(main())
