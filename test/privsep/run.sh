#!/bin/sh
# tests/privsep/run.sh -- the privilege pass: everything this system does
# DIFFERENTLY for a user who is not root.
#
# WHY IT EXISTS.  Until dist/files/etc/passwd grew the `guest' account there was
# no interactive non-root account on any image, so every privilege-dependent
# path in the system had never been executed once.  Four utilities need the
# setuid bit because this kernel refuses the syscall outright without it --
# umknod() takes no type but IFPIPE from a non-super caller (sys/coh/sys2.c),
# iucheck() will not unlink an IFDIR (fs1.c) and ulink() will not link one
# (sys2.c) -- and a sweep found eleven wrong setuid modes on the images.  Fixing
# a mode proves nothing while root is the only user, because root needs none of
# them.
#
# WHAT IT CHECKS, and each of these can fail:
#
#   the modes		read out of the PACKED IMAGE, not the staging tree,
#			because that is how the eleven wrong ones were found
#   mkdir rmdir rm mv	the four kernel-forced utilities, run as guest
#   ownership		the directory mkdir(1) leaves behind belongs to the
#			REAL user, read back out of the packed bytes
#   newgrp		into a group with no access list (allowed), a group
#			with one (see below), and a group that does not exist
#   rmdir -f, chgrp	two refusals the programs make themselves
#   imode()		the owner, group and OTHER arms of sys/coh/fs1.c, in
#			both directions, including that a matching GROUP arm
#			refuses without falling through to a laxer other arm
#   real vs effective	getuid() survives a setuid exec, access(2) answers
#			for the real ids and open(2) for the effective ones, and
#			setuid(2) sets BOTH
#
#	sh run.sh		the checks on the system as committed
#	MODE=mutant sh run.sh	the same checks with the four setuid bits taken
#				off inside the guest -- they must FAIL
#	sh run.sh login		the login half (below)
#	sh run.sh nodes		the device half (below)
#	sh run.sh nodemutant	the device half with the bits and the modes put
#				back the way they were -- it must find the hole
#	sh run.sh gate		every half, and a verdict
#	KEEP=1 ...		leave the work directory
#
# THE MUTATION is `chmod 755' on mkdir, rmdir, rm, mv and newgrp, applied in the
# guest before any check runs.  That is exactly the defect the sweep found, it
# needs no rebuild, and it is confined to the throwaway copy of the image this
# harness boots.  A privilege test that passes because everything is root is the
# vacuous check this project keeps finding; a mutation gate enforces that a
# privilege loss must break the check.  The mutant half states, per check, whether
# losing the bit must break it: mkdir, rmdir, rm -r, a directory mv and newgrp
# must fail, while a file mv and a file rm must go on working, because unlinking
# and linking a REGULAR file needs no privilege at all.
#
# THE LOGIN HALF (`sh run.sh login', cmds-login.in).  Two checks need a
# credential and an /etc/utmp record, which `su' produces neither of: passwd(1)
# changing its OWN password, and newgrp into a group whose access list names the
# caller -- newgrp identifies the caller with getlogin(), which reads /etc/utmp,
# which only login(1) writes.  This harness types at the
# console rather than at a getty, so it runs login(1) itself from the shell it
# is already in -- the same program doing the same work -- and it works
# because /usr is mounted: login(1) locks the terminal under /usr/spool/uucp
# and chdir()s to the home directory, and without the mount it stops at
# "cannot lock terminal".
#
# THE DEVICE HALF (`sh run.sh nodes', `sh run.sh nodemutant', cmds-nodes.in).
# /dev/hd*, /dev/rhd*, /dev/swap, /dev/mem and /dev/kmem are 600
# root, and ps and top are setuid root because they read three of them -- the
# two such programs the delivered images install, and the only two.  The clean
# run says an ordinary user still gets a process listing, a load average and no
# way into the raw disk; that the setuid bit buys him nothing beyond that, since
# ps(1) -k, which names the memory file, drops the privilege before its first
# open and so refuses the guest both /dev/mem and /dev/rhd4; and the mutant run
# puts the nodes back to 666 -- which is what they shipped as -- and shows the
# guest reading the /etc/passwd block straight off /dev/rhd4 and opening it for
# writing, i.e. the hole the modes close, exercised rather than asserted.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
HB=$OS/hostbuild
DIST=${DIST:-coherent3-full-test}
MODE=${MODE:-clean}

