"""talk-test.py -- two people on two C900s, talking to each other.

    python3 talk-test.py [--cut] [--image PATH] [--keep] [--trace]

WHAT THIS TESTS, and why it needs two machines.  talk(1) is the one program on
this system that cannot be exercised on one machine and cannot be exercised
without a daemon: a call is a rendezvous through the ntalk service at BOTH ends
-- the caller leaves an invitation with its OWN daemon and announces the call to
the CALLEE's, and the callee then asks the caller's daemon for the invitation and
opens a TCP connection to the address in it.

THE DAEMON IS /etc/inetd.  ntalk is a `dgram udp wait' line in /etc/inetd.conf
and the switchboard answers it itself: on the first datagram it forks a child
WITHOUT exec and gives it the socket, and that child holds the invitation table
(net/talkserv.c).  There is no /etc/talkd any more.  So this is also the gate on
the absorbed service under the only conditions that exercise all of it -- two
real /bin/talk clients, two logged-in users in /etc/utmp, and a terminal for the
announcement to be written on.

THE RUN, and what each step proves:

  1. Both machines boot to multi-user, log in as root, and get a stack, with a
     ping to prove the wire carries before any daemon is asked anything
     (talk_common.setup()).  A is RE-ADDRESSED on a running stack, which is what
     a person does and is what FINDINGS T-51 used to break: an ifconfig after
     /etc/inet had learnt the address lost UDP in both directions, and presented
     as an ntalk defect.  The stack now reads the address live, so there is
     nothing to work around here -- see talk_common.readdress().
  2. Each machine is asked whether it has an ntalk service at all: inetd
     running, and an ntalk line in /etc/inetd.conf for it to have bound.  Both
     are read off the machine rather than assumed, because an image built before
     the service moved has neither and would fail step 4 with nothing to say why.
  3. B makes its console writable (mesg y).  An announcement is a write on
     somebody else's terminal and the daemon refuses one to a terminal that does
     not permit it, which is a REFUSAL and not a failure -- so if this step is
     skipped the test measures PERMISSION_DENIED.
  4. A runs `talk root@c900'.  A's switchboard takes the LEAVE_INVITE; B's finds
     root in /etc/utmp, resolves A's address through /etc/hosts, and writes the
     announcement on B's console.
  5. B runs `talk root@peer'.  Its LOOK_UP goes to A's daemon, which answers
     with the invitation and A's data port, and B connects to it.  That answer
     can only come out of the invitation table A's inetd child has been holding
     since step 4 -- a different datagram, seconds earlier.
  6. Each side types a nonce that only it knows, and the other side's screen is
     searched for it.

PASS is all three of: the announcement on B's console naming A; each nonce
arriving at the far end.  The nonces are the criterion that cannot be produced
by a harness bug -- they are generated per run, typed at one guest, and looked
for in the other guest's console bytes, with a TCP connection and a curses
screen between them.

THE NEGATIVE CONTROL.  `--cut' relays nothing on the wire.  Every criterion must
then fail; a --cut run that passes is measuring something other than the network.
"""
import os
import random
import re
import shutil
import signal
import string
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import twohost as T                                           # noqa: E402
from talk_common import setup                                  # noqa: E402

# The two names are /etc/hosts', not this file's: A configures as 10.0.0.1,
# which the shipped hosts file calls `peer', and B as 10.0.0.2, which it calls
# `c900'.  Every image carries the same /etc/hosts, so each machine's name for
# the other is the other's entry in it.
NAME_A, NAME_B = "peer", "c900"


