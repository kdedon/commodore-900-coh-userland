#!/bin/sh
# pack-component.sh -- cut ONE component-kind package out of this repository.
#
#	sh dist/pack-component.sh [-o OUTDIR] <component>-<kind>
#	sh dist/pack-component.sh [-o OUTDIR] <component> <kind>
#
#	  net        the runtime files            -> c900-net-bin-v<V>.tar.gz
#	  net-src    the corresponding source     -> c900-net-src-v<V>.tar.gz
#	  net-man    the pages for net's programs -> c900-net-man-v<V>.tar.gz
#
# A bare component name means its `bin' kind, because that is what an owner
# means by "give me net".  OUTDIR defaults to hostbuild/build/packages.
#
# hostbuild/component.py decides WHICH FILES via `component.py component <name>
# <kind>'.  This script copies, stamps and tarballs; refusals come from there.
#
# The format the result must satisfy is the four declarations in
# dist/packages/component-{bin,src,man,dev}.pkg, and the gate that judges one is
# commodore-900-coh-dist's `os/dist/check-package.sh -d dist/packages'.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)			# dist/
OS=$(cd "$HERE/.." && pwd)				# repository root
PYTHON=${PYTHON:-python3}
COMPPY="$OS/hostbuild/component.py"
OUT="$OS/hostbuild/build/packages"

while [ $# -gt 0 ]; do
	case "$1" in
	-o) OUT=$2; shift 2 ;;
	-h|--help) sed -n '2,13p' "$0"; exit 0 ;;
	--) shift; break ;;
	-*) echo "pack-component.sh: unknown option $1" >&2; exit 2 ;;
	*) break ;;
	esac
done
[ $# -ge 1 ] || { sed -n '2,13p' "$0" >&2; exit 2; }

if [ $# -ge 2 ]; then
	COMP=$1; KIND=$2
else
	# `net-src' splits at the LAST hyphen, and only when the tail is a kind:
	# `mail-light' is a component and `mail-light-src' is its source package,
	# so splitting on the first hyphen would ask for a component called
	# `mail'.  component.py refuses a component named after a kind, which is
	# what makes this split unambiguous rather than merely usual.
	case "$1" in
	*-bin) COMP=${1%-bin}; KIND=bin ;;
	*-src) COMP=${1%-src}; KIND=src ;;
	*-man) COMP=${1%-man}; KIND=man ;;
	*-dev) COMP=${1%-dev}; KIND=dev ;;
	*)     COMP=$1;        KIND=bin ;;
	esac
fi

. "$OS/hostbuild/provenance.sh"

B="$OS/hostbuild/build"
# THE BUILD MUST HAVE BEEN PUBLISHED.  build/.ulstamp is written by
# publish-userland.sh and carries the content id of the published set; a
# package cut from a tree that never reached that point could not say which
# build of this repository its files came out of.
[ -f "$B/.ulstamp" ] || {
	echo "pack-component.sh: no $B/.ulstamp -- nothing has been published." >&2
	echo "  Run \`make -C hostbuild', which publishes as its last step." >&2
	exit 1
}
ULID=$(prov_get "$B/.ulstamp" ulid)
[ -n "$ULID" ] || {
	echo "pack-component.sh: $B/.ulstamp carries no ulid." >&2; exit 1; }

V=$(sh "$OS/hostbuild/version.sh")
[ -n "$V" ] || { echo "pack-component.sh: no version -- see hostbuild/version.sh" >&2; exit 1; }
NAME="c900-$COMP-$KIND-v$V"

W=$(mktemp -d "${TMPDIR:-/tmp}/packcomp.XXXXXX")
trap 'rm -rf "$W"' EXIT INT TERM
T="$W/$NAME"
mkdir -p "$T/files"

# ---- the file set.  component.py refuses here, with the reason, when it must.
"$PYTHON" "$COMPPY" component "$COMP" "$KIND" > "$W/set" || exit $?
[ -s "$W/set" ] || {
	echo "pack-component.sh: $COMP-$KIND resolved to no files at all." >&2
	echo "  An empty package that unpacks and passes a gate is read as" >&2
	echo "  coverage of the component it names.  Refusing." >&2
	exit 1
}

