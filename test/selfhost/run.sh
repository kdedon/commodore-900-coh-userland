#!/bin/sh
# tests/selfhost/run.sh -- compile and link ON the target, from the compiler
# THE IMAGE ITSELF SHIPS.  Nothing is mapped in from the host.
#
# Next to tests/buildenv, and deliberately its mirror image.  buildenv boots
# coherent3-buildenv -- a dist that carries NO compiler -- and maps a compiler
# environment in from the host on a rendered medium; what it proves is that the
# compiler RUNS on the machine when it is present.  This harness gives the
# machine ONE medium, the image: it boots coherent3-dev, which stages
# lists/toolchain.list (/usr/bin/cc, /usr/lib/cc0-2, as, ld, libc.a and
# /usr/include), and asserts that the machine compiled its own source with its
# own tools.  The two together are the whole claim: the compiler works there,
# and it is there.
#
# "Nothing mapped in" is asserted, not assumed -- see assert_media.  Each run is
# required to show, from emu-run.sh's per-medium record, from the emulator it
# names, and from the guest's own mount table, that the machine had the image
# and nothing else; $FLOPPY, which would attach a second medium outright, is
# cleared.
#
# WHY THE EMULATOR.  emu-run.sh boots the machine and logs in at the multi-user
# login prompt, so /etc/rc has already mounted /usr -- which is where the
# compiler lives on media/hd42-coh.media.  The first command lists the mount
# table rather than mounting anything: what the run needs is that /usr is
# there, and the same listing is the guest's own account of every filesystem it
# can reach, which is what phase 2's media claims read.
#
# THE SOURCE IS TYPED AT THE CONSOLE, one echo per translation unit, because
# `cat > file' cannot be terminated without a Ctrl-D.  It carries a NONCE
# minted per run: phase 1 asserts the nonce is not already on the image, so
# printing it can only mean the guest compiled the source it was handed.  Note
# that COHERENT's echo expands \n, so a string literal here may not contain one
# -- putchar(10) is the newline.
#
# Phase 3 is the negative control, and it is what makes phase 2 mean anything:
# the identical commands on coherent3-full-test -- the same media, the same
# userland, the same console, WITHOUT lists/toolchain.list -- must fail every
# assertion.
# A green run there would mean the assertions measure something other than the
# compiler being on the disk.
#
# Cost: two emulator boots, about 3 minutes.  Host-side only; no image is
# rebuilt and nothing is installed on the target.
#
#	sh run.sh		positive + negative control
#	KEEP=1 sh run.sh	leave the work directory for inspection
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
HB="$OS/hostbuild"
# The dist that SHIPS a compiler, and the one that does not.  If a second dist
# ever includes lists/toolchain.list, this is the variable to point at it; the
# control must stay a dist on the same media with the same userland, so that
# the only difference between the two runs is the component under test.
DIST=${DIST:-coherent3-dev}
CTRL=${CTRL:-coherent3-full-test}

BAD=0
fail() { echo "  FAIL $*"; BAD=$((BAD + 1)); }
ok()   { echo "  ok   $*"; }

C900_ROOT="$OS"
# The image, and the tool that reads a packed COHERENT filesystem, are both the
# distribution repository's -- see mk/dist.sh.  A copy of fsread.py has not
# lived in this tree since the split, so it is spelled from $C900_DIST and
# never from $HB.
. "$C900_ROOT/mk/dist.sh"
FSREAD="$C900_DIST/os/hostbuild/fsread.py"

IMG=$(dist_img "$DIST") || exit 2
# The control image is resolved HERE, before three minutes of emulator time, so
# that "the control was never packed" is a refusal at the start rather than a
# failed control at the end -- which reads as the assertions being weak.
dist_img "$CTRL" > /dev/null || exit 2

WORK=${WORK:-$HERE/work}
rm -rf "$WORK"
mkdir -p "$WORK"
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$WORK"' 0 1 2 15

NONCE=SELFHOST-$$-$(date +%s)
echo "selfhost: dist $DIST, control $CTRL, nonce $NONCE"

