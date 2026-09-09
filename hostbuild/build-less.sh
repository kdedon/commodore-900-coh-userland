#!/bin/sh
# build-less.sh -- less 177 (tools/less), the full-screen pager.
#
# less drives the terminal through termcap, so it links libterm (base/lib/libterm)
# the same way more does, and it matches search patterns with the Spencer regexp
# package in base/lib/regexp, selected by REGCOMP in tools/less/defines.h.
# lesskey compiles a key-binding file and needs neither.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
BIN="$HERE/build/bin"
ROOT="$HERE/build/root"
SRC="$OS/tools/less"
LIBTERM="$TCB/curses/libterm.a"
# getopt, strtok, strchr, strtoul, memcmp, memcpy, memset and strcasecmp all
# come from libc-z8001.a.  They used to be named here as SOURCES out of
# games/lib/src, which made each an object on the link line and bound it in
# preference to the archive member of the same name -- so this program got a
# 65-line shim getopt where the library holds the de-ANSI'd 4.2 MWC one.
# games/lib/src now holds only err(3) and fgetln(3), which libc has not got.
GLIB=""
INC="-I $OS/include -I $OS/include/sys -I $SRC -I $OS/base/lib/regexp"
mkdir -p "$BIN" "$ROOT/usr/lib"
# Compile logs go under the build tree, not /tmp: a fixed name outside
# the build is not this build's output, two runs share it, and a CI
# runner that cleans /tmp between steps loses the one thing a failure
# leaves behind.  $LOGD names them; the FAIL lines quote the path.
LOGD="$HERE/logs"; mkdir -p "$LOGD"
[ -f "$LIBTERM" ] || sh "$HERE/build-curses.sh" >/dev/null 2>&1

LESSSRC="$SRC/brac.c $SRC/ch.c $SRC/charset.c $SRC/cmdbuf.c $SRC/command.c \
$SRC/decode.c $SRC/edit.c $SRC/filename.c $SRC/forwback.c $SRC/help.c \
$SRC/ifile.c $SRC/input.c $SRC/jump.c $SRC/line.c $SRC/linenum.c \
$SRC/lsystem.c $SRC/main.c $SRC/mark.c $SRC/optfunc.c $SRC/option.c \
$SRC/opttbl.c $SRC/os.c $SRC/output.c $SRC/position.c $SRC/prompt.c \
$SRC/screen.c $SRC/search.c $SRC/signal.c $SRC/tags.c $SRC/ttyin.c \
$SRC/version.c"

ok=0; fail=0; fl=""
build() {
	name="$1"; shift
	if CCZ_VAR=800000020800 "$CCZ" -s -i $INC -o "$BIN/.$name.new" "$@" >"$LOGD"/less-$name.log 2>&1; then
		mv -f "$BIN/.$name.new" "$BIN/$name"
		ok=$((ok+1)); echo "  $name: OK ($(wc -c < "$BIN/$name") B)"
	else
		fail=$((fail+1)); fl="$fl $name"; rm -f "$BIN/.$name.new"
		echo "  $name: FAIL -- $(grep -iE 'error|undefined|no match|Internal' "$LOGD"/less-$name.log | grep -v 'Strict\|Warning' | head -1)"
	fi
}

build less $LESSSRC "$OS/base/lib/regexp/regexp.c" "$LIBTERM" $GLIB
build lesskey "$SRC/lesskey.c" $GLIB

# The help text less shows for `h' is read at run time from HELPFILE
# (tools/less/defines.h), so it ships alongside the other command data files.
cp "$SRC/less.hlp" "$ROOT/usr/lib/less.hlp"

echo "== less: $ok linked, $fail failed:$fl"
# A sweep that linked nothing is not a sweep that succeeded: the caller stamps
# the step as done on this status, and dist staging then ships whatever binary
# the previous build left behind.
[ "$fail" -eq 0 ]
