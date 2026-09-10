#!/bin/sh
# build-patch.sh -- Larry Wall's patch(1), the FSF 2.0.1 kit (unified-diff
# capable), ported to Z8001 COHERENT.  Sources live in base/cmd/patch.
#
# The kit's Configure script probes the build machine, which is the wrong
# machine here, so base/cmd/patch/config.h is hand-written for the target and
# NODIR selects the single-suffix backup naming (no directory scan, so
# backupfile.c is not built).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
SRC="$OS/base/cmd/patch"
BIN="$HERE/build/bin"
# getopt, strtok, strchr, strtoul, memcmp, memcpy, memset and strcasecmp all
# come from libc-z8001.a.  They used to be named here as SOURCES out of
# games/lib/src, which made each an object on the link line and bound it in
# preference to the archive member of the same name -- so this program got a
# 65-line shim getopt where the library holds the de-ANSI'd 4.2 MWC one.
# games/lib/src now holds only err(3) and fgetln(3), which libc has not got.
GLIB=""
INC="-I $OS/include -I $OS/include/sys -I $SRC"
mkdir -p "$BIN"
# Compile logs go under the build tree, not /tmp: a fixed name outside
# the build is not this build's output, two runs share it, and a CI
# runner that cleans /tmp between steps loses the one thing a failure
# leaves behind.  $LOGD names them; the FAIL lines quote the path.
LOGD="$HERE/logs"; mkdir -p "$LOGD"

if CCZ_VAR=800000020800 "$CCZ" -s -i $INC -o "$BIN/.patch.new" \
	"$SRC/patch.c" "$SRC/pch.c" "$SRC/inp.c" "$SRC/util.c" "$SRC/version.c" \
	$GLIB >"$LOGD"/patch-build.log 2>&1
then
	mv -f "$BIN/.patch.new" "$BIN/patch"
	echo "  patch: OK ($(wc -c < "$BIN/patch") B)"
	exit 0
else
	rm -f "$BIN/.patch.new"
	echo "  patch: FAIL -- $(grep -iE 'error|undefined|no match|Internal' "$LOGD"/patch-build.log | grep -v 'Strict\|Warning' | head -1)"
	exit 1
fi
