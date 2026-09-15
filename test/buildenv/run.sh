#!/bin/sh
# tests/buildenv/run.sh -- compile IN THE GUEST, from a compiler environment
# mapped in from the host.
#
# A compiler environment (the toolchain repository's `make env': cc,
# cc0/cc1/cc2, as, ld, ar, libc.a and the headers, all Z8001 binaries in a
# host directory) is packed onto a floppy medium with a small multi-file C
# program, by the distribution repository's mkimage.py over media/fdvol.media;
# the guest -- booted from the coherent3-buildenv dist, which carries NO
# compiler -- runs its own make(1) over it, then executes the result.
#
# The string the program prints is a NONCE minted per run and compiled into
# the exported source; phase 1 asserts it is absent from the booted image, so
# printing it proves the source crossed the boundary, was compiled by a
# compiler that came the same way, linked, and ran on the guest.  Phase 5 is
# the negative control: the identical command file with no medium attached
# must fail every assertion.
#
# THE GUEST HAVING NO COMPILER IS THIS HARNESS'S PREMISE, not an accident of
# which dist was to hand: phase 5 can only discriminate while the image cannot
# have compiled the program itself.  An image that DOES ship a compiler exists
# now -- coherent3-dev, staging the dist repository's lists/toolchain.list --
# and the other half of the claim is tests/selfhost/run.sh, which compiles on
# the target with nothing mapped in at all.  Do not point this harness at that
# dist; run both.
#
# Cost: two emulator boots, about 70 s, plus whatever `make env' cost earlier.
# Host-side only; nothing is installed on the target and no image is rebuilt.
#
#	sh run.sh		positive + negative control
#	KEEP=1 sh run.sh	leave the work directory for inspection
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
HB="$OS/hostbuild"
DIST=${DIST:-coherent3-buildenv}
C900_ROOT=$OS
# The image is packed in the distribution repository (mk/dist.sh), which is
# also the directory emu-run.sh resolves a dist name in.
. "$OS/mk/dist.sh"
IMG=$(dist_img "$DIST") || exit 2
# The medium is packed by the distribution repository's own packer, from its
# floppy volume descriptor, and read back by the emulator's disk tool.
MKIMAGE=$C900_DIST/os/hostbuild/mkimage.py
FDVOL=$C900_DIST/os/dist/media/fdvol.media
# CCENV is the TOOLCHAIN repository's axis (`make env CCENV=': ours,
# inherited, mwc1985), independent of this tree's dist names.  If the
# toolchain repository renames the value, this default must change with it.
CCENV=${CCENV:-ours}

# The environment is the toolchain's deliverable, located through the same
# variable this tree uses to find the compiler.
. "$HB/toolchain.sh"			# sets $TC = $C900_TOOLCHAIN/host
ENV=${C900_ENV:-$TCB/env/$CCENV}

EMUBIN=${EMUBIN:-$(sh "$C900_TOOLCHAIN/host/runner.sh" 2>/dev/null)}
EMUROM=${EMUROM:-$(dirname "$(dirname "$EMUBIN")")/rom}
DISKPY=$(dirname "$EMUROM")/tools/disk.py

BAD=0
fail() { echo "  FAIL $*"; BAD=$((BAD + 1)); }
ok()   { echo "  ok   $*"; }

for f in "$MKIMAGE" "$FDVOL" "$DISKPY"; do
	[ -f "$f" ] || { echo "buildenv: no $f" >&2; exit 2; }
done
[ -x "$EMUBIN" ] || { echo "buildenv: no emulator ($EMUBIN)" >&2; exit 2; }
[ -d "$ENV/bin" ] || {
	echo "buildenv: no compiler environment at $ENV" >&2
	echo "  build one in the toolchain checkout:  make env CCENV=$CCENV" >&2
	echo "  (or set C900_ENV to a tree that has bin/ lib/ usr/include/)" >&2
	exit 2; }

WORK=${WORK:-$HERE/work}
rm -rf "$WORK"
mkdir -p "$WORK/export/env" "$WORK/export/src" "$WORK/stage"
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$WORK"' 0 1 2 15

NONCE=BUILDENV-$$-$(date +%s)
echo "buildenv: compiler environment $CCENV, nonce $NONCE"

