#!/bin/sh
# publish-userland.sh -- write the stamp that says WHICH userland this tree has
# built.  `make -C hostbuild publish', and the tail of `make all'.
#
# This repository builds programs; it does not pack an image.  The image is
# packed in commodore-900-coh-dist, which consumes what is published here
# (RELEASE-STYLES.md sec. H.4) -- and the one question it cannot answer for itself
# is the one this file answers: which userland is in build/, and does it still
# match the sources beside it.
#
# Two ways an image could otherwise be quietly wrong, and the stamp is what
# turns each into a refusal over there:
#
#   NOTHING BUILT      build/bin exists from an older sweep, or half of one.  A
#                      packer cannot tell that from a finished build: every file
#                      it looks for is there.  No stamp means nothing was
#                      published, and the consumer refuses instead of packing
#                      whatever was lying about.
#   BUILT, THEN EDITED four lanes edit this tree while an image is being packed.
#                      `srcid' (see provenance.sh) covers committed AND
#                      uncommitted content over the scope below, so the consumer
#                      recomputes it and sees that the binaries are behind the
#                      sources -- which no mtime comparison across a repository
#                      boundary can be trusted to see.
#
# The stamp also carries an id over the PUBLISHED BYTES (`ulid'), for the
# opposite direction: an image's own stamp records it, so a packed image can be
# asked which userland it contains without being taken apart, and two images
# built from one sweep can be shown to carry the same programs.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/.." && pwd)
B="$HERE/build"
. "$HERE/provenance.sh"

# The scope: everything a published program can be compiled from, plus the
# harnesses that decide what and how.  Naming every tree would fold in ones
# nothing builds, and an edit to one of those does not change a shipped byte --
# a staleness alarm that fires on them would be on permanently, and an alarm
# that is always on is read as decoration.
SCOPE="base games mgr net test archive comms graphics hr man
       hostbuild/build.sh"
# Every build-*.sh, named by pattern rather than one by one: a new sweep script
# is part of the scope the moment it exists, without anybody remembering.
for s in "$HERE"/build-*.sh; do
	[ -f "$s" ] && SCOPE="$SCOPE hostbuild/$(basename "$s")"
done
# Paths that do not exist in this tree are dropped: git treats an unmatched
# pathspec as an error, which would make the whole id `unknown' -- the stamp
# would then be written, look fine, and say nothing.
KEEP=
for p in $SCOPE; do
	[ -e "$OS/$p" ] && KEEP="$KEEP $p"
done

# The published set: the flat binary sweep the image builder stages from
# (dist/systems/extended.sys names hostbuild/build/bin, hostbuild/build/root and
# hostbuild/build/mgr), and nothing else in build/ -- the logs, the working
# copies and the stamps are this tree's business.
[ -d "$B/bin" ] || {
	echo "publish-userland.sh: no $B/bin -- nothing has been built." >&2
	echo "  Run \`make' here first; \`make all' publishes as its last step." >&2
	exit 1
}
N=$(find "$B/bin" -type f | wc -l)
[ "$N" -gt 0 ] || {
	echo "publish-userland.sh: $B/bin is empty -- nothing to publish." >&2
	exit 1
}
# THE SOURCE MAP.  The published binaries carry licence obligations -- rcs is
# GPLv1, gzip and screen GPLv2, ttycity GPLv3 with a sec. 7 term, lharc, unzip and
# zoo carry a COPYING each -- and the consumer discharges them by cutting a
# -src package beside each -bin one.  It cannot work out which sources built
# which program: one directory is not one program in this tree, and reaching
# across the split to guess is what the split exists to prevent.  So the map is
# published here, from the build's own record of what it compiled for what, and
# a -src package refuses by name for every program the map does not account for.
#
# Written before the stamp and not gated on: a tree that has built programs has
# something to publish either way, and the map's own coverage line says how much
# of the published set it accounts for.  What must not happen is the stamp
# appearing without it, which would read as a complete publication.
${PYTHON:-python3} "$HERE/ulsrcmap.py" -o "$B/.ulsrcmap" || {
	echo "publish-userland.sh: no source map published -- every -src package" >&2
	echo "  in the consuming repository will refuse by name." >&2
}

# Content over names AND bytes, sorted, so the id is a property of the set and
# not of the order a filesystem happened to return it in.
ULID=$( (cd "$B" && find bin root mgr -type f 2>/dev/null | LC_ALL=C sort |
	 tr '\n' '\0' | xargs -0 -r sha1sum) | sha1sum | cut -c1-12)

# shellcheck disable=SC2086
prov_write "$B/.ulstamp" userland $KEEP \
	-- "binaries=$N" \
	   "version=$(sh "$OS/hostbuild/version.sh")" \
	   "ulid=$ULID" \
	   "srcmapped=$(grep -vc '^#' "$B/.ulsrcmap" 2>/dev/null || echo 0)" \
	   "toolchain=$(prov_get "$B/.tcstamp" toolchain_commit)" \
	   "toolchain_id=$(prov_get "$B/.tcstamp" toolchain_id)" \
	   "man=$([ -f "$OS/hostbuild/build/man/man.index" ] && echo yes || echo no)"

echo "== published userland: $N programs in build/bin, content id $ULID"
prov_header "userland" "$B/.ulstamp" || true
