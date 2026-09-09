#!/bin/sh
# build-curses-games.sh -- curses games (worm, snake) against the in-tree
# libcurses.a + libterm.a (build-curses.sh must run first).  Separated-I/D,
# large model (curses games can exceed one segment).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
CURSES="$TCB/curses"
BIN="$HERE/build/bin/games"
LOG="$HERE/build/curses-games.log"
# ttycity's run-time resources (stri.*/snro.* strings, the 24 bundled cities)
# are NOT in this repository -- see ttycity.mk, which documents the checkout,
# the licence reason it stays a checkout, and the same search list this uses.
# The default here was $HOME/git/ttycity, which is one developer's disk: on that
# machine the resources appeared, on every other the game shipped with empty
# strings and no cities and the build said nothing that named a repository.
# $TTYCITY_SRC still names a checkout explicitly.
C900_TCITY_SEARCH="$OS/../commodore-900-ttycity $OS/repos/commodore-900-ttycity $OS/../ttycity $OS/repos/ttycity"
if [ -z "${TTYCITY_SRC:-}" ]; then
	for c in $C900_TCITY_SEARCH; do
		[ -f "$c/src/c900.h" ] && { TTYCITY_SRC=$c; break; }
	done
fi
: "${TTYCITY_SRC:=}"
mkdir -p "$BIN"; : > "$LOG"
# libm last: snake computes a distance with sqrt().  Without it the link failed
# with TWO undefined symbols, and the second one was misleading -- ld defines
# etext/edata/end only when the link is otherwise complete (`if (nundef)' in
# ld/main.c suppresses dcomm), so a missing sqrt also takes out `end', which
# libc's brk.s references.  Chasing `end_' as its own problem is chasing a
# symptom.
LIB="$CURSES/libcurses.a $CURSES/libterm.a $TCB/libm-z8001/libm-z8001.a"
# games/lib/src holds only what libc has not got: err(3) and fgetln(3).  A
# source named here is an OBJECT on the link line, so it is bound in preference
# to the archive member of the same name -- which is why nothing that libc
# already defines may be listed, and why getopt, strtok, strdup, strchr,
# memcmp, memcpy, memset, strcasecmp and strtoul are not.
GLIB="$OS/games/lib/src/err.c $OS/games/lib/src/fgetln.c"
# games/lib/cursrc: the curses-only support.  It is NOT in games/lib/src because
# the plain sweep compiles that whole directory into every game and these two
# reference libcurses.  waddch.c must stay ahead of libcurses.a on the link
# line -- it overrides the archive's broken member; the file says why.
GLIB="$OS/games/lib/cursrc/waddch.c $OS/games/lib/cursrc/curs_set.c $GLIB"
# System headers come from include and include/sys only; see
# build-curses.sh.
# $OS/include is gone -- the userland no longer keeps a shadow copy of the
# system headers.  ccz appends the toolchain's include directories to every
# compile, which is where <curses.h> and the C library's headers now come
# from; only the machine layer needs naming, and only by the targets below
# that actually reach it.
INC="-DCOHERENT -I $OS/base/lib/libcurses"

ok=0; fail=0; fl=""

build() { # name  "src ..."
	name="$1"; shift
	if CCZ_VAR=800000020800 "$CCZ" -s -i -L $INC -o "$BIN/.$name.new" "$@" $GLIB $LIB >>"$LOG" 2>&1; then
		mv -f "$BIN/.$name.new" "$BIN/$name"
		ok=$((ok+1)); echo "== $name linked ($(wc -c < "$BIN/$name") B)"
	else
		fail=$((fail+1)); fl="$fl $name"; rm -f "$BIN/.$name.new"
		echo "== $name FAILED"; grep -v 'Strict\|Warning' "$LOG" | tail -8
	fi
}

# clear(1) is not a game, but it is the other thing in this tree that links
# libterm: it asks termcap for `cd' and `cm' and writes them to stdout.  Building
# it here keeps the one place that knows where libterm is.
#
# It is NOT built with build(), which links $LIB -- libcurses AHEAD of libterm --
# and $GLIB, whose waddch.c is a libcurses override.  libcurses.a and libterm.a
# both define the termcap globals UP, BC and PC, so pulling in a curses module
# alongside libterm gives `Ld: symbol UP_: redefined'.  clear wants termcap
# alone, so it links libterm alone, as top(1) below does.
#
# clear goes to build/bin, NOT build/bin/games: games.list stages that whole
# directory as a tree, so anything left there also appears in /usr/games.
if CCZ_VAR=800000020800 "$CCZ" -s -i -L $INC \
    -o "$HERE/build/bin/.clear.new" "$OS/base/cmd/clear.c" \
    "$CURSES/libterm.a" >>"$LOG" 2>&1; then
	mv -f "$HERE/build/bin/.clear.new" "$HERE/build/bin/clear"
	ok=$((ok+1)); echo "== clear linked ($(wc -c < "$HERE/build/bin/clear") B)"
