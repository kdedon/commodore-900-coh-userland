#!/bin/sh
# build-net.sh -- cross-build net (the TCP/IP stack and its clients) for the
# Z8001 guest.
#
# net and net/inet carry their own traditional Makefiles, which build
# natively on the C900 with cc/as/ar.  This is the cross-build recipe body: it
# points those same Makefiles at the host toolchain rather than duplicating their
# rules, so the native and cross builds cannot drift apart.
#
# Three substitutions are what the cross-build needs:
#
#   CC=ccz      the host-run cc0/cc1/cc2 + as + ld pipeline
#   AR=arz      Coherent l.out archiver (mkarz); host ar writes the wrong format
#   CFLAGS      the Makefiles' own -O is not a ccz option, and -I. means the
#               wrong directory once make has descended into inet/ -- so the
#               include path is given absolutely and covers both levels
#   UCFLAGS     the same path with COHERENT's own headers AHEAD of net/include
#
# TWO include orders, and which file gets which is net/Makefile's judgement --
# this only has to spell both paths absolutely.  The difference is <errno.h>:
# net/include holds Minix's, whose network numbers are the stack's (ECONNREFUSED
# 59), while COHERENT's libc sets and prints its own (45).  CFLAGS keeps
# net/include first, for the stack and for the two files that pass stack
# statuses through ichan_fail(); UCFLAGS puts the toolchain's headers first, for
# everything whose errno a caller reads.  ccz appends the toolchain's include
# directories to every compile, but it appends them LAST, which is where
# net/include won for both halves.
#
# The overrides go through the ENVIRONMENT with make -e, not on the command line:
# net's inet-daemon rule descends with `cd inet; make CC=...', which forwards CC
# alone.  Environment plus -e reaches the sub-make too.
#
# LFLAGS is left to the Makefiles: they already ask for -i (separated I/D) and,
# for the stack, -L (large model -- it is bigger than one 64K segment), and
# ccz passes both straight to ld.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
NET="$OS/net"

CCZ="$TC/ccz"
ARZ="$TC/arz"
[ -x "$CCZ" ] || { echo "build-net.sh: no $CCZ" >&2; exit 1; }

# -I. in the Makefiles resolves to whichever directory make is in; spell every
# search path out so a sub-make finds the same headers.
NETINC="-I$NET -I$NET/include -I$NET/inet -I$NET/inet/generic"

# COHERENT's headers are $TCSYSINC, resolved once by toolchain.sh.
[ -f "${TCSYSINC:-}/errno.h" ] || {
	echo "build-net.sh: no errno.h at $TCSYSINC -- the toolchain did not" >&2
	echo "  resolve, or its layout changed.  Refusing rather than compiling" >&2
	echo "  the resolver against the stack's error numbers." >&2
	exit 1
}

# EXTRA_NETDEFS: bring-up switches for one build, e.g. -DNWTRACE (a letter per
# stack event on the console).  Emulator runs only -- on the simulator the
# console shares a chip with the line under test.
export CC="$CCZ" AS="$TCB/as-z8001" AR="$ARZ" \
	CFLAGS="$NETINC ${EXTRA_NETDEFS:-}" \
	UCFLAGS="-I$TCSYSINC $NETINC ${EXTRA_NETDEFS:-}"

# ccz supplies crt0.o and libc-z8001.a to the link itself, so a Makefile here
# cannot name them as prerequisites: after libc changed, every one of these
# programs was already up to date against its objects and kept the previous
# libc.  Dropping a product older than the archive makes the link happen; the
# objects are untouched, so this costs a relink and not a rebuild.
LIBCA="$TCB/libc-z8001/libc-z8001.a"
for f in inet/inet inetd slip ppp ifconfig sntp echoclient echoserver udpecho udpserver \
	 udppoll netdbtest acceptmany chanmax sockcycle ephport discotime devtcp dropclient repeatclient fdhog portholder psipping ichanprobe rlecho; do
	if [ -f "$NET/$f" ] && [ "$LIBCA" -nt "$NET/$f" ]; then rm -f "$NET/$f"; fi
done

# The daemon, the two link daemons, libsocket + its clients ...
( cd "$NET" && make -e all )
# ... and the test programs, which the Makefile deliberately keeps out of `all'
# (they are diagnostics, not shipped tools -- but net.list stages them, so a
# dist build has to produce them).
( cd "$NET" && make -e psipping ichanprobe )

echo "net: OK"
for f in inet/inet inetd slip ppp ifconfig sntp echoclient psipping ichanprobe; do
	[ -f "$NET/$f" ] && echo "  $f: $(wc -c < "$NET/$f") B"
done
