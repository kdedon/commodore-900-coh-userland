#!/usr/bin/env python3
"""rcmds-test.py -- does this machine answer finger, and run a remote command?

    python3 rcmds-test.py [dist]

Cold-boots a dist, takes it to multi-user, and then, at the console, fingers
ITSELF over TCP and runs a command on ITSELF with remsh.  Nothing is typed at
either daemon and neither is started by hand: if the queries are answered,
/etc/rc.net started fingerd and remshd on its own.

WHAT THIS PROVES.  The connection is to a name in the guest's own /etc/hosts, so
the stack routes it internally and no serial line, no slip and no host peer are
involved.  A finger pass says: fingerd is running after a plain boot, a passive
open on port 79 accepted a connection, the query line was parsed, /bin/finger
ran on a pipe and its output came back over the connection.  A remsh pass says
the same for port 514 plus the rest of the protocol -- the reserved source port,
the three NUL-terminated strings, iruserok() accepting the caller out of a
.rhosts, and a shell running with its standard output piped back.

AND THE ACCOUNT THAT MUST NOT BE SERVED.  remshd refuses uid 0: it asks for no
password, so a command run as root here would be a root shell for whoever the
address database names.  The same run therefore asks for one as root and
requires the server's own diagnostic to come back instead of output.  No .rhosts
is shipped for anybody, so the session that does succeed is admitted by one this
harness writes for the guest account.

WHAT IT DOES NOT PROVE: interoperation with a stock BSD rsh, which asks for the
second connection this daemon does not serve; and nothing about a real remote
peer, since both ends are this machine.

The proof is a FILE in both cases.  A remote command's output arrives on the
console mixed with the echo of the command that asked for it, so the answers are
redirected to files and read back off the image.  The remsh marker in
particular cannot be confused with its own request: the request says
`REMSH_WORKED' inside an argument to echo, and what comes back is the same word
on a line of its own written by a shell on the other side of a TCP connection --
so the file is read, not the transcript.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "hostbuild"))

from slipwire import Guest                                    # noqa: E402

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"
TARGET = "c900"


def main():
    with Guest(DIST, "rcmds") as g:
        g.boot()
        g.multiuser(timeout=600)

        print("waiting for rc.net to bring the stack up ...")
        time.sleep(120)

        g.run("root", "toolchain", timeout=180)
        print("logged in on the console")
        time.sleep(5)

        # finger, twice: the whole user list and then one named user.  The
        # named form is the one that exercises the query parser, since the
        # empty query runs finger with no arguments at all.
        g.run("/bin/finger @%s > /f1 2>&1" % TARGET, pause=90)
        g.run("/bin/finger root@%s > /f2 2>&1" % TARGET, pause=90)

        # remshd refuses uid 0 and no .rhosts is shipped for anybody, so the
        # session below is a non-root one and this is what admits it: root on
        # this machine may act as guest.  644 and owned by root is what
        # iruserok() will read -- it ignores a file the world can write.
        g.run("/bin/mkdir /usr/guest", pause=10)
        g.run("echo c900.localnet root > /usr/guest/.rhosts", pause=10)
        g.run("/bin/chmod 644 /usr/guest/.rhosts", pause=10)

        # remsh: a command whose output cannot come from anywhere else.
        # Standard input comes from /dev/null.  remsh forwards its own standard
        # input to the remote command, and left on the console tty it would eat
        # the next line this harness types at the shell.
        g.run("/bin/remsh -l guest %s echo REMSH_WORKED < /dev/null > /rs1 2>&1"
              % TARGET, pause=90)
        g.run("/bin/remsh -l guest %s /bin/pwd < /dev/null > /rs2 2>&1" % TARGET,
              pause=90)
        # And the account the daemon must never run a command for.  The
        # diagnostic is the server's, sent as protocol and printed by the
        # client, so a file holding it is evidence the refusal came off the
        # connection and not out of the local client.
        g.run("/bin/remsh -l root %s /bin/pwd < /dev/null > /rs3 2>&1" % TARGET,
              pause=90)

        out = {}
        for name in ("f1", "f2", "rs1", "rs2", "rs3"):
            out[name] = g.read_file("/" + name, lines=30)
            print("--- /%s ---" % name)
            for line in out[name]:
                print("    " + line)

        print("--- /tmp/rc.net.log ---")
        for line in g.rcnetlog():
            print("    " + line)

        def has(name, what):
            return any(what in ln for ln in out[name])

        finger_all = has("f1", "root")
        finger_one = has("f2", "root")
        remsh_echo = has("rs1", "REMSH_WORKED")
        remsh_pwd = has("rs2", "/")
        remsh_root = has("rs3", "root may not run commands over the network")

        print("finger @host answered:        %s" % ("yes" if finger_all else "NO"))
        print("finger user@host answered:    %s" % ("yes" if finger_one else "NO"))
        print("remsh ran echo:               %s" % ("yes" if remsh_echo else "NO"))
        print("remsh ran pwd:                %s" % ("yes" if remsh_pwd else "NO"))
        print("remsh -l root refused:        %s" % ("yes" if remsh_root else "NO"))

        fpass = finger_all and finger_one
        rpass = remsh_echo and remsh_pwd and remsh_root
        print("=== fingerd: %s" % ("PASS" if fpass else "FAIL"))
        print("=== remshd:  %s" % ("PASS" if rpass else "FAIL"))
        return 0 if (fpass and rpass) else 1


if __name__ == "__main__":
    sys.exit(main())