# ---------------------------------------------------------------- phase 0
# The compiler is IN THE IMAGE, asserted from the packed bytes before anything
# is booted: a boot that cannot find cc and a boot that failed look alike on a
# console, and only one of them is this harness's business.
# /usr IS ITS OWN FILESYSTEM on media/hd42-coh.media (hd6, block 44105), so the
# paths below are spelled as that filesystem sees them -- bin/cc, not
# /usr/bin/cc.  fsread.py reads one partition at a time and the mount point is
# not inside it.
echo "phase 0: the packed image carries the compiler"
USRPART=${USRPART:-44105}
for p in /bin/cc /bin/as /bin/ld /lib/cc0 /lib/cc1 \
	 /lib/cc2 /lib/crts0.o /lib/libc.a /include/stdio.h
do
	if python3 "$FSREAD" "$IMG" cat "$p" --part "$USRPART" \
		> "$WORK/probe" 2>/dev/null && [ -s "$WORK/probe" ]
	then
		ok "/usr$p is on the image ($(wc -c < "$WORK/probe") bytes)"
	else
		fail "/usr$p is not on the image's /usr filesystem"
	fi
done

# ---------------------------------------------------------------- phase 1
# The nonce is not already there.  Without this, a string the image happened to
# contain would satisfy phase 2 without a compile.
echo "phase 1: the nonce is not on the image already"
if grep -aq "$NONCE" "$IMG"; then
	fail "$NONCE already appears in $DIST.bin"
else
	ok "$NONCE appears nowhere in the packed image"
fi

# The commands, identical for both runs.  Two translation units, so ld has two
# objects to resolve a symbol across and the run proves a LINK and not only a
# compile.
cat > "$WORK/cmds" <<EOF
PATH=/bin:/usr/bin:/etc
export PATH
/etc/mount
echo 'extern char *msg(); main() { printf("%s", msg()); putchar(10); }' > /a.c
echo 'char *msg() { return "$NONCE"; }' > /b.c
cc -o /prog /a.c /b.c
ls -l /prog
/prog
EOF

# The three per-run paths are computed into variables FIRST.  A command prefix
# of `WORK=$WORK/x OUT=$WORK/y' is not safe: the shells here perform the
# assignments left to right and the second expansion sees the first, so OUT
# landed inside the image copy's own name and emu-run could not create it.
run() {		# run <tag> <dist>
	tag=$1
	rwork=$WORK/work.$tag.bin
	rout=$WORK/$tag.out
	rerr=$WORK/$tag.err
	# FLOPPY is cleared, not passed through: it attaches a second medium,
	# and there is no reading of this harness under which a caller's floppy
	# is a legitimate override.  Emptied rather than unset, since `VAR= cmd'
	# is what stops an exported one reaching the child.
	#
	# C900_EMU/EMU/EMUBIN are passed through -- release-check hands the
	# sweep's emulator down that way, and a harness that ignored it would
	# boot whatever its own search found.  What they can smuggle in is
	# covered by assert_media instead: test/buildenv maps its compiler in
	# through a WRAPPER named by C900_EMU, and a wrapper's own --floppy
	# never reaches emu-run.sh's record.
	FLOPPY= WORK=$rwork OUT=$rout ERR=$rerr \
		sh "$HB/emu-run.sh" "$WORK/cmds" "$2" > "$WORK/$tag.harness" 2>&1
	[ -s "$WORK/$tag.out" ]
}

