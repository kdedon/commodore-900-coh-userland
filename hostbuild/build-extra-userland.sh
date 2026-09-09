#!/bin/sh
# build-extra-userland.sh -- port the USEFUL commands that are not otherwise
# built by build-cmd.sh's sweep of cmd/*.c: most compile straight out of cmd/,
# a few (ps, pr, tr) with flags of their own because a same-named file
# already exists, and is built, at the cmd/ path.  Duplicates of already-built
# cmd/ commands are intentionally excluded (the built cmd/ version wins)
# EXCEPT where the higher-ranked source is the one that ships and
# build-userland.sh skips the cmd/ copy -- ps is that case, and the only one.
# All build against the unified 3.2 headers with the games-lib gap-fills,
# like build-cmd.sh.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
BIN="$HERE/build/bin"
# getopt, strtok, strchr, strtoul, memcmp, memcpy, memset and strcasecmp all
# come from libc-z8001.a.  They used to be named here as SOURCES out of
# games/lib/src, which made each an object on the link line and bound it in
# preference to the archive member of the same name -- so this program got a
# 65-line shim getopt where the library holds the de-ANSI'd 4.2 MWC one.
# games/lib/src now holds only err(3) and fgetln(3), which libc has not got.
GLIB=""
INC="-I $OS/include -I $OS/include/sys -I $OS/base/cmd"
LIBMISC="$TCB/libmisc-z8001/libmisc-z8001.a"
LIBTERM="$TCB/curses/libterm.a"
mkdir -p "$BIN"
# Compile logs go under the build tree, not /tmp: a fixed name outside
# the build is not this build's output, two runs share it, and a CI
# runner that cleans /tmp between steps loses the one thing a failure
# leaves behind.  $LOGD names them; the FAIL lines quote the path.
LOGD="$HERE/logs"; mkdir -p "$LOGD"
# libmisc (base/lib/misc) -- cgrep and other misc-lib users need it
[ -f "$LIBMISC" ] || sh "$TC/build-libmisc-z8001.sh" >/dev/null 2>&1
# libterm (base/lib/libterm) -- termcap users (more) need it
[ -f "$LIBTERM" ] || sh "$HERE/build-curses.sh" >/dev/null 2>&1
ok=0; fail=0; fl=""

# build <name> <-I extra...> -- <src...>   (sources after the `--')
build() {
	name="$1"; shift
	incs=""
	while [ "$1" != "--" ]; do incs="$incs $1"; shift; done
	shift
	if CCZ_VAR=800000020800 "$CCZ" -s -i $INC $incs -o "$BIN/.$name.new" "$@" $GLIB >"$LOGD"/ex-$name.log 2>&1; then
		mv -f "$BIN/.$name.new" "$BIN/$name"
		ok=$((ok+1)); echo "  $name: OK ($(wc -c < "$BIN/$name") B)"
	else
		fail=$((fail+1)); fl="$fl $name"; rm -f "$BIN/.$name.new"
		echo "  $name: FAIL -- $(grep -iE 'error|undefined|no match|Internal' "$LOGD"/ex-$name.log | grep -v 'Strict\|Warning' | head -1)"
	fi
}

# single-file utilities
build calendar  -- "$OS/base/cmd/calendar.c"
build man       -- "$OS/base/cmd/man.c"
build strings   -- "$OS/base/cmd/strings.c"
build which     -- "$OS/base/cmd/which.c"
build chmog     -- "$OS/base/cmd/chmog.c"
build chroot    -- "$OS/base/cmd/chroot.c"
build msgs      -- "$OS/base/cmd/msgs.c"
# ps: THE shipped /bin/ps.  Not a duplicate of a built cmd/ command -- cmd/ps.c
# is skipped by build-userland.sh so that this one is the only /bin/ps built.
# A file of that name already exists, and is built, at cmd/ps.c, so this one
# ps reads the kernel's own /proc structures, so it is the one command here
# that compiles against the KERNEL's headers: <sys/proc.h> pulls in
# <sys/timeout.h>, and <sys/machine.h> pulls in <sys/machz8001.h>, neither of
# which exists in this tree or the toolchain.  $KINC is the kernel repository's
# include directory, resolved in toolchain.sh; empty means no kernel resolved,
# and ps refuses by name rather than compiling against something else.
if [ -z "$KINC" ]; then
	fail=$((fail+1)); fl="$fl ps"
	echo "  ps: FAIL -- needs the kernel's headers; sh mk/deps.sh -n kernel"
