#!/bin/sh
# build-hyacc.sh -- build the COHERENT 3.x yacc (cmd/yacc) as a HOST tool, so
# yacc-grammar commands (awk/find/test/sh/...) can have their parser C generated.
# Output: $HERE/build/hyacc.  Idempotent; re-run cheaply.
#
# This uses the SAME source as the target yacc binary (cmd/yacc, 3.x) -- the
# pinned 0.7.3 host-yacc is retired: the 3.x yacc generates every grammar's
# parser cleanly on the host once three throwaway-copy patches make the K&R tool
# build+run under glibc/LP64:
#   - y0.c: hard-coded skeleton path "/lib/yyparse.c" -> the in-tree copy.
#   - y1.c: freopen mode "rwb" -> "w+b" (MWC libc accepts "rwb"; glibc does not,
#     which surfaced only as a lost "temp file i/o error" -- the optimizer temp).
#   - y6.c: skip frlset() -- its bulk-free of the lookahead sets dereferences a
#     union slot uninitialised on LP64 (ng_lset/ng_rel overlap); cleanup-only, so
#     skipping it just leaks in this one-shot generator (emitted parser unaffected).
# <sys/mdata.h> -- the 3.x yacc.h includes it -- belongs to the KERNEL, and is
# taken from $KINC, the kernel repository's include directory resolved in
# toolchain.sh.  This tree used to keep a copy under include; it never
# had one, so every yacc-grammar command (awk, find, sh, bc, ...) failed here.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"		# sets $KINC: the kernel's header set
YACC="$OS/base/cmd/yacc"
OUT="$HERE/build/hyacc"
mkdir -p "$HERE/build"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/inc/sys" "$T/src"
if [ -z "$KINC" ] || [ ! -f "$KINC/sys/mdata.h" ]; then
	echo "hyacc: <sys/mdata.h> is the kernel's; sh mk/deps.sh -n kernel"
	exit 1
fi
cp "$KINC/sys/mdata.h" "$T/inc/sys/"
cp "$YACC"/y?.c "$YACC"/yacc.h "$YACC"/assert.h "$YACC"/action.h "$T/src/"
sed "s#\"/lib/yyparse.c\"#\"$YACC/yyparse.c\"#" "$YACC/y0.c" > "$T/src/y0.c"
sed 's/"rwb"/"w+b"/' "$YACC/y1.c" > "$T/src/y1.c"
sed 's/^\tfrlset();/\t\/* frlset() skipped on LP64 host: cleanup-only, harmless leak *\//' \
	"$YACC/y6.c" > "$T/src/y6.c"
if gcc -std=gnu89 -w -O -I "$T/inc" -I "$T/src" -o "$OUT" "$T"/src/y?.c 2>"$T/err"; then
	echo "hyacc: built ($(wc -c < "$OUT") bytes)"
else
	echo "hyacc: BUILD FAILED"; grep -v warning "$T/err" | head; exit 1
fi
