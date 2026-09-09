# mk/dist.sh -- resolve the distribution repository.  SOURCE this file
# (`. "$C900_ROOT/mk/dist.sh"'); do not exec it.
#
# The image format is not this repository's subject.  dist, the packer and
# the tools that read a packed COHERENT filesystem live in
#
#	commodore-900-dist
#
# and the harnesses here consume two of them:
#
#   hostbuild/workimg.sh	a scratch COPY of an image to boot, so a run
#				cannot depend on its predecessor's writes.
#   hostbuild/fsread.py	read a file back out of an image after a run.
#
# THE IMAGES THEMSELVES ARE THERE TOO, and that is the other half of the same
# move: `make dist DIST=<name>' runs in the dist repository and writes an image
# into a directory DERIVED FROM THE PARTS IT RESOLVED, so two builds share one
# directory only when they are the same build.  `hostbuild/build/<name>.bin'
# over there is a symlink to whichever build packed last, which is a property
# of that checkout's history and not of this harness -- so the directory is
# asked for rather than spelled: `make -s imgdir' prints it, and this
# repository is passed as the userland so the answer is the directory of a
# build made from the programs about to be booted.
#
# Sets $C900_DIST to the checkout, $C900_IMGDIR to where it writes images, and
# $C900_WORKIMG to the script inside it,
# and leaves both EMPTY when none is found.  Empty is not fatal here: the
# refusal belongs at the command that wanted to boot something, which is what
# dist_need is for.  Compiling and linking the userland needs none of this.
#
# $C900_ROOT must already name this repository's root -- a sourced file cannot
# portably find its own path.

if [ -z "${C900_ROOT:-}" ] || [ ! -d "$C900_ROOT/mk" ]; then
	echo "dist.sh: \$C900_ROOT must name the repository root before sourcing" >&2
	exit 2
fi

# One query answers for every consumer: mk/deps.sh owns the search list and its
# order, so a path is spelled in exactly one place.
#
# The marker is checked here too, not just trusted from a non-empty
# $C900_DIST: `${C900_DIST:-...}' only calls deps.sh's search -- and its
# ok() -- when $C900_DIST was UNSET.  A caller that gave a WRONG value skips
# that search entirely, and without this check C900_WORKIMG/C900_IMGDIR would
# still get set from it, dist_need would see a non-empty C900_WORKIMG and
# say nothing is wrong, and the failure would land wherever the first
# consumer opened a file under the bad path instead of here, by name.
C900_DIST=${C900_DIST:-$(sh "$C900_ROOT/mk/deps.sh" dist)}
C900_WORKIMG=
C900_IMGDIR=
if [ -n "$C900_DIST" ] && [ -f "$C900_DIST/os/hostbuild/workimg.sh" ]; then
	C900_WORKIMG="$C900_DIST/os/hostbuild/workimg.sh"
	# --no-print-directory, and the make[N] lines dropped anyway: a make
	# invoked from inside another one inherits -w, so `Entering'/`Leaving
	# directory' wrap the answer and the last line of the output is a
	# message rather than a path.  Taken as the image directory it names a
	# file nothing packed, and every harness that asks for an image then
	# SKIPS -- a sweep that reports success by not testing anything.
	C900_IMGDIR=$(C900_USERLAND="$C900_ROOT" \
		make -s --no-print-directory -C "$C900_DIST/os/hostbuild" \
		imgdir 2>/dev/null |
		grep -v '^make\[' |
		sed -n '$p')
fi

# dist_img <dist> -- the packed image for a dist, refusing by name when it has
# not been packed.  The message names the command AND the repository so a user
# knows where to look.
dist_img() {
	dist_need "boot $1"
	if [ -z "$C900_IMGDIR" ]; then
		echo "*** cannot boot $1: $C900_DIST could not say where a build" >&2
		echo "*** of these parts writes its images.  Ask it directly:" >&2
		echo "***     make -C $C900_DIST/os/hostbuild imgdir" >&2
		exit 2
	fi
	if [ ! -f "$C900_IMGDIR/$1.bin" ]; then
		echo "*** no packed image for $1: $C900_IMGDIR/$1.bin" >&2
		echo "*** Images are packed in the distribution repository:" >&2
		echo "***     make -C $C900_DIST/os/hostbuild dist DIST=$1" >&2
		exit 2
	fi
	echo "$C900_IMGDIR/$1.bin"
}

# Refuse by name, at the command that wanted it, quoting deps.sh's own message
# rather than a second copy of the search list.
dist_need() {
	[ -n "$C900_WORKIMG" ] && return 0
	echo "*** cannot $1 without the distribution repository." >&2
	sh "$C900_ROOT/mk/deps.sh" -n dist >&2
	exit 2
}
