#!/usr/bin/env python3
"""console-stall-test.py -- does a big console write ever fail to finish?

    python3 console-stall-test.py [dist] [rounds]

A process writing to the console sleeps in ttwrite() once the output queue
passes OHILIM and is woken as Tx interrupts drain it.  If a Tx interrupt stops
being delivered, that sleep never ends: the process is stuck mid-write, at
whatever byte it had reached.

The stall's tell is an arbitrary stopping point mid-write: nothing is hanging
inside the writer, its console write simply never completes.

The mechanism guarded against is the SCC's interrupt-under-service
bookkeeping: a handler that drops priority before issuing Reset Highest IUS
lets a nested SCC interrupt arrive in the window, clear its own level, and
leave the outer handler clearing a level no longer its own -- after which a
level stays marked in service and everything below it, Tx included, stops
being delivered.

So the stress is exactly the shape that nests: the guest writing a lot to the
console (Tx) while the host types at it (Rx, a higher priority on the same
chip).  A plain write loop cannot show it; it needs both at once.

Each round runs a big `cat' and then checks the shell still answers.  A round
that never returns to a prompt IS the bug.
"""
import sys
import time

from slipwire import Guest, console_bytes, typeline

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"
ROUNDS = int(sys.argv[2]) if len(sys.argv) > 2 else 6

# Something several kilobytes long that every dist ships, so the write is far
# past OHILIM (128) and must sleep and be woken many times.
BIG = "/etc/termcap"


def main():
    with Guest(DIST, "consolestall") as g:
        g.boot()

        stalls = 0
        for r in range(1, ROUNDS + 1):
            mark = len(g.text())
            typeline("cat %s" % BIG)

            # Type WHILE it writes.  Each character is an Rx interrupt on the
            # same channel the output is going out of, at a higher priority --
            # which is the nesting the bug needs.  They are spaces so the shell
            # has nothing to run when the line eventually ends.
            for _ in range(12):
                time.sleep(0.4)
                console_bytes([0x20], delay=0.02)

            done = False
            for _ in range(60):
                time.sleep(1)
                g.pump()
                # cat has finished when the marker we type after it comes back.
                if "__ROUND%d__" % r in g.text()[mark:]:
                    done = True
                    break
                typeline("echo __ROUND%d__" % r)
            if done:
                print("round %d: console still writing and answering" % r)
            else:
                stalls += 1
                tail = g.text()[mark:].strip().splitlines()[-1:]
                print("round %d: STALLED -- last output %r" % (r, tail))
                break

        print("=== console: %d/%d rounds ok, %d stalled  %s"
              % (ROUNDS - stalls, ROUNDS, stalls,
                 "PASS" if stalls == 0 else "FAIL"))
        return 0 if stalls == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