case ${1:-} in
gate)	# Every half, each in its own run, and a verdict over them.
	bad=0
	MODE=clean  sh "$0" || bad=1
	echo
	MODE=mutant sh "$0" || bad=1
	echo
	sh "$0" login || bad=1
	echo
	sh "$0" nodes || bad=1
	echo
	sh "$0" nodemutant || bad=1
	echo
	if [ $bad = 0 ]; then
		echo "privsep gate: PASS -- the checks hold on the system as"
		echo "              committed, break when the setuid bits go, a"
		echo "              real login(1) reaches the two su cannot, and"
		echo "              the raw disk is shut to an ordinary user"
		exit 0
	fi
	echo "privsep gate: FAIL"
	exit 1;;
login)	MODE=login;;
nodes)	MODE=nodes;;
nodemutant) MODE=nodemutant;;
esac

C900_ROOT="$OS"
. "$C900_ROOT/mk/emulator.sh"
emu_need "boot the target and run the privilege pass"
. "$C900_ROOT/mk/dist.sh"
# The image and the filesystem reader are the distribution repository's, not
# this tree's (mk/dist.sh); a copy left in this tree's build/ would be read
# while the transcript came from somewhere else.
IMG=$(dist_img "$DIST") || exit 2
FSREAD=$C900_DIST/os/hostbuild
OS=$OS . "$HB/toolchain.sh"
CCZ=$TC/ccz

BAD=0
fail() { echo "  FAIL $*"; BAD=$((BAD + 1)); }
ok()   { echo "  ok   $*"; }

for f in "$IMG" "$FSREAD/fsread.py"; do
	[ -e "$f" ] || { echo "privsep: missing $f -- cannot run" >&2; exit 2; }
done

