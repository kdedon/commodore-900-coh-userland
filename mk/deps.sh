#!/bin/sh
# deps.sh -- resolve the things this build consumes from outside this
# repository, and refuse by name when one is missing.
#
#   sh mk/deps.sh <dep>              print the resolved path, or nothing
#   sh mk/deps.sh -k <dep>           print the SHAPE the path resolved to, or
#                                    nothing: `checkout', or `release X.Y.Z'
#   sh mk/deps.sh -n <dep> [value]   print nothing; refuse and exit 2 if
#                                    <value> (or, empty, the search) does not
#                                    resolve
#
# The two modes exist because the edges are wanted at different times.
# The search runs when a makefile is read, so a variable can be assigned from
# it; the REFUSAL belongs in the recipe that wanted the thing, because packing
# a stock image needs none of them and must not be blocked by a missing
# emulator.  -n takes the make variable's current value so that a
# `make C900_EMU=/wrong' is refused as what the user asked for rather than
# silently re-searched.
#
#   dep         variable          what it names
#   emu         C900_EMU          the emulator CHECKOUT (bin/c900 inside it)
#   toolchain   C900_TOOLCHAIN    the toolchain checkout
#   kernel      C900_KERNEL       the kernel checkout, or its headers release
#
# This repository does not link a kernel or stage a loader into an image --
# that moved to separate repositories -- so kboot is not listed here, unlike
# the kernel and dist repositories which name it.  Trimmed to the edges DEPS
# actually names.
#
# This file is what DEPS and `make deps' talk to: the name column there is a
# name here, which is what lets the fetcher ask "is this edge already
# satisfied?" before placing anything, and what keeps `make deps' honest --
# it may only put things where these lists already look.
#
# The per-consumer resolvers are unchanged and remain the ones the build uses:
# mk/emulator.sh and mk/emulator.mk for the emulator, hostbuild/toolchain.sh
# (and its .mk twin) for the toolchain.  This file searches the same paths in
# the same order and exists so that ONE query answers for both of them.
#
# Search order for each: the variable wins; then (emulator only) the pinned
# release in deps/ and a c900 on $PATH; then a sibling checkout, walking
# outward AT MOST THREE PARENTS, then one inside a `repos/' directory beside
# this repository.  Three parents is what reaches the enclosing workspace from
# a repository staged at <workspace>/repos/<repo>; further out is not a
# sibling, it is a coincidence -- an unbounded walk finds another job's
# checkout on a CI runner and reports a false success.

root=$(cd "$(dirname "$0")/.." && pwd)

# The sibling search list for a repository name: three parents, then repos/.
siblings() {
	_d=$root
	_n=0
	while [ $_n -lt 3 ] && [ "$_d" != / ]; do
		_d=$(cd "$_d/.." && pwd)
		echo "$_d/$1"
		_n=$((_n + 1))
	done
	echo "$root/repos/$1"
}

# Per dep: VAR names the variable, WANT what is being looked for, LIST the
# candidate paths, ok() the test that a candidate is the real thing, fixup()
# the value a caller's own spelling maps to, and shape() what KIND of thing the
# resolved path turned out to be.  Every edge but the toolchain has one shape and
# say so; the toolchain has two, and a build that did not report which it used
# would leave "what compiled this" to be worked out from the filesystem.
shape() { echo checkout; }
case "$1" in
-n) mode=need; dep=$2; given=$3 ;;
-k) mode=kind; dep=$2; given= ;;
*)  mode=find; dep=$1; given= ;;
esac

