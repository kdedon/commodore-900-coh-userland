#!/bin/sh
# test/mkfsgetlink/run.sh -- pin the name-matching loop in mkfs(1M)'s
# getlink().
#
# WHY THIS EXISTS.  getlink() resolves a path in the PROTOTYPE filesystem mkfs
# is building, and it is what a `l' line in a proto file goes through.  A wrong
# answer links a file to the wrong inode, or reports a name that is there as
# missing, in a filesystem that is then written out -- and one that is linked
# wrongly still checks clean, because the link count is right and the block
# lists are right.  There is nothing to notice.
#
# THE TWO WRONG LOOPS ARE KEPT HERE, and they are why this file is not a
# formality.  The tree carried two versions of mkfs and their loops disagreed:
#
#   stock	tests the end of the ENTRY's name first and, when the name has
#		more characters left, returns 0 from the whole of getlink()
#		instead of trying the next entry.  So a directory holding
#		`ab' before `abc' answers `no such name' for `abc'.  This one
#		fails visibly: mkfs stops with `unknown link name'.
#
#   base	breaks only on a character it can see, so when the NAME ends
#		first it runs on past the terminator, compares bytes that are
#		not part of the name, and can reach the end of a LONGER entry
#		and take it.  A directory holding `qrs' before `qr' answers
#		`qr' with the inode of `qrs'.  This one fails silently, and
#		only when the bytes after the name happen to be NUL -- which
#		is exactly why the loop must not read them.  Under a real
#		mkfs run the name is a gettoken() buffer whose tail is not
#		NUL, and the same prototype comes out right; that is luck,
#		not correctness, and it is what this file removes.
#
#   merged	folds both ends to 0 -- a name ends at NUL or `/', an entry
#		ends at its NUL or at DIRSIZ -- so one comparison decides the
#		match and neither side is read past its end.
#
# Fourteen cases.  `merged' must pass all of them; `stock' and `base' must
# each fail at least one, or the cases are measuring nothing.
#
#	sh run.sh		host and target, all three loops
#	sh run.sh host		host only (no emulator needed)
#
# The subject is pure name matching, so it runs on the host as well as on the
# target, and it must give the same verdicts on both.  The host is where the
# memory after the name buffer can be made definite, which is what lets the
# `base' loop's answer be pinned at all.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
WHERE=${1:-both}

HOSTCC=${HOSTCC:-gcc}
CCZ=${CCZ:-$OS/../commodore-900-toolchain/host/ccz}

rc=0

# want <loop> <expected exit> <label> <command...>
want() {
	_loop=$1; _want=$2; _label=$3; shift 3
	"$@" >/dev/null 2>&1
	_got=$?
	if [ "$_got" = "$_want" ]; then
		echo "  ok   $_label $_loop (exit $_got)"
	else
		echo "  FAIL $_label $_loop (exit $_got, want $_want)"
		rc=1
	fi
}

if [ "$WHERE" = both ] || [ "$WHERE" = host ]; then
	"$HOSTCC" -std=gnu89 -w -o "$HERE/getlink" "$HERE/getlink.c" || exit 2
	echo "mkfsgetlink: host"
	want merged 0 host "$HERE/getlink" merged
	want stock  1 host "$HERE/getlink" stock
	want base   1 host "$HERE/getlink" base
fi

if [ "$WHERE" = both ] || [ "$WHERE" = target ]; then
	C900_ROOT=$OS
	. "$OS/mk/emulator.sh"
	if [ -z "${C900_EMU:-}" ] || [ ! -x "$CCZ" ]; then
		echo "mkfsgetlink: no emulator or no ccz -- target half SKIPPED"
	else
		"$CCZ" -o "$HERE/getlink-z" "$HERE/getlink.c" >/dev/null 2>&1 \
			|| exit 2
		echo "mkfsgetlink: target"
		want merged 0 target "$C900_EMU" --exec "$HERE/getlink-z" merged
		want stock  1 target "$C900_EMU" --exec "$HERE/getlink-z" stock
		want base   1 target "$C900_EMU" --exec "$HERE/getlink-z" base
	fi
fi

if [ "$rc" = 0 ]; then
	echo "mkfsgetlink: PASS"
else
	echo "mkfsgetlink: FAIL"
fi
exit "$rc"
