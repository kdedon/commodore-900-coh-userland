#!/bin/sh
# build-screen.sh -- screen(1), GNU screen 3.2 in MWC's own COHERENT port.
#
# Its own script rather than a line in build-netcmds.sh or the cmd/ sweep:
# nine objects, -DCOHERENT, the large model (its text is 117 KB, two hardware
# segments), and it links libterm.a rather than libsocket.a.  The source is
# mwcbbs/386/screen/screen.tar.Z -- screen 3.2 already adapted to COHERENT --
# not autometer-ftp's screen.tgz, which is a VGA screen-saver library of the
# same name.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
CURSES="$TCB/curses"
BIN="$HERE/build/bin"
OBJ="$HERE/build/screenobj"
LOG="$HERE/logs/screen.log"
mkdir -p "$BIN" "$OBJ" "$HERE/logs"; : > "$LOG"

[ -f "$CURSES/libterm.a" ] || sh "$HERE/build-curses.sh" >/dev/null 2>&1
[ -f "$CURSES/libterm.a" ] || { echo "== screen FAILED: no libterm.a"; exit 1; }

SCR="screen ansi help fileio mark window socket putenv getpty"

for f in $SCR; do
	( cd "$OS" && "$CCZ" -c -i -L -DCOHERENT \
		-Iinclude -Iinclude/sys -Inet/screen \
		-o "$OBJ/screen-$f.o" "net/screen/$f.c" ) >>"$LOG" 2>&1 || {
		echo "== screen FAILED ($f.c) -- $(grep -iE 'error|no match|not defined|Internal' "$LOG" | grep -v Warning | tail -1)"
		exit 1; }
done

objs=""
for f in $SCR; do objs="$objs $OBJ/screen-$f.o"; done

# A short output path on purpose: ld-z8001 answers "Ld: cannot create" for a long
# -o argument (a 96-character path failed where a short one linked), so the name
# it is handed here stays inside whatever fixed buffer cmd/ld has.
if "$CCZ" -i -L -s -o "$BIN/.screen.new" $objs "$CURSES/libterm.a" >>"$LOG" 2>&1; then
	mv -f "$BIN/.screen.new" "$BIN/screen"
	echo "== screen: OK ($(wc -c < "$BIN/screen") B)"
else
	# `end_' appears in ld's undefined list whenever anything else is
	# undefined, even though ld defines it itself -- ignore it and read the
	# other names.
	echo "== screen FAILED (link) -- $(grep -iE 'undefined|error|no match' "$LOG" | grep -v 'end_' | tail -1)"
	rm -f "$BIN/.screen.new"
	exit 1
fi
