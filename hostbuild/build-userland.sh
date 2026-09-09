#!/bin/sh
# build-userland.sh -- sweep the single-file 3.2 userland commands through the
# native toolchain: ccz (cc0/cc1/cc2 + as + ld against crt0.o/libc-z8001.a)
# -> stripped C900 Coherent l.out binaries in build/bin.  Commands whose
# source spans a directory (awk, bc, as, ...) are a follow-on.
#
# Usage: build-userland.sh [name.c ...]   (default: all of cmd/*.c)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
BIN="$HERE/build/bin"
LOG="$HERE/build/userland.log"
mkdir -p "$BIN"
: > "$LOG"

if [ $# -gt 0 ]; then set -- "$@"; else set -- "$OS"/base/cmd/*.c; fi

ok=0; failed=0; faillist=""
for f in "$@"; do
	case "$f" in /*) ;; *) f="$OS/base/cmd/$f";; esac
	b="$(basename "$f" .c)"
	# clear links libterm and top links libcurses, neither of which this
	# sweep does: build-curses-games.sh is the one place that knows where
	# those archives are, so it builds both.  Left in, they fail here and
	# show as failures of a sweep they are not part of.
	case "$b" in clear|top) continue;; esac
	# Built by build-extra-userland.sh with their own flags, so the sweep does
	# not build them twice and does not fail on flags it does not pass:
	# cgrep needs libmisc (regcomp_/regexec_) and env needs execvep.c beside
	# it -- as single files here, both link short and are reported as broken
	# commands when the fault is this sweep's, not theirs.
	case "$b" in ps|pr|tr|cgrep|env) continue;; esac
	# Not commands at all: execvep.c is env's helper and lock.c is init's
	# (the tty-lock code; its main() is #ifdef UUCP); neither has a main().
	case "$b" in execvep|lock) continue;; esac
	# The COHERENT 3.2 headers come from the TOOLCHAIN (ccz puts its own
	# src/include and src/include/sys last on the path), which is the same set
	# libc-z8001.a is built against.  $OS/include is kept on the list although
	# the directory is gone: this tree owns no header any more -- every one it
	# held was a stale duplicate that SHADOWED the toolchain's or the kernel's
	# -- and a missing -I directory costs nothing, while removing the flag from
	# thirty harnesses buys nothing.
	# ccz appends the toolchain's include directories to every compile, so
	# the C library's headers need no naming here; $OS/include was the
	# userland's shadow copy of them and is gone.
	IORD="-I $OS/base/cmd"
	# Per-command extra link inputs the single .c misses:
	#   factor -- libm (double sqrt + %.0f).
	#   init   -- the tty-lock code, lock.c beside it.  Nothing else:
	#             libc-z8001.a exports both strrchr_ (strrchr.o) and setpgrp_
	#             (setpgrp.o, generated from syscalls.tab), so the two source
	#             files this used to name were a second copy of what init
	#             already links.  Neither could be compiled here anyway --
	#             libc/string/strrchr.c says NULL while including only
	#             <string.h>, which does not define it, and libc/sys/setpgrp.s
	#             has never existed (the only setpgrp source in this tree is
	#             libc/sys/i386/setpgrp.c, for a machine this is not).
	#   date   -- carries MSDOS/COHERENT arms; COHERENT selects the wtmp
	#             clock-change record, /etc/boottime and the "no permission"
	#             diagnostic.  Without it none of that is compiled in.
	#   sort   -- COHERENT selects the signal cleanup that removes the
	#             temporary files on an interrupt, and the mktemp'd names
	#             that let two sorts run at once; the other arm has neither.
	#   tail   -- COHERENT selects the character-device test that decides
	#             whether the input can be seeked at all, and the sleep in
	#             the -f loop, which without it spins on the CPU.
	#   units  -- COHERENT selects <path.h>'s `/'-separated DEFLIBPATH, which
	#             is where it looks for the units and binunits tables.
	XLIB=""
	case "$b" in
	date|sort|tail|units)	IORD="$IORD -D COHERENT=1";;
	factor)	XLIB="$TCB/libm-z8001/libm-z8001.a";;
	init)	XLIB="$OS/base/cmd/lock.c";;
	# The file-system check tools: libfs is the block/inode access layer and
	# the inode->pathname engine all three share, and <fs.h> is its public
	# header, so the library's directory is both the include path and the
	# extra source.  <check.h>, the exit-status bits icheck and dcheck
	# return, is beside them in base/cmd and is already on $IORD.
	dcheck|icheck|ncheck)
		IORD="$IORD -I $OS/base/lib/libfs"
		XLIB="$(ls "$OS"/base/lib/libfs/*.c | tr '\n' ' ')";;
	# The six commands that reach the KERNEL's machine layer.  <sys/machine.h>
	# is the toolchain's, and under Z8001 -- which cc0-z8001 predefines -- it
	# includes <sys/machz8001.h>, which exists only in the kernel repository;
	# load, uload and mount likewise want <con.h> and <mount.h> from there,
	# and fdformat wants <sys/fdioctl.h>, the floppy ioctl interface, which
	# the driver's repository owns and the toolchain has not got.
	# $KINC is that repository's include directory, resolved in toolchain.sh.
	load|uload|mount|sa|time|fdformat)
		if [ -z "$KINC" ]; then
			echo "$b: needs the kernel's headers; sh mk/deps.sh -n kernel" >>"$LOG"
			failed=$((failed+1)); faillist="$faillist $b"
			continue
		fi
		IORD="$IORD -I $KINC -I $KINC/sys";;
	esac
	if "$CCZ" -s -i $IORD \
	          -o "$BIN/.$b.new" "$f" $XLIB >>"$LOG" 2>&1; then
		mv -f "$BIN/.$b.new" "$BIN/$b"
		ok=$((ok+1))
	else
		failed=$((failed+1)); faillist="$faillist $b"
		rm -f "$BIN/.$b.new"
	fi
done
echo "== userland sweep: $ok linked, $failed failed"
[ -n "$faillist" ] && echo "== failed:$faillist" | fold -s -w 100
# A sweep that linked nothing is not a sweep that succeeded: the caller stamps
# the step as done on this status, and dist staging then ships whatever binary
# the previous build left behind.
[ "$failed" -eq 0 ]
