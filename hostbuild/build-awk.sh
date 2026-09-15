#!/bin/sh
# build-awk.sh -- awk needs its yacc grammar (awk.y) turned into a C parser, plus
# libm.  Generate the parser with the host Coherent yacc (build-hyacc.sh), which
# gives awk.y's tokens the `_'-suffixed names awk's sources use, then
# cross-compile awk0..6 + that parser + libm-z8001.a.
# Prereqs: build-libc-z8001.sh, build-libm-z8001.sh (libm-z8001.a), the toolchain.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
HYACC="$HERE/build/hyacc"
BIN="$HERE/build/bin"
LOG="$HERE/build/awk.log"
AWK="$OS/base/cmd/awk"
LIBM="$TCB/libm-z8001/libm-z8001.a"
mkdir -p "$BIN"; : > "$LOG"

[ -f "$LIBM" ] || { echo "== awk FAILED: no libm-z8001.a (run build-libm-z8001.sh)"; exit 1; }
[ -x "$HYACC" ] || sh "$HERE/build-hyacc.sh" >>"$LOG" 2>&1
[ -x "$HYACC" ] || { echo "== awk FAILED: no hyacc (build-hyacc.sh)"; exit 1; }

# ---- generate the parser (48 shift/reduce conflicts are expected for awk.y) ----
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
( cd "$T" && cp "$AWK/awk.y" "$AWK/awk.h" . && "$HYACC" -d awk.y ) >>"$LOG" 2>&1
if ! grep -q 'yyparse' "$T/y.tab.c" 2>/dev/null; then
	echo "== awk FAILED: parser generation (no yyparse in y.tab.c)"; exit 1
fi
cp "$T/y.tab.c" "$T/awkparse.c"

# ---- cross-compile + link.  awk0..5 include "y.tab.h" from cmd/awk, a header
# maintained by hand; the parser includes the y.tab.h yacc wrote beside it in $T.
# The two carry the same token numbers. ----
if CCZ_VAR=800000020800 "$CCZ" -s -i -L \
     -I "$AWK" -I "$OS/include" -I "$OS/include/sys" \
     -o "$BIN/awk" \
     "$AWK"/awk0.c "$AWK"/awk1.c "$AWK"/awk2.c "$AWK"/awk3.c "$AWK"/awk4.c "$AWK"/awk5.c "$AWK"/awk6.c \
     "$T/awkparse.c" "$LIBM" >>"$LOG" 2>&1; then
	echo "== awk linked: $(wc -c < "$BIN/awk") bytes"
else
	echo "== awk FAILED"; grep -v 'Strict\|Warning' "$LOG" | tail -12
	exit 1
fi