else
	build ps        -I "$KINC" -I "$KINC/sys" -- "$OS/base/cmd/ps.c"
fi
build qfind     -- "$OS/base/cmd/qfind.c"
# pr and tr: the rank-0 generation.  cmd/pr.c and cmd/tr.c are the relicD
# copies and build-userland.sh skips both, so one pr and one tr are built.  tr
# needs nothing extra -- it drops sys/mdata.h/NBCHAR for <limits.h> CHAR_BIT,
# which the in-tree headers have.  Files of both names already exist, and are
# built, at cmd/pr.c and cmd/tr.c, so these two stay at their own path
# -DCOHERENT selects pr's EMFILE diagnostic, which is the one -m gives when it
# runs out of file descriptors; the other arm reports every failure to open as
# a missing file.
build pr        -DCOHERENT -- "$OS/base/cmd/pr.c"
build tr        -- "$OS/base/cmd/tr.c"
build enable    -- "$OS/base/cmd/enable.c"
build unmkfs    -- "$OS/base/cmd/unmkfs.c"
build badscan   -- "$OS/base/cmd/badscan.c"
# cut: field/column cutter (uses libc strtol/strerror + getopt).  cmd/cut.c
# is the 4.2.12 source, byte for byte, and the quarantined 3.x cutpaste/cut.c
# differed in a single line, _BSD_LINE_MAX, which is also the longest -f
# line the program can handle -- 1024 here against the 3.x file's BUFSIZ (512).
build cut       -- "$OS/base/cmd/cut.c"
# test: THE shipped /bin/test and /bin/[.  base/cmd/test.c is the 4.2.12
# source, a right-to-left recursive-descent parse with no grammar.  It carries
# -nt, -ot, -ef, -e, -x, -b, -c, -p, -g, -u, -K and -L, and it answers an
# unsupported operator with status 2 and the expression on stderr rather than
# with a plain false -- which `if' still reads as false, but a person can see.
build test      -- "$OS/base/cmd/test.c"
# `[' IS test, switching on argv[0] (base/cmd/test.c reads its own name and
# demands a closing `]' under the second one).  Staged as a second copy of the
# same bytes rather than a link: the list format has no link type, and
# ulsrcmap.py matches a byte-for-byte copy to the original by content, so the
# second name needs no source row of its own.
[ -f "$BIN/test" ] && cp "$BIN/test" "$BIN/["
# cgrep: context grep (SysV regexp); pulls the misc lib (alloc/fatal/usage/regexp)
build cgrep -I "$OS/base/lib/misc" -- "$OS/base/cmd/cgrep.c" "$LIBMISC"
# env: env.c + its execvep helper
build env       -- "$OS/base/cmd/env.c" "$OS/base/cmd/execvep.c"
# mail: 3-file client (cmd/mail)
build mail  -I "$OS/base/cmd/mail" -- "$OS"/base/cmd/mail/*.c
# compress: LZW compress/uncompress/zcat.  VIRTUAL keeps the code table in a
# scratch file instead of core and BITS=16 caps it, which is what the 16-bit
# build wants; the three names are the same binary, switching on argv[0].
build compress -DVIRTUAL -DBITS=16 -- "$OS/base/cmd/compress/compress.c" "$OS/base/cmd/compress/is_fs.c"
[ -f "$BIN/compress" ] && { cp "$BIN/compress" "$BIN/uncompress"; cp "$BIN/compress" "$BIN/zcat"; }
# more: the BSD pager, termcap-driven (libterm) with regexp for its / search
build more -DCOHERENT -I "$OS/base/cmd/more" -I "$OS/base/lib/regexp" \
	-- "$OS/base/cmd/more/more.c" "$OS/base/lib/regexp/regexp.c" "$LIBTERM"

echo "== extra userland: $ok linked, $fail failed:$fl" | fold -s -w 100
# A sweep that linked nothing is not a sweep that succeeded: the caller stamps
# the step as done on this status, and dist staging then ships whatever binary
# the previous build left behind.
[ "$fail" -eq 0 ]
