#!/bin/sh
# build-elvis.sh -- elvis 1.4 (vi clone, native COHERENT target upstream),
# against the real termcap; separated-I/D link.
#
# NOT tinytcap.  elvis carries its own cut-down termcap for hosts that have none,
# and it answers 25 lines by 80 columns for EVERY terminal (tinytcap.c tgetnum),
# ignoring the name it is handed -- so on the 85x32 HR console elvis used 25 rows
# of 32 and $TERM had no effect at all.  elvis' own Coherent profile does not use
# it either: cmd/elvis/Makefile's COHERENT target is `EXTRA=' plus `-lterm'.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
CURSES="$TCB/curses"
BIN="$HERE/build/bin"
LOG="$HERE/build/elvis.log"
mkdir -p "$BIN"; : > "$LOG"
[ -f "$CURSES/libterm.a" ] || sh "$HERE/build-curses.sh" >/dev/null 2>&1
SRCS=""
for f in blk cmd1 cmd2 curses cut ex input main misc modify move1 move2 \
         move3 move4 move5 opts recycle redraw regexp regsub system tio \
         tmp vars vcmd vi; do
	SRCS="$SRCS $OS/base/cmd/elvis/$f.c"
done
# System headers come from include and include/sys only; see
# build-curses.sh.
if CCZ_VAR=800000020800 "$CCZ" -s -i -L -DUSE_TERMCAP=1 \
     -I "$OS/include" -I "$OS/include/sys" \
     -o "$BIN/.elvis.new" $SRCS "$CURSES/libterm.a" >>"$LOG" 2>&1; then
	mv -f "$BIN/.elvis.new" "$BIN/elvis"
	echo "== elvis linked: $(wc -c < "$BIN/elvis") bytes"
else
	echo "== elvis FAILED"; grep -v 'Strict\|Warning' "$LOG" | tail -12
	rm -f "$BIN/.elvis.new"; fail=1
fi
[ "${fail:-0}" -eq 0 ]
