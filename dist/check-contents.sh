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
# The toolchain ships the same check in its own packages
# (host/check-contents.sh): a package carries its own check, and each producer
# ships the copy that goes out under its name.  The FORMAT the two agree on is
# written down once, in commodore-900-dist's os/dist/PACKAGE-FORMAT.
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

# WHAT THE PACKAGE SAYS IT CARRIES.  .contents describes the files that ARE
# here and cannot see one that should be and is not, so the manifest is read
# back against files/: an `f' row with no payload, and an `l' row with no target
# or whose two names are not one inode, are a package installing a hole.
if [ -f manifest.tab ]; then
	bad=0
	while read -r typ path mode uid gid tgt; do
		case "$typ" in
		f)	[ -f "files/$path" ] || {
				echo "*** check-contents: manifest.tab declares $path and files/$path is not here." >&2
				bad=$((bad + 1)); } ;;
		l)	a=$(ls -di "files/$path" 2>/dev/null | awk '{print $1}')
			b=$(ls -di "files/$tgt" 2>/dev/null | awk '{print $1}')
			if [ -z "${tgt:-}" ]; then
				echo "*** check-contents: manifest.tab gives $path no link target, so nothing can recreate it." >&2
				bad=$((bad + 1))
			elif [ -n "$b" ] && [ "$a" != "$b" ]; then
				echo "*** check-contents: $path and the target $tgt beside it are not one inode." >&2
				bad=$((bad + 1))
			elif [ -z "$b" ] && [ -n "$a" ]; then
				echo "*** check-contents: $path is a copy here, and $tgt, the inode it must share, is in another package." >&2
				bad=$((bad + 1))
			fi ;;
		esac
	done < manifest.tab
	[ "$bad" -eq 0 ] || {
		echo "*** check-contents: $bad manifest row(s) name files this package does not carry." >&2
		exit 1
	}
fi

# No listing at all is not a pass: it is this check having nothing to check,
# which is the shape of every gate this project has shipped that could not fail.
[ "$found" -gt 0 ] || {
	echo "check-contents: no .contents in this tree -- nothing was verified." >&2
	exit 2
}
echo "check-contents: $n file(s) in $found listing(s) match their recorded md5"
# end of check-contents.sh
