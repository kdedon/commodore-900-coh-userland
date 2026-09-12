#!/bin/sh
# build-yacc-cmd.sh [-s <srcdir>] <cmd> [extra-ccz-args...] -- build a command whose parser comes
# from a yacc grammar (cmd/<cmd>/*.y).  Generates the parser with the host Coherent
# yacc (build-hyacc.sh), then cross-compiles the generated parser + every other .c in
# the directory (minus the yyparse.c skeleton) + the games-lib gap fills.  Extra ccz
# args (e.g. an archive, a -D, an -I) are passed through for commands that need them.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
HYACC="$HERE/build/hyacc"
BIN="$HERE/build/bin"
# -s <srcdir> builds <cmd> from cmd/<srcdir> rather than cmd/<cmd>.  /usr/bin/rsh
# is the restricted shell, and a restricted shell is the SAME program as /bin/sh
# with its restrictions turned on by the name it was invoked under -- one source
# directory, two output names.  It was two copies of the source once, and they
# drifted until rsh restricted nothing at all.
src=""
while [ $# -gt 0 ]; do
	case "$1" in
	-s)	src="$2"; shift 2;;
	*)	break;;
	esac
done
name="$1"; shift
DIR="$OS/base/cmd/${src:-$name}"
LOG="$HERE/build/$name.log"
# libc-z8001.a supplies string routines and getopt.
# games/lib/src supplies err(3) and fgetln(3).
GLIB=""
mkdir -p "$BIN"; : > "$LOG"

[ -x "$HYACC" ] || sh "$HERE/build-hyacc.sh" >/dev/null 2>&1
[ -x "$HYACC" ] || { echo "== $name FAILED: no hyacc"; exit 1; }

Y=$(ls "$DIR"/*.y 2>/dev/null | head -1)
[ -n "$Y" ] || { echo "== $name FAILED: no .y grammar"; exit 1; }

# ---- generate the parser next to a copy of the grammar + its headers ----
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cp "$Y" "$DIR"/*.h "$T/" 2>/dev/null
( cd "$T" && "$HYACC" -d "$(basename "$Y")" ) >>"$LOG" 2>&1
grep -q 'yyparse' "$T/y.tab.c" 2>/dev/null || { echo "== $name FAILED: parser generation"; exit 1; }
cp "$T/y.tab.c" "$T/${name}parse.c"

# the command's own .c (skeleton + y.tab aux excluded) -- often none (grammar-only)
SRCS=$(ls "$DIR"/*.c 2>/dev/null | grep -vE '/yyparse\.c|/y\.tab|\.host' | tr '\n' ' ')

# The parser the compiler sees is a generated file in a temporary directory, and
# for a grammar-only command (cmd/find has no .c at all) it is the ONLY input.
# A record of what was compiled would then name a path that no longer exists and
# no source at all, so the grammar's directory is declared as a source of the
# program directly.  See host/buildlog.sh, $C900_SRC_EXTRA.
export C900_SRC_EXTRA="$Y"
# $T is on the include path because `hyacc -d' writes y.tab.h there, beside the
# parser it belongs to, and a command's own source reaches for it as
# <y.tab.h> (cmd/sh/lex.c): a generated header has no home in the source tree.
if CCZ_VAR=800000020800 "$CCZ" -s -i -L \
     -I "$T" -I "$DIR" -I "$OS/include" -I "$OS/include/sys" "$@" \
     -o "$BIN/$name" "$T/${name}parse.c" $SRCS $GLIB >>"$LOG" 2>&1; then
	echo "== $name linked: $(wc -c < "$BIN/$name") bytes"
else
	echo "== $name FAILED"; grep -v 'Strict\|Warning' "$LOG" | tail -12
	exit 1
fi
