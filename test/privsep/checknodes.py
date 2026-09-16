#!/usr/bin/env python3
"""checknodes.py <transcript> <nodes|nodemutant> -- judge one device-half run.

The device half asks two questions of one system.  Can an ordinary user still
see what a user is entitled to see -- the process table, the load average -- now
that /dev/kmem, /dev/mem, /dev/swap and every hd/rhd node is 600 root?  And is
the raw disk really shut to that user, or does the refusal come from somewhere
else?

A third question is asked of the same programs in both modes: the setuid bit
must buy the caller nothing beyond the three nodes ps and top open for
themselves, so ps(1) -k -- which lets the caller name the memory file -- must
refuse the guest /dev/mem and /dev/rhd4 while ps is still setuid, because
cmd/ps.c drops the privilege before its first open as soon as a file is named.

The first two answers are worthless on their own, so the mutant run takes the
setuid bit off ps and top (both must then FAIL) and afterwards puts the
nodes back to 666 (the guest must then read /etc/passwd off /dev/rhd4 and open
it for writing, and the named-file read that was refused must now succeed).
Every check below says which mode expects which answer, and
the ones that expect the same answer in both modes are the ones neither
mutation touches -- they are controls, and saying so is the point.

Exit 0 when every check answered as its mode requires.
"""
import re
import sys

RC = re.compile(r'^PRIV (?:END )?(\w+) rc=(\d+)', re.M)

bad = 0


def ok(m):
    print("  ok   %s" % m)


def fail(m):
    global bad
    print("  FAIL %s" % m)
    bad += 1


def block(text, tag):
    """The console output between `PRIV BEGIN tag' and `PRIV END tag'."""
    m = re.search(r'^PRIV BEGIN %s$(.*?)^PRIV END %s rc=' % (tag, tag),
                  text, re.M | re.S)
    return m.group(1) if m else ''


def want(rcs, tag, zero, what):
    """rc must be 0 (zero=True) or non-zero, or the answer is missing."""
    rc = rcs.get(tag)
    if rc is None:
        fail("%s: no answer" % what)
        return False
    if zero and rc == 0:
        ok(what)
        return True
    if not zero and rc != 0:
        ok("%s (rc=%d)" % (what, rc))
        return True
    fail("%s: rc=%d" % (what, rc))
    return False


