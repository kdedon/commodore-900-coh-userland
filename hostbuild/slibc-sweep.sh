#!/bin/sh
# slibc-sweep.sh -- try to link every command against libc.1, and name what
# each failure is missing.  slibc.list comes from this; rerun it after any
# change to libc.1's export list.  Commands and flags are cmdflags.sh's.
#
# Output, on stdout:
#   <name> OK                       linked shared
#   <name> NEEDS <sym> ...          ld refused it, naming these libc symbols
#   <name> STATIC-FAILS             it does not link statically either
# and a summary, plus a tally of every refused symbol by how many commands
# named it.  The full ld diagnostics go to logs/slibc-sweep.log.
#
# Usage: slibc-sweep.sh [-q] [name ...]
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"		# $TC, $TCB, $KINC
. "$OS/hostbuild/cmdflags.sh"		# cmdflags <name> -> $IORD, $XLIB
CCZ="$TC/ccz"
LOG="$HERE/logs/slibc-sweep.log"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
mkdir -p "$(dirname "$LOG")"; : > "$LOG"
quiet=0; [ "${1:-}" = -q ] && { quiet=1; shift; }

if [ $# -gt 0 ]; then set -- "$@"; else
	set -- $(ls "$OS"/base/cmd/*.c | sed 's|.*/||;s|\.c$||')
fi

nok=0; nno=0; nstatic=0; oklist=""
: > "$TMP/syms"
for b in "$@"; do
	f="$OS/base/cmd/$b.c"
	[ -f "$f" ] || continue
	cmdflags "$b" || continue
	echo "=== $b" >> "$LOG"
	if ! ( cd "$OS" && "$CCZ" -s -i $IORD -o "$TMP/a" $(c900_rel "$f") $XLIB ) \
	     >>"$LOG" 2>&1; then
		nstatic=$((nstatic+1)); echo "$b STATIC-FAILS"; continue
	fi
	if ( cd "$OS" && "$CCZ" -i -slibc $IORD -o "$TMP/a" $(c900_rel "$f") $XLIB ) \
	     >"$TMP/err" 2>&1; then
		cat "$TMP/err" >> "$LOG"
		nok=$((nok+1)); oklist="$oklist $b"; echo "$b OK"; continue
	fi
	cat "$TMP/err" >> "$LOG"
	# Both refusals name the symbol: unexported, or undefined anywhere.
	sed -n 's/^.*[/ ]\([A-Za-z_][A-Za-z_0-9]*\): referenced, but the library.*/\1/p;
		s/^ld \([A-Za-z_][A-Za-z_0-9]*\) undefined.*/\1/p' "$TMP/err" |
		sort -u > "$TMP/s"
	if [ ! -s "$TMP/s" ]; then
		nno=$((nno+1)); echo "$b NEEDS ?(see $LOG)"; continue
	fi
	cat "$TMP/s" >> "$TMP/syms"
	nno=$((nno+1)); echo "$b NEEDS $(tr '\n' ' ' < "$TMP/s")"
done

[ "$quiet" = 1 ] && exit 0
echo ""
echo "== slibc sweep: $nok link shared, $nno refused, $nstatic do not link statically"
echo "== shared:$oklist" | fold -s -w 78
if [ -s "$TMP/syms" ]; then
	echo "== refused symbols, by how many commands name them:"
	sort "$TMP/syms" | uniq -c | sort -rn | awk '{printf "     %-18s %d\n", $2, $1}'
fi
exit 0
