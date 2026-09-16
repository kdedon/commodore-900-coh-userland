#!/bin/sh
# build-bc.sh -- bc(1) and dc(1).  Both come out of this one script because dc
# links six of bc's objects (cmd/dc/Makefile's bco=), so one source tree feeds
# two binaries and a bc that compiles is not yet a dc that links.
#
# bc's parser is GENERATED here, with the host Coherent yacc (build-hyacc.sh),
# because cmd/bc/Makefile's yacc invocation is not a default one:
#
#     yacc -hdr yy.h -st -terms 55 -nterms 34 -prods 109 -states 203 gram.y
#     sed -f gram.fix < y.tab.c > gram.c
#
# The four table sizes are exact -- the grammar has 55 terminals, 34
# non-terminals, 109 productions and 203 states, and yacc allocates what it is
# told -- and -st takes the statistics and the 1 shift/reduce conflict (the
# dangling `else') as expected rather than as a complaint.  -hdr writes the
# token defines and the YYSTYPE union to yy.h, which lex.c includes.
#
# gram.fix then rewrites the yytnames table, which -st ... YYTNAMES puts in the
# parser, from the grammar's token spellings into the input's: a syntax error
# reports `at '+='' where the raw table would say `at ADDAB'.  It is a sed
# script of 35 substitutions and it runs over the whole generated file, so the
# ones whose names are substrings of identifiers the actions use -- IBASE inside
# LIBASE/SIBASE, DO inside DOT -- are anchored on the surrounding quotes.
#
# yy.h and the parser are build products and are not in the tree: -I on the
# generation directory puts the freshly generated yy.h ahead of anything else,
# so lex.c's token numbers cannot come from a stale copy of a grammar that has
# since changed.
#
# -DCOHERENT selects path.h's COHERENT arm (DEFLIBPATH, LISTSEP), which bc -l
# needs: it finds lib.b along $LIBPATH or /lib:/usr/lib rather than at one
# hard-coded name.
#
# Prereqs: build-libc-z8001.sh, the toolchain.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
HYACC="$HERE/build/hyacc"
BIN="$HERE/build/bin"
BC="$OS/base/cmd/bc"
DC="$OS/base/cmd/dc"
LOG="$HERE/build/bc.log"
MP="$(ls "$OS"/base/lib/libmp/*.c 2>/dev/null | tr '\n' ' ')"
mkdir -p "$BIN"; : > "$LOG"

[ -n "$MP" ] || { echo "== bc FAILED: no libmp sources"; exit 1; }
[ -x "$HYACC" ] || sh "$HERE/build-hyacc.sh" >>"$LOG" 2>&1
[ -x "$HYACC" ] || { echo "== bc FAILED: no hyacc (build-hyacc.sh)"; exit 1; }

# ---- generate the parser ----
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cp "$BC"/gram.y "$BC"/gram.fix "$BC"/*.h "$T/"
( cd "$T" && "$HYACC" -hdr yy.h -st \
	-terms 55 -nterms 34 -prods 109 -states 203 gram.y ) >>"$LOG" 2>&1
grep -q 'yyparse' "$T/y.tab.c" 2>/dev/null ||
	{ echo "== bc FAILED: parser generation"; tail -12 "$LOG"; exit 1; }
sed -f "$T/gram.fix" < "$T/y.tab.c" > "$T/gram.c"
# The rewrite is the reason gram.fix exists, so a gram.fix that matched nothing
# (a token renamed in the grammar, a yacc that quotes the table differently)
# must fail the build rather than ship a bc whose errors name ADDAB.
grep -q "\"'+='\"" "$T/gram.c" ||
	{ echo "== bc FAILED: gram.fix did not rewrite the terminal names"; exit 1; }

INC="-I $T -I $BC -I $OS/include -I $OS/include/sys -DCOHERENT"
# The six bc objects dc links, per cmd/dc/Makefile: the arithmetic and the
# number reader, with none of the grammar or the interpreter.
DCBC="$BC/bcmch.c $BC/bcmutil.c $BC/getnum.c $BC/globals.c $BC/output.c $BC/putnum.c"

# Compiled from the repository root with repository-relative source paths: the
# multi-precision library and bc's own files assert(), and <assert.h> puts
# __FILE__ in the binary.  The generated parser in $T is outside the tree and
# keeps its own path, which no assert reaches.  See c900_rel in toolchain.sh.
link_one() {	# link_one <outname> <src...>
	lname="$1"; shift
	if ( cd "$COHERENT_OS" && CCZ_VAR=800000020800 "$CCZ" -s -i -L $INC \
	   -o "$BIN/.$lname.new" $(c900_rel "$@") ) >>"$LOG" 2>&1; then
		return 0
	fi
	rm -f "$BIN/.$lname.new"
	echo "== $lname FAILED"
	grep -iE 'error|undefined|not defined|redefined|no match|Internal|^Ld:' "$LOG" |
		grep -v 'Strict\|Warning' | head -3
	return 1
}

# The segment check.  A Z8001 process gets ONE 64 KB segment of instructions and
# ONE of data; the header is include/n.out.h -- l_ssize[9], fsize_t longs in
# PDP order from offset 8 -- with INSTRUCTION space SHRI+PRVI+BSSI and DATA
# SHRD+PRVD+BSSD.  bc carries a 1000-entry code stream and the whole
# multi-precision library, so it is worth knowing where it sits; the check reads
# the SIDE name, before the rename, because a binary that does not fit is not
# this build's product and must not reach $BIN at all.
fits() {	# fits <name>
	python3 - "$BIN/.$1.new" "$1" <<'PYEOF'
import sys
b = open(sys.argv[1], 'rb').read()
name = sys.argv[2]
u16 = lambda o: b[o] | b[o+1] << 8
pdp = lambda o: (u16(o) << 16) | u16(o+2)
if u16(0) != 0o407:
    sys.exit("%s: not an l.out (magic %o)" % (name, u16(0)))
seg = [pdp(8 + 4*i) for i in range(9)]
text = sum(seg[0:3])
data = sum(seg[3:6])
LIM = 65536
print("  %s: text %d/%d (%.1f%%), data %d/%d (%.1f%%), file %d B"
      % (name, text, LIM, 100.0*text/LIM, data, LIM, 100.0*data/LIM, len(b)))
bad = 0
for what, n in (("instruction", text), ("data", data)):
    if n > LIM:
        print("== FATAL: %s %s segment is %d bytes, over the 64 KB the Z8001 "
              "gives a process" % (name, what, n))
        bad = 1
sys.exit(bad)
PYEOF
}

rc=0
for prog in bc dc; do
	case "$prog" in
	bc)	SRCS="$T/gram.c $BC/bcmch.c $BC/bcmutil.c $BC/getnum.c \
		      $BC/globals.c $BC/grmact.c $BC/interp.c $BC/lex.c \
		      $BC/main.c $BC/output.c $BC/putnum.c $MP";;
	dc)	SRCS="$DC/dc.c $DC/dcsub.c $DC/undefined.c $DCBC $MP";;
	esac
	if link_one "$prog" $SRCS && fits "$prog"; then
		mv -f "$BIN/.$prog.new" "$BIN/$prog"
		echo "== $prog linked: $(wc -c < "$BIN/$prog") bytes"
	else
		rm -f "$BIN/.$prog.new"
		echo "== $prog: see $LOG"
		rc=1
	fi
done
exit "$rc"