# manifest.tab: the installation instruction.  The same columns component.py
# emits, minus the source column, which named a path on the machine that packed
# it and means nothing afterwards.  Space-separated, which is the list format's
# own spelling (`f /bin/ls 755 3 1'), so an installer that already reads one
# reads this and awk needs no -F.
: > "$T/manifest.tab"
nf=0
while IFS='	' read -r typ path mode uid gid src; do
	[ -n "${typ:-}" ] || continue
	printf '%s %s %s %s %s\n' "$typ" "$path" "$mode" "$uid" "$gid" \
		>> "$T/manifest.tab"
	case "$typ" in
	d|e|l) continue ;;			# no payload of their own
	esac
	[ "$src" = "-" ] && continue		# generated below (man.index)
	d="$T/files/$(dirname "$path")"
	mkdir -p "$d"
	cp -p "$src" "$T/files/$path"
	nf=$((nf + 1))
done < "$W/set"

# man.index: GENERATED with exactly the pages packed.
NPROG=$("$PYTHON" "$COMPPY" components | awk -v c="$COMP" '$1==c{print $4}')
NDOC=$("$PYTHON" "$COMPPY" components | awk -v c="$COMP" '$1==c{print $5}')
NSRC=$("$PYTHON" "$COMPPY" components | awk -v c="$COMP" '$1==c{print $7}')
if [ "$KIND" = man ]; then
	IDX=$("$PYTHON" - "$OS" <<'EOF'
import os, sys
sys.path.insert(0, os.path.join(sys.argv[1], 'hostbuild'))
import component
print(component.osp(os.path.join(component.MANTREE, 'man.index')))
EOF
)
	awk 'NR==FNR { if ($1=="f") want[$2]=1; next }
	         { split($0, r, "\t"); if (r[1] in want) print }' \
	    "$T/manifest.tab" "$IDX" > "$T/files/man.index"
	[ -s "$T/files/man.index" ] || {
		echo "pack-component.sh: generated man.index is empty" >&2; exit 1; }
	nf=$((nf + 1))
	# WHICH LIBRARIES' ARTICLES ARE IN HERE.  `documented' counts programs, so
	# a package holding 27 pages for a component with one documented program
	# would otherwise say nothing about the other 26; this names their
	# subject.  `-' is a component that builds no documented library.
	NLIBS=$("$PYTHON" - "$OS" "$COMP" <<'EOF'
import os, sys
sys.path.insert(0, os.path.join(sys.argv[1], 'hostbuild'))
import component
print(",".join(sorted(component.Component(sys.argv[2]).libman())) or "-")
EOF
)
fi

# ---- the package's own furniture ----
echo "$V" > "$T/VERSION"
cp "$OS/LICENSE" "$T/LICENSE"
cp "$HERE/check-contents.sh" "$T/check-contents.sh"

EXTRA="version=$V package=$COMP-$KIND component=$COMP pkgkind=$KIND entries=$nf"
case "$KIND" in
man) EXTRA="$EXTRA programs=$NPROG documented=$NDOC libraries=$NLIBS" ;;
src) EXTRA="$EXTRA programs=$NPROG mapped=$NSRC" ;;
# WHICH BUILD THESE BYTES CAME OUT OF.  ulid is this repository's own content
# id over its published set, so two packages cut from one sweep can be shown to
# carry the same programs without being taken apart.
bin) EXTRA="$EXTRA ulid=$ULID" ;;
esac
# shellcheck disable=SC2086
prov_write "$T/.provenance" component dist -- $EXTRA "contentid=pending"

# ---- which bytes.  .contents over every other file, contentid its sha1. ----
( cd "$T" && find . -type f ! -path ./.contents ! -path ./.provenance -print |
  LC_ALL=C sort | sed 's|^\./||' | xargs md5sum ) > "$T/.contents"
CID=$(sha1sum "$T/.contents" | cut -c1-12)
sed "s|^contentid=.*|contentid=$CID|" "$T/.provenance" > "$T/.provenance.new"
mv -f "$T/.provenance.new" "$T/.provenance"

# Make OUT absolute; tar runs from inside the staging directory.
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
# Sorted members, numeric owners, gzip -n for a deterministic checksum.
( cd "$W" && find "$NAME" -print | LC_ALL=C sort |
  tar czf "$OUT/$NAME.tar.gz" --numeric-owner --owner=0 --group=0 \
      --mtime="@0" --no-recursion -T - ) 2>/dev/null ||
( cd "$W" && tar czf "$OUT/$NAME.tar.gz" "$NAME" )

echo "packed $OUT/$NAME.tar.gz -- $nf file(s), contentid $CID"
# end of pack-component.sh