# assert_media <tag> -- nothing was mapped in from the host, and /usr was
# there.  Asserted for BOTH runs, and separately from assert_run, because it
# must HOLD on the control: the control differs from the positive run in the
# dist it boots and in nothing else, which is the only reason phase 3 says
# anything about the compiler.
#
# Both records are the run's own.  emu-run.sh prints a line per medium it
# attaches; the guest prints its mount table, which is every filesystem it
# reached whatever the emulator was told to attach.
assert_media() {
	h=$WORK/$1.harness
	t=$WORK/$1.out
	if grep -q "^=== medium disk $WORK/work\.$1\.bin\$" "$h"; then
		ok "$1: the machine's only disk is this run's copy of the image"
	else
		fail "$1: emu-run.sh did not record the image copy as the disk"
	fi
	if grep -q '^=== medium floppy' "$h"; then
		fail "$1: $(grep '^=== medium floppy' "$h")"
	else
		ok "$1: no second medium was attached"
	fi
	# The emulator itself, and not a stand-in for it.  test/buildenv's is a
	# shell script that appends --floppy, so a medium attached there appears
	# in no record emu-run.sh keeps; the guest's mount table below still
	# sees it, and this says so at the argument rather than at the effect.
	e=$(sed -n 's/^=== emulator //p' "$h")
	if [ -n "$e" ] && [ "$(head -c 2 "$e" 2>/dev/null)" != '#!' ]; then
		ok "$1: the emulator is $e, not a wrapper around one"
	else
		fail "$1: the emulator is a script ($e), which can attach media"
		fail "  of its own that no record here would show"
	fi
	if tr -d '\r' < "$t" | grep -q '^/dev/fd'; then
		fail "$1: the guest has a filesystem mounted off a floppy"
	elif tr -d '\r' < "$t" | grep -q '^/dev/hd6 on /usr '; then
		ok "$1: the guest mounted /usr, and nothing off any other medium"
	else
		fail "$1: the guest's mount table does not show /usr on /dev/hd6"
	fi
}

# assert_run <tag> -- the claims that constitute "the machine compiled it",
# counted so the control can require zero.  The transcript ECHOES every command
# fed to the guest, including the two echo lines that CONTAIN the nonce, so the
# nonce alone is not a claim: what is matched is the nonce on a line of its own,
# which only the compiled program prints.
assert_run() {
	t=$WORK/$1.out
	held=0
	tr -d '\r' < "$t" | grep -q "^$NONCE\$" && held=$((held + 1))
	# cc(1) echoes each source file name as it compiles it, and prints
	# nothing else on a clean compile.  Two files, two lines.
	tr -d '\r' < "$t" | grep -q '^/a\.c:$' && held=$((held + 1))
	tr -d '\r' < "$t" | grep -q '^/b\.c:$' && held=$((held + 1))
	# ld wrote a linked, executable file: `ls -l' shows it 755 and non-empty.
	tr -d '\r' < "$t" | grep -q '^-rwxr-xr-x.*/prog$' && held=$((held + 1))
	echo $held
}

# ---------------------------------------------------------------- phase 2
echo "phase 2: the guest compiles two units, links them and runs the result"
if run pos "$DIST"; then
	assert_media pos
	held=$(assert_run pos)
	if [ "$held" = 4 ]; then
		ok "all 4 claims hold (2 compiles, a link, and $NONCE printed)"
	else
		fail "only $held of 4 claims hold"
		sed -n '/mount/,$p' "$WORK/pos.out" | head -30 | sed 's/^/       | /'
	fi
else
	fail "the positive run produced no transcript"
fi

# ---------------------------------------------------------------- phase 3
echo "phase 3: negative control -- the same commands on $CTRL, which ships none"
if run neg "$CTRL"; then
	assert_media neg
	held=$(assert_run neg)
	if [ "$held" = 0 ]; then
		ok "all 4 claims fail on a dist without lists/toolchain.list"
	else
		fail "$held claim(s) still hold on $CTRL, which ships no compiler --"
		fail "  those assertions do not measure the compiler being installed"
		sed -n '/mount/,$p' "$WORK/neg.out" | head -30 | sed 's/^/       | /'
	fi
else
	fail "the control run produced no transcript"
fi

echo
if [ "$BAD" = 0 ]; then
	echo "selfhost: PASS -- the machine compiled and linked a two-unit program"
	echo "          with the compiler its own dist ships, nothing mapped in from"
	echo "          the host, and the same checks fail on a dist without it"
	exit 0
fi
echo "selfhost: FAIL ($BAD)"
[ "${KEEP:-0}" = 1 ] && echo "          work kept in $WORK"
exit 1
