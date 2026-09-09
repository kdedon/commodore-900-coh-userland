#!/bin/sh
# depcheck.sh -- does touching a source actually make its product out of date?
#
# The bug this catches is silent: a target that does not list what it is
# built from still builds, still reports success, and stages the previous
# binary.
#
# `make -q' asks "is this target out of date?" and builds nothing, so the
# whole audit runs in seconds.  Exit 1 from it means "would rebuild", which
# is the answer we want after touching a source.
#
# NODEPS=1 suppresses the generated per-dist fragment: GNU make remakes an
# included makefile during parsing, before -q takes effect, and that fragment
# order-only-depends on every stamp, so a plain `make -q' builds the whole
# userland first.  Do not suppress it by naming an INFO_GOALS goal instead:
# those are .PHONY, so make -q always answers "out of date" and every check
# passes vacuously -- hence the negative control below.
#
# Each source's mtime is restored afterwards, so a run leaves the tree exactly
# as it found it and does not provoke a rebuild of its own.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
DIST=${DIST:-coherent3-full-test}

# `fail' cannot be a plain variable: the main loop runs inside a pipeline,
# so its subshell's assignment is lost and a FAIL would still exit 0.  A
# marker file crosses the subshell boundary.
FAILF=$(mktemp)
trap 'rm -f "$FAILF"' EXIT
mark_fail() { echo x >> "$FAILF"; }
pass=0

# Two things `make -q' can mean by a nonzero status, and conflating them is how
# this instrument lies.  Exit 1 is "would rebuild", the answer we want.  Exit 2
# is an ERROR -- a refusing parse-time gate, a missing include, a syntax error --
# and read as exit 1 it makes every case SKIP with the words "already out of
# date", which names a dirty tree for what is really a broken query.  So the
# status is carried, not collapsed.
mstat() {	# mstat <make args...> : echoes make -q's exit status
	make -q "$@" >/dev/null 2>&1; echo $?
}
uptodate() {	# uptodate <make args...> : true when make has nothing to do
	[ "$(mstat "$@")" = 0 ]
}

# The toolchain stamp is EXCLUDED from every question about a target under $(OS).
# It is the prerequisite of nearly all of them, it belongs to another repository
# and another lane, and one uncommitted edit in src/ld there makes every case in
# this file report SKIP -- the whole audit vacuous for a reason that has nothing
# to do with what it measures.  `make -o' (--assume-old) answers as if that
# stamp were current, which isolates the question to this tree.  The four cases
# that ASK ABOUT the toolchain stamp must not have it assumed old, so the flag
# is chosen per target rather than set once.
TCSTAMPF="$HERE/build/.stamp-toolchain"
isolate() {	# isolate <target> : echoes the -o flags to use for it
	case "$1" in
	"$TCSTAMPF") ;;
	*) [ -e "$TCSTAMPF" ] && printf -- '-o %s' "$TCSTAMPF";;
	esac
}

