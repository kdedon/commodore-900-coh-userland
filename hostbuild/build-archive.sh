#!/bin/sh
# build-archive.sh -- the file-archiver set (archive): zoo, lharc, unzip.
# Each is a multi-file directory of its own, so the plain cmd/ sweep cannot
# build them: they need per-package -D switches and their own file selection.
# Same model as build-cmd.sh -- separated I/D, large model, 3.2 headers plus
# the games-lib gap fills.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
BIN="$HERE/build/bin"
A="$OS/archive"
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

# build <name> <dir> <defines...> -- <sources...>
build() {
	name="$1"; dir="$2"; shift 2
	defs=""
	while [ "$1" != "--" ]; do defs="$defs $1"; shift; done
	shift
	if CCZ_VAR=800000020800 "$CCZ" -s -i -L -I "$dir" $INC $defs \
	    -o "$BIN/.$name.new" "$@" $GLIB >"$LOGD"/ar-$name.log 2>&1; then
		mv -f "$BIN/.$name.new" "$BIN/$name"
		ok=$((ok+1)); echo "  $name: OK ($(wc -c < "$BIN/$name") B)"
	else
		fail=$((fail+1)); fl="$fl $name"; rm -f "$BIN/.$name.new"
		echo "  $name: FAIL -- $(grep -iE 'error|undefined|no match|Internal' "$LOGD"/ar-$name.log | grep -v 'Strict\|Warning' | head -1)"
	fi
}

# zoo 2.1 -- SYS_V is the closest of its configurations to Coherent 3.2.
# SMALL_MEM is not optional: 2.1's zoomem.h defines ZOOCOUNT and MAXADD only
# inside the four memory-model #ifdefs and has no default, so a build with none
# of them set stops on an undefined MAXADD.  SMALL_MEM is the arm whose values
# (ZOOCOUNT 30, MAXADD 100) are the ones 2.01 used when it needed no symbol.
# NDEBUG is upstream's own release setting (see its Makefile CFLAGS): zoo 2.1
# ships an assert in portable.c that counts a directory entry's variable part
# without the eight bytes the system-id/attribute/version fields occupy, so it
# fires on every entry written.  sysv.i and the *.i time/mode fragments are
# #included by machine.c and portable.c, not compiled on their own.
build zoo "$A/zoo" -DSYS_V -DNDEBUG -DSMALL_MEM -- "$A"/zoo/*.c

# lharc, C-LHarc 1.00 (Tagawa, Rommel).  lharc.c defines NOBSTRING itself, so
# passing -DNOBSTRING is a redefinition and cc0 stops on it.  It also defines
# NODIRECTORY, which compiles out every opendir()/readdir() call -- COHERENT
# 3.2's libc has none -- so lhdir.c stays out and lharc archives the files it is
# named without descending into directories.
build lharc "$A/lharc" -- "$A"/lharc/lharc.c "$A"/lharc/lzhuf.c "$A"/lharc/lhio.c

# unzip 4.1 -- extract only; the matching creator is zip(1), not built here.
# NOPROTO picks the K&R declarations out of the __(X) prototype macro.  NOTINT16
# is not optional on this target: unzip reads header fields byte by byte instead
# of overlaying a struct, and without it the program's own endian self-check
# reports "It appears that your machine is big-endian" and exits 51.
build unzip "$A/unzip" -DUNIX -DNOPROTO -DNOTINT16 -- "$A"/unzip/*.c

echo "== archivers: $ok linked, $fail failed:$fl"
# A sweep that linked nothing is not a sweep that succeeded: the caller stamps
# the step as done on this status, and dist staging then ships whatever binary
# the previous build left behind.
[ "$fail" -eq 0 ]
