#!/bin/sh
# build-slibc.sh -- link the programs that opt in to the shared C library, and
# report what it costs and what it saves on disk.
#
# libc.1 comes from the toolchain's `make libc1'.  The programs in slibc.list
# are linked both ways from the same sources; static is what ships.
#
# A shared client can't be stripped (ld -s would drop its LI_LIB and LI_IMP
# records), so shared is compared unstripped against static stripped.
#
# slibc-sweep.sh decides what goes on the list.
#
# Usage: build-slibc.sh [-q]
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"		# $TC: the Z8001 toolchain checkout
. "$OS/hostbuild/cmdflags.sh"		# cmdflags <name> -> $IORD, $XLIB
CCZ="$TC/ccz"
B="${C900_TC_BUILD:-$TC/build}"
LIB="$B/libc1/libc.1"
LIST="$HERE/slibc.list"
OUTS="$HERE/build/bin-slibc"
OUTT="$HERE/build/bin-slibc-static"
LOG="$HERE/logs/slibc.log"
quiet=0; [ "${1:-}" = -q ] && quiet=1

[ -f "$LIB" ] || { echo "build-slibc: $LIB not built -- run \`make libc1' in the toolchain" >&2; exit 1; }
mkdir -p "$OUTS" "$OUTT" "$(dirname "$LOG")"
: > "$LOG"

libsz=$(wc -c < "$LIB")
[ "$quiet" = 1 ] || {
	echo "libc.1: $libsz B  ($LIB)"
	echo ""
	echo "command        static   shared    saved"
}
tot_s=0; tot_h=0; n=0; failed=""
for b in $(sed 's/#.*//' "$LIST" | tr -d ' \t\r' | grep -v '^$'); do
	src="$OS/base/cmd/$b.c"
	[ -f "$src" ] || { failed="$failed $b(no source)"; continue; }
	cmdflags "$b" || { failed="$failed $b(not built here)"; continue; }
	( cd "$OS" && "$CCZ" -i -s $IORD -o "$OUTT/$b" "$(c900_rel "$src")" $XLIB ) >>"$LOG" 2>&1 ||
		{ failed="$failed $b(static)"; continue; }
	( cd "$OS" && "$CCZ" -i -slibc $IORD -o "$OUTS/$b" "$(c900_rel "$src")" $XLIB ) >>"$LOG" 2>&1 ||
		{ failed="$failed $b(shared)"; continue; }
	s=$(wc -c < "$OUTT/$b"); h=$(wc -c < "$OUTS/$b")
	tot_s=$((tot_s+s)); tot_h=$((tot_h+h)); n=$((n+1))
	[ "$quiet" = 1 ] || printf "%-12s %8d %8d %8d\n" "$b" "$s" "$h" "$((s-h))"
done
[ -z "$failed" ] || { echo "build-slibc: FAILED:$failed -- see $LOG" >&2; exit 1; }
[ "$n" -gt 0 ] || { echo "build-slibc: nothing in $LIST" >&2; exit 1; }

saved=$((tot_s-tot_h))
[ "$quiet" = 1 ] || {
	echo ""
	echo "$n programs: $tot_s B static -> $tot_h B shared, saved $saved B"
	echo "less the $libsz B library: net $((saved-libsz)) B"
	echo "break-even at $(( (libsz + saved/n - 1) / (saved/n) )) programs of this mean size ($((saved/n)) B each)"
}
exit 0
