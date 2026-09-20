#!/bin/sh
# mgr-slib-sweep.sh -- link every MGR client static and shared, and report the
# sizes and any refusals.
#
# Shared is libmgr.1 + libc.1, static libmgrcl.a + libc-z8001.a.  A shared
# client can't be stripped (exec binds it by its LI_LIB/LI_IMP records), so
# static is compared stripped.
#
# Usage: mgr-slib-sweep.sh [-q]
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$(cd "$HERE/.." && pwd)"
. "$OS/hostbuild/toolchain.sh"
B="${C900_TC_BUILD:-$TC/build}"
MGR="$OS/mgr"
LOG="$HERE/logs/mgr-slib.log"
quiet=0; [ "${1:-}" = -q ] && quiet=1
mkdir -p "$(dirname "$LOG")"

( cd "$MGR" && make -f Makefile.c900 clients ) > "$LOG" 2>&1 ||
	{ echo "mgr-slib-sweep: the static clients do not build -- see $LOG" >&2; exit 1; }
( cd "$MGR" && C900_LIBPATH="$MGR/build/lib" \
	make -f Makefile.c900 -k CLMODE=shared libmgr1 build/lib/libmgrcl-sl.a clients ) \
	>> "$LOG" 2>&1 || true

LIBMGR="$MGR/build/lib/libmgr.1"
LIBC="$B/libc1/libc.1"
[ -f "$LIBMGR" ] || { echo "mgr-slib-sweep: libmgr.1 not built -- see $LOG" >&2; exit 1; }

# A client that ld refused leaves no output; the log names the reason.
[ "$quiet" = 1 ] || echo "client         static   shared    saved"
ts=0; th=0; n=0; miss=""
for f in "$MGR"/build/bin/cl/*; do
	c=$(basename "$f")
	h="$MGR/build/bin-sl/cl/$c"
	if [ ! -f "$h" ]; then miss="$miss $c"; continue; fi
	s=$(wc -c < "$f"); x=$(wc -c < "$h")
	ts=$((ts+s)); th=$((th+x)); n=$((n+1))
	[ "$quiet" = 1 ] || printf "%-12s %8d %8d %8d\n" "$c" "$s" "$x" "$((s-x))"
done
libsz=$(( $(wc -c < "$LIBMGR") + $(wc -c < "$LIBC") ))
saved=$((ts-th))
echo ""
echo "$n of $((n + $(echo $miss | wc -w))) clients link shared"
echo "$ts B static -> $th B shared, saved $saved B"
echo "less the $libsz B of libmgr.1 + libc.1: net $((saved-libsz)) B"
[ "$saved" -gt 0 ] &&
	echo "break-even at $(( (libsz + saved/n - 1) / (saved/n) )) clients of this mean size ($((saved/n)) B each)"
if [ -n "$miss" ]; then
	echo ""
	echo "REFUSED:$miss"
	grep -a "^Ld: " "$LOG" | sort -u | sed 's/^/  /'
fi
exit 0
