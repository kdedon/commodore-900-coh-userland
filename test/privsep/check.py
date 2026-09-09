#!/usr/bin/env python3
"""check.py <transcript> <clean|mutant> -- judge one privsep run.

Every check names the tag its command echoed and what the answer must be, and
says so per MODE: the mutant run has the four kernel-forced setuid bits taken
off, so the checks that depend on one must FAIL there.  A check with the same
expectation in both modes is one the bit has nothing to do with, and saying so
is the point -- `rm' on a regular file and `mv' of a regular file need no
privilege at all, and a gate that expected them to break would be asserting
something untrue.

Exit 0 when every check answered as its mode requires.
"""
import re
import sys

RC = re.compile(r'^PRIV (\w+) rc=(\d+)', re.M)
IDS = re.compile(r'^privids ids ruid=(-?\d+) euid=(-?\d+) rgid=(-?\d+) egid=(-?\d+)',
                 re.M)
FILEANS = re.compile(r'^privids (read|write|access) (\S+) (ALLOWED|DENIED)'
                     r'(?: errno=(\d+))?(?: bytes=\d+ text=(\S*))?', re.M)

GUEST_UID, GUEST_GID = 8, 5
EACCES = 13

bad = 0
def ok(m):
    print("  ok   %s" % m)
def fail(m):
    global bad
    print("  FAIL %s" % m)
    bad += 1


