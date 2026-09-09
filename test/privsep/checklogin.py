#!/usr/bin/env python3
"""checklogin.py <transcript> -- judge the login half of the privilege pass.

Three things, in order, none of which `su' can produce:

  1. passwd(1) as root writes a WELL-FORMED field.  13 characters, two of which
     are the salt.  A shorter field is FINDINGS T-57: crypt(3) copies salt[0]
     and salt[1] into its result, so a one-character field comes back as itself
     for every password.  login(1) and su(1) both guard against that with a
     strlen()==13 test; newgrp(1) and mgrlogin do not, so the length is checked
     here rather than assumed.
  2. passwd(1) as GUEST reads the old password back through crypt(3) and rewrites
     /etc/passwd, which it can only do through its setuid bit.  The new field
     must differ from the old one -- a passwd that silently changed nothing
     would otherwise pass.
  3. login(1) authenticates, writes /etc/utmp, and sets both ids; then newgrp
     into a group whose access list names the caller works, because getlogin()
     now has a record to find.  That member arm is the one thing this project
     has never been able to exercise.
"""
import re
import sys

FIELD = re.compile(r'^guest:([^:]*):8:5:', re.M)
IDS = re.compile(r'^privids ids ruid=(-?\d+) euid=(-?\d+) rgid=(-?\d+) egid=(-?\d+)',
                 re.M)

bad = 0
def ok(m):
    print("  ok   %s" % m)
def fail(m):
    global bad
    print("  FAIL %s" % m)
    bad += 1


def main():
    text = open(sys.argv[1], errors='replace').read()
    fields = FIELD.findall(text)
    ids = [tuple(int(x) for x in t) for t in IDS.findall(text)]

    # The transcript holds one `cat /etc/passwd' before any change, one after
    # root set a password, and one after guest changed it.  The echo of the
    # typed command contributes no match, because the regex anchors on the
    # entry itself.
    if len(fields) < 3:
        fail("only %d guest entries in the transcript (%r) -- the run did not "
             "get through the two passwd changes" % (len(fields), fields))
        return 1
    shipped, byroot, byguest = fields[0], fields[1], fields[2]
    if shipped == 'nologin':
        ok("the shipped field is `nologin': 7 characters, so login(1) and su(1) "
           "reject it on length and no shipped credential exists")
    else:
        fail("the shipped field is %r, expected `nologin'" % shipped)
    if len(byroot) == 13:
        ok("passwd(1) as root wrote a 13-character field (%s)" % byroot)
    else:
        fail("passwd(1) as root wrote a %d-character field %r -- anything but "
             "13 is unusable, and 1 accepts every password (T-57)"
             % (len(byroot), byroot))
    if len(byguest) == 13 and byguest != byroot:
        ok("passwd(1) as guest read the old password back and wrote a new "
           "13-character field (%s) -- through its setuid bit, since "
           "/etc/passwd is mode 644 root" % byguest)
    else:
        fail("passwd(1) as guest left the field %r (was %r)" % (byguest, byroot))

    # login(1), then the two newgrps.  The identity lines are positional: after
    # the login, after newgrp system, after newgrp user.
    if len(ids) < 3:
        fail("only %d identity lines after the login -- login(1) did not hand "
             "out a shell.  Look for `cannot lock terminal' (it needs "
             "/usr/spool/uucp) or a chdir failure on the home directory."
             % len(ids))
        return 1
    if ids[0] == (8, 8, 5, 5):
        ok("login(1) as guest set both ids: ruid=euid=8 rgid=egid=5")
    else:
        fail("after login the ids were %s, wanted (8, 8, 5, 5)" % (ids[0],))
    if ids[1][2] == 1 and ids[1][3] == 1 and ids[1][0] == 8:
        ok("newgrp into a group with no access list moved it to gid 1 (%s)"
           % (ids[1],))
    else:
        fail("after newgrp system the ids were %s, wanted rgid=egid=1, ruid=8"
             % (ids[1],))
    if ids[2][2] == 5 and ids[2][3] == 5 and ids[2][0] == 8:
        ok("newgrp into a group whose ACCESS LIST names guest moved it back to "
           "gid 5 (%s) -- the member arm, verified for the first time" % (ids[2],))
    elif 'not in access list' in text:
        fail("newgrp said 'not in access list' for a group guest IS in -- "
             "getlogin() found no /etc/utmp record even after a login")
    else:
        fail("after newgrp user the ids were %s, wanted rgid=egid=5" % (ids[2],))

    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
