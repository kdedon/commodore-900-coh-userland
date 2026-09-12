#!/bin/sh
# publish-userland.sh -- stamp built userland for the distribution repository.
# Run through make publish after the sweep.  Record source identity,
# published bytes and source-map coverage in build/.ulstamp.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/.." && pwd)
B="$HERE/build"
. "$HERE/provenance.sh"

# Source trees and build scripts included in the freshness check.
SCOPE="base games mgr net test archive comms hr man
       hostbuild/build.sh"
# Every build-*.sh, named by pattern rather than one by one: a new sweep script
# is part of the scope the moment it exists, without anybody remembering.
for s in "$HERE"/build-*.sh; do
	[ -f "$s" ] && SCOPE="$SCOPE hostbuild/$(basename "$s")"
done
# Omit scope paths absent from this checkout.
KEEP=
for p in $SCOPE; do
	[ -e "$OS/$p" ] && KEEP="$KEEP $p"
done

# Require a nonempty binary directory before publishing.
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
# Publish the source map before the stamp.  A mapping failure is reported
# but does not stop publication; source packages check their own coverage.
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