else
	rm -f "$HERE/build/bin/.clear.new"
	fail=$((fail+1)); fl="$fl clear"
	echo "== clear FAILED"; grep -v 'Strict\|Warning' "$LOG" | tail -8
fi

# top(1) is the second: it draws its screen with its own termcap package
# (screen.c/display.c), so it links libterm and not libcurses.  It is NOT built
# with $GLIB -- the games' BSD compatibility sources -- because it needs none
# of them and every one of them is a definition that would rather come from
# libc.  It does not link libm either.  Its own line, therefore, rather than
# the games' build().
#
# sigdesc.h is generated, not vendored: it is the signal name/number table the
# `k' command matches against, and the distribution's sigconv.awk derives it
# from the system's own headers so that the two cannot drift.  sigsel.awk runs
# first and says why: <signal.h> holds two sets of signal numbers and only one
# of them is this machine's.
#
# The machine-dependent half (SIGEPA/SIGPRV/...) is msig.h, and it is NOT
# taken from this tree: include/msig.h is a kernel-owned header
# that is mid-reconciliation and may be removed here, and $OS/include/sys
# has no msig.h at all -- $OS/include/sys/msig.h was tried and does not
# exist, which is what silently starved this cat and produced an empty
# sigdesc.h (the games then reported as failing on the literal directory
# "hostbuild/." -- cc1's name for a compiland it was never given).  The
# kernel's own Z8001 msig.h (os/sys/z8001/h/msig.h) is the durable source;
# it is reached through $KINC, the kernel include root toolchain.sh
# resolves and exports, rather than a hardcoded path into either tree.
# $OS/include/sys/msig.h is a different, generic donor stub (SIGDIVE/...)
# and is not a substitute -- using it would generate a sigdesc.h with the
# wrong signal names instead of failing.
build_top() {
	TOPSRC="$OS/base/cmd/top"
	TOPGEN="$HERE/build/top"
	mkdir -p "$TOPGEN"
	MSIG=""
	if [ -n "${KINC:-}" ]; then
		KROOT="${KINC%/os/include}"
		if [ "$KROOT" != "$KINC" ] && [ -f "$KROOT/os/sys/z8001/h/msig.h" ]; then
			MSIG="$KROOT/os/sys/z8001/h/msig.h"
		fi
	fi
	if [ -z "$MSIG" ]; then
		fail=$((fail+1)); fl="$fl top"
		echo "== top FAILED: no Z8001 msig.h reachable via \$KINC (KINC='${KINC:-}'); need <kernel>/os/sys/z8001/h/msig.h"
		return
	fi
	# <signal.h> comes from the kernel too, and for the same reason as msig.h:
	# the signal numbers are the kernel's, not the C library's.  sigsel.awk
	# wants the file with BOTH arms in it -- it strips the _I386 one itself --
	# so this must be the conditional header, which is what $KINC/signal.h is.
	SIGH=""
	[ -n "${KINC:-}" ] && [ -f "$KINC/signal.h" ] && SIGH="$KINC/signal.h"
	if [ -z "$SIGH" ]; then
		fail=$((fail+1)); fl="$fl top"
		echo "== top FAILED: no signal.h reachable via \$KINC (KINC='${KINC:-}'); sigsel.awk needs <kernel>/os/include/signal.h"
		return
	fi
	if ! { awk -f "$TOPSRC/sigsel.awk" "$SIGH"; \
	       cat "$MSIG"; } \
	     | awk -f "$TOPSRC/sigconv.awk" > "$TOPGEN/sigdesc.h"; then
		fail=$((fail+1)); fl="$fl top"
		echo "== top FAILED: cannot generate sigdesc.h"; return
	fi
	if CCZ_VAR=800000020800 "$CCZ" -s -i -L \
	    -I "$TOPSRC" -I "$TOPGEN" -I "$KINC" -I "$KINC/sys" \
	    -o "$HERE/build/bin/.top.new" \
	    "$TOPSRC/top.c" "$TOPSRC/display.c" "$TOPSRC/screen.c" \
	    "$TOPSRC/commands.c" "$TOPSRC/utils.c" "$TOPSRC/username.c" \
	    "$TOPSRC/version.c" "$TOPSRC/m_coherent.c" \
	    "$CURSES/libterm.a" >>"$LOG" 2>&1; then
		mv -f "$HERE/build/bin/.top.new" "$HERE/build/bin/top"
		ok=$((ok+1)); echo "== top linked ($(wc -c < "$HERE/build/bin/top") B)"
	else
		fail=$((fail+1)); fl="$fl top"; rm -f "$HERE/build/bin/.top.new"
		echo "== top FAILED"; grep -v 'Strict\|Warning' "$LOG" | tail -8
	fi
}
build_top

