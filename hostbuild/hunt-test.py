#!/usr/bin/env python3
"""hunt-test.py -- play a game of hunt(6) at the console, one keystroke at a time.

    python3 hunt-test.py [dist]

Cold-boots a dist, brings the stack up, starts the driver, and then sits at the
keyboard: types a code name, enters the maze, presses movement keys and quits.
The pass is that the maze and the status panel arrive on the terminal, that a
keypress changes what is drawn, and that `q' `y' puts the shell back.

Why this is a simulator harness and not an `emu-run.sh' script: emu-run feeds
its script through a gate that a curses game never opens.  It does pace the
bytes -- one at a time, only once the guest has consumed the last one and the
console has gone quiet -- but after every carriage return it waits for a fresh
`#' shell prompt before feeding another byte (bus.c, inq_wait_seq).  hunt
prints no prompt, so the newline that ends the code name is the last input it
would ever receive.  Every key here goes down the console channel on its own
instead, paced by the harness, exactly as a person types.

That also makes this the end-to-end test of task #47: playit.c's main loop is a
poll(2) on the socket AND on stdin at once, which is the reason a terminal had
to become pollable at all.  A game that responds to a keypress has proved that
path in a way the unit test could not.

The network is entirely local -- the client talks to a driver on the same
machine -- so no SLIP peer is involved and nothing is typed at the wire.
"""
import sys
import time

from hunt_common import Player, net_setup, visible
from slipwire import console_bytes

# A run takes half an hour and its progress IS the diagnosis -- which step the
# game stopped at.  Block-buffered to a file, none of that appears until the
# end, and a run that has to be interrupted leaves nothing at all.
try:
    sys.stdout.reconfigure(line_buffering=True)
except AttributeError:
    pass

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"

HUNTD = "/usr/games/lib/huntd"
HUNT = "/usr/games/hunt"
NAME = "c900"


