#!/bin/sh
# build-nawk.sh -- nawk (cmd/nawk), the 1989 Hirabayashi awk clone.
#
# A DIFFERENT PROGRAM FROM awk(1), not a newer version of it.  /bin/awk is the
# 1985 Mark Williams language, whose grammar terminates a rule with a newline
# (tests/awkrules); this is a nawk, so
# `nawk '{n++} END{print n}'' -- refused by awk -- works.  The two are built
# from different sources, ship as different files, and are gated separately.
#
# No yacc: the parser and lexer are hand-written, so unlike build-awk.sh this
# needs no build-hyacc.sh.  It does need libm, because `%' calls fmod().
#
# THE SIZE ASSERTION BELOW IS THE POINT OF THIS SCRIPT.  This program is 90% of
# the 64 KB instruction segment, which is far closer to the wall than anything
# else in the userland, and a compiler change that costs 7% of text turns it
# from "large" into "does not run".  A comment would not catch that.
# Prereqs: build-libc-z8001.sh, build-libm-z8001.sh (libm-z8001.a), the toolchain.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
BIN="$HERE/build/bin"
LOG="$HERE/build/nawk.log"
SRC="$OS/base/cmd/nawk"
LIBM="$TCB/libm-z8001/libm-z8001.a"
mkdir -p "$BIN"; : > "$LOG"

[ -f "$LIBM" ] || { echo "== nawk FAILED: no libm-z8001.a (run build-libm-z8001.sh)"; exit 1; }

if CCZ_VAR=800000020800 "$CCZ" -s -i -L \
     -I "$SRC" -I "$OS/include" -I "$OS/include/sys" \
     -o "$BIN/.nawk.new" \
     "$SRC"/m.c "$SRC"/e.c "$SRC"/n.c "$SRC"/l.c "$SRC"/r.c "$SRC"/v.c \
     "$SRC"/y.c "$SRC"/regexp.c "$SRC"/k.c "$LIBM" >>"$LOG" 2>&1; then
	:
else
	rm -f "$BIN/.nawk.new"
	echo "== nawk FAILED"; grep -v 'Strict\|Warning' "$LOG" | tail -12
	exit 1
fi

# The segment check.  A Z8001 process gets ONE 64 KB segment of instructions and
# ONE of data, so the gate is per segment and not on the file: a 90 KB n.out
# with 40 KB of symbol table would run, and a 60 KB one whose text is 66 KB
# would not.  The header is include/n.out.h -- l_ssize[9], fsize_t longs in
# PDP order from offset 8 -- and INSTRUCTION space is SHRI+PRVI+BSSI while DATA
# is SHRD+PRVD+BSSD; taking text as SHRI alone is right only while the linker
# emits nothing in the other two, which is not something this file should
# assume on the program that is closest to the wall.
#
# The parse is checked against a binary whose sizes were measured
# independently: /bin/awk is 35,252 + 7,340 + 3,412 (SHRI/PRVD/BSSD).
# The gate reads the SIDE name, before the rename: a binary that does not fit is
# not this build's product either, so it must not reach $BIN/nawk at all.  Run
# after the rename it would install the oversized one and then delete a side file
# that was no longer there.
python3 - "$BIN/.nawk.new" <<'PYEOF'
import sys
b = open(sys.argv[1], 'rb').read(48)
u16 = lambda o: b[o] | b[o+1] << 8
pdp = lambda o: (u16(o) << 16) | u16(o+2)
if u16(0) != 0o407:
    sys.exit("nawk: not an l.out (magic %o)" % u16(0))
seg = [pdp(8 + 4*i) for i in range(9)]
text = seg[0] + seg[1] + seg[2]         # SHRI + PRVI + BSSI
data = seg[3] + seg[4] + seg[5]         # SHRD + PRVD + BSSD
LIM = 65536
print("== nawk: text %d/%d (%.1f%%), data %d/%d (%.1f%%), file %d bytes"
      % (text, LIM, 100.0*text/LIM, data, LIM, 100.0*data/LIM,
         len(open(sys.argv[1], 'rb').read())))
bad = 0
for what, n in (("instruction", text), ("data", data)):
    if n > LIM:
        print("== FATAL: %s segment is %d bytes, over the 64 KB the Z8001 "
              "gives a process" % (what, n))
        bad = 1
# The margin is stated because it is small and shrinking it is a decision.  This
# program landed at 59,202 bytes of text, 90.3% full; if a change takes it past
# 95% someone should be told before the build that takes it past 100%.
if not bad and text > LIM * 95 // 100:
    print("== WARNING: text is over 95%% of the segment (%d of %d, %d bytes "
          "left).  It landed at 59202; something has grown it." % (text, LIM, LIM - text))
sys.exit(bad)
PYEOF
[ $? -eq 0 ] || { rm -f "$BIN/.nawk.new"; echo "== nawk FAILED: does not fit"; exit 1; }
mv -f "$BIN/.nawk.new" "$BIN/nawk"
echo "  nawk: OK ($(wc -c < "$BIN/nawk") B)"