def ntalk_up(g):
    """Has this machine an ntalk service?  Two questions, both read off it.

    The switchboard has to be RUNNING -- rc.net starts it in the background -- and
    its conf file has to carry the ntalk line, because that is what makes it bind
    518.  Neither is started or edited here: this harness must not manufacture the
    thing under test, and an image built before the service moved into inetd
    should say so here rather than fail the announcement with no reason.

    The greps exclude their own pipeline: a grep for a string has that string in
    its argument list, which `ps' prints.
    """
    m = g.mark()
    g.line("/bin/ps -ax | /bin/grep inetd | /bin/grep -v grep")
    g.expect("# ", 180, "(ps for inetd)")
    if "inetd" not in g.since(m).split("\n", 1)[-1]:
        T.say("%s: no inetd is running -- rc.net did not start the switchboard"
              % g.name)
        return False
    # THE PATTERN AND THE THING WAITED FOR ARE DIFFERENT WORDS, deliberately.
    # The console echoes the command as it is typed, so waiting for anything the
    # command line itself contains matches the echo and not the answer -- and
    # waiting for the PROMPT is no better here, because every comment line in
    # inetd.conf begins with "# " and the first of them ends the wait before the
    # line being looked for has arrived.  That is what this check did first, and
    # it reported no ntalk service on a machine that had one.  So: grep for
    # `^ntalk', wait for `dgram', which only the matched line can produce.
    g.line("/bin/grep '^ntalk' /etc/inetd.conf")
    if not g.expect("dgram", 120, "(the ntalk line in /etc/inetd.conf)"):
        T.say("%s: /etc/inetd.conf has no ntalk line -- this image predates the"
              " service moving into the switchboard" % g.name)
        return False
    T.say("%s: inetd is running and serves ntalk" % g.name)
    return True


def typeat(g, s, delay=1.5):
    """Type into a RAW-MODE program, one character at a time.

    talk turns the line discipline off (screen.c), so there is no kernel buffer
    between the serial line and the program: every character has to be taken by
    talk itself, and between two of them talk redraws a window on a 6 MHz
    machine.  A whole line written at once therefore arrives faster than it is
    read -- the SCC's receive FIFO is three deep -- and the rest of the line is
    lost to an overrun.  Measured: of a nine-character nonce written in one
    write, exactly ONE character reached the far end (one 1-byte segment on the
    wire, acknowledged, and then silence in both directions), which from the
    screen is indistinguishable from a connection that carries nothing.

    A person types at this rate.  `getty' and the shell do not need it because
    the line discipline buffers for them, which is why every other typed line in
    this harness is written whole.
    """
    for ch in s:
        g.send(ch)
        time.sleep(delay)
        g.pump(0.1)


ESCSEQ = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b.")


def drawn(s):
    """What a curses program PUT ON THE SCREEN, out of the bytes it wrote.

    A nonce never appears as a contiguous string in the console stream, and
    looking for one there is what made a working `talk' report FAIL: 4.3BSD
    curses refreshes after every character (screen.c calls wrefresh per
    ScreenPut), so each letter is written on its own behind a cursor address and
    a run of termcap padding NULs --

        \\x1b[14;01H\\0\\0\\0\\0B\\x1b[14;02H\\0\\0\\0\\0\\x1b[14;02H\\0\\0\\0\\0R...

    Stripping the escape sequences, the padding NULs and the carriage returns
    puts the letters back next to each other in the order they were drawn, which
    for one window filling left to right is what the window says.  This is a
    projection and not a terminal emulator: it does not honour the cursor
    addresses, so it must only be trusted for text that was drawn in order --
    which a typed nonce is.
    """
    return ESCSEQ.sub("", s).replace("\0", "").replace("\r", "")


def wait_drawn(g, mark, needle, timeout, why):
    """Wait for `needle' to have been DRAWN on the guest's screen since `mark'."""
    end = time.time() + timeout
    while time.time() < end:
        g.pump(1.0)
        if needle in drawn(g.since(mark)):
            return True
    T.say("%s: TIMEOUT after %ds waiting for %r to be drawn %s"
          % (g.name, timeout, needle, why))
    return False