# <source file>|<make target>|<what the pair proves>
#
# ONE CASE PER SOURCE DIRECTORY the build compiles out of.  The defect this
# catches is an ABSENT TREE, not a wrong rule, and a spot-check of one file per
# stamp cannot see it.  The directory a case stands for is what matters, so the
# file chosen inside it is arbitrary and the case name says the directory.
#
# sys, hrtty and the kernel-linked half of base/cmd/hostfs are not cases
# here any more: the kernel and its loadable drivers are in a separate
# repository, and $HERE/kobj/kernel.out and $HERE/build/drv/{hrtty,hostfs}
# are not targets this Makefile builds.
CASES="
$OS/base/cmd/echo.c|$HERE/build/.stamp-userland|base/cmd -- a userland command
$OS/base/cmd/ps.c|$HERE/build/.stamp-userland|base/cmd -- ps(1), compiled by build-extra-userland.sh
$OS/base/cmd/pr.c|$HERE/build/.stamp-userland|base/cmd -- pr(1), compiled by build-extra-userland.sh
$OS/base/cmd/enable.c|$HERE/build/.stamp-userland|base/cmd -- the command set
$OS/base/lib/regexp/regexp.c|$HERE/build/.stamp-userland|base/cmd -- the regexp more(1) and less(1) link
$OS/base/lib/libcurses/curses.h|$HERE/build/.stamp-userland|base/lib -- the libraries the sweep links
$OS/games/bsd/fish.c|$HERE/build/.stamp-userland|games -- a game
$OS/games/bsd/quiz/quiz.c|$HERE/build/.stamp-userland|games -- a multi-file game
$OS/games/lib/fortunes|$HERE/build/.stamp-userland|games/lib -- a DATA file staged into the image
$OS/games/bsd/CURSES.list|$HERE/build/.stamp-userland|the list deciding which games get curses
$OS/libc/ctype/_ctype.c|$HERE/build/.stamp-libc|libc -- libc
$OS/net/libsocket.c|$HERE/build/.stamp-net|net -- the net stack
$OS/net/include/net/gen/tcp_io.h|$HERE/build/.stamp-net|a net ioctl header
$OS/net/inet/generic/tcp.c|$HERE/build/.stamp-net|the inet daemon's generic code
$OS/archive/zoo/zoo.c|$HERE/build/.stamp-archive|archive -- an archiver package
$OS/comms/zmodem/rz.c|$HERE/build/.stamp-comms|comms -- a serial transfer program
$OS/base/lib/ndir/libndir/opendir.c|$HERE/build/.stamp-comms|the ndir shim ckermit links, from ANOTHER tree
$OS/test/tickrate/tickrate.c|$HERE/build/.stamp-tests|test -- a guest test program
$C900_TOOLCHAIN/src/cc/n2/z8001/outcoh.c|$HERE/build/.stamp-toolchain|the compiler backend
$C900_TOOLCHAIN/src/cc/n1/tables/leaves.t|$HERE/build/.stamp-toolchain|a cc1 selection table
$C900_TOOLCHAIN/src/as/z8001/machine.c|$HERE/build/.stamp-toolchain|the assembler
$C900_TOOLCHAIN/src/ld/all.c|$HERE/build/.stamp-toolchain|the linker
$OS/games/net/hunt/hunt/hunt.c|$HERE/build/.stamp-hunt|hunt's own source
$OS/net/libsocket.a|$HERE/build/.stamp-hunt|a LIBRARY hunt links
"

# There are no `include' cases left, because there is no include:
# every header it held duplicated -- and shadowed -- one owned by the toolchain
# or by the kernel, and the sweep now takes them from those repositories.  The
# toolchain's half is still covered, through $C900_TOOLCHAIN cases above; the
# kernel's is not covered by anything here, and a change to $KINC will not by
# itself mark the userland stamp out of date.
#
# The kernel's build variant (KTTY/KDDT, relinking kobj/kernel.out) was a case
# here too, until the kernel moved to a separate repository: this Makefile has
# no kobj/kernel.out target left to ask about, so there is no variant_case here
# any more -- that repository's own depcheck, if it has one, is where it belongs.

# NEGATIVE CONTROL: a file no target is built from.  This one MUST report that
# the target stays up to date; if it does not, the check is answering "would
# rebuild" unconditionally and every result above it is meaningless.
NEGSRC="$OS/README.md"
NEGTGT="$HERE/build/.stamp-userland"