def main():
    text = open(sys.argv[1], errors='replace').read()
    mode = sys.argv[2]
    rcs = dict((t, int(v)) for t, v in RC.findall(text))
    ids = IDS.findall(text)
    answers = FILEANS.findall(text)

    # ------------------------------------------------------------ set-up
    for tag, what in (('mount', '/usr mounted'),
                      ('home', "guest's home directory made"),
                      ('sfactor', 'a setuid-root reporter built'),
                      ('mutate', 'the mutation step')):
        if rcs.get(tag) == 0:
            ok("%s" % what)
        else:
            fail("%s: rc=%s" % (what, rcs.get(tag)))
    if bad:
        print("  (set-up failed: nothing below can be believed)")
        return 1

    # ------------------------------------------------------------ identity
    # Three `privids ids' lines in order: root, guest, guest through a
    # setuid-root program.  The third is the whole real-vs-effective question.
    if len(ids) < 3:
        fail("only %d identity lines -- the run did not get that far" % len(ids))
        return 1
    got = [tuple(int(x) for x in t) for t in ids[:3]]
    if got[0] == (0, 0, got[0][2], got[0][3]):
        ok("the single-user shell is root (ruid %d euid %d)" % got[0][:2])
    else:
        fail("the single-user shell reported %s" % (got[0],))
    if got[1] == (GUEST_UID, GUEST_UID, GUEST_GID, GUEST_GID):
        ok("su(1) put both ids down to guest: ruid=euid=%d rgid=egid=%d"
           % (GUEST_UID, GUEST_GID))
    else:
        fail("after su guest the ids were %s, wanted (8, 8, 5, 5) -- setuid(2) "
             "sets BOTH (sys/coh/sys1.c usetuid), so anything else is a kernel "
             "answer sudo (#279) must not be given" % (got[1],))
    if got[2][0] == GUEST_UID and got[2][1] == 0:
        ok("a setuid-root program run by guest has ruid=%d euid=0 -- getuid() "
           "survives the setuid exec, which is what #279 will read" % GUEST_UID)
    else:
        fail("a setuid-root program run by guest reported %s, wanted ruid=8 "
             "euid=0" % (got[2],))

    # ------------------------------------------------------------ the four
    # Per tag: (needs the setuid bit, what it is).
    forced = [
        ('keep',      True,  'mkdir a directory as guest'),
        ('mkdir',     True,  'mkdir a second directory as guest'),
        ('mkdirsub',  True,  'mkdir a subdirectory inside a guest-owned one '
                             '(mkdir(1) access(2)es the parent as the real user)'),
        ('mvdir',     True,  'mv a DIRECTORY as guest (ulink of an IFDIR)'),
        ('rmr',       True,  'rm -r a directory tree as guest'),
        ('rmdir',     True,  'rmdir a directory as guest (iucheck of an IFDIR)'),
        ('innerfile', False, 'create a regular file as guest'),
        ('mvfile',    False, 'mv a regular file as guest'),
        ('rmfile',    False, 'rm a regular file as guest'),
    ]
    for tag, needsbit, what in forced:
        rc = rcs.get(tag)
        if rc is None:
            fail("%s: no answer" % what)
            continue
        wantfail = needsbit and mode == 'mutant'
        if wantfail and rc != 0:
            ok("%s failed (rc=%d) with the setuid bit off, as it must" % (what, rc))
        elif wantfail and rc == 0:
            fail("%s SUCCEEDED with the setuid bit off -- then the clean run "
                 "proves nothing about the bit" % what)
        elif not wantfail and rc == 0:
            ok("%s" % what)
        else:
            fail("%s: rc=%d" % (what, rc))

    # ------------------------------------------------- refusals owned by the
    # programs themselves.  Neither depends on a setuid bit, so both modes want
    # the same answer, and each must refuse with its own words.
    for tag, needle, what in (
            ('rmdirf', 'only superuser can force',
             "rmdir -f as guest (cmd/rmdir.c tests getuid())"),
            ('chgrp', 'not a super user',
             "chgrp as guest (cmd/chgrp.c tests geteuid())"),
            ('passwdroot', 'not allowed to change password',
             "passwd root as guest (cmd/passwd.c tests getuid() inside a "
             "setuid-root program)")):
        rc = rcs.get(tag)
        if rc is None:
            fail("%s: no answer" % what)
        elif rc == 0:
            fail("%s was ALLOWED -- the refusal expected did not happen" % what)
        elif needle in text:
            ok('%s refused: "%s"' % (what, needle))
        else:
            fail("%s exited %d but never printed %r" % (what, rc, needle))

    # ------------------------------------------------------------ imode()
    # (file, verb, want, why).  The group and other arms of sys/coh/fs1.c had
    # never been executed; p604g is the one that matters most -- a matching
    # GROUP arm must refuse rather than fall through to the laxer other arm.
    matrix = [
        ('/tmp/p600',  'read',  'DENIED',
         'mode 600 root:system -- the OTHER arm is 0'),
        ('/tmp/p604',  'read',  'ALLOWED',
         'mode 604 root:system -- the OTHER arm grants read'),
        ('/tmp/p640g', 'read',  'ALLOWED',
         'mode 640 root:user -- the GROUP arm grants read to a member'),
        ('/tmp/p604g', 'read',  'DENIED',
         'mode 604 root:user -- the GROUP arm matches and is 0, and imode() '
         'must NOT fall through to the other arm that would allow it'),
        ('/tmp/p060',  'read',  'DENIED',
         'mode 060 guest:system -- the OWNER arm matches and is 0, so the '
         'group bits set on it do not help their own owner'),
        ('/tmp/p444',  'write', 'DENIED',
         'mode 444 -- IPW is refused to everyone but root'),
    ]
    seen = {}
    for verb, path, verdict, errno, textv in answers:
        seen.setdefault((path, verb), (verdict, errno, textv))
    for path, verb, want, why in matrix:
        got = seen.get((path, verb))
        if got is None:
            fail("no %s answer for %s (%s)" % (verb, path, why))
            continue
        verdict, errno, textv = got
        if verdict != want:
            fail("%s %s was %s, wanted %s -- %s" % (verb, path, verdict, want, why))
        elif want == 'DENIED' and int(errno or 0) != EACCES:
            fail("%s %s was denied with errno %s, wanted EACCES %d -- a "
                 "refusal for the wrong reason is not the refusal under test"
                 % (verb, path, errno, EACCES))
        elif want == 'ALLOWED' and verb == 'read' and not textv:
            fail("%s %s opened but read nothing -- an empty file would pass "
                 "this check for the wrong reason" % (verb, path))
        else:
            ok("%s %s %s -- %s" % (verb, path, verdict, why))

    # ---------------------------------------------- access(2) vs open(2)
    # The pair on one file inside one setuid-root program.  access() is answered
    # for the real ids (uaccess() calls schizo() around the lookup) and open()
    # for the effective ones, and mkdir(1) depends on exactly that.
    a600, sa600, sr600 = rcs.get('a600'), rcs.get('sa600'), rcs.get('sr600')
    if a600 and sa600 and sr600 == 0:
        ok("in a setuid-root program access(2) refuses /tmp/p600 (real ids) "
           "while open(2) allows it (effective ids) -- the two answers a "
           "setuid program has to be able to tell apart")
    else:
        fail("access/open split: guest access rc=%s, setuid access rc=%s, "
             "setuid open rc=%s -- wanted non-zero, non-zero, 0"
             % (a600, sa600, sr600))

    # ------------------------------------------------------------ newgrp
    # `system' has no access list and no password, so any caller may enter it;
    # that is the path the setuid bit is needed for, since setgid(2) is refused
    # to an unprivileged process.  A group WITH an access list is the phase this
    # harness cannot reach: newgrp identifies the caller with getlogin(), which
    # reads /etc/utmp, which only login(1) writes.
    ng = rcs.get('ngsystem')
    if mode == 'clean':
        if ng == 0 and len(ids) > 3:
            r = tuple(int(x) for x in ids[3])
            if r[0] == GUEST_UID and r[2] == 1 and r[3] == 1:
                ok("newgrp into a group with no access list changed guest's "
                   "group to 1 and left its uid at %d (ids %s)" % (GUEST_UID, r))
            else:
                fail("after newgrp system the ids were %s, wanted ruid=8 "
                     "rgid=egid=1" % (r,))
        else:
            fail("newgrp system: rc=%s and %d identity lines" % (ng, len(ids)))
    else:
        if ng != 0:
            ok("newgrp failed (rc=%s) with its setuid bit off, as it must -- "
               "setgid(2) is refused to an unprivileged process" % ng)
        else:
            fail("newgrp SUCCEEDED with the setuid bit off")

    if 'not in access list' in text:
        ok('newgrp into a group WITH an access list answered "not in access '
           'list" -- correct here and for the recorded reason: getlogin() has '
           'no /etc/utmp record to find in single user, so the member arm '
           'itself is still unverified (needs a real login)')
    else:
        fail("newgrp into a group with an access list did not say 'not in "
             "access list' -- read the transcript before believing anything "
             "about the member arm")
    if 'non-existent group' in text:
        ok('newgrp into a group that does not exist answered "non-existent '
           'group"')
    else:
        fail("newgrp nosuchgroup did not report a non-existent group")

    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
