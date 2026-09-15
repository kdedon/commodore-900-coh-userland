#!/bin/sh
# build-hunt.sh -- cross-build hunt(6) and huntd for the C900.
#
# The recipe body for the `hunt' target in hostbuild/Makefile; the port's own
# Makefile (games/net/hunt/Makefile) holds the defines and the object lists, and
# this only supplies the cross-compiler and the libraries.
#
# huntd links against libsocket alone.  hunt also needs libcurses + libterm,
# and compiles against the toolchain's src/include/curses.h, found on ccz's
# default include path: the 4.3BSD/sgtty header that carries _tty_ch and the
# terminal capability strings the game drives directly.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
BE="$TC"
CCZ="$BE/ccz"
CURSES="$TCB/curses"
D="$OS/games/net/hunt"
LOG="$HERE/build/hunt.log"
mkdir -p "$HERE/build"; : > "$LOG"

[ -f "$OS/net/libsocket.a" ] || { echo "hunt: libsocket.a not built (make net)"; exit 1; }
[ -f "$CURSES/libcurses.a" ] || { echo "hunt: libcurses.a not built (make curses)"; exit 1; }

# getopt(3) is not in libc; it lives in the games link-line shim, as it does
# for every other game here.
# libc-z8001.a supplies string routines and getopt.
# games/lib/src supplies err(3) and fgetln(3).
GLIB=""
CF="-I$OS/include -I$OS/net/include"

# ccz supplies crt0.o and libc-z8001.a to the link itself, so the port's
# Makefile cannot name them as prerequisites and a rebuilt libc relinks nothing:
# huntd stayed two days old across a libc fix while this script printed its size
# and looked built.  Drop a binary older than the archive so the link runs.
LIBCA="$TCB/libc-z8001/libc-z8001.a"
for f in huntd/huntd hunt/hunt; do
	if [ -f "$D/$f" ] && [ "$LIBCA" -nt "$D/$f" ]; then rm -f "$D/$f"; fi
done

if make -C "$D" CC="$CCZ" CFLAGS="$CF" \
	LIBS="$OS/net/libsocket.a $GLIB" huntd/huntd >>"$LOG" 2>&1
then
	echo "  huntd: $(wc -c < "$D/huntd/huntd") B"
else
	echo "== huntd: BUILD FAILED"; tail -12 "$LOG"; exit 1
fi

if make -C "$D" CC="$CCZ" CFLAGS="$CF" \
	LIBS="$OS/net/libsocket.a $GLIB $CURSES/libcurses.a $CURSES/libterm.a" \
	hunt/hunt >>"$LOG" 2>&1
then
	chmod +x "$D/hunt/hunt"
	echo "  hunt:  $(wc -c < "$D/hunt/hunt") B"
else
	echo "== hunt: BUILD FAILED"; tail -12 "$LOG"; exit 1
fi
echo "hunt: OK"
