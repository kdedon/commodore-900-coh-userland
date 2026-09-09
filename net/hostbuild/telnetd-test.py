#!/usr/bin/env python3
"""telnetd-test.py -- can somebody log in over the network?

    python3 telnetd-test.py [dist]

Cold-boots a dist, takes it to multi-user with a Ctrl-D, and then -- typing only
at the CONSOLE -- has the guest telnet to itself, log in, run a command, and
leave.  Nothing is typed at telnetd and nothing is started by hand: if the
session works, /etc/rc.net started telnetd on its own.

WHAT THIS PROVES, EXACTLY.  The connection is to a name in the guest's own
/etc/hosts, so the stack routes it to its own address internally and no serial
line, no slip and no host peer are involved.  A pass says: telnetd is running
after a plain boot, a passive open on port 23 accepts a connection, get_pty()
found a free channel, getty and login ran on the pty slave, and a shell reached
a prompt and executed a command.

WHAT IT DOES NOT PROVE: resolution.  `c900' is answered by /etc/hosts before
netdb.c ever reaches the resolver, so this run says nothing about the DNS.
dns-test.py in this directory is the test that does.

The proof is a FILE, not the transcript.  The command the guest is asked to run
writes /telnet.ok on the pty session, and the harness reads that file back two
ways: printed at the console, and off the disk image.  The transcript on its own
proves nothing, because the line that asks for the marker is typed at the
console AND echoed back by the remote shell, so the marker is on the screen
twice before anything has run it.  What the console can carry is the marker
coming back OUT of the file, since `sed -n 1p' does not contain it.

Both readings are taken because each has failed on its own for reasons that have
nothing to do with telnetd: a disk read has found the directory entry with the
inode still unwritten, which reads as "no such file" for a file that exists.
Either one seeing the marker is a pass, and a disagreement is worth reporting.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "hostbuild"))

from slipwire import Guest, typeline           # noqa: E402

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"

# The guest telnets to this name.  A name and not a dotted quad, so a failure to
# find it in /etc/hosts is visible as telnet saying so rather than as a
# connection that never happens.
TARGET = "c900"

# Typed into telnet's standard input, which is a pipe: ttn.c reads the keyboard
# from fd 0 and its raw-mode ioctls fail harmlessly on one.  The sleeps are the
# only pacing available -- there is no expect here -- and they are GUEST seconds,
# which under the simulator are many wall-clock seconds each.  Deliberately
# short for that reason.
#
# The leading sleep is not optional: telnetd allocates the pty and forks getty
# only after the connection arrives, so a login name sent the instant `Connected'
# appears can reach the pty before anything is reading it.
#
# In the BACKGROUND, and reporting to /dev/console.  Run in the foreground the
# console shell waits on the pipeline, so a client that does not return takes
# every later reading with it -- the `cat' that would have said why prints
# nothing, because nothing is reading the console.  To a file, the same output
# is unreadable for a different reason: a file the guest has not synced is on
# the image as a directory entry with a blank inode.  Straight to the console it
# is evidence as it happens, and it costs nothing to keep.
SCRIPT = ("( sleep 8; echo root; sleep 12;"
          " echo 'echo TELNETD_LOGIN_OK > /telnet.ok'; sleep 12;"
          " echo exit; sleep 5 ) | /bin/telnet %s > /dev/console 2>&1 &"
          % TARGET)


def wait_for(g, marker, timeout):
    """Wait for `marker' to appear on the console, from here on."""
    mark = len(g.text())
    for _ in range(timeout):
        time.sleep(1)
        g.pump()
        if marker in g.text()[mark:]:
            return True
    return False


def main():
    with Guest(DIST, "telnetd") as g:
        g.boot()
        g.multiuser()

        # rc.net runs in the background and the inet daemon is a 143 KB binary
        # initialising ten protocol layers on a 6 MHz machine, so the login
        # prompt appears long before the stack is up.  Wait for slip's readiness
        # line, which rc.net logs last, rather than starting the session into a
        # daemon that is still coming up.
        print("waiting for rc.net to finish starting the stack ...")
        time.sleep(120)

        typeline("root")
        if not wait_for(g, "toolchain", 180):
            print("--- guest console ---")
            print("\n".join(g.text().splitlines()[-20:]))
            print("=== telnetd login: FAIL (no console login)")
            return 1
        print("logged in on the console")
        time.sleep(5)

        mark = len(g.text())
        typeline(SCRIPT)
        # The sleeps add up to 37 guest seconds before telnet even sees the end
        # of its input, and a guest second here is many wall-clock ones.
        time.sleep(600)
        g.pump()
        print("--- the session, as the client itself printed it ---")
        print(g.text()[mark:])

        # The marker, out of the file and onto the console.  `sed -n 1p' does
        # not carry it, so it can only have been read out of /telnet.ok -- which
        # matters here, since the line that ASKS for the marker is echoed by the
        # remote shell and is on this console twice already.
        g.run("sync")
        time.sleep(10)
        mark = len(g.text())
        console_ok = False
        typeline("sed -n 1p /telnet.ok")
        if wait_for(g, "TELNETD_LOGIN_OK", 300):
            console_ok = True
        g.pump()
        print("--- sed -n 1p /telnet.ok (at the console) ---")
        print(g.text()[mark:])

        print("--- /tmp/rc.net.log (what the boot actually did) ---")
        for l in g.rcnetlog():
            print("    " + l)

        print("--- /telnet.ok (written BY the shell on the pty) ---")
        ok = g.read_file("/telnet.ok", lines=4)
        for l in ok:
            print("    " + l)

        disk_ok = any("TELNETD_LOGIN_OK" in l for l in ok)
        print("marker read at the console: %s; read off the image: %s"
              % ("yes" if console_ok else "NO", "yes" if disk_ok else "NO"))
        passed = console_ok or disk_ok
        print("=== telnetd login over the network: %s"
              % ("PASS" if passed else "FAIL"))
        return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