build worm  "$OS/base/cmd/worm/worm.c"
[ -d "$OS/base/cmd/snake" ] && build snake $(ls "$OS"/base/cmd/snake/*.c)

# The BSD games that need curses.  games/bsd/CURSES.list is the single list;
# build-games.sh reads the same file to skip them.
for g in $(sed 's/#.*//' "$OS/games/bsd/CURSES.list" 2>/dev/null); do
	[ -d "$OS/games/bsd/$g" ] || {
		fail=$((fail+1)); fl="$fl $g"
		echo "== $g FAILED: CURSES.list names it, no games/bsd/$g"; continue; }
	build "$g" $(ls "$OS/games/bsd/$g"/*.c)
done

# robots keeps a high-score table.  It survives the file being absent -- it says
# so and plays on -- but an empty file is enough to start from: read_score()
# treats a short read as "no scores yet" and writes the table back in full.
R="$HERE/build/root/usr/games/lib"
if [ -x "$BIN/robots" ]; then
	mkdir -p "$R" && [ -f "$R/robots_roll" ] || : > "$R/robots_roll"
fi

# Data files.  Everything under build/root/usr/games/lib is staged as a tree by
# dist/lists/games.list, so a directory copied here appears at
# /usr/games/lib/<name> on the target and needs no dist-list entry of its own.
#
# atc: the sixteen scenario files plus Game_List, read by name out of
# _PATH_GAMES (games/bsd/atc/def.h).  The score file must exist and be writable
# by the player: open_score_file() creates it with O_CREAT, but only if the
# directory allows it, and it EXITS if the descriptor lands below 3 -- so an
# empty file staged here is what makes a first run silent.
if [ -x "$BIN/atc" ]; then
	mkdir -p "$R/atc" && cp "$OS"/games/lib/atc/* "$R/atc/" &&
	{ [ -f "$R/atc_score" ] || : > "$R/atc_score"; } &&
	echo "== atc: $(ls "$R/atc" | wc -l) scenario files staged"
fi

# hangman picks its word out of a plain newline-separated list by seeking to a
# random file offset, so the list must be present -- setup() exits if it is not.
if [ -x "$BIN/hangman" ]; then
	mkdir -p "$R" && cp "$OS/games/lib/hangman.words" "$R/hangman.words" &&
	echo "== hangman: $(grep -c . "$R/hangman.words") words staged"
fi

# cribbage: the rules text its instructions() cats, and the per-game log.
if [ -x "$BIN/cribbage" ]; then
	mkdir -p "$R" && cp "$OS/games/lib/cribbage.instr" "$R/cribbage.instr" 2>/dev/null
	[ -f "$R/criblog" ] || : > "$R/criblog"
fi

# canfield keeps one betting record per uid, indexed by uid, so the file is
# sparse and must exist: the game plays without it but records nothing.
if [ -x "$BIN/canfield" ]; then
	mkdir -p "$R" && [ -f "$R/cfscores" ] || : > "$R/cfscores"
fi

# battlestar appends one line per finished game.
if [ -x "$BIN/battlestar" ]; then
	mkdir -p "$R" && [ -f "$R/battlestar.log" ] || : > "$R/battlestar.log"
fi

# monop: the card deck is a PACKED file, built from monop/cards.inp by the
# upstream initdeck tool.  That tool never runs on the target -- it is a
# generator, like fortune's strfile -- so it is kept as initdeck.c.host and built
# with the host cc here.  The file it writes is byte-order-explicit (big-endian
# 32-bit counts, big-endian 64-bit card offsets), so a host-generated deck is
# valid on the Z8001 unchanged; games/bsd/monop/cards.c reads it.
#
# initdeck.c.host is the PRISTINE upstream file, deliberately not run through the
# K&R/de-ANSI prep: it is host code, and the prep's u_int32_t rewrite collides
# with its own "#ifndef u_int32_t" fallback.  -std=gnu89 because it has K&R
# function definitions, which a current gcc rejects by default.
if [ -x "$BIN/monop" ]; then
	mkdir -p "$R"
	if [ ! -x "$HERE/build/initdeck" -o \
	     "$OS/games/bsd/monop/initdeck.c.host" -nt "$HERE/build/initdeck" ]; then
		cc -w -std=gnu89 -x c -I "$OS/games/bsd/monop" \
		   -o "$HERE/build/initdeck" \
		   "$OS/games/bsd/monop/initdeck.c.host" 2>/dev/null &&
		echo "== monop: initdeck built"
	fi
	if [ -x "$HERE/build/initdeck" ]; then
		( cd "$R" && "$HERE/build/initdeck" "$OS/games/lib/monop.inp" ) &&
		echo "== monop: cards.pck generated"
	fi
fi

# tetris keeps its own score table, same reasoning as robots.  The name is the
# one compiled into games/bsd/tetris/score.c (HIGH_SCORE_TABLE); read_high_scores
# complains to stderr if the file is missing, which lands on top of the drawn
# board, so staging an empty one is not merely cosmetic.
if [ -x "$BIN/tetris" ]; then
	mkdir -p "$R" && [ -f "$R/Tetris_scores" ] || : > "$R/Tetris_scores"
fi

# ttycity (Micropolis/SimCity) builds from its own Makefile rather than this
# sweep's one-file rule: 35 sources, its own de-ANSI'd tree, and a second
# target (hrtiles) that is independent of the engine.  Its binaries land in
# $BIN, so `t /usr/games' ships them with no dist entry; only the resources
# need list lines.
#
# HELD BACK FROM THE IMAGE.  Set TTYCITY=1 to build and stage it again; the
# resource entries in dist/lists/games.list are commented out to match, and
# any stale $BIN/ttycity is removed so a `t /usr/games' does not ship one from
# an earlier sweep.
if [ "${TTYCITY:-0}" = 0 ]; then
	rm -f "$BIN/ttycity" "$BIN/hrtiles"
	rm -rf "$R/ttycity"
	echo "  ttycity: held back (set TTYCITY=1 to build)"
elif [ -d "$OS/games/ttycity" ]; then
	if make -C "$OS/games/ttycity" CCZ="$CCZ" CURSES="$CURSES" \
		OBJ="$HERE/build/ttycityobj" BIN="$BIN" >>"$LOG" 2>&1; then
		echo "  ttycity: OK ($(wc -c < "$BIN/ttycity") B)"
		# The resources are not baked into the binary:
		# w_resrc.c reads them from /usr/games/lib/ttycity at run time.  They
		# still live in the upstream tree rather than here -- vendoring the
		# 869 KB is a call for whoever owns the image budget -- so stage them
		# if that tree is present and say so plainly if it is not.
		T="$R/ttycity"
		if [ -d "$TTYCITY_SRC/res" ]; then
			mkdir -p "$T/cities"
			cp "$TTYCITY_SRC"/res/* "$T/" 2>/dev/null
			cp "$TTYCITY_SRC"/cities/* "$T/cities/" 2>/dev/null
			echo "  ttycity: resources staged ($(du -sk "$T" | cut -f1) KB)"
		else
			echo "  ttycity: NO resources -- the strings and the 24 cities"
			echo "    are not in this repository (licence; see"
			echo "    hostbuild/ttycity.mk).  Clone the C900 fork of"
			echo "    tenox7/ttycity -- the one carrying src/c900.h -- to"
			for c in $C900_TCITY_SEARCH; do echo "      $c"; done
			echo "    or set TTYCITY_SRC to it.  Without them the game runs"
			echo "    with empty strings and no cities."
		fi
	else
		fail=$((fail+1)); fl="$fl ttycity"
		echo "  ttycity: FAILED -- $(grep -iE 'error|no match|Internal' "$LOG" | tail -1)"
	fi
fi

# The summary line and the exit status are the same fact.  Both are needed: the
# caller reads the status, and a reader of the log needs the count to sit next to
# the per-program lines rather than being recomputed by counting them.
echo "== curses games: $ok linked, $fail failed:$fl"
[ "$fail" -eq 0 ]
