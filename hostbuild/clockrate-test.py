#!/usr/bin/env python3
"""clockrate-test.py -- how fast does the guest's clock run against ours?

    python3 clockrate-test.py [dist]

Every timing conclusion drawn from a simulator run rests on this number and I
had never measured it.  A `poll(1000)' that has not returned after five wall
minutes is a bug if the guest's second is a second, and is nothing at all if
the guest's second takes twenty of ours.  Those need opposite work, and from
outside they look identical.

So: time `sleep N' at the guest's own prompt, from here, with a wall clock.
The guest's sleep(1) is built on the same 100 Hz tick (lbolt) that drives
timeout() and therefore poll()'s timeout, so this measures exactly the clock
that the hunt investigation depends on -- not the instruction rate, which is a
different quantity and is the one boot time reflects.

A ratio near 1 means guest time is real time and a timeout that never fires is
a fault.  A large ratio means the tick is slow and long guest waits are simply
expensive, which would retire the whole question.
"""
import sys
import time

from hunt_common import Player

try:
    sys.stdout.reconfigure(line_buffering=True)
except AttributeError:
    pass

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"

# Short enough that a slow tick does not make the run unbearable, long enough
# that the typing and the prompt round trip are not most of what is measured.
SLEEPS = (5, 15)


def timed_sleep(g, secs):
    """Type `sleep N' and time how long the SHELL PROMPT takes to come back.

    The prompt, not a marker echoed by the command.  The first version of this
    ran `sleep N; echo __SLEPTN__' and waited for that tag -- which the
    terminal echoes as the line is TYPED, so both measurements were the
    typing time (24 characters at 0.12 s) and reported ratios of 1.0x and
    0.4x for sleeps of 5 and 15 seconds.  A 15-second sleep finishing in 5.4
    seconds should have been the giveaway; the marker has to be something the
    command produces and the echo cannot contain.

    The prompt qualifies: nothing in `sleep N' prints a `#'.
    """
    mark = len(g.text())
    t0 = time.time()
    g.run("sleep %d" % secs, pause=0)
    if not g.done(mark, timeout=900):
        print("    sleep %d never finished in 900 s" % secs)
        return None
    wall = time.time() - t0
    print("    sleep %2d -> %6.1f s wall" % (secs, wall))
    return wall


def main():
    with Player(DIST, "clockrate") as g:
        g.boot()
        # No network at all: the tick is a property of the machine, and
        # bringing the stack up would only add four minutes and a variable.
        print("--- timing the guest's own sleep(1) ---")
        walls = [timed_sleep(g, s) for s in SLEEPS]

        # Two lengths, because the fixed cost -- typing the line at 0.12 s a
        # character, the shell's round trip -- is the same for both.  The
        # DIFFERENCE divides it out and is the honest tick ratio.
        if all(w is not None for w in walls):
            dw = walls[1] - walls[0]
            ds = SLEEPS[1] - SLEEPS[0]
            print("--- result ---")
            print("  marginal: %.1f s wall per %d guest s = %.2fx"
                  % (dw, ds, dw / ds))
            print("  %s" % ("guest time is real time; a timeout that does not"
                            " fire is a FAULT" if dw / ds < 2.0 else
                            "the guest tick is slow; long guest waits are"
                            " expensive, not broken"))
            # Sanity: the marginal figure is only meaningful if each sleep
            # took at least about its own length.  A shorter one means the
            # marker fired early again, and the ratio is measuring the
            # harness rather than the guest.
            for w, sec in zip(walls, SLEEPS):
                if w < sec * 0.8:
                    print("  SUSPECT: sleep %d returned in %.1f s -- the wait"
                          " is not measuring the guest" % (sec, w))
        return 0


if __name__ == "__main__":
    sys.exit(main())
