#!/bin/sh
# version.sh -- print the release version.
#
# The git tag is the source.  A release is tagged `v<major>.<minor>.<patch>':
#
#	3.5.0			built at the tag
#	3.5.0-4-g1a2b3c4	four commits past it
#	3.5.0-4-g1a2b3c4-dirty	...with the tree modified
#
# So an image built off a working tree says so in its own banner, and cannot
# be mistaken for the release it was built near.
#
# Outside a checkout -- an unpacked source archive -- `git archive' has already
# substituted the tag into ARCHIVE below (.gitattributes marks this file
# export-subst), so the answer travels with the archive.
#
# An untagged checkout is not an error -- a branch build, a fresh clone, a fork
# with no tags is an ordinary thing to compile.  It answers 0.0.0-g<commit>,
# which is honest and cannot be mistaken for a release.  Whoever cuts a release
# is responsible for refusing anything that is not exactly a tag; the release
# workflow does that, because only it knows a release is what was meant.
#
# Exits 1 printing nothing only when there is no answer at all: no git, no
# commit, no archive substitution.

ARCHIVE='$Format:%(describe:tags=v*)$'

here=$(dirname "$0")
v=$(git -C "$here" describe --tags --match 'v*' --dirty 2>/dev/null) || v=

if [ -z "$v" ]; then
	case $ARCHIVE in
	*Format:*)	;;			# unsubstituted: not an archive
	*)		v=$ARCHIVE ;;
	esac
fi

# Untagged, but a checkout: name the commit rather than refuse to build.
if [ -z "$v" ]; then
	c=$(git -C "$here" describe --always --dirty 2>/dev/null) || c=
	[ -z "$c" ] || v=0.0.0-g$c
fi

if [ -z "$v" ]; then
	echo "version.sh: no version -- not a git checkout and not an" >&2
	echo "  exported archive, so there is nothing to read a version from." >&2
	exit 1
fi

echo "${v#v}"