# The partition offsets are read out of the image rather than named: the media
# descriptors move them (hd21 and hd42 disagree about /usr), and a stale number
# reads a filesystem that is not the one under test.  Root is the partition that
# has /etc/passwd; /usr is the one that has /spool.
eval "$(python3 - "$IMG" "$FSREAD" 2>/dev/null <<'PY'
import sys
img, fsdir = sys.argv[1], sys.argv[2]
sys.path.insert(0, fsdir)
import fsread
data = open(img, 'rb').read()
BS = 512
root = usr = ''
# Every block, not every eighth: fsread.partitions() steps by 8 and /usr on the
# 42 MB media starts at 44105.  A block that merely looks like a superblock is
# discarded by trying to read a directory out of it.
for base in range(0, len(data) // BS):
    sb = data[(base + 1) * BS:(base + 1) * BS + BS]
    if len(sb) < BS:
        break
    isize, fsize = fsread.u16(sb, 0), fsread.u32(sb, 2)
    if not (2 < isize < 2000 and isize < fsize <= (len(data) // BS) - base):
        continue
    fs = fsread.Fs(data, base)
    for path, which in (('/etc/passwd', 'root'), ('/spool', 'usr')):
        try:
            fs.lookup(path)
        except BaseException:
            # fsread.lookup raises SystemExit for a missing path, and a false
            # superblock raises IndexError out of the block walk.
            continue
        if which == 'root':
            root = base
        else:
            usr = base
        break
print("ROOTPART=%s USRPART=%s" % (root, usr))
PY
)"
[ -n "${ROOTPART:-}" ] || { echo "privsep: no root filesystem in $IMG" >&2; exit 2; }

WORK=${WORK:-$HERE/work.$MODE}
rm -rf "$WORK"; mkdir -p "$WORK"
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$WORK"' 0 1 2 15

echo "== privsep ($MODE) on $DIST"

# ---------------------------------------------------------------- phase 0
# The modes, out of the packed image.  A staged tree can be right while the
# packer writes something else, which is how the eleven wrong modes survived.
echo "phase 0: the setuid modes and the device modes in the packed image"
python3 - "$IMG" "$ROOTPART" "$FSREAD" <<'PY' || BAD=$((BAD + 1))
import sys
img, part, fsdir = sys.argv[1:4]
sys.path.insert(0, fsdir)
import fsread

data = open(img, 'rb').read()
fs = fsread.Fs(data, int(part))
bad = 0

def look(f, path):
    try:
        return f.inode(f.lookup(path))
    except SystemExit:
        return None

# The four the kernel forces, then the three that authenticate, then the two
# namelist readers -- ps and top read /dev/kmem, /dev/mem and /dev/swap, which
# are 600 root, and are setuid root so an ordinary user can.  4755 is what
# dist/lists/base.list and dist/lists/runtime.list state for each; anything
# else is the bug returning.
for path in ('/bin/mkdir', '/bin/rmdir', '/bin/rm', '/bin/mv',
             '/bin/newgrp', '/bin/passwd', '/bin/su', '/bin/login',
             '/bin/ps', '/bin/top'):
    e = look(fs, path)
    if e is None:
        print("  FAIL %-12s absent" % path)
        bad += 1
    elif e['mode'] & 0o4000 and e['uid'] == 0:
        print("  ok   %-12s %o uid %d" % (path, e['mode'], e['uid']))
    else:
        print("  FAIL %-12s %o uid %d -- wanted setuid root" %
              (path, e['mode'], e['uid']))
        bad += 1

# The device nodes, both directions: every node the policy names must have the
# mode named, and no OTHER node in /dev may be group- or world-writable unless
# it is one a user is meant to write.  A table walked only rows-to-tree cannot
# see a node nobody judged.
WANT = {}
# No `dump' node: the distribution's devices file states why -- with wd(4)'s
# minor being drive<<4|partition the only sequential medium is the floppy, so a
# node called `dump' could only be a second name for it, and `dump 0u /dev/hd4'
# would then overwrite whatever disc is in the drive without ever naming it.
for n in ('hd0', 'hd1', 'hd2', 'hd3', 'hd4', 'hd6',
          'rhd0', 'rhd1', 'rhd2', 'rhd3', 'rhd4', 'rhd6',
          'swap', 'mem', 'kmem'):
    WANT[n] = 0o600
for n in ('fd1', 'rfd1'):
    WANT[n] = 0o666
# Writable to everyone by design, and each for a reason: the terminals a user
# owns or shares, the bit bucket, the printer, the pty slaves, the CP/M
# partition (a filesystem of no COHERENT user's), and the mouse pair.
OPEN_OK = set(('null', 'tty', 'lp', 'rlp', 'cpma', 'rcpma', 'fd1', 'rfd1'))
devino = fs.lookup('/dev')
seen = {}
for ino, name in fs.readdir(fs.inode(devino)):
    if name in ('.', '..'):
        continue
    seen[name] = fs.inode(ino)['mode'] & 0o7777
for name in sorted(WANT):
    got = seen.get(name)
    if got is None:
        print("  FAIL /dev/%-6s absent" % name)
        bad += 1
    elif got == WANT[name]:
        print("  ok   /dev/%-6s %o" % (name, got))
    else:
        print("  FAIL /dev/%-6s %o -- wanted %o" % (name, got, WANT[name]))
        bad += 1
for name in sorted(seen):
    if name in WANT or name in OPEN_OK:
        continue
    if seen[name] & 0o022 and not name.startswith('tty'):
        print("  FAIL /dev/%-6s %o is writable by others and is judged by "
              "nothing -- add it to this table or shut it" % (name, seen[name]))
        bad += 1
sys.exit(1 if bad else 0)
PY

# ---------------------------------------------------------------- phase 1
case $MODE in
nodes|nodemutant)
	echo "phase 1: stage a test image and the device script"
	cp --reflink=auto -f "$IMG" "$WORK/priv.bin" 2>/dev/null \
		|| cp -f "$IMG" "$WORK/priv.bin"
	# The block, within the root partition, that holds the first 512 bytes of
	# /etc/passwd.  The raw-disk path is only demonstrated by reading the
	# actual password file off the actual device, and a guest `dd' needs the
	# offset; scanning 6 MB inside the emulator is not a test, it is a wait.
	PWBLK=$(python3 - "$WORK/priv.bin" "$ROOTPART" "$FSREAD" <<'PY'
import sys
img, part, fsdir = sys.argv[1], int(sys.argv[2]), sys.argv[3]
sys.path.insert(0, fsdir)
import fsread
fs = fsread.Fs(open(img, 'rb').read(), part)
ino = fs.inode(fs.lookup('/etc/passwd'))
print(next(iter(fs.blocks(ino))))
PY
)
	ok "/etc/passwd starts at block $PWBLK of the root partition"
	# Two mutations at two points: the setuid bits first, so the three
	# readers are measured against the nodes as shipped, and the node modes
	# afterwards, because a 666 /dev/kmem hides everything the first one did.
	if [ "$MODE" = nodemutant ]; then
		MUTB='chmod 755 /bin/ps /bin/top; echo PRIV mutbits rc=$?'
		MUTN='chmod 666 /dev/rhd4 /dev/hd4 /dev/mem /dev/kmem /dev/swap; echo PRIV mutnodes rc=$?'
	else
		MUTB='echo PRIV mutbits rc=0'
		MUTN='echo PRIV mutnodes rc=0'
	fi
	sed -e "s|@MUTBITS@|$MUTB|" -e "s|@MUTNODES@|$MUTN|" \
		-e "s|@PWBLK@|$PWBLK|" "$HERE/cmds-nodes.in" > "$WORK/cmds"
	ok "command script: $(grep -c . "$WORK/cmds") lines"
	;;
*)
echo "phase 1: build privids and stage a test image"
[ -e "$CCZ" ] || { echo "privsep: missing $CCZ -- cannot run" >&2; exit 2; }
[ -e "$HERE/../rawalign/inject.py" ] || {
	echo "privsep: tests/rawalign/inject.py is not in this tree -- the" >&2
	echo "         privids phases need it to place a program in the image." >&2
	exit 2; }
