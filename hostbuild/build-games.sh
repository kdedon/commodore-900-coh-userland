#!/bin/sh
# build-games.sh -- build the games tree into native C900 binaries
# (build/bin/games).  games/bsd = BSD games ports; games/lib/src = support
# code (getopt, ...).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
BIN="$HERE/build/bin/games"
LOG="$HERE/build/games.log"
mkdir -p "$BIN"; : > "$LOG"

# games/lib/src holds only what libc has not got: err(3) and fgetln(3).  These
# are compiled as SOURCES, so each is an object on the link line and is bound in
# preference to an archive member of the same name; nothing libc already defines
# may live in this directory.
LIBSRC="$OS/games/lib/src"/*.c
# libm last, and always: pom computes the moon's phase with sin/cos, and ld only
# synthesizes etext_/edata_/end_ when nothing else is undefined, so a missing
# libm routine also takes out `end' (which libc's brk.s references) and reports
# itself as a second, misleading error.  factor's long-standing "end_" failure
# in the userland sweep was only ever this.
LIBM="$TCB/libm-z8001/libm-z8001.a"

# fortune: COHERENT's fortune(1) reads the plaintext, blank-line-separated
# database directly, so there is no index to generate and no host tool to build
# -- the file is staged as it stands.  A copy that fails is a failure of this
# sweep: fortune finds nothing to say without it.
fortunefail=0
R="$HERE/build/root/usr/games/lib"; mkdir -p "$R"
if [ ! -f "$R/fortunes" -o "$OS/games/lib/fortunes" -nt "$R/fortunes" ]; then
	if cp "$OS/games/lib/fortunes" "$R/fortunes"; then
		echo "== fortune: $(grep -c '^$' "$R/fortunes") fortunes staged"
	else
		fortunefail=1
		echo "== fortune: fortunes FAILED"
	fi
fi

# adventure: the original game scans its whole database out of an encrypted
# copy of glorkz at every start.  Here that scan runs once, on the HOST:
# games/bsd/adventure/host/dumper.c.host links the pristine bsd-games scan
# sources beside it with setup's encryption of glorkz, runs rdata(), and writes
# the message image adventure reads by offset from /usr/games/lib/adventure.msg
# and the tables.c that indexes it.  tables.c goes beside the game's sources,
# where the sweep below compiles it; adventure.msg goes straight to the staged
# tree.  Both are remade whenever either is absent or anything they are made
# from is newer.  A game linked against tables that were not regenerated would
# index a message file it does not match, so a failed generation is fatal.
A="$OS/games/bsd/adventure"; AG="$HERE/build/adventure"
advgen=0
[ -f "$A/tables.c" ] && [ -f "$R/adventure.msg" ] || advgen=1
for i in "$A/glorkz" "$A"/host/*; do
	if [ "$i" -nt "$A/tables.c" ] || [ "$i" -nt "$R/adventure.msg" ]; then
		advgen=1
	fi
done
if [ "$advgen" -ne 0 ]; then
	mkdir -p "$AG"
	if cc -w -x c -o "$AG/setup" "$A/host/setup.c.host" >"$AG/gen.log" 2>&1 &&
	   "$AG/setup" "$A/glorkz" >"$AG/data.c" 2>>"$AG/gen.log" &&
	   cc -w -D'__RCSID(x)=' -D'__COPYRIGHT(x)=' -I "$AG" -x c \
	      -o "$AG/dumper" "$A/host/dumper.c.host" "$A/host/io.c.host" \
	      "$A/host/vocab.c.host" "$A/host/init.c.host" \
	      "$A/host/wizard.c.host" "$A/host/save.c.host" \
	      "$A/host/crc.c.host" >>"$AG/gen.log" 2>&1 &&
	   (cd "$AG" && ./dumper) 2>>"$AG/gen.log" &&
	   mv -f "$AG/tables.c" "$A/tables.c" &&
	   mv -f "$AG/adventure.msg" "$R/adventure.msg"; then
		echo "== adventure: tables.c and adventure.msg ($(wc -c < "$R/adventure.msg") B) generated"
	else
		echo "== adventure: FAIL -- not generated from glorkz, see $AG/gen.log"
		exit 1
	fi
fi

# The other games that read a data file out of /usr/games/lib by absolute path.
# Each name is the path its program opens, so a file absent here is a program
# that runs and then cannot do the one thing the file is for: wump and fish
# print their apology instead of the instructions, as adventure (above) exits
# without its message file.  Counted with fortune's, for fortune's reason.
for d in wump.info fish.instr; do
	if [ ! -f "$R/$d" -o "$OS/games/lib/$d" -nt "$R/$d" ]; then
		if cp "$OS/games/lib/$d" "$R/$d"; then
			echo "== data: $d staged ($(wc -c < "$R/$d") B)"
		else
			fortunefail=1
			echo "== data: $d FAILED"
		fi
	fi
done

# quiz(6) reads an INDEX plus one file per category, so its data is a directory
# and not a file.  dist/lists/games.list needs its own `t /usr/games/lib/quiz.db'
# line for it, the way atc's scenario directory has one: a `t' directive copies a
# directory's files without descending into it.
#
# The index is GENERATED and not vendored: index.in names each category file by
# the placeholder @quiz_dir@, which is the directory the files are installed in
# -- quiz opens the paths the index gives it, so the placeholder has to become
# the target path here and not the host one.  index.in and the upstream
# Makefrag are inputs and are not staged.
#
# Staged per category and not as a unit: each file is copied when the staged
# tree has not got it or the source is newer, so an edited answer reaches the
# image and a category deleted from under the staged tree comes back.
QD=/usr/games/lib/quiz.db
mkdir -p "$R/quiz.db"
quizfail=0; quizstaged=0
for c in "$OS"/games/lib/quiz.db/*; do
	b="$(basename "$c")"
	case "$b" in index.in|Makefrag) continue;; esac
	if [ ! -f "$R/quiz.db/$b" -o "$c" -nt "$R/quiz.db/$b" ]; then
		if cp "$c" "$R/quiz.db/$b"; then
			quizstaged=$((quizstaged+1))
		else
			quizfail=1
		fi
	fi
done
if [ ! -f "$R/quiz.db/index" -o \
     "$OS/games/lib/quiz.db/index.in" -nt "$R/quiz.db/index" ]; then
	if sed "s|@quiz_dir@|$QD|g" "$OS/games/lib/quiz.db/index.in" \
		> "$R/quiz.db/index"
	then
		quizstaged=$((quizstaged+1))
	else
		quizfail=1
	fi
fi
if [ "$quizfail" -ne 0 ]; then
	fortunefail=1
	echo "== quiz: quiz.db FAILED"
elif [ "$quizstaged" -ne 0 ]; then
	echo "== quiz: $quizstaged of $(ls "$R/quiz.db" | wc -l) datfiles staged"
fi

ok=0; failed=0; faillist=""; skipped=""
# Curses games are built by build-curses-games.sh, which puts libcurses on the
# link line; skip them here rather than reporting a link failure for a game that
# is not this sweep's to build.
CURSES_GAMES="$(sed 's/#.*//' "$OS/games/bsd/CURSES.list" 2>/dev/null | tr -s '[:space:]' ' ')"
# single-file games: games/bsd/<name>.c ; multi-file: games/bsd/<name>/*.c
for f in "$OS"/games/bsd/*.c "$OS"/games/bsd/*/; do
	[ -e "$f" ] || continue
	case "$f" in
	*/games/bsd/scripts/) continue;;	# handled below: not a C game
	*/) b="$(basename "$f")"; srcs="$f"*.c;;
	*)  b="$(basename "$f" .c)"; srcs="$f";;
	esac
	case " $CURSES_GAMES " in
	*" $b "*) skipped="$skipped $b"; continue;;
	esac
	# System headers come from the toolchain, which ccz puts on the path
	# itself; this tree owns none.  See build-curses.sh.
	if "$CCZ" -s -i -I "$OS/include" -I "$OS/include/sys" \
	          -o "$BIN/.$b.new" $srcs $LIBSRC $LIBM >>"$LOG" 2>&1; then
		mv -f "$BIN/.$b.new" "$BIN/$b"
		ok=$((ok+1))
	else
		failed=$((failed+1)); faillist="$faillist $b"; rm -f "$BIN/.$b.new"
	fi
done
# games/bsd/scripts: the shell games (wargames).  They install like binaries --
# games.list stages build/bin/games as a tree -- but there is nothing to compile,
# so they are copied rather than built, and executable.
nscript=0
for f in "$OS"/games/bsd/scripts/*; do
	[ -f "$f" ] || continue
	cp "$f" "$BIN/$(basename "$f")" && chmod 755 "$BIN/$(basename "$f")" &&
	nscript=$((nscript+1))
done
[ "$nscript" -gt 0 ] && echo "== shell games installed: $nscript"

echo "== games sweep: $ok linked, $failed failed"
[ -n "$faillist" ] && echo "== failed:$faillist"
[ -n "$skipped" ] && echo "== curses games (build-curses-games.sh):$skipped"
# A sweep that linked nothing is not a sweep that succeeded: the caller stamps
# the step as done on this status, and dist staging then ships whatever binary
# the previous build left behind.  fortune's database counts the same way -- it
# is staged from here, so it is this sweep's product.
[ "$failed" -eq 0 ] && [ "$fortunefail" -eq 0 ]
