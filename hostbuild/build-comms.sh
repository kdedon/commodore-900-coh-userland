#!/bin/sh
# build-comms.sh -- the serial file-transfer set (comms): Omen Technology
# rz/sz (ZMODEM/YMODEM/XMODEM) and C-Kermit.  cu(1) dials; these move files.
#
# rz.c and sz.c #include rbsb.c and zm.c rather than linking them, so each is a
# single translation unit and the two share no objects.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
BIN="$HERE/build/bin"
C="$OS/comms"
# libc-z8001.a supplies string routines and getopt.
# games/lib/src supplies err(3) and fgetln(3).
GLIB=""
INC="-I $OS/include -I $OS/include/sys"
mkdir -p "$BIN"
# Compile logs go under the build tree, not /tmp: a fixed name outside
# the build is not this build's output, two runs share it, and a CI
# runner that cleans /tmp between steps loses the one thing a failure
# leaves behind.  $LOGD names them; the FAIL lines quote the path.
LOGD="$HERE/logs"; mkdir -p "$LOGD"
ok=0; fail=0; fl=""

build() {
	name="$1"; dir="$2"; shift 2
	defs=""
	while [ "$1" != "--" ]; do defs="$defs $1"; shift; done
	shift
	if CCZ_VAR=800000020800 "$CCZ" -s -i -L -I "$dir" $INC $defs \
	    -o "$BIN/.$name.new" "$@" $GLIB >"$LOGD"/co-$name.log 2>&1; then
		mv -f "$BIN/.$name.new" "$BIN/$name"
		ok=$((ok+1)); echo "  $name: OK ($(wc -c < "$BIN/$name") B)"
	else
		fail=$((fail+1)); fl="$fl $name"; rm -f "$BIN/.$name.new"
		echo "  $name: FAIL -- $(grep -iE 'error|undefined|no match|Internal' "$LOGD"/co-$name.log | grep -v 'Strict\|Warning' | head -1)"
	fi
}

# V7 selects the sgtty(3) tty model, which is the one COHERENT 3.2's driver
# answers.  MD gives rz the "make missing directories" path handling, ONEREAD
# the single-read input model, and NFGVMIN the sz timing that suits a line
# without a select()/FIONREAD read-ahead check.
build rz "$C/zmodem" -DV7 -DMD -DONEREAD -- "$C/zmodem/rz.c"
build sz "$C/zmodem" -DV7 -DNFGVMIN     -- "$C/zmodem/sz.c"
# minirb: the bootstrap receiver, small enough to type in over a dumb line and
# then use to pull rz/sz across.
build minirb "$C/zmodem" -DV7 -- "$C/zmodem/minirb.c"

# C-Kermit 4E(072).  ckcpro.c is generated from the ckcpro.w state-machine
# description by wart (ckwart.c), which runs on the host.
#
# BSD29 is the configuration the mwcbbs COHERENT note names; it selects the
# sgtty(3) tty model and a modem-control path that COHERENT answers.
#
# It does NOT bring the directory library the upstream Makefile's -lndir names.
# BSD29 selects opendir()/readdir() for the wildcard walk, but <sys/dir.h> here
# has no DIR type and no readdir() declaration, so that arm called an undeclared
# pointer-returning function -- fatal on this target, where a K&R implicit int
# truncates a far pointer and drops the segment -- and it expanded no wildcard
# at all.  ckufio.c now takes the arm COHERENT's own ls(1) uses: open(2) the
# directory and read(2) its 16-byte struct direct records.  No library, no
# declaration, and seven fewer translation units.
CKSRC=""
for f in ckcmai ckutio ckufio ckcfns ckcfn2 ckcpro ckucmd ckuus2 ckuus3 \
	 ckuusr ckucon ckudia ckuscr; do
	CKSRC="$CKSRC $C/ckermit/$f.c"
done
if [ ! -f "$C/ckermit/ckcpro.c" ] || [ "$C/ckermit/ckcpro.w" -nt "$C/ckermit/ckcpro.c" ]; then
	# wart is a HOST program (it generates ckcpro.c from the state-machine
	# description), so it is build output like any other and belongs under
	# the build tree; a fixed /tmp name is shared by every checkout on the
	# machine and survives no CI step.
	cc -w -o "$HERE/build/ckwart" "$C/ckermit/ckwart.c" 2>/dev/null &&
		(cd "$C/ckermit" && "$HERE/build/ckwart" ckcpro.w ckcpro.c >/dev/null)
fi
build kermit "$C/ckermit" -DBSD29 -DDEBUG -DTLOG -DBIT_16 \
	-- $CKSRC

echo "== comms: $ok linked, $fail failed:$fl"
# A sweep that linked nothing is not a sweep that succeeded: the caller stamps
# the step as done on this status, and dist staging then ships whatever binary
# the previous build left behind.
[ "$fail" -eq 0 ]
