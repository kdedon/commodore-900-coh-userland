#!/bin/sh
# build-gzip.sh -- gzip 0.8.2 (archive/gzip) for the Z8001 COHERENT target.
#
# One binary, three names: gzip switches on argv[0], so gunzip and zcat are
# copies of it (the same trick build-extra-userland.sh uses for compress).
#
# -DCOHERENT selects the target block in archive/gzip/tailor.h, which turns on
# DYN_ALLOC.  That is not optional here: the deflate hash chains, the hash
# heads and the shared 32K window are 32K each, all the static data has to
# fit one 64K segment, and ld refuses a larger one.  On the heap each is a
# separate object under the 64K single-object limit and sbrk carries the
# break into the following segments.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
SRC="$OS/archive/gzip"
CCZ="$TC/ccz"
BIN="$HERE/build/bin"
INC="-I $OS/include -I $OS/include/sys"
mkdir -p "$BIN"
# Compile logs go under the build tree, not /tmp: a fixed name outside
# the build is not this build's output, two runs share it, and a CI
# runner that cleans /tmp between steps loses the one thing a failure
# leaves behind.  $LOGD names them; the FAIL lines quote the path.
LOGD="$HERE/logs"; mkdir -p "$LOGD"

if CCZ_VAR=800000020800 "$CCZ" -s -i -DCOHERENT $INC -o "$BIN/.gzip.new" \
	"$SRC/gzip.c" "$SRC/zip.c" "$SRC/deflate.c" "$SRC/trees.c" \
	"$SRC/bits.c" "$SRC/util.c" "$SRC/unzip.c" "$SRC/inflate.c" \
	"$SRC/unpack.c" "$SRC/lzw.c" >"$LOGD"/gzip-build.log 2>&1
then
	mv -f "$BIN/.gzip.new" "$BIN/gzip"
	cp "$BIN/gzip" "$BIN/gunzip"
	cp "$BIN/gzip" "$BIN/zcat"
	echo "== gzip: OK ($(wc -c < "$BIN/gzip") B; gunzip and zcat are copies)"
else
	rm -f "$BIN/.gzip.new"
	echo "== gzip: FAIL -- $(grep -iE 'error|undefined|no match|Internal' "$LOGD"/gzip-build.log | grep -v 'Strict\|Warning' | head -1)"
	exit 1
fi
exit 0
