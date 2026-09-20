#!/bin/sh
# build-libmgr1.sh - build libmgr.1, the shared MGR client library.
#
# libmgr imports from libc.1 like any program.  It can't carry its own libc:
# two brk.s copies would hand out overlapping memory.
#
# Same sources as libmgrcl.a, compiled with -VPIC.  libmgr.1.exp is the ABI.
#
# Prereq: the toolchain's `make libc1', and mgr's `picobjs'.
# Usage: build-libmgr1.sh [-v]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$(cd "$HERE/.." && pwd)"
. "$OS/hostbuild/toolchain.sh"		# $TC: the Z8001 toolchain checkout
B="${C900_TC_BUILD:-$TC/build}"
MGR="$OS/mgr"
OBJ="$MGR/build/obj"
OUT="$MGR/build/lib"
EXP="$MGR/src/libmgr/libmgr.1.exp"
LIBC1="$B/libc1/libc.1"
verbose=
[ "${1:-}" = -v ] && verbose=-v

for f in "$B/slgen" "$B/ld-z8001" "$B/as-z8001" "$LIBC1" "$EXP"; do
	[ -e "$f" ] || { echo "build-libmgr1: missing $f" >&2; exit 1; }
done

( cd "$MGR" && make -f Makefile.c900 picobjs >/dev/null )
mkdir -p "$OUT"

# Last, so ld takes from libc.1 only what the objects still need.
"$B/slgen" $verbose -A "$B/as-z8001" -L "$B/ld-z8001" -T "$OUT" -e "$EXP" \
	-o "$OUT/libmgr.1" $OBJ/pic-*.o "$LIBC1"

# A library over either 64 KB segment can't load, and nothing else checks.
python3 - "$OUT/libmgr.1" "$EXP" <<'PY'
import sys
b = open(sys.argv[1], 'rb').read()
sh = lambda o: b[o] | b[o+1] << 8
ln = lambda o: ((b[o] | b[o+1] << 8) << 16) | (b[o+2] | b[o+3] << 8)
SHRI, PRVI, BSSI, SHRD, PRVD, BSSD, DEBUG, SYM = range(8)
sz = [ln(8 + 4*i) for i in range(9)]
shared = sz[SHRI] + sz[SHRD]
private = sz[PRVI] + sz[BSSI] + sz[PRVD] + sz[BSSD]
tb = sh(6)
nexp = (b[tb+4] << 8) | b[tb+5]
nfix = (b[tb+8] << 8) | b[tb+9]
nlist = sum(1 for l in open(sys.argv[2])
            if l.strip() and not l.lstrip().startswith('#'))
off = 48 + sz[SHRI] + sz[PRVI] + sz[SHRD] + sz[PRVD] + sz[DEBUG]
libs = imps = 0
for k in range(sz[SYM] // 22):
    o = off + k*22
    t = b[o+16] | b[o+17] << 8
    if t == 0o13: libs += 1
    elif t == 0o14: imps += 1
print("libmgr.1: %d B on disk, %d exports (list names %d), %d fixups,"
      " %d import%s from %d librar%s"
      % (len(b), nexp, nlist, nfix, imps, "" if imps == 1 else "s",
         libs, "y" if libs == 1 else "ies"))
print("  shared  seg  SHRI %6d + SHRD %5d = %6d B of 65536  (%d B free)"
      % (sz[SHRI], sz[SHRD], shared, 65536 - shared))
print("  private seg  PRVD %6d + BSSD %5d = %6d B of 65536  (%d B free)"
      % (sz[PRVD], sz[BSSD], private, 65536 - private))
if shared > 65536 or private > 65536:
    raise SystemExit("build-libmgr1: a segment is over 64 KB")
PY
