#!/bin/sh
# check-contents.sh -- a component package verifies its own bytes.
#
#	cd <unpacked package> && sh check-contents.sh
#
# SHIPPED INSIDE every package pack-component.sh cuts, and run by the format
# gate (check-package.sh's `gate' directive runs a package's own check and takes
# its verdict as its own).  It answers one question, offline, with no baseline
# and no network: are the files in this package the bytes the packer put in it?
#
# The chain, so a reader can see where it ends:
#
#	.provenance:contentid  is the sha1 of .contents   (the gate's `idfile'
#	                       recomputes it from the shipped bytes)
#	.contents              is the md5 of every other file, in md5sum's own
#	                       format, so a consumer with neither this script
#	                       nor a gate can run `md5sum -c .contents' with the
#	                       tool their system already has
#
# Corrupting any file is caught here and NAMED; corrupting .contents is caught
# by contentid.  Corrupting both is caught by nothing self-describing, and the
# answer to that is the manifest a release recorded (check-package.sh -b) or the
# published checksum -- not a deeper hash in here.
#
# THIS IS A SECOND COPY of the same twenty lines the toolchain ships in its own
# packages (host/check-contents.sh), and that is deliberate rather than
# overlooked: a package must carry its check, this repository is the producer of
# these packages, and the alternative is reaching into a dependency's checkout at
# pack time for a file that would then ship under our name.  The FORMAT the two
# agree on is written down once, in commodore-900-coh-dist's os/dist/
# PACKAGE-FORMAT, which is what stops them drifting into meaning different
# things.
set -e

command -v md5sum >/dev/null 2>&1 || {
	echo "check-contents: no md5sum on this host, so nothing was verified." >&2
	echo "  A check that cannot run must not report success." >&2
	exit 2
}

found=0
n=0
for c in $(find . -name .contents | LC_ALL=C sort); do
	found=$((found + 1))
	d=$(dirname "$c")
	if ! (cd "$d" && md5sum --quiet -c .contents); then
		echo "*** check-contents: $c does not describe the files beside it." >&2
		echo "*** These are not the bytes this package was cut with." >&2
		exit 1
	fi
	n=$((n + $(grep -c . "$c")))
done

# No listing at all is not a pass: it is this check having nothing to check,
# which is the shape of every gate this project has shipped that could not fail.
[ "$found" -gt 0 ] || {
	echo "check-contents: no .contents in this tree -- nothing was verified." >&2
	exit 2
}
echo "check-contents: $n file(s) in $found listing(s) match their recorded md5"
# end of check-contents.sh
