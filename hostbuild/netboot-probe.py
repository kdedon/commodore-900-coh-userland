#!/usr/bin/env python3
"""netboot-probe.py -- what does the boot-time network actually leave running?

    python3 netboot-probe.py [dist]

netboot-test.py answers "did a booted machine reply on the wire", which is the
question that matters but a poor one to debug with: a 0/3 looks identical
whether the inet daemon never started, slip never started, or both are running and the
line is wrong.

This boots the same way -- cold, Ctrl-D to multi-user, nothing typed until the
login prompt -- then logs in and asks the machine directly:

    ps                  which of the daemons survived
    cat /tmp/rc.net.log what rc.net said, if anything reached the disk
    /etc/ifconfig       whether the interface is configured

The distinction it exists to draw: /etc/rc backgrounds rc.net and then EXITS.
inet and slip are children of that exiting shell.  If they are gone from ps
but the interface is configured, the boot ran and something reaped them; if
they are present, the fault is on the wire, not in the boot.

Single user cannot answer this -- emu-run.sh cannot send a Ctrl-D, so every
result it gives is about a machine that never left single user.
"""
import sys
import time

from slipwire import Guest, typeline

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"


def main():
    with Guest(DIST, "netprobe") as g:
        g.boot()
        g.multiuser()

        # Log in.  The root account has no password in this image (the login
        # bring-up work left it that way deliberately), so the name is enough.
        time.sleep(3)
        typeline("root")
        time.sleep(8)
        g.pump()

        for cmd in ("ps", "cat /tmp/rc.net.log", "/etc/ifconfig",
                    "ls -l /dev/inet"):
            print("=== %s ===" % cmd)
            mark = len(g.text())
            typeline(cmd)
            time.sleep(12)
            g.pump()
            print(g.text()[mark:])

        return 0


if __name__ == "__main__":
    sys.exit(main())
