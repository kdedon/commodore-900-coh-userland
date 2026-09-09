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

# The header this archive is compiled against decides the layout of a WINDOW,
# and the toolchain's <curses.h> is the same header under the name every -I
# path reaches.  Both spell their guard `# ifndef WINDOW', so a program that
# finds one of them sees the other's declarations suppressed: two different
# structs under one name, chosen by include order, with no diagnostic at
# compile or link time.  Building the archive while the two disagree is what
# makes that possible, so it is refused here.
#
# The system copy is the one ccz appends to every compile, resolved the way
# ccz itself resolves it: src/include in a checkout, usr/include in an
# unpacked release.  Naming a path this build does not actually compile
# against would make this check answer for a file nobody reads.
if [ -d "$C900_TOOLCHAIN/native" ]; then
	TCSYSINC="$C900_TOOLCHAIN/usr/include"
else
	TCSYSINC="$C900_TOOLCHAIN/src/include"
fi
[ -f "$TCSYSINC/curses.h" ] || {
	echo "curses: no curses.h at $TCSYSINC -- the toolchain did not resolve,"
	echo "  or its layout changed.  Refusing rather than skipping the check."
	exit 1
}
if ! cmp -s "$OS/base/lib/libcurses/curses.h" "$TCSYSINC/curses.h"; then
	echo "curses: HEADERS DISAGREE"
	echo "  base/lib/libcurses/curses.h is what libcurses.a is compiled against;"
	echo "  $TCSYSINC/curses.h is what a program reaches without -I on that directory."
	echo "  They must be the same file.  First difference:"
	diff "$OS/base/lib/libcurses/curses.h" "$TCSYSINC/curses.h" | head -8
	exit 1
fi

# System headers come from include and nowhere else.  Do not add the donor
# 0.7.3 include directory: it is not in this repository, everything libcurses
# and libterm include resolves against include and include/sys alone,
# and it shadows 46 of our headers without agreeing with them -- its ctype.h
# numbers the bits above _L differently, so an object compiled against it and
# linked with our libc tests the wrong bit of the same table (isdigit answers
# for space, isspace for punctuation).
INC="-I $OS/include -I $OS/include/sys"

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