# ---------------------------------------------------------------- phase 0
# The guest is about to EXECUTE these files, and a host binary among them
# would mount and list exactly like a target one.
echo "phase 0: the environment holds Z8001 binaries, not host ones"
if python3 "$C900_TOOLCHAIN/host/loutid.py" -q -m z8001 \
	$(find "$ENV/bin" "$ENV/lib" -type f | sort)
then
	ok "$(find "$ENV/bin" "$ENV/lib" -type f | wc -l) binaries in bin/ + lib/ are Z8001 l.out"
else
	fail "the environment contains something that is not a Z8001 binary"
fi

cp -r "$ENV/bin" "$ENV/lib" "$ENV/usr" "$WORK/export/env/"

# The program.  Two translation units and a header, so the guest's make(1) has
# real dependencies to resolve, and one of them carries the nonce.
cat > "$WORK/export/src/util.h" <<'EOF'
extern int addup();
extern char *tag();
EOF
cat > "$WORK/export/src/util.c" <<EOF
#include "util.h"

int
addup(n)
int n;
{
	int i, s;

	s = 0;
	for (i = 1; i <= n; i++)
		s += i;
	return s;
}

char *
tag()
{
	return "$NONCE";
}
EOF
cat > "$WORK/export/src/main.c" <<'EOF'
#include <stdio.h>
#include "util.h"

main()
{
	printf("%s %d\n", tag(), addup(10));
	return 0;
}
EOF
cat > "$WORK/export/src/Makefile" <<'EOF'
CC = /mnt/env/bin/cc
CFLAGS = -t012sdlr -B/mnt/env/bin:/mnt/env/lib -I/mnt/env/usr/include

prog: main.o util.o
	$(CC) $(CFLAGS) -o prog main.o util.o

main.o: main.c util.h
	$(CC) $(CFLAGS) -c main.c

util.o: util.c util.h
	$(CC) $(CFLAGS) -c util.c
EOF

# ---------------------------------------------------------------- phase 1
# Provenance precondition: the nonce must not already be on the guest's disk.
echo "phase 1: the nonce is absent from the booted image"
if grep -ac "$NONCE" "$IMG" 2>/dev/null | grep -qv '^0$'; then
	fail "the nonce is already in $DIST.bin -- phase 3 would prove nothing"
else
	ok "$(basename "$IMG") does not contain $NONCE"
fi

# ---------------------------------------------------------------- phase 2
echo "phase 2: pack the environment and the sources onto a medium"
# mkimage.py writes <stage>/<partition>/ into each partition; fdvol's one
# partition is `root'.  Host execute bits become mode 755 on the medium.
cp -r "$WORK/export" "$WORK/stage/root"
if python3 "$MKIMAGE" "$WORK/floppy.img" "$FDVOL" "$WORK/stage" \
	> "$WORK/render.log" 2>&1
then
	ok "$(head -1 "$WORK/render.log")"
else
	fail "mkimage.py: $(tail -1 "$WORK/render.log")"
	echo "buildenv: cannot continue"; exit 1
fi

# The guest command file, used by BOTH runs unchanged.  make(1) drives the
# compiler: a cc command line typed at the console costs more emulator time
# than the compile does (console input pacing).
cat > "$WORK/cmds" <<'EOF'
/etc/mount /dev/fd1 /mnt
cd /mnt/src
/bin/time /bin/make
/bin/ls
./prog
cd /
/etc/umount /dev/fd1
EOF

run() {		# run <tag> <floppy-or-none>
	tag=$1; fl=$2
	if [ "$fl" = none ]; then
		cat > "$WORK/emu.$tag" <<EOF
#!/bin/sh
exec "$EMUBIN" --firmware="$EMUROM" "\$@"
EOF
	else
		cat > "$WORK/emu.$tag" <<EOF
#!/bin/sh
exec "$EMUBIN" --firmware="$EMUROM" --floppy="$fl" "\$@"
EOF
	fi
	chmod +x "$WORK/emu.$tag"
	rwork=$WORK/work.$tag.bin; rout=$WORK/$tag.out; rerr=$WORK/$tag.err
	remu=$WORK/emu.$tag
	start=$(date +%s)
	# C900_EMU, not EMU: mk/emulator.sh takes $EMU only when C900_EMU is
	# unset, and a caller that resolved the emulator once and exported it
	# (the release sweep does) would otherwise silently get the bare
	# binary here -- and with it no --floppy, which is the whole medium
	# this harness is about.
	C900_EMU=$remu WORK=$rwork OUT=$rout ERR=$rerr \
		sh "$HB/emu-run.sh" "$WORK/cmds" "$DIST" > "$WORK/$tag.harness" 2>&1
	echo $(($(date +%s) - start)) > "$WORK/$tag.secs"
	[ -s "$WORK/$tag.out" ]
}