if "$CCZ" -s -i -I "$OS/include" -I "$OS/include/sys" \
	-o "$WORK/privids" "$HERE/privids.c" > "$WORK/cc.log" 2>&1; then
	ok "privids: $(wc -c < "$WORK/privids") B"
else
	fail "privids did not compile: $(tail -3 "$WORK/cc.log")"
	echo "privsep: cannot continue"; exit 1
fi
# Over /bin/factor: no new inode and no directory surgery, and factor is on no
# path this test walks.  Mode 755 root, so guest can run it, and a COPY of it
# is what becomes the setuid-root reporter inside the guest.
cp --reflink=auto -f "$IMG" "$WORK/priv.bin" 2>/dev/null || cp -f "$IMG" "$WORK/priv.bin"
python3 "$HERE/../rawalign/inject.py" "$WORK/priv.bin" "$ROOTPART" \
	"$WORK/privids" /bin/factor > "$WORK/inj.log" 2>&1 \
	|| { fail "inject privids over /bin/factor"; cat "$WORK/inj.log"; exit 1; }
ok "privids staged over /bin/factor"

# The mutation, or nothing.
if [ "$MODE" = login ]; then
	cp -f "$HERE/cmds-login.in" "$WORK/cmds"
else
	if [ "$MODE" = mutant ]; then
		MUT='chmod 755 /bin/mkdir /bin/rmdir /bin/rm /bin/mv /bin/newgrp; echo PRIV mutate rc=$?'
	else
		MUT='echo PRIV mutate rc=0'
	fi
	awk -v mut="$MUT" '{ if ($0 == "@MUTATE@") print mut; else print }' \
		"$HERE/cmds-priv.in" > "$WORK/cmds"
fi
ok "command script: $(grep -c . "$WORK/cmds") lines"
	;;
esac

# ---------------------------------------------------------------- phase 2
echo "phase 2: run it"
TAG=privsep-$MODE
# emu-run.sh takes a dist NAME and resolves it in the distribution repository,
# so the scratch image has to be put where that resolution looks.
cp -f "$WORK/priv.bin" "$C900_IMGDIR/$TAG.bin"
rwork=$WORK/work.bin; rout=$WORK/out; rerr=$WORK/err
WORK=$rwork OUT=$rout ERR=$rerr EMUWAIT=${EMUWAIT:-900} \
	sh "$HB/emu-run.sh" "$WORK/cmds" "$TAG" > "$WORK/harness" 2>&1
rm -f "$C900_IMGDIR/$TAG.bin"
if [ -s "$WORK/out" ]; then
	ok "transcript: $(wc -l < "$WORK/out") lines"
else
	fail "no transcript"; sed 's/^/       | /' "$WORK/harness"
	echo "privsep: cannot continue"; exit 1
fi
grep -n '^PRIV \|^privids \|not allowed\|not a super user\|only superuser\|access list\|non-existent\|not the super-user\|cannot change group\|annot open\|Permission' \
	"$WORK/out" | sed 's/^/       | /'