def main():
    text = open(sys.argv[1], errors='replace').read()
    mode = sys.argv[2]
    rcs = dict((t, int(v)) for t, v in RC.findall(text))
    mutant = mode == 'nodemutant'

    # ------------------------------------------------------------- set-up
    for tag, what in (('ls', 'the nodes listed'),
                      ('mount', '/usr mounted'),
                      ('mutbits', 'the setuid mutation step'),
                      ('mutnodes', 'the device-mode mutation step')):
        want(rcs, tag, True, what)
    if bad:
        print("  (set-up failed: nothing below can be believed)")
        return 1

    # ------------------------------------- root still reaches every node
    # None of this depends on either mutation: these are the tools the policy
    # costs nothing, and they are run rather than asserted.
    # The checkers walk /dev/rhd2 (/usr/man, mounted read-only), not /dev/rhd3:
    # rc mounts hd3 read/write on /tmp and logs into it, so a checker reading
    # its raw device reports the inconsistency of a live filesystem -- exit 8
    # from icheck and dcheck -- which says nothing about privilege.
    # cmds-nodes.in carries the whole account.
    for tag, what in (
            ('check',  'check(1) reads /dev/rhd2 as root'),
            ('icheck', 'icheck(1) reads /dev/rhd2 as root'),
            ('dcheck', 'dcheck(1) reads /dev/rhd2 as root'),
            ('ncheck', 'ncheck(1) reads /dev/rhd2 as root'),
            ('rootraw3', 'dd(1) reads a block off /dev/rhd3 as root'),
            ('rootdf', 'df(1) opens the mounted device as root'),
            ('rootraw', 'dd(1) reads the /etc/passwd block off /dev/rhd4 as root'),
            ('rootwrite', 'the raw device is readable AND writable to root, '
                          'which is all mkfs(8) and dump(1) need of it')):
        want(rcs, tag, True, what)

    # ------------------------------------- the three namelist readers
    # 600 nodes plus the setuid bit must leave an ordinary user exactly as
    # informed as before; without the bit all three must stop dead, and the
    # message has to be the one about the node, not some later failure.
    psb, topb = block(text, 'ps'), block(text, 'top')
    if not mutant:
        if want(rcs, 'ps', True, 'ps(1) as guest'):
            rows = [l for l in psb.splitlines()
                    if re.search(r'\d+\s', l) and 'PID' not in l]
            if 'PID' in psb and len(rows) >= 2:
                ok("ps printed a header and %d process rows as guest" % len(rows))
            else:
                fail("ps exited 0 as guest but printed no table:\n%s" % psb)
        if want(rcs, 'top', True, 'top(1) -b as guest'):
            if 'processes:' in topb and 'Memory:' in topb:
                ok("top printed its process and memory summaries as guest")
            else:
                fail("top exited 0 as guest but printed no summary:\n%s" % topb)
    else:
        want(rcs, 'ps', False, 'ps(1) as guest with the setuid bit off')
        want(rcs, 'top', False, 'top(1) as guest with the setuid bit off')
        for what, b, needle in (('ps', psb, '/dev/mem'),
                                ('top', topb, '/dev/kmem')):
            if needle in b:
                ok("%s named the node it could not open (%s)" % (what, needle))
            else:
                fail("%s failed without naming a node -- a refusal for another "
                     "reason proves nothing:\n%s" % (what, b))

    # ------------------------------------- what the setuid bit does NOT buy
    # Both are run before either mutation, so ps is setuid root in both modes
    # and both modes want the same answer: this is the escalation arm, and it
    # is a control on the two checks above.  ps reads /dev/mem for its own
    # purposes one line earlier; asked to read the same node FOR THE CALLER it
    # must refuse, because naming a file makes cmd/ps.c setuid(getuid()) before
    # the first open.  /dev/rhd4 is the same refusal aimed at the raw disk,
    # which is the whole of the escalation: a setuid-root program that read a
    # named file with its effective uid would hand the guest /etc/passwd.
    for tag, b, node in (('psk', block(text, 'psk'), '/dev/mem'),
                         ('pskraw', block(text, 'pskraw'), '/dev/rhd4')):
        if want(rcs, tag, False,
                "guest cannot make setuid ps(1) read %s for him (-k)" % node):
            if 'annot open %s' % node in b:
                ok("and ps named the file it would not open for the guest "
                   "(%s), so the refusal is the privilege drop and not a "
                   "rejected flag" % node)
            else:
                fail("ps refused -k %s without naming it -- a refusal for "
                     "another reason proves nothing:\n%s" % (node, b))

    # ------------------------------------- the raw disk, kernel memory, swap
    # Before the second mutation in both runs, so both runs must refuse: this
    # is the control on the exploit below.
    for tag, what in (
            ('wrawroot', "open /dev/rhd4 for writing (the /etc/passwd path)"),
            ('wblkroot', "open /dev/hd4 for writing (the same span, buffered)"),
            ('wmem',     "open /dev/mem for writing"),
            ('wkmem',    "open /dev/kmem for writing"),
            ('wswap',    "open /dev/swap for writing"),
            ('rrawroot', "read the /etc/passwd block off /dev/rhd4"),
            ('testr',    "access(2) read on /dev/rhd4"),
            ('testw',    "access(2) write on /dev/rhd4")):
        want(rcs, tag, False, "guest is refused: %s" % what)
    if 'Cannot create /dev/rhd4' in text:
        ok('the shell named the refusal: "Cannot create /dev/rhd4"')
    else:
        fail("no shell refusal for /dev/rhd4 in the transcript -- the writes "
             "may have failed for another reason")

    # The floppy is the exception the policy keeps, and df is what it costs.
    want(rcs, 'testfd', True, "guest may read and write /dev/fd1")
    want(rcs, 'testrfd', True, "guest may read and write /dev/rfd1")
    want(rcs, 'guestdf', False,
         "df(1) as guest cannot open the device (the known cost of the policy)")

    # ------------------------------------------------------- the exploit
    ex = block(text, 'exploit')
    if mutant:
        if want(rcs, 'exploit', True,
                "with /dev/rhd4 back at 666 the guest reads the password file "
                "off the raw device"):
            if re.search(r'^root:', ex, re.M):
                ok("and the line it printed is root's own passwd entry, so the "
                   "600 modes are what closes this and nothing else is")
            else:
                fail("the read succeeded but printed no root entry:\n%s" % ex)
        want(rcs, 'wraw2', True,
             "and opens the same device for writing, which is the whole attack")
        want(rcs, 'psk2', True,
             "and with /dev/mem at 666 the named-file read ps refused earlier "
             "goes through, so that refusal measured the node mode and the "
             "privilege drop rather than ps declining the flag")
    else:
        want(rcs, 'exploit', False,
             "the exploit read is refused with the nodes as shipped")
        if re.search(r'^root:', ex, re.M):
            fail("a password entry reached the transcript from the raw device:\n%s"
                 % ex)
        else:
            ok("no password entry reached the transcript from the raw device")
        want(rcs, 'wraw2', False, "and the write is refused too")
        want(rcs, 'psk2', False,
             "and ps -k /dev/mem is still refused with the nodes as shipped")

    want(rcs, 'done', True, "the script ran to the end")
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
