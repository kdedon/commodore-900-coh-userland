#!/bin/sh
# build-editors.sh -- the two screen editors the image was missing:
#
#   me    MicroEMACS, the MWC-provided COHERENT editor (editors/me), linked
#         against the real libterm; `emacs' is a link to it on the image.
#   pico  the standalone pico (editors/pico), modeless, also over libterm.
#
# Both are separated-I/D links, like elvis.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
CURSES="$TCB/curses"
BIN="$HERE/build/bin"
LOG="$HERE/build/editors.log"
mkdir -p "$BIN"; : > "$LOG"
[ -f "$CURSES/libterm.a" ] || sh "$HERE/build-curses.sh" >/dev/null 2>&1

# System headers come from include and include/sys only; see
# build-curses.sh.
INC="-I $OS/include -I $OS/include/sys"

ME=""
for f in ansi basic bracket buffer comtab display error execute file fileio \
         helplib line lookup main random region search spawn tcap termio \
         vt52 window word; do
	ME="$ME $OS/editors/me/$f.c"
done
if CCZ_VAR=800000020800 "$CCZ" -s -i -L $INC \
     -o "$BIN/.me.new" $ME "$CURSES/libterm.a" >>"$LOG" 2>&1; then
	mv -f "$BIN/.me.new" "$BIN/me"
	echo "== me linked: $(wc -c < "$BIN/me") bytes"
else
	echo "== me FAILED"; grep -v 'Strict\|Warning' "$LOG" | tail -15
	rm -f "$BIN/.me.new"; fail=1
fi

# pico: every .c in the directory, with -DCOHERENT selecting the code paths
# pine 3.87 already carries for this system.
if CCZ_VAR=800000020800 "$CCZ" -s -i -L -DCOHERENT -I "$OS/editors/pico" $INC \
     -o "$BIN/.pico.new" "$OS"/editors/pico/*.c "$CURSES/libterm.a" >>"$LOG" 2>&1; then
	mv -f "$BIN/.pico.new" "$BIN/pico"
	echo "== pico linked: $(wc -c < "$BIN/pico") bytes"
else
	echo "== pico FAILED"; grep -v 'Strict\|Warning' "$LOG" | tail -15
	rm -f "$BIN/.pico.new"; fail=1
fi
[ "${fail:-0}" -eq 0 ]
