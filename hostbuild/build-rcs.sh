#!/bin/sh
# build-rcs.sh -- RCS 4.3 (ci/co/rcs/rlog/ident/rcsdiff/rcsmerge/rcsclean/merge)
# for the Z8001 COHERENT target.  Sources live in base/cmd/rcs: RCS 4.3 with the
# Coherent patch kit (CohRCS001..006) applied, plus the fixes this target needs.
# The largest program, ci, links inside one 64K text segment, so no large model.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
SRC="$OS/base/cmd/rcs"
CCZ="$TC/ccz"
BIN="$HERE/build/bin"
INC="-I $OS/include -I $OS/include/sys"
RCSDIR=/usr/bin
# DIFF/DIFF3 are the programs ci/rcsdiff/merge exec to compute and apply deltas.
DEFS="-D COHERENT -D SIGNAL_TYPE=int -D STRICT_LOCKING=0
	-D DIFF=\"/bin/diff\" -D DIFF3=\"/bin/diff3\"
	-D CO=\"$RCSDIR/co\" -D RCS_CMD=\"$RCSDIR/rcs\" -D MERGE=\"$RCSDIR/merge\""
mkdir -p "$BIN"
# Compile logs go under the build tree, not /tmp: a fixed name outside
# the build is not this build's output, two runs share it, and a CI
# runner that cleans /tmp between steps loses the one thing a failure
# leaves behind.  $LOGD names them; the FAIL lines quote the path.
LOGD="$HERE/logs"; mkdir -p "$LOGD"

COMMON="$SRC/rcslex.c $SRC/rcssyn.c $SRC/rcsrev.c $SRC/rcsutil.c $SRC/rcsfnms.c"
TIME="$SRC/partime.c $SRC/maketime.c"

ok=0; fail=0; fl=""
build() {
	name="$1"; shift
	if CCZ_VAR=800000020800 "$CCZ" -s -i $INC $DEFS -o "$BIN/.$name.new" "$@" \
	   >"$LOGD"/rcs-$name.log 2>&1; then
		mv -f "$BIN/.$name.new" "$BIN/$name"
		ok=$((ok+1)); echo "  $name: OK ($(wc -c < "$BIN/$name") B)"
	else
		fail=$((fail+1)); fl="$fl $name"; rm -f "$BIN/.$name.new"
		echo "  $name: FAIL -- $(grep -iE 'error|undefined|no match|Internal' "$LOGD"/rcs-$name.log | grep -v 'Strict\|Warning' | head -1)"
	fi
}

build ci	$SRC/ci.c   $SRC/rcsgen.c $SRC/rcsedit.c $SRC/rcskeys.c \
		$SRC/rcskeep.c $SRC/rcsfcmp.c $COMMON $TIME
build co	$SRC/co.c   $SRC/rcsgen.c $SRC/rcsedit.c $SRC/rcskeys.c $COMMON $TIME
build rcs	$SRC/rcs.c  $SRC/rcsgen.c $SRC/rcsedit.c $SRC/rcskeys.c $COMMON
build rlog	$SRC/rlog.c $COMMON $TIME
build ident	$SRC/ident.c $SRC/rcskeys.c
build rcsdiff	$SRC/rcsdiff.c $COMMON $TIME
build rcsmerge	$SRC/rcsmerge.c $COMMON
build rcsclean	$SRC/rcsclean.c $COMMON

# merge(1) is a shell script wrapping diff3; the Makefile patches in the paths.
sed -e '/^#/d' -e 's:DIFF=.*$:DIFF=/bin/diff:' -e 's:DIFF3=.*$:DIFF3=/bin/diff3:' \
	"$SRC/merge.sh" > "$BIN/merge" && chmod 755 "$BIN/merge" && echo "  merge: OK (script)"

echo "== rcs: $ok linked, $fail failed:$fl" | fold -s -w 100
# A sweep that linked nothing is not a sweep that succeeded: the caller stamps
# the step as done on this status, and dist staging then ships whatever binary
# the previous build left behind.
[ "$fail" -eq 0 ]
