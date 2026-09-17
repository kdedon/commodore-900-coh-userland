#!/bin/sh
# build-netcmds.sh -- cross-build the net clients whose source is a DIRECTORY
# (net/<name>/, and the mail component's SMTP pair) rather than one flat file.
#
# Their own script rather than lines in build-userland.sh for the same reason awk
# has one: that sweep is one .c file per command.
#
# All of them come from Minix 2.0.4, de-ANSI'd (hostbuild/deansi.pl), and reach
# the stack through libsocket's /dev/tcp shim -- open("/dev/tcp") plus NWIO*
# ioctls -- so none of them knows the transport is a daemon over FIFOs.
#
# What is not mechanical, and is the same in each: COHERENT is termio, not POSIX
# termios, so tcgetattr/tcsetattr become ioctl(TCGETA/TCSETA) -- which is why
# these want a kernel built KTTY=termio; <stdarg.h> does not exist here, so a
# varargs diagnostic uses this library's %r printf; and <sys/ioctl.h> becomes
# <net/ioctl.h>, where the NWIO* codes live.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/.." && pwd)
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
OUT="$HERE/build/bin"
LOG="$HERE/logs/netcmds.log"
LIBSOCKET="$OS/net/libsocket.a"
CURSES="$TCB/curses"
OBJ="$HERE/build/netcmdobj"
mkdir -p "$OUT" "$HERE/logs" "$OBJ"
: > "$LOG"

[ -f "$LIBSOCKET" ] || { echo "== netcmds FAILED: no $LIBSOCKET (run build-net.sh)"; exit 1; }

# THE TARGET'S OWN HEADERS FIRST.  net/include holds the stack's <net/gen/*> and
# <net/ioctl.h> -- which is why it is on the path at all, and the ioctl codes
# must be the ones the daemon answers -- but it ALSO has a top-level errno.h,
# the Minix one, whose network errnos differ from ours: EURG is 62 there and 42
# here, ECONNREFUSED 59 there and 45 here, and EADDRINUSE and ENOURG likewise.
# A client compiled against those tests for a value the runtime never sets --
# inet_chan.c's ichan_fail translates the stack's status to the COHERENT number
# -- so `if (errno == EURG)' is dead code in both arms.  errno.h is the only
# name the two sets share, and nothing here needs a Minix-only errno: everything
# else the clients want from net/include is still found below it.
#
# The COHERENT set is $TCSYSINC, resolved by toolchain.sh.  ccz appends it
# too, but last, after net/include.
[ -f "${TCSYSINC:-}/errno.h" ] || {
	echo "== netcmds FAILED: no errno.h at $TCSYSINC -- the toolchain did not"
	echo "  resolve, or its layout changed.  Refusing rather than compiling the"
	echo "  clients against the stack's error numbers."
	exit 1
}
INC="-I$TCSYSINC -Inet/include"

ok=0; bad=0
for name in telnet telnetd ftp host rlogin finger talk ping \
	    pr_routes add_route netstat fingerd remsh remshd smtpd smtpsend; do
	# smtpd and smtpsend are the mail component's; the rest are net's.
	case "$name" in
	smtpd|smtpsend)	comp=mail;;
	*)		comp=net;;
	esac
	src="$OS/$comp/$name"
	[ -d "$src" ] || { echo "  $name: SKIP (no $src)"; continue; }
	# talk is the one that also draws: it needs the in-tree libcurses header
	# AHEAD of $INC, because the toolchain's <curses.h> is the one ccz reaches
	# by default, and libcurses.a + libterm.a on the link.
	xinc=""; xlib=""
	case "$name" in
	talk)	xinc="-DCOHERENT -I$OS/base/lib/libcurses"
		xlib="$CURSES/libcurses.a $CURSES/libterm.a";;
	esac
	objs=""; fail=0
	for c in "$src"/*.c; do
		stem=$(basename "$c" .c)
		( cd "$OS" && "$CCZ" -c -i $xinc $INC -I"$src" -o "$OBJ/$name-$stem.o" \
			"$comp/$name/$stem.c" ) >>"$LOG" 2>&1 &&
			objs="$objs $OBJ/$name-$stem.o" || {
			echo "  $name: FAIL ($stem.c) -- $(grep -iE 'error|no match|not defined|Internal' "$LOG" | grep -v Warning | tail -1)"
			fail=1; break; }
	done
	[ "$fail" = 0 ] || { bad=$((bad+1)); continue; }
	# Link to a side name and rename into place.  A FAILING link must leave the
	# previous build's binary alone: build/bin is what dist staging reads, and
	# deleting the product on failure meant a pre-existing failure destroyed the
	# last binary that worked -- this is where telnet had to be recovered out of
	# an old disk image.  What stops a stale binary shipping is the non-zero exit
	# status this script ends on.
	if "$CCZ" -s -i -o "$OUT/.$name.new" $objs "$LIBSOCKET" $xlib >>"$LOG" 2>&1; then
		mv -f "$OUT/.$name.new" "$OUT/$name"
		ok=$((ok+1)); echo "  $name: OK ($(wc -c < "$OUT/$name") B)"
	else
		rm -f "$OUT/.$name.new"
		bad=$((bad+1))
		echo "  $name: FAIL (link) -- $(grep -iE 'error|undefined|redefined|no match|^Ld:' "$LOG" | tail -1)"
	fi
done
echo "== netcmds: $ok ok, $bad failed"
[ "$bad" = 0 ]
