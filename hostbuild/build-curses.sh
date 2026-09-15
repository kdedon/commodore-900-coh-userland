#!/bin/sh
# build-curses.sh -- compile the in-tree Coherent 3.2 libcurses + libterm
# (base/lib/) into native C900 archives.  These are MWC's Coherent-adapted
# 4.3BSD curses + termcap; -DCOHERENT selects the Coherent code paths.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
BE="$TC"
CCZ="$BE/ccz"
# The archiver is the toolchain's, at the path the toolchain publishes it on
# ($TCB, which C900_TC_BUILD moves) -- not a fixed name under /tmp, which is
# nobody's build output and exists only on a machine where something once put
# one there.  arz builds it on first use; asking for it is cheaper than
# discovering it missing, and `arz -b' is that build alone.
MKARZ="${MKARZ:-$TCB/mkarz}"
# Read back out of $TCB for the same reason: build-screen.sh and the makefile
# both look for libcurses.a there, so writing it anywhere else produces an
# archive nothing finds.
OUT="$TCB/curses"
LOG="$HERE/build/curses.log"
mkdir -p "$OUT/obj"; : > "$LOG"
[ -x "$MKARZ" ] || sh "$TC/arz" -b >>"$LOG" 2>&1 || :
[ -x "$MKARZ" ] || { echo "curses: no archiver at $MKARZ, and \`$TC/arz -b' did not build one (see $LOG)"; exit 1; }

# No -I beyond the source directory itself: <curses.h>, <unctrl.h> and every
# other system header these two archives include is the toolchain's, reached
# through the include directory ccz appends to every compile.  That is the same
# header a program linking against libcurses.a sees, so the WINDOW layout the
# archive was compiled with and the one its callers compile against are one
# declaration and cannot disagree.
#
# In particular NOT the 0.7.3 include directory, which shadows 46 of these
# headers without agreeing with them -- its ctype.h numbers the bits above _L
# differently, so an object compiled against it and linked with our libc tests
# the wrong bit of the same table (isdigit answers for space, isspace for
# punctuation).
INC=

comp() { # dir  archive  "file file ..."
	dir="$1"; arch="$2"; shift 2
	n=0; fail=""
	for f in "$@"; do
		b=$(basename "$f" .c)
		if "$CCZ" -c -DCOHERENT -I "$OS/$dir" $INC -o "$OUT/obj/$b.o" "$OS/$dir/$f" >>"$LOG" 2>&1; then
			n=$((n+1))
		else
			fail="$fail $b"
		fi
	done
	if [ -n "$fail" ]; then
		echo "== $arch: COMPILE FAIL:$fail"; grep -v 'Strict\|Warning' "$LOG" | tail -8; return 1
	fi
	objs=""
	for f in "$@"; do objs="$objs $OUT/obj/$(basename "$f" .c).o"; done
	# The archiver's status decides whether $OUT/$arch is this run's objects or
	# the previous run's: unchecked, a failed mkarz left the old archive in
	# place and the size line below reported it as the new one, so every curses
	# program linked an archive that predated the fix it was built to carry.
	"$MKARZ" "$OUT/$arch" $objs || {
		echo "== $arch: ARCHIVE FAILED -- $OUT/$arch is the PREVIOUS build"; return 1; }
	echo "== $arch: $n objects -> $(wc -c < "$OUT/$arch") B"
}

# Both archives are carried to the exit status.  With only the last one deciding
# it, a libterm failure was invisible to the caller as long as libcurses built.
rc=0
comp base/lib/libterm libterm.a termcap.c tgoto.c tputs.c || rc=1

CURSES=$(cd "$OS/base/lib/libcurses" && ls *.c | tr '\n' ' ')
comp base/lib/libcurses libcurses.a $CURSES || rc=1
exit "$rc"
