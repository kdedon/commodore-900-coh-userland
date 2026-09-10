#!/usr/bin/env python3
"""route-test.py -- can this machine see and change its routing table?

    python3 route-test.py [dist]

Cold-boots a dist, takes it to multi-user so /etc/rc.net configures the
interface, and then, at the console, reads the routing table, adds a route,
reads it again, deletes it, and reads it a third time.

WHAT THIS PROVES.  pr_routes drives NWIOGIPOROUTE, the first _IORW ioctl this
port has ever issued -- a struct out and the same struct back in one request --
so a table that reads correctly says the daemon's ip_ioctl route branch works
AND that ichan_ioctl_rw() frames the exchange the way coh_sr.c answers it.
add_route/del_route drive NWIOSIPOROUTE/NWIODIPOROUTE.  The proof that they
agree is that a route pr_routes did not print before add_route ran IS printed
after it, and is gone again after del_route.

The three tables are captured to FILES and read back off the image, not scraped
off the console: the console echoes every command as it is typed, so an address
that appears there may only be the request.  A table read out of a file the
guest wrote can only have come from the program.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "hostbuild"))

from slipwire import Guest                                    # noqa: E402

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"

# A destination this machine has no reason to know about, reached through the
# SLIP peer -- which has to be on-link, because ipr_add_oroute() rejects a
# gateway outside the interface's own subnet.
DEST = "192.168.7.0/24"
GW = "10.0.0.1"


def main():
    with Guest(DIST, "route") as g:
        g.boot()
        g.multiuser(timeout=600)

        # rc.net runs in the background and the inet daemon initialises ten protocol
        # layers on a 6 MHz machine, so the login prompt appears long before
        # the stack can answer an ioctl.
        print("waiting for rc.net to bring the stack up ...")
        time.sleep(120)

        g.run("root", "toolchain", timeout=180)
        print("logged in on the console")
        time.sleep(5)

        g.run("/etc/pr_routes > /r1 2>&1", pause=60)
        g.run("/etc/add_route -v -g %s -d %s > /radd 2>&1" % (GW, DEST),
              pause=30)
        g.run("/etc/pr_routes > /r2 2>&1", pause=60)
        g.run("/etc/del_route -g %s -d %s > /rdel 2>&1" % (GW, DEST),
              pause=30)
        g.run("/etc/pr_routes > /r3 2>&1", pause=60)

        out = {}
        for name in ("r1", "radd", "r2", "rdel", "r3"):
            out[name] = g.read_file("/" + name, lines=40)
            print("--- /%s ---" % name)
            for line in out[name]:
                print("    " + line)

        def table_has(lines, what):
            return any(what in ln for ln in lines)

        header = table_has(out["r1"], "ent #")
        before = table_has(out["r1"], "192.168.7.0")
        after = table_has(out["r2"], "192.168.7.0")
        gone = not table_has(out["r3"], "192.168.7.0")

        print("table header printed:            %s" % ("yes" if header else "NO"))
        print("route absent before add_route:   %s" % ("yes" if not before else "NO"))
        print("route present after add_route:   %s" % ("yes" if after else "NO"))
        print("route gone after del_route:      %s" % ("yes" if gone else "NO"))

        passed = header and not before and after and gone
        print("=== routing table read/add/delete: %s"
              % ("PASS" if passed else "FAIL"))
        return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