# The SECOND negative control, and it is about breadth rather than about make.
# Eight more trees were added to ULSRC, and the cheap way to name a tree is
# `find -type f' -- which would put every documentation file into the userland's
# prerequisite list, so a documentation edit would rebuild 109 binaries.
# These files sit INSIDE the
# named trees and must still leave the stamp alone; a FAIL here means the fix
# bought its correctness by making the stamp fire on everything, which is
# indistinguishable from firing on nothing.
NEG2="
$OS/games/bsd/COPYING
$OS/base/lib/libcurses
"
breadth_control() {
	tgt="$HERE/build/.stamp-userland"
	[ -e "$tgt" ] || { echo "  SKIP  the breadth control -- stamp not built yet"; return; }
	iso=$(isolate "$tgt")
	# shellcheck disable=SC2086
	uptodate -C "$HERE" $iso NODEPS=1 DIST="$DIST" "$tgt" || {
		echo "  SKIP  the breadth control -- the stamp is already out of date"; return; }
	for d in $NEG2; do
		[ -e "$d" ] || continue
		# One file inside it, whatever it is called; a directory that holds
		# no file the loop can touch is skipped rather than passed.
		f=$(find "$d" -type f ! -name '*.c' ! -name '*.h' ! -name '*.s' \
		    ! -name '*.y' 2>/dev/null | head -1)
		[ -n "$f" ] || { [ -f "$d" ] && f="$d" || continue; }
		ref=$(mktemp); touch -r "$f" "$ref"; touch "$f"
		# shellcheck disable=SC2086
		if uptodate -C "$HERE" $iso NODEPS=1 DIST="$DIST" "$tgt"; then
			echo "  ok    breadth control: $(echo "$f" | sed "s|$OS/||") is not a userland source"
		else
			echo "  FAIL  breadth control: touching $(echo "$f" | sed "s|$OS/||")"
			echo "        rebuilds the whole userland -- ULSRC names non-sources"
			mark_fail
		fi
		touch -r "$ref" "$f"; rm -f "$ref"
	done
}

echo "== dependency audit (make -q: exit 1 = would rebuild = correct)"
echo "$CASES" | while IFS='|' read -r src target what; do
	[ -n "$src" ] || continue
	if [ ! -e "$src" ]; then
		echo "  SKIP  $what -- no $src"
		continue
	fi
	if [ ! -e "$target" ]; then
		echo "  SKIP  $what -- $target not built yet"
		continue
	fi
	iso=$(isolate "$target")
	# shellcheck disable=SC2086 -- $iso is a make flag pair, deliberately split
	st=$(mstat -C "$HERE" $iso NODEPS=1 DIST="$DIST" "$target")
	case "$st" in
	0)	;;
	1)	echo "  SKIP  $what -- $(basename "$target") is already out of date"
		continue;;
	*)	echo "  BROKEN $what -- \`make -q $(basename "$target")' EXITED $st."
		echo "         Not a dependency answer at all: the makefile refused or"
		echo "         failed to parse.  Run it without -q to see what it said."
		mark_fail
		continue;;
	esac
	# Remember the mtime so the tree is left untouched.
	ref=$(mktemp); touch -r "$src" "$ref"
	touch "$src"
	# shellcheck disable=SC2086
	if [ "$(mstat -C "$HERE" $iso NODEPS=1 DIST="$DIST" "$target")" = 0 ]; then
		echo "  FAIL  $what: touching $(basename "$src") leaves $(basename "$target") up to date"
		mark_fail
	else
		echo "  ok    $what"
		pass=$((pass+1))
	fi
	touch -r "$ref" "$src"; rm -f "$ref"
done

breadth_control

# The same question, asked of the compiler's own build tree's Makefile.  Its
# goals are relative names, so it is asked with -C and the goal it actually
# publishes.
BE="$TC"
backend_cases() {
	[ -d "$BE" ] || { echo "  SKIP  the z8001 track -- no $BE"; return; }
	[ -e "$BE/build/stamp/z8001" ] || {
		echo "  SKIP  the z8001 track -- not built yet"; return; }
	# `z8001' there is .PHONY, which is the shape this file's header warns
	# about -- but it carries no recipe of its own, it only names
	# build/stamp/z8001, so make -q answers about that stamp and
	# discriminates.
	uptodate -C "$BE" z8001 || {
		echo "  SKIP  the z8001 track -- make z8001 is already out of date"
		return; }
	for src in \
	    "$C900_TOOLCHAIN/src/cc/n1/tables/leaves.t" \
	    "$C900_TOOLCHAIN/src/cc/n2/z8001/outcoh.c" \
	    "$C900_TOOLCHAIN/src/as/z8001/machine.c" \
	    "$C900_TOOLCHAIN/src/ld/all.c"
	do
		[ -e "$src" ] || { echo "  SKIP  make z8001 vs $(basename "$src")"; continue; }
		ref=$(mktemp); touch -r "$src" "$ref"
		touch "$src"
		if make -q -C "$BE" z8001 >/dev/null 2>&1; then
			echo "  FAIL  make z8001: touching $(basename "$src") leaves it up to date"
			mark_fail
		else
			echo "  ok    make z8001 vs $(basename "$src")"
		fi
		touch -r "$ref" "$src"; rm -f "$ref"
	done
	# Negative control for this Makefile too: a doc file is not a source.
	if [ -e "$BE/BUILD.md" ]; then
		ref=$(mktemp); touch -r "$BE/BUILD.md" "$ref"; touch "$BE/BUILD.md"
		if make -q -C "$BE" z8001 >/dev/null 2>&1; then
			echo "  ok    negative control (make z8001 vs BUILD.md)"
		else
			echo "  BROKEN: make z8001 says \"would rebuild\" for a non-source"
			echo "          -- every backend result above it is vacuous."
			mark_fail
		fi
		touch -r "$ref" "$BE/BUILD.md"; rm -f "$ref"
	fi
}
backend_cases

