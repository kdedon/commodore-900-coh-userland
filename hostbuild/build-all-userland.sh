#!/bin/sh
# build-all-userland.sh -- the full userland build: the base component's
# single-file cmd/*.c, every multi-directory command in it that links, the
# editors and tools components, the games, and the curses libs + curses games.
# Prints a grand total.  Prereqs: build-libc-z8001.sh, build-curses.sh.
#
# The net and mail components are NOT here: their programs link libsocket.a
# and are built by `make -C hostbuild netcmds screen'.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
rc=0; fl=""
# Every build step below runs through step().  A pipeline takes the exit status
# of its LAST element, so `build-x.sh | tail -1' reports whether tail ran, and
# `| grep -c linked' reports whether grep matched -- neither says whether the
# build linked.  step() buffers the output, takes the status from the script,
# and shows more of the log when that status is non-zero.
step() {	# label  lines-to-show-when-ok  command ...
	lbl="$1"; n="$2"; shift 2
	if out=$("$@" 2>&1); then
		echo "$out" | tail -"$n"
	else
		st=$?
		rc=1; fl="$fl $lbl"
		echo "$out" | tail -20
		echo "== $lbl FAILED (exit status $st)"
	fi
}
# libm before the sweeps, not beside awk: factor(1) in the single-file sweep
# links it too, so built later it is missing on any build whose toolchain
# build directory starts empty -- and the sweep reports that as a compile
# failure of factor.
# An unpacked release carries libm-z8001.a and carries no harness to rebuild it
# with; running the missing script would report "no such file" as a build
# failure of a library that is present and correct.
if [ -f "$TC/build-libm-z8001.sh" ]; then
	step libm 0 sh "$TC/build-libm-z8001.sh"
elif [ ! -f "$TCB/libm-z8001/libm-z8001.a" ]; then
	echo "== libm: no $TC/build-libm-z8001.sh and no libm-z8001.a" >&2
	rc=1; fl="$fl libm"
fi
echo "=== single-file commands ==="
step userland 2 sh "$HERE/build-userland.sh"
echo "=== multi-directory commands ==="
ok=0; fail=0
for d in "$OS"/base/cmd/*/; do
	n=$(basename "$d")
	case "$n" in snake) continue;; esac		# snake parked
	case "$n" in elvis) continue;; esac		# elvis via its own KTTY-style script
	# more links libterm and compiles its own regexp engine (lib/regexp),
	# neither of which this loop supplies; build-extra-userland.sh owns it.
	case "$n" in more) continue;; esac
	case "$n" in worm) continue;; esac	# curses game -- built by build-curses-games.sh
	case "$n" in rogue) continue;; esac	# bundled curses -- build-rogue.sh, below
	# top links libterm and needs a GENERATED sigdesc.h, neither of which this
	# loop can supply; build-curses-games.sh owns it (see build_top there).
	case "$n" in top) continue;; esac
	case "$n" in awk|expr|find|test|sh|rsh) continue;; esac	# yacc-grammar commands -- built below
	# bc's parser is generated from gram.y, and dc links six of bc's
	# objects, so both come out of build-bc.sh below.
	case "$n" in bc|dc) continue;; esac
	# nawk needs libm (fmod, for `%') and carries its own size gate -- own script.
	case "$n" in nawk) continue;; esac
	# compress selects its memory model from -DVIRTUAL -DBITS=16, which only
	# build-extra-userland.sh passes.  Built flagless by this loop it takes the
	# in-core arm, which declares neither ramsw nor tmpf, while closeRam() still
	# references both -- so the same directory linked there and failed here, and
	# the failure was the second build, not the program.
	case "$n" in compress) continue;; esac
	if r=$(sh "$HERE/build-cmd.sh" "$OS/base/cmd/$n" 2>&1); then
		ok=$((ok+1))
	else
		fail=$((fail+1)); fl="$fl $n"; echo "$r" | tail -3
	fi
done
echo "multi-dir: $ok linked, $fail failed:$fl"
# Carried to the exit status: `make userland' must not stamp the sweep as done
# when a command in it did not link, because dist staging then ships whatever
# binary the previous build left in build/bin.
[ "$fail" = 0 ] || rc=1
echo "=== yacc-grammar commands (host Coherent yacc parser) ==="
step hyacc 1 sh "$HERE/build-hyacc.sh"
step awk 1 sh "$HERE/build-awk.sh"	# awk also needs libm
# nawk is not a yacc-grammar command -- its parser is hand-written -- but it is
# built here because it needs libm for the same reason awk does.
step nawk 1 sh "$HERE/build-nawk.sh"
step find 1 sh "$HERE/build-yacc-cmd.sh" find
# expr is a grammar and nothing else: cmd/expr holds expr.y and no .c at all,
# so the multi-directory loop above has nothing to compile.
step expr 1 sh "$HERE/build-yacc-cmd.sh" expr
# bc and dc: one script, because dc links six of bc's objects.  Not
# build-yacc-cmd.sh -- bc's yacc invocation carries hand-set table sizes and its
# generated parser goes through cmd/bc/gram.fix before it is compiled.
step bc 2 sh "$HERE/build-bc.sh"
# test is not a yacc-grammar command: /bin/test and /bin/[ come from
# base/cmd/test.c, and build-extra-userland.sh builds them.
# No -DVERSION: $VERSION is the release the RUNNING kernel reports, asked at
# shell start-up (cmd/sh/var.c), because a version compiled into a program is
# the version of that program's build and not of the system it ends up on.
SHVER=""
step sh 1 sh "$HERE/build-yacc-cmd.sh" sh "$SHVER"
# rsh IS sh: the restricted shell is the same program, restricting itself
# because of the name it was invoked under (cmd/sh/main.c).  Built from cmd/sh.
step rsh 1 sh "$HERE/build-yacc-cmd.sh" -s sh rsh "$SHVER"
echo "=== elvis (own script: its own tinytcap) ==="
step elvis 1 sh "$HERE/build-elvis.sh"
step editors 2 sh "$HERE/build-editors.sh"
echo "=== third-party tools (less, gzip, patch, rcs, rogue) ==="
# rogue carries its own bundled curses, so the multi-dir loop skips it and
# build-curses-games.sh (which reads games/bsd/CURSES.list) never names it.
# With no caller it was outside the build entirely: its binary is staged by
# `t /usr/games' whatever its age, so it shipped a libc that was three days old.
for t in less gzip patch rcs rogue; do
	[ -f "$HERE/build-$t.sh" ] || { rc=1; fl="$fl $t"; echo "== $t FAILED: no build-$t.sh"; continue; }
	step "$t" 1 sh "$HERE/build-$t.sh"
done
echo "=== extra userland (cmd / etc) ==="
step extra 1 sh "$HERE/build-extra-userland.sh"
echo "=== games + curses games ==="
step games 3 sh "$HERE/build-games.sh"
step curses-games 1 sh "$HERE/build-curses-games.sh"
[ "$rc" = 0 ] || echo "=== BUILD FAILED:$fl"
exit "$rc"