# ---------------------------------------------------------------- phase 3
echo "phase 3: the answers"
case $MODE in
login)		python3 "$HERE/checklogin.py" "$WORK/out" || BAD=$((BAD + 1));;
nodes|nodemutant)
		python3 "$HERE/checknodes.py" "$WORK/out" "$MODE" \
			|| BAD=$((BAD + 1));;
*)		python3 "$HERE/check.py" "$WORK/out" "$MODE" || BAD=$((BAD + 1));;
esac

# ---------------------------------------------------------------- phase 4
# What the run left on the disk, read out of the packed bytes.  mkdir(1) mknod()s
# with euid 0 and then chown()s to getuid()/getgid(), so a directory owned by
# root here would mean the real ids did not survive the setuid exec.
case $MODE in
nodes|nodemutant)
	# The device half writes nothing it does not read back through the
	# device itself, inside the run, so there is nothing here to look at.
	;;
*)
echo "phase 4: what it left on the disk"
python3 - "$WORK/work.bin" "$ROOTPART" "$USRPART" "$FSREAD" "$MODE" <<'PY' || BAD=$((BAD + 1))
import sys
img, rpart, upart, fsdir, mode = (sys.argv[1], int(sys.argv[2]),
                                  int(sys.argv[3]), sys.argv[4], sys.argv[5])
sys.path.insert(0, fsdir)
import fsread

data = open(img, 'rb').read()
root = fsread.Fs(data, rpart)
usr = fsread.Fs(data, upart)
bad = 0

def look(fs, path):
    try:
        return fs.inode(fs.lookup(path))
    except SystemExit:
        return None

def want_owner(fs, path, uid, gid, what):
    global bad
    e = look(fs, path)
    if e is None:
        print("  FAIL %s does not exist -- %s" % (path, what)); bad += 1
    elif e['uid'] == uid and e['gid'] == gid:
        print("  ok   %s is uid %d gid %d -- %s" % (path, uid, gid, what))
    else:
        print("  FAIL %s is uid %d gid %d, wanted %d/%d -- %s" %
              (path, e['uid'], e['gid'], uid, gid, what)); bad += 1

want_owner(usr, '/guest', 8, 5, "the home directory root made for guest")

if mode == 'login':
    pass
elif mode == 'clean':
    want_owner(root, '/privsep/keep', 8, 5,
               "mkdir(1) chowns the new directory to the REAL uid and gid")
    if look(root, '/privsep/gdir') is None:
        print("  ok   /privsep/gdir is gone -- rmdir(1) as guest unlinked an IFDIR")
    else:
        print("  FAIL /privsep/gdir survived the rmdir"); bad += 1
else:
    if look(root, '/privsep/keep') is None:
        print("  ok   /privsep/keep was never created -- mkdir without the bit "
              "cannot mknod an IFDIR")
    else:
        print("  FAIL /privsep/keep exists: mkdir worked with the setuid bit off, "
              "so this gate proves nothing"); bad += 1

sys.exit(1 if bad else 0)
PY
	;;
esac

echo
if [ "$BAD" = 0 ]; then
	if [ "$MODE" = clean ]; then
		echo "privsep ($MODE): PASS -- an ordinary user can make, move and"
		echo "                 remove directories, changes group, and is"
		echo "                 refused everything it should be"
	elif [ "$MODE" = login ]; then
		echo "privsep ($MODE): PASS -- login(1) authenticates the account,"
		echo "                 passwd(1) changes its own password, and"
		echo "                 newgrp enters a group by access list"
	elif [ "$MODE" = nodes ]; then
		echo "privsep ($MODE): PASS -- an ordinary user still gets a process"
		echo "                 listing and a load average, and cannot read or"
		echo "                 write the raw disk, kernel memory or swap"
	elif [ "$MODE" = nodemutant ]; then
		echo "privsep ($MODE): PASS -- with the nodes back at 666 the guest"
		echo "                 reads /etc/passwd off /dev/rhd4 and opens it"
		echo "                 for writing, so the clean half is not passing"
		echo "                 because the attempt was never possible"
	else
		echo "privsep ($MODE): PASS -- with the setuid bits off the same"
		echo "                 operations fail, so the clean half is not"
		echo "                 passing because everything is root"
	fi
	exit 0
fi
echo "privsep ($MODE): FAIL ($BAD)"
exit 1