# assert_positive <tag> -- the claims that constitute "the guest built it",
# printed as a count so the control can require zero.  The transcript ECHOES
# the commands fed to the guest, so no claim may match a bare file name.
assert_positive() {
	t=$WORK/$1.out
	held=0
	# 1: the guest's own program ran and printed the nonce it was compiled
	#    with -- and the arithmetic its other translation unit computes.
	grep -q "$NONCE 55" "$t" && held=$((held + 1))
	# 2 and 3: make(1) ran the compiler on the two sources; the matched
	#    line is the one make echoes only for a rule it ran.
	grep -q 'cc .*-c main\.c' "$t" && held=$((held + 1))
	grep -q 'cc .*-c util\.c' "$t" && held=$((held + 1))
	# 4: ls after the build lists the linked program.  The console
	#    transcript is CR-terminated, so strip CRs before anchoring.
	tr -d '\r' < "$t" | grep -q '^prog$' && held=$((held + 1))
	echo $held
}

# ---------------------------------------------------------------- phase 3
echo "phase 3: the guest compiles, links and runs the program"
if run pos "$WORK/floppy.img"; then
	held=$(assert_positive pos)
	if [ "$held" = 4 ]; then
		ok "all 4 claims hold (2 compiles, a link, and the result printed $NONCE)"
		ok "session wall clock: $(cat "$WORK/pos.secs") s (boot + mount + build + run)"
		grep -A3 '^Real:' "$WORK/pos.out" | head -4 | sed 's/^/       | /'
	else
		fail "only $held of 4 claims hold"
		sed -n '/make/,$p' "$WORK/pos.out" | head -25 | sed 's/^/       | /'
	fi
else
	fail "the positive run produced no transcript"
fi

# ---------------------------------------------------------------- phase 4
echo "phase 4: the objects and the binary come back to the host"
if python3 "$DISKPY" "$WORK/floppy.img" --read --dest "$WORK/back" \
	/src/main.o /src/util.o /src/prog > "$WORK/extract.log" 2>&1
then
	ok "read back $(grep -c ' read ' "$WORK/extract.log") file(s) from the medium"
else
	fail "disk.py --read: $(tail -1 "$WORK/extract.log")"
fi
for f in main.o util.o prog; do
	if [ -f "$WORK/back/$f" ]; then
		if python3 "$C900_TOOLCHAIN/host/loutid.py" -q -m z8001 \
			"$WORK/back/$f"
		then
			ok "$f came back, and is a Z8001 l.out"
		else
			fail "$f came back but is not a Z8001 l.out"
		fi
	else
		fail "$f did not come back from the guest"
	fi
done

# ---------------------------------------------------------------- phase 5
# The control.  Same command file, same image, same assertions -- no medium.
echo "phase 5: negative control -- identical run with no medium attached"
if run neg none; then
	held=$(assert_positive neg)
	if [ "$held" = 0 ]; then
		ok "all 4 claims fail without the medium (they can fail)"
	else
		fail "$held claim(s) still hold with NO medium attached --"
		fail "  those assertions do not measure the guest build"
		sed -n '/make/,$p' "$WORK/neg.out" | head -20 | sed 's/^/       | /'
	fi
else
	fail "the control run produced no transcript"
fi

echo
if [ "$BAD" = 0 ]; then
	echo "buildenv: PASS -- a guest with no compiler on its disk built and ran a"
	echo "          multi-file program from a host-mapped compiler environment,"
	echo "          and the same checks demonstrably fail without it"
	exit 0
fi
echo "buildenv: FAIL ($BAD)"
[ "${KEEP:-0}" = 1 ] && echo "          work kept in $WORK"
exit 1
