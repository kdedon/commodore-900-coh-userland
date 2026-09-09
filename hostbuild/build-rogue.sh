#!/bin/sh
# build-rogue.sh -- rogue-clone 5.4 (the Coherent rogue from the FTP), its
# own -DCURSES termcap curses bundled (Coherent Makefile config:
# -DUNIX -DUNIX_SYSV -DCURSES, + -DCOHERENT for the in-source guards).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
BIN="$HERE/build/bin/games"
LOG="$HERE/build/rogue.log"
mkdir -p "$BIN"; : > "$LOG"
DEFS="-DUNIX -DUNIX_SYSV -DCURSES -DCOHERENT"
# System headers come from include and include/sys only; see
# build-curses.sh.
INC="-I $OS/base/cmd/rogue -I $OS/include -I $OS/include/sys"
SRCS=$(ls "$OS"/base/cmd/rogue/*.c | grep -v sim_getlogin | tr '\n' ' ')
SRCS="$OS/base/cmd/rogue/sim_getlogin.c $SRCS"
if CCZ_VAR=800000020800 "$CCZ" -s -i -L $DEFS $INC \
     -o "$BIN/.rogue.new" $SRCS >>"$LOG" 2>&1; then
	mv -f "$BIN/.rogue.new" "$BIN/rogue"
	echo "== rogue linked: $(wc -c < "$BIN/rogue") bytes"
else
	echo "== rogue FAILED"; grep -v 'Strict\|Warning' "$LOG" | tail -14
	rm -f "$BIN/.rogue.new"; fail=1
fi
[ "${fail:-0}" -eq 0 ]
