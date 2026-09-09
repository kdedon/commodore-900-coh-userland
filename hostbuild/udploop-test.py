#!/usr/bin/env python3
"""udploop-test.py -- does the net suite pass on the SIMULATOR?

    python3 udploop-test.py [dist]

hunt's discovery is a datagram sent to this machine's own address and a reply
read back on the SAME socket.  It works under the instruction-level emulator
and finds nothing on the simulator, and the two ends of that -- a slow reply, a
broken loopback -- need opposite fixes.  This narrows it without hunt in the
picture at all.

Three steps, cheapest first, each a control for the next:

  discotime, WITH NO DRIVER RUNNING.  Nothing can answer, so the only correct
  outcome is "no reply in 30 s" -- printed after thirty one-second polls.  If
  it hangs instead, poll() or recvfrom() is at fault and huntd was never the
  question.  This is the control that costs one command and rules out half the
  hypotheses.

  udpecho -- a datagram to ourselves and back, one process, one socket pair.
  Local delivery with no second program to blame.

  udppoll -- poll() before reading, which is the order a select-driven program
  uses and the one a request/reply channel does not naturally support.

Both of the latter pass under the emulator, so a failure here is a difference
between the machines rather than a bug in the stack's logic.
"""
import sys
import time

from hunt_common import Player, net_setup

try:
    sys.stdout.reconfigure(line_buffering=True)
except AttributeError:
    pass

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"

# TCP first, because that is where hunt now stops: `hunt -S' discovers the
# driver, prints its hostname and then blocks in dump_scores' connect/read.
# ephport is that exact shape -- bind(0), listen, getsockname, connect, accept,
# read to EOF -- and acceptmany is the same over a fixed port.  Both pass under
# the emulator and NEITHER has ever been run on the simulator: the whole net
# suite was verified on the emulator only.  udp last, as the control.
# The program's own verdict, not merely that it finished.  Each of these prints
# "<name>: PASS" or "<name>: FAIL" as its last word; reading only whether the
# prompt came back reported "finished" for a run that had printed FAIL, which is
# the same hole as a test that exits 0 whatever it saw.
STEPS = [("/bin/acceptmany", "acceptmany"),
         ("/bin/ephport", "ephport"),
         ("/bin/udpecho", "udpecho"),
         ("/bin/udppoll", "udppoll")]


def main():
    with Player(DIST, "udploop") as g:
        g.boot()
        g.setup(net_setup())

        # Waited on the PROMPT, not on a marker inside a wall-clock window.
        # The guest's waits are counted in ITS seconds and an unknown number of
        # ours pass per guest second, so a fixed window reports "silence" for a
        # program that is still running -- which is how a 30-second discotime
        # looked like a hang for ten minutes.  The prompt is the guest saying
        # it has finished; if THAT never comes back, the command really is
        # stuck, and that is a result rather than a harness artefact.
        results = []
        bad = 0
        for cmd, tag in STEPS:
            print("--- %s ---" % cmd)
            mark = len(g.text())
            g.run(cmd, pause=2)
            ok = g.done(mark)
            body = g.text()[mark:]
            out = [l.strip() for l in body.splitlines() if l.strip()]
            print("\n".join("    " + l for l in out[-6:]))
            if not ok:
                verdict = "STUCK -- no prompt"
            elif ("%s: PASS" % tag) in body:
                verdict = "PASS"
            elif ("%s: FAIL" % tag) in body:
                verdict = "FAIL"
            else:
                # Finished without saying either.  Not a pass: the program did
                # not reach its own verdict, so nothing here knows what happened.
                verdict = "NO VERDICT -- finished without PASS or FAIL"
            if verdict != "PASS":
                bad += 1
            results.append((cmd, verdict))

        print("--- guest console ---")
        print("\n".join(g.text().splitlines()[-30:]))
        print("--- results ---")
        for cmd, r in results:
            print("  %-22s %s" % (cmd, r))
        print("=== udploop: %s" % ("PASS" if bad == 0 else "FAIL"))
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