case "$dep" in
emu)
	VAR="C900_EMU"
	WANT="the Commodore 900 emulator"
	LIST="$root/deps/commodore-900-emulator"
	p=$(command -v c900 2>/dev/null) &&
		LIST="$LIST $(dirname "$(dirname "$p")")"
	LIST="$LIST $(siblings commodore-900-emulator)"
	[ -n "$given" ] || given=${C900_EMU:-${EMU:-${EMUBIN:-}}}
	# A file names bin/c900; a directory names the checkout.  Both are
	# accepted, and the checkout is what is printed: rom/ sits beside bin/
	# in it, which is how $C900_EMU_ROM falls out with no second variable.
	fixup() {
		case "$1" in
		*/bin/c900) dirname "$(dirname "$1")" ;;
		*) echo "$1" ;;
		esac
	}
	ok() { [ -x "$1/bin/c900" ]; }
	HOW="  Clone https://github.com/MichalPleban/commodore-900-emulator
  and \`make' it, to one of the paths above -- or put its c900 on \$PATH,
  or set C900_EMU to the checkout or to its bin/c900.
  Only targets that RUN something need it; packing a stock image does not.
  \`make deps DEP=emu' unpacks the release DEPS pins."
	;;
toolchain)
	VAR="C900_TOOLCHAIN"
	WANT="the Z8001 cross toolchain"
	LIST="$root/deps/commodore-900-toolchain $(siblings commodore-900-toolchain)"
	[ -n "$given" ] || given=${C900_TOOLCHAIN:-}
	fixup() { echo "$1"; }
	# TWO shapes resolve here: a source CHECKOUT, and an unpacked RELEASE
	# archive placed in deps/ by `make deps' from a `toolchain release' line.
	# host/ccz is the marker because it is the one path both shapes have --
	# a checkout tracks it, and the archive carries a host/ view of its own
	# bin/ precisely so that the parts keep ONE spelling ($TC/ccz,
	# $TC/build/z8001/cc0-z8001) in every consumer here.  A release cannot
	# carry the build-*.sh harnesses or what they build from an OS tree
	# (curses, libm, libmisc), so it serves a kernel and plain userland and
	# not those; which shape was used is reported, per shape() below.
	#
	# The toolchain builds its Z8001 libc from this tree's sources, so the
	# edge is reciprocal and neither side may contain the other.
	ok() { [ -f "$1/host/ccz" ]; }
	shape() {
		if [ -f "$1/host/build-cc.sh" ]; then
			echo checkout
		elif [ -f "$1/bin/ccz" ] && [ -f "$1/VERSION" ]; then
			echo "release $(sed -n 1p "$1/VERSION")"
		else
			echo unknown
		fi
	}
	HOW="  The compiler, assembler and linker are a repository of their own,
  because COHERENT, CP/M and kboot all consume them:
      git clone <...>/commodore-900-toolchain
  or point C900_TOOLCHAIN= at a checkout, or at an unpacked release archive
  (one with host/ccz in it).  \`make deps DEP=toolchain' places whichever
  kind DEPS names -- a \`git' line clones, a \`release' line unpacks the
  pinned archive into deps/."
	;;
dist)
	VAR="C900_DIST"
	WANT="the distribution repository (image format tooling)"
	LIST="$root/deps/commodore-900-dist $(siblings commodore-900-dist)"
	[ -n "$given" ] || given=${C900_DIST:-}
	fixup() { echo "$1"; }
	# workimg.sh is the marker because it is what the test harnesses here
	# reach for: every gate that boots an image runs it against a scratch
	# copy so build/<dist>.bin stays pristine.  It reads and writes the
	# COHERENT filesystem layout, which is that repository's subject, so the
	# knowledge lives in one place rather than being copied into each
	# consumer -- a duplicated format parser drifts, and a drifted one
	# returns a plausible wrong answer instead of an error.
	ok() { [ -f "$1/os/hostbuild/workimg.sh" ]; }
	shape() { echo checkout; }
	HOW="  The media descriptors, the image packer and the tools that read a
  packed filesystem are a repository of their own:
      git clone <...>/commodore-900-dist
  or point C900_DIST= at a checkout.  Only targets that BOOT an image need
  it -- compiling and linking the userland does not."
	;;
kernel)
	VAR="C900_KERNEL"
	WANT="the COHERENT kernel (its header set)"
	LIST="$root/deps/commodore-900-coh-kernel3 $(siblings commodore-900-coh-kernel3)"
	[ -n "$given" ] || given=${C900_KERNEL:-}
	fixup() { echo "$1"; }
	# TWO shapes, as for the toolchain: a source CHECKOUT, and an unpacked
	# kernel-HEADERS release placed in deps/.  os/hostbuild/pack-headers.sh
	# cuts that archive, and what it contains is COMPUTED (kheaders.py walks
	# the kernel's own include closure), so a header added to a kernel source
	# joins the package rather than being remembered by hand.
	#
	# Nothing here LINKS a kernel or boots one, but the image this tree
	# stages carries kernel-side files, so the edge answers for two things:
	# the header set the userland compiles against -- the machine layer it
	# cannot own, <sys/machz8001.h> above all, which the toolchain's own
	# <sys/machine.h> includes under Z8001 and which exists in no other
	# tree -- and the built artifacts staged into build/root (the console
	# drivers on /drv, and the symboled kernel as /coherent).  A copy of
	# those headers kept here instead would be a second opinion about
	# struct layouts and device numbers the kernel alone decides, and a
	# wrong one compiles quietly.
	#
	# The marker below is a HEADER, so a headers-only release resolves this
	# edge.  That is deliberate: the compiles are what most of this tree
	# needs, and the staging rules test for the artifacts they want and
	# refuse by name when they are not there, rather than making every
	# target here wait on a kernel image.
	#
	# There is no cycle: the kernel names the toolchain and kboot, and does
	# not name this repository.
	ok() {
		[ -f "$1/os/include/sys/machz8001.h" ] ||
		[ -f "$1/include/sys/machz8001.h" ]
	}
	shape() {
		if [ -f "$1/os/hostbuild/pack-headers.sh" ]; then
			echo checkout
		elif [ -f "$1/VERSION" ]; then
			echo "release $(sed -n 1p "$1/VERSION")"
		else
			echo unknown
		fi
	}
	HOW="  The kernel is a repository of its own, and the userland compiles
  against its headers:
      git clone <...>/commodore-900-coh-kernel3
  or point C900_KERNEL= at a checkout, or at an unpacked kernel-headers
  release (one with include/sys/machz8001.h in it).  \`make deps DEP=kernel'
  places whichever kind DEPS names."
	;;
*)
	echo "deps.sh: unknown dependency \`$dep' (emu, toolchain, dist, kernel)" >&2
	exit 2
	;;
esac

found=
if [ -n "$given" ]; then
	given=$(fixup "$given")
	ok "$given" && found=$given
else
	for c in $LIST; do
		c=$(fixup "$c")
		ok "$c" && { found=$c; break; }
	done
fi

if [ -n "$found" ]; then
	case "$mode" in
	find) echo "$found" ;;
	kind) shape "$found" ;;
	esac
	exit 0
fi

case "$mode" in find|kind) exit 0 ;; esac

{
	if [ -n "$given" ]; then
		echo "*** $WANT: nothing usable at $VAR=$given."
		echo "*** That is $VAR's own value, so nothing else was tried."
		echo "*** Unset it to search these instead:"
	else
		echo "*** $WANT: none found, and this target needs one."
		echo "*** $VAR is unset; the paths tried were:"
	fi
	for c in $LIST; do echo "***     $c"; done
	echo "$HOW" | sed 's/^/*** /'
} >&2
exit 2