def main():
    with Player(DIST, "hunt") as g:
        g.boot()
        g.setup(net_setup())

        # Timeouts here are generous because a guest second costs about 25
        # of ours under the simulator.  That penalty falls only
        # on waits that expire on the clock -- hunt's discovery poll, and the
        # sleep(1) between its connect retries.  Play itself is event-driven
        # and runs at the simulator's ordinary speed.
        #
        # The client asks the system its own name and looks THAT up to decide
        # which network to search, so a machine that thinks it is `localhost'
        # hunts on 127.0.0.1 and finds nothing.  Check the name before blaming
        # the game.
        g.run("cat /etc/hostname", "c900", timeout=30)
        # Nothing has read /etc/profile: init runs the single-user shell
        # directly, so TERM is whatever the environment was, and hunt refuses
        # to start without one ("no terminal type").
        g.run("TERM=vt100; export TERM")

        g.run("%s &" % HUNTD, pause=8)
        if not wait_for_driver(g):
            # The whole console, not just the last probe's window: whether
            # huntd was even exec'd -- a dropped character in its path gives a
            # `not found' far earlier than anything a probe can see.
            print("--- guest console (all of it) ---")
            print(visible(g.text())[-4000:])
            print("=== hunt: the driver never answered a scores probe  FAIL")
            return 1

        # ---- into the maze ----
        mark = len(g.text())
        g.run(HUNT, pause=2)
        if not g.expect("Enter your code name:", timeout=600, mark=mark):
            print("=== hunt: the client never got to the code-name prompt  FAIL")
            return 1

        g.keys(NAME + "\r")
        if not g.expect("Enter your team", timeout=300):
            print("=== hunt: no team prompt after the code name  FAIL")
            return 1

        mark = len(g.text())
        g.keys("\r")                    # no team

        # `Damage:' and `Ammo:' are drawn by the DRIVER (huntd/draw.c) and
        # reach the terminal only over the player's TCP connection, so finding
        # them here proves the whole chain: driver -> stack -> client -> curses
        # -> tty.  A locally-drawn frame could not contain them.
        drew = g.expect("Damage:", timeout=300, mark=mark)
        screen = g.text()[mark:]
        print("--- the screen as the game drew it ---")
        print(visible(screen)[:1800])
        if not drew:
            print("=== hunt: the maze never reached the terminal  FAIL")
            return 1

        checks = [("status panel", "Ammo:" in screen and "Damage:" in screen),
                  ("our code name on screen", NAME in screen)]

        # ---- does it answer the keyboard? ----
        #
        # This is the part no other test can reach.  playit.c blocks in a
        # single poll(2) over the socket AND the terminal; before task #47 the
        # terminal returned POLLNVAL and the game would never see a keystroke.
        # Moving must therefore produce fresh output from the driver.
        moved = 0
        for key in ("h", "j", "k", "l", "h", "k"):
            mark = len(g.text())
            g.keys(key)
            for _ in range(60):
                time.sleep(1)
                g.pump()
                if len(g.text()) > mark:
                    break
            n = len(g.text()) - mark
            print("    key %r -> %d bytes back" % (key, n))
            if n:
                moved += 1
        checks.append(("keys move the player (%d/6 redrew)" % moved, moved >= 4))

        # ---- quit ----
        #
        # `q' is caught by the client itself and turned into a confirmation
        # prompt; only the `y' reaches the driver.  Both go through the same
        # poll, so a clean exit is one more crossing of that path.
        mark = len(g.text())
        g.keys("q")
        asked = g.expect("Really quit?", timeout=60, mark=mark)
        if not asked:
            # send_stuff() drops -- and beeps at -- every keystroke while
            # nchar_send is zero, including the `q' it would otherwise catch,
            # so the driver can leave the game unquittable by that route.
            # SIGINT reaches intr() directly and asks the same question.
            print("    `q' was not taken; interrupting instead")
            mark = len(g.text())
            console_bytes([3])
            asked = g.expect("Really quit?", timeout=60, mark=mark)
        if asked:
            g.keys("y")
        checks.append(("the game offered to quit", asked))
        mark = len(g.text())
        g.run("echo __SHELL_BACK__", pause=2)
        checks.append(("the shell came back", g.expect("__SHELL_BACK__",
                                                       timeout=60, mark=mark)))

        print("--- results ---")
        for what, ok in checks:
            print("  %-34s %s" % (what, "PASS" if ok else "FAIL"))
        bad = [w for w, ok in checks if not ok]
        print("=== hunt interactive play: %s"
              % ("PASS" if not bad else "FAIL (%s)" % ", ".join(bad)))
        return 0 if not bad else 1


def wait_for_driver(g, tries=4):
    """Ask the driver for the score table until it answers.

    huntd forks and drops its controlling terminal, so `ps' will not show it
    and its silence proves nothing -- there is no readiness message to wait
    for.  `hunt -S' is the probe: the driver answers a scores request whether
    or not anyone is playing, unlike `hunt -q', which replies only once a
    player has joined and whose silence against an empty game is correct
    behaviour.  (Capital S.  Lower-case -s enters the game in scanning mode.)

    Each probe waits for the PROMPT, not for a marker inside a wall-clock
    window.  hunt's discovery is a single one-second poll, and a guest second
    costs about 25 of ours here, so that one poll is half a minute of wall
    clock before hunt has even decided it found nothing.

    Distinguishes the two outcomes that matter: the prompt came back and the
    table was not in it (no driver yet, retry), or the prompt never came back
    at all (hunt itself is stuck, which is a fault and worth stopping for).
    """
    for i in range(tries):
        mark = len(g.text())
        g.run("%s -S" % HUNT, pause=2)
        if not g.done(mark, timeout=900):
            print("    probe %d: no prompt in 900 s -- hunt is stuck, not slow"
                  % (i + 1))
            return False
        if "Ducked" in g.text()[mark:]:
            print("ok: the driver answered with its score table")
            return True
        print("    no driver yet (probe %d of %d)" % (i + 1, tries))
    # Distinguish a driver that died from one that never started.  huntd
    # detaches, so `ps' cannot see it -- but every channel it opened left a
    # pair of fifos behind (inet_chan.c names them /tmp/ic<pid>.<seq>.{q,r}),
    # and huntd opens three: two tcp and one udp.
    g.run("ls /tmp", timeout=60)
    return False


if __name__ == "__main__":
    sys.exit(main())
