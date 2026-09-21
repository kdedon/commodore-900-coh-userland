#!/bin/sh
# ports-gate.sh -- compile and link every third-party port in this tree with
# the toolchain under test, and fail on the first package that does not link.
#
# Ported 1980s C (far pointers, register-hungry loops, K&R) is what compiler
# tests don't resemble; run this before trusting a compiler change.  Compile
# and link only, so it needs no image or emulator.
#
# Prereqs: libc-z8001.a and crt0.o from the toolchain (`make libc' there).
# libm, libterm and libcurses are built here if they are missing.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout

for f in libc-z8001/libc-z8001.a libc-z8001/crt0.o; do
	[ -f "$TCB/$f" ] || {
		echo "ports-gate: no $TCB/$f -- run \`make libc' in the toolchain" >&2
		exit 2
	}
done

rc=0; fl=""; n=0
step() {	# label  command ...
	lbl="$1"; shift
	n=$((n+1))
	if out=$("$@" 2>&1); then
		echo "$out" | sed -n '/OK\|linked\|FAIL/p' | tail -3
	else
		st=$?
		rc=1; fl="$fl $lbl"
		echo "$out" | tail -20
		echo "== $lbl FAILED (exit status $st)"
	fi
}

# factor(1) and nawk need libm; an unpacked release already has it.
if [ -f "$TC/build-libm-z8001.sh" ]; then
	step libm sh "$TC/build-libm-z8001.sh"
	n=$((n-1))
fi

step gzip	sh "$HERE/build-gzip.sh"
step archivers	sh "$HERE/build-archive.sh"		# zoo, lharc, unzip
step comms	sh "$HERE/build-comms.sh"		# ckermit, zmodem
step compress	sh "$HERE/build-extra-userland.sh" compress
step less	sh "$HERE/build-less.sh"
step patch	sh "$HERE/build-patch.sh"
step rcs	sh "$HERE/build-rcs.sh"
step nawk	sh "$HERE/build-nawk.sh"
step elvis	sh "$HERE/build-elvis.sh"
step editors	sh "$HERE/build-editors.sh"		# pico, MicroEmacs
step rogue	sh "$HERE/build-rogue.sh"
step screen	sh "$HERE/build-screen.sh"
step games	sh "$HERE/build-games.sh"
step curses-games sh "$HERE/build-curses-games.sh"

if [ "$rc" = 0 ]; then
	echo "== ports: $n packages linked"
else
	echo "== ports: FAILED:$fl"
fi
exit $rc