# Not a dependency question: whether a toolchain rebuild ever leaves the
# shared compiler path missing.  build-cc.sh builds into a private directory
# and publishes by renaming a symlink onto build/z8001 -- anything else would
# empty the path under a concurrent compile -- so the check is that the path
# IS a symlink to a complete toolchain.
atomic_toolchain_case() {
	pub="$BE/build/z8001"
	[ -e "$pub" ] || { echo "  SKIP  the shared toolchain path -- not built yet"; return; }
	if [ ! -L "$pub" ]; then
		echo "  FAIL  the shared toolchain path: build/z8001 is a plain"
		echo "        directory, so a rebuild empties it under everyone else"
		mark_fail
		return
	fi
	miss=''
	for b in cc0-z8001 cc1-z8001 cc2-z8001; do
		[ -x "$pub/$b" ] || miss="$miss $b"
	done
	if [ -n "$miss" ]; then
		echo "  FAIL  the shared toolchain path: published but missing$miss"
		mark_fail
	else
		echo "  ok    the shared toolchain path (symlink to a complete tree)"
	fi
}
atomic_toolchain_case

# The same claim, measured rather than inferred: rebuild the compiler for real
# and sample the shared path throughout.  Costs a full toolchain build, so it is
# opt-in.
if [ "${DEEP:-0}" = 1 ]; then
	sh "$BE/build-cc.sh" >/dev/null 2>&1 &
	bg=$!; tot=0; miss=0
	while kill -0 $bg 2>/dev/null; do
		tot=$((tot+1))
		[ -x "$BE/build/z8001/cc0-z8001" ] || miss=$((miss+1))
	done
	wait $bg
	if [ "$miss" = 0 ]; then
		echo "  ok    DEEP: cc0-z8001 present in all $tot samples during a rebuild"
	else
		echo "  FAIL  DEEP: cc0-z8001 absent in $miss of $tot samples during a rebuild"
		mark_fail
	fi
fi

# Verify the instrument itself before believing any of the above.
NEGISO=$(isolate "$NEGTGT")
# shellcheck disable=SC2086
if [ -e "$NEGSRC" ] && [ -e "$NEGTGT" ] && \
   uptodate -C "$HERE" $NEGISO NODEPS=1 DIST="$DIST" "$NEGTGT"; then
	ref=$(mktemp); touch -r "$NEGSRC" "$ref"; touch "$NEGSRC"
	# shellcheck disable=SC2086
	if make -q -C "$HERE" $NEGISO NODEPS=1 DIST="$DIST" "$NEGTGT" >/dev/null 2>&1; then
		echo "  ok    negative control (a non-source does not trigger a rebuild)"
	else
		echo "  BROKEN: the check reports \"would rebuild\" for a file nothing"
		echo "          is built from -- every result above it is vacuous."
		mark_fail
	fi
	touch -r "$ref" "$NEGSRC"; rm -f "$ref"
fi

if [ -s "$FAILF" ]; then
	echo "== done: $(wc -l < "$FAILF") FAILED (the target does not list that source)"
	exit 1
fi
echo "== done: no gaps found"