def run(cut, trace, image, keep):
    st = setup("talk", image, cut, keep)
    if not isinstance(st, dict):
        return st
    A, B, work, wire, guests, why = (st["A"], st["B"], st["work"], st["wire"],
                                     st["guests"], st["why"])
    try:
        if not (ntalk_up(A) and ntalk_up(B)):
            why.append("a machine has no ntalk service")
            return T.report(False, why, work, wire, guests, keep)

        # An announcement is a write on somebody else's terminal.  Both consoles
        # are made writable: A's too, because the same run then tests the call in
        # the other direction if the first one succeeds.
        # ... and it is read back, because the bit `mesg' sets is COHERENT's
        # (owner execute, cmd/mesg.c) and not BSD's group-write.  The first run
        # of this test found talk and the daemon reading the wrong one, which presents
        # as a call refused to a terminal that had just said yes.
        for g in (A, B):
            g.line("/bin/mesg y")
            g.expect("# ", 120, "(mesg y)")
            m = g.mark()
            g.line("/bin/mesg")
            g.expect("# ", 120, "(mesg readback)")
            if "yes" not in g.since(m).split("\n", 1)[-1]:
                why.append("%s's console does not permit messages" % g.name)
        if why:
            return T.report(False, why, work, wire, guests, keep)

        nonce_a = "A" + "".join(random.choice(string.ascii_uppercase) for _ in range(7))
        nonce_b = "B" + "".join(random.choice(string.ascii_uppercase) for _ in range(7))

        # A calls B.  The client is full screen and never returns to the shell on
        # its own, so nothing waits for a prompt after this line.
        mB = B.mark()
        mA = A.mark()
        A.line("/bin/talk root@%s" % NAME_B)

        # The announcement on B's console.  `would like to talk' is the service's
        # own wording and the host name in it is the one B resolved from the
        # datagram's source address, not anything A sent.
        announced = B.expect("would like to talk to you", 300,
                             "(the announcement on B)")
        if not announced:
            why.append("no announcement on B's console")
        T.say("B: console after the call ->\n%s" % B.since(mB)[-800:])

        # B answers.  Its LOOK_UP goes to A's daemon for the invitation A left
        # there, so this is the step that needs the daemon on the CALLING side.
        answered = False
        if announced:
            B.line("/bin/talk root@%s" % NAME_A)
            # The connected marker is the WHO LINE in the middle of the divider,
            # which screen.c's ScreenWho() draws from DoTalk() -- i.e. only after
            # TalkInit() has a connection.  Nothing in this client ever prints
            # "Connection established", so waiting for that phrase spent 240 s
            # timing out on every run and then went on regardless.  The spaces
            # matter: they are what tells the divider's ` root@peer ' apart from
            # the announcement text `talk root@peer.localnet'.
            answered = B.expect(" root@%s " % NAME_A, 240,
                                "(talk connected on B)")
            if not answered:
                # A connected session is also proven by the nonces below, so this
                # is reported and not fatal.
                T.say("B: no who-line on the divider -- going on to the nonces")
            time.sleep(20)
            typeat(B, nonce_b + "\r")
            time.sleep(5)
            typeat(A, nonce_a + "\r")

        got_b_on_a = wait_drawn(A, mA, nonce_b, 240, "(B's nonce on A's screen)")
        got_a_on_b = wait_drawn(B, mB, nonce_a, 240, "(A's nonce on B's screen)")
        if not got_b_on_a:
            why.append("what B typed never reached A")
        if not got_a_on_b:
            why.append("what A typed never reached B")

        T.say("A: drawn ->\n%s" % drawn(A.since(mA))[-800:])
        T.say("B: drawn ->\n%s" % drawn(B.since(mB))[-800:])
        T.say("A: screen bytes ->\n%s" % A.since(mA)[-1500:])
        T.say("B: screen bytes ->\n%s" % B.since(mB)[-1500:])

        ok = bool(announced and got_b_on_a and got_a_on_b)
        return T.report(ok, why, work, wire, guests, keep)
    finally:
        for g in guests.values():
            g.stop()
        try:
            wire.wait(timeout=10)
        except subprocess.TimeoutExpired:
            wire.send_signal(signal.SIGINT)
            wire.wait(timeout=5)


def main(argv):
    opts = [a for a in argv[1:] if a.startswith("--")]
    image = None
    for o in opts:
        if o.startswith("--image="):
            image = o.split("=", 1)[1]
    cut = "--cut" in opts
    rc = run(cut, "--trace" in opts, image, "--keep" in opts)
    if cut:
        T.say("=== negative control: %s"
              % ("PASS (the cut wire fails the test)" if rc
                 else "FAIL (the test passed with no wire!)"))
        return 0 if rc else 1
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
