#!/bin/sh
# build-slib.sh - build the Z8001 shared C library (libc.sl) and measure it.
#
# The whole facility in one script: a host slgen, a host ld carrying the
# toolchain's current src/ld, the two csu glue objects, the two slgen passes
# around a link, then a set of commands linked both ways so the saving is a
# measurement rather than an estimate.
#
# Everything is written under $OUT (hostbuild/build/slib) and nothing else -- the
# shared toolchain build directories, hostbuild/build/bin and the staging tree are
# read only here, so a run cannot disturb another workstream's measurements.
# build/ is already gitignored, which is why the output lives there.
#
# Prereq: the host tool chain is already built (build-as.sh, build-ld.sh,
# build-z8001.sh, build-cc2-z8001.sh, build-libc-z8001.sh) -- this script builds
# its own slgen and its own ld but uses the existing as, cc passes and libc
# objects where they stand.
#
# Usage: build-slib.sh [-m] [-t]
#	-m   also link the whole cmd/*.c sweep both ways (the /bin projection)
#	-t   also build the on-target execution tests
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
OS="$HERE/.."
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
BE="$TC"	# host tool chain
OUT="$HERE/build/slib"			# its own directory under the (gitignored) build tree
LIBCOBJ="$BE/build/libc-z8001/obj"
AS="$BE/build/as-z8001"

sweep=0; tests=0
for a in "$@"; do
	case "$a" in
	-m) sweep=1;;
	-t) tests=1;;
	*) echo "usage: build-slib.sh [-m] [-t]" >&2; exit 2;;
	esac
done

for f in "$AS" "$LIBCOBJ/printf.o" "$BE/build/z8001/cc0-z8001"; do
	[ -e "$f" ] || { echo "build-slib: missing $f -- build the host tool chain first" >&2; exit 1; }
done

rm -rf "$OUT"
mkdir -p "$OUT/ld" "$OUT/slgen" "$OUT/obj" "$OUT/lib" "$OUT/bin-static" "$OUT/bin-shared"
LD="$OUT/ld-z8001"
SLGEN="$OUT/slgen-host"

# ---------------------------------------------------------------------------
# 1.  A private host ld, built from the toolchain's src/ld as it stands.
#
# The shared build/ld-z8001 is whatever the last build-ld.sh run produced; the
# library and the measurements have to come from the current source of record.
# The host-port shims are the same set build-ld.sh applies, kept in step with it.
# ---------------------------------------------------------------------------
cp "$C900_TOOLCHAIN"/src/ld/*.c "$C900_TOOLCHAIN"/src/ld/*.h "$OUT/ld"/
for h in canon.h mtype.h ar.h; do cp "$OS/include/$h" "$OUT/ld"/; done
mkdir -p "$OUT/ld/sys"; cp "$OS/include/sys/dir.h" "$OUT/ld/sys"/
cp "$BE/build/as/canon.c" "$BE/build/as/n.out.h" "$OUT/ld"/	# exact-width, PDP-canonical

python3 - "$OUT/ld/ar.h" <<'PY'
import sys
f = sys.argv[1]; s = open(f).read()
for old, new in (("time_t\tar_date", "int\tar_date"),
                 ("fsize_t\tar_size", "unsigned int\tar_size"),
                 ("fsize_t\tar_off", "unsigned int\tar_off")):
    assert s.count(old) == 1, old
    s = s.replace(old, new)
open(f, "w").write("#pragma pack(2)\n" + s.rstrip() + "\n#pragma pack()\n")
PY
cat > "$OUT/ld/stat.h" <<'EOF'
#include <sys/stat.h>
EOF
cat > "$OUT/ld/types.h" <<'EOF'
#ifndef TYPES_H
#define TYPES_H TYPES_H
#include <sys/types.h>
typedef unsigned saddr_t; typedef unsigned long vaddr_t; typedef long ctime_t;
#endif
EOF
sed -i 's/^FILE\t\*setoutput();/static FILE *setoutput();/' "$OUT/ld/main.c"
sed -i 's/%D/%ld/g' "$OUT/ld"/pass1.c "$OUT/ld"/pass2.c "$OUT/ld"/main.c
sed -i 's/union {lds_t; ars_t;} \*ldsp;/lds_t *ldsp;/' "$OUT/ld/pass1.c"
sed -i 's/register size_t \*sp, \*ep;/register unsigned int *sp, *ep;/' "$OUT/ld/pass1.c"
sed -i 's/^void\tmessage(), fatal.*/void message(char*,...),fatal(char*,...),usage(char*,...),filemsg(char*,char*,...),modmsg(char*,char*,char*,...),mpmsg(mod_t*,char*,...),spmsg(sym_t*,char*,...);/' "$OUT/ld/data.h"
cp "$BE/build/ld/message.c" "$OUT/ld/message.c"		# the stdarg host replacement
cd "$OUT/ld"
gcc -std=gnu89 -w -DBREADBOX=0 -DZ8001 -c -I. all.c
gcc -std=gnu89 -w -DBREADBOX=0 -DZ8001 -c -I. canon.c
gcc -std=gnu89 -w -DBREADBOX=0 -o "$LD" all.o canon.o
cd "$HERE"
echo "ld-z8001:  $(wc -c < "$LD") B (from $C900_TOOLCHAIN/src/ld)"

# ---------------------------------------------------------------------------
# 2.  A host slgen.
#
# Two shims only: the exact-width n.out.h/canon.c from the as build (so the
# on-file l.out structs keep their native sizes), and slmsg(), which uses MWC's
# recursive %r varargs format that glibc has no equivalent for.
# ---------------------------------------------------------------------------
cp "$OS/base/cmd/slgen/slgen.c" "$OUT/slgen"/
cp "$OS/include/canon.h" "$OS/include/mtype.h" "$OUT/slgen"/
cp "$BE/build/as/canon.c" "$BE/build/as/n.out.h" "$OUT/slgen"/
cp "$OUT/ld/types.h" "$OUT/slgen"/		# n.out.h wants the Coherent address types
python3 - "$OUT/slgen/slgen.c" <<'PY'
import sys
f = sys.argv[1]; s = open(f).read()
old = """/* VARARGS 1 */
slmsg(x)
{
	fprintf(stderr, "slgen: %r\\n", &x);
}"""
new = """slmsg(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("slgen: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\\n', stderr);
	va_end(ap);
}"""
assert s.count(old) == 1, "slmsg() not found in the expected MWC form"
s = s.replace(old, new)
assert s.count("#include <canon.h>") == 1
s = s.replace("#include <canon.h>",
              "#include <canon.h>\n#include <stdarg.h>\nvoid slmsg(char *, ...);")
open(f, "w").write(s)
PY
cd "$OUT/slgen"
gcc -std=gnu89 -w -DZ8001 -I. -o "$SLGEN" slgen.c canon.c
cd "$HERE"
echo "slgen:     $(wc -c < "$SLGEN") B (host)"

# slgen is a deliverable that has to run ON the machine, so it is also built
# with our own compiler every time.  A host build that works while the native
# one does not is a regression, not a convenience.
"$BE/ccz" -s -i -L -o "$OUT/slgen-native" "$OS/base/cmd/slgen/slgen.c" > "$OUT/slgen/native.log" 2>&1 \
	|| { cat "$OUT/slgen/native.log" >&2
	     echo "build-slib: slgen does not build natively" >&2; exit 1; }
echo "slgen:     $(wc -c < "$OUT/slgen-native") B (native, Z8001)"

# ---------------------------------------------------------------------------
# 3.  The run-time glue.  slrt.s is linked INTO the library; crt0sl.s is the
#     client start-off that replaces crt0.o.
# ---------------------------------------------------------------------------
"$AS" -o "$OUT/lib/slrt.o"   "$OS/csu/slrt.s"
"$AS" -o "$OUT/lib/crt0sl.o" "$OS/csu/crt0sl.s"

# ---------------------------------------------------------------------------
# 4.  The library.
#
# Excluded members, all pre-existing archive facts rather than shared-library
# problems: _prof.o refers to monitor_/etext_, which do not
# exist here; strrchr.o and _finish.o are each a duplicate definition that an
# archive link resolves by scan order and a whole-archive link cannot.
# ---------------------------------------------------------------------------
members=$(ls "$LIBCOBJ"/*.o | grep -vE '/(_prof|strrchr|_finish)\.o$')
nmem=$(echo "$members" | wc -w)

"$SLGEN" -1 -v "$OUT/lib/jmptab.o" "$OUT/lib/slrt.o" $members > "$OUT/lib/pass1.log"
ncent=$(sed -n 's/^Number of code entries: *//p' "$OUT/lib/pass1.log")

# jmptab.o MUST be first: pass 2 fills the table at the start of L_SHRI.
# The link must be SILENT -- an undefined symbol leaves ld's dcomm clear, and
# then end_/etext_/edata_ and every common relocate to zero.
if ! "$LD" -n -R 0x01000000 -o "$OUT/lib/libc.sl" \
	"$OUT/lib/jmptab.o" "$OUT/lib/slrt.o" $members 2> "$OUT/lib/link.log"; then
	cat "$OUT/lib/link.log" >&2; echo "build-slib: library link FAILED" >&2; exit 1
fi
if [ -s "$OUT/lib/link.log" ]; then
	cat "$OUT/lib/link.log" >&2
	echo "build-slib: library link was not silent -- see above" >&2; exit 1
fi
"$SLGEN" -2 -v "$OUT/lib/libc.sl" > "$OUT/lib/pass2.log"

SLSZ=$(wc -c < "$OUT/lib/libc.sl")
echo "libc.sl:   $SLSZ B, $ncent code entries from $nmem members"

# Static verification of the library: every jump slot a well-formed long-DA
# `jp' into library text, every export naming exactly one slot, no two exports
# agreeing in the first 16 characters (all ld keeps of an external name), the
# loader's own etext_/edata_/end_ not exported, and each segment inside 64 KB.
cat > "$OUT/slcheck.py" <<'SLCHECK_EOF'
#
# slcheck -- static verification of a shared library and of a client linked
# against it.  Everything here can be decided from the two l.out files, so it
# runs on the host and gates the build; what it cannot decide is whether the
# kernel maps the segments, which is what the on-target tests (-t) are for.
#
#	slcheck.py lib  libc.sl
#	slcheck.py client libc.sl prog
#
import sys

L_SHRI, L_PRVI, L_BSSI, L_SHRD, L_PRVD, L_BSSD, L_DEBUG, L_SYM, L_REL = range(9)
L_ABS, L_REF = 9, 10
GLOBAL = 0o20				# n.out.h L_GLOBAL
CODE = (L_SHRI, L_PRVI, L_BSSI)
LF_SLREF, LF_SLIB = 0o40, 0o100
SYMSZ = 22				# ls_id[16] + short ls_type + long ls_addr
JSLOT = 6				# a long-DA `jp' on the Z8001

# On-file fields are canonical: 16-bit little-endian, 32-bit PDP word-swapped.
class Lout:
	def __init__(self, path):
		self.b = b = open(path, 'rb').read()
		self.path = path
		self.sh = lambda o: b[o] | b[o+1] << 8
		self.ln = lambda o: (b[o] | b[o+1] << 8) << 16 | (b[o+2] | b[o+3] << 8)
		if self.sh(0) != 0o407:
			raise SystemExit("%s: not an l.out (magic 0%o)" % (path, self.sh(0)))
		self.flag = self.sh(2)
		self.tbase = self.sh(6)
		self.sz = [self.ln(8 + 4*i) for i in range(9)]
		off = self.tbase + sum(s for i, s in enumerate(self.sz[:L_SYM])
				       if i not in (L_BSSI, L_BSSD))
		self.syms = []
		for i in range(self.sz[L_SYM] // SYMSZ):
			e = off + i*SYMSZ
			self.syms.append((b[e:e+16].split(b'\0')[0].decode('latin1'),
					  self.sh(e+16), self.ln(e+18)))

	def jtable(self):		# size word, slot count
		js = (self.b[self.tbase] << 8) | self.b[self.tbase+1]
		return js, (js - 2) // JSLOT


def checklib(lib):
	fail = []
	jsize, nslot = lib.jtable()
	print("jump table: %d B, %d slots at seg 1 offsets 0x%04x..0x%04x"
	      % (jsize, nslot, 2, jsize))
	if not lib.flag & LF_SLIB:
		fail.append("l_flag 0%o has no LF_SLIB -- slgen -2 did not run" % lib.flag)

	# Every slot must be a long-DA `jp' whose target is library text past
	# the table.  A stale or half-written slot is the failure that would
	# only show up as a wild jump on the target.
	for k in range(nslot):
		o = lib.tbase + 2 + k*JSLOT
		if lib.b[o:o+4] != b'\x5e\x08\x81\x00':
			fail.append("slot %d is not `jp <seg 1 DA>': %s"
				    % (k, lib.b[o:o+JSLOT].hex()))
		tgt = (lib.b[o+4] << 8) | lib.b[o+5]
		if not jsize <= tgt < lib.sz[L_SHRI]:
			fail.append("slot %d targets 0x%04x, outside the library text"
				    % (k, tgt))

	# Every exported code symbol must name one slot, and no two the same.
	slots = {}
	exported = [(n, a) for n, t, a in lib.syms
		    if t & GLOBAL and (t & ~GLOBAL) in CODE]
	for n, a in exported:
		if not 0x01000002 <= a < 0x01000000 + jsize:
			fail.append("exported %s at 0x%08x is outside the jump table" % (n, a))
		elif (a - 0x01000002) % JSLOT:
			fail.append("exported %s at 0x%08x is not on a slot boundary" % (n, a))
		slots.setdefault(a, []).append(n)
	for a, ns in sorted(slots.items()):
		if len(ns) > 1:
			fail.append("slot 0x%08x shared by %s" % (a, ns))
	print("exported code entries: %d, in %d distinct slots of %d"
	      % (len(exported), len(slots), nslot))

	# ld keeps 16 characters of an external name.  Two exports that agree
	# in the first 16 would silently share a slot.
	trunc = {}
	for n, t, a in lib.syms:
		if t & GLOBAL:
			trunc.setdefault(n[:16], set()).add(n)
	coll = {k: v for k, v in trunc.items() if len(v) > 1}
	for k, v in coll.items():
		fail.append("16-character collision on `%s': %s" % (k, sorted(v)))
	print("exported names: %d, 16-character collisions: %d" % (len(trunc), len(coll)))

	# The loader's per-program symbols describe THIS link and no other.
	for n, t, a in lib.syms:
		if n in ("etext_", "edata_", "end_") and t & GLOBAL:
			fail.append("%s is exported -- a client would take the "
				    "library's break for its own" % n)

	# Each of the two segments must fit one hardware segment.
	shared = lib.sz[L_SHRI] + lib.sz[L_SHRD]
	private = lib.sz[L_PRVI] + lib.sz[L_BSSI] + lib.sz[L_PRVD] + lib.sz[L_BSSD]
	for what, n in (("shared (seg 1)", shared), ("private (seg 2)", private)):
		if n > 0x10000:
			fail.append("%s segment is %d B, over the 64 KB hardware segment"
				    % (what, n))
	print("segments: shared %d B, private %d B, both under 65536" % (shared, private))
	return fail


def checkclient(lib, cl):
	fail = []
	jsize, nslot = lib.jtable()
	if not cl.flag & LF_SLREF:
		fail.append("l_flag 0%o has no LF_SLREF -- ld did not see the library"
			    % cl.flag)
	nlib = 0
	seen = {}
	for n, t, a in cl.syms:
		seg = a >> 24
		if t & GLOBAL and (t & ~GLOBAL) == L_REF and a == 0:
			fail.append("%s is still undefined" % n)
		if seg == 1:			# reaches the library's shared segment
			nlib += 1
			if not 0x01000002 <= a < 0x01000000 + jsize:
				fail.append("%s at 0x%08x is in segment 1 but outside "
					    "the jump table" % (n, a))
			elif (a - 0x01000002) % JSLOT:
				fail.append("%s at 0x%08x is not on a slot boundary" % (n, a))
		if n in ("etext_", "edata_", "end_"):
			seen[n] = a
			if seg == 2:
				fail.append("%s resolved to 0x%08x, the LIBRARY's -- "
					    "the client must define its own" % (n, a))
	if "end_" not in seen:
		fail.append("end_ is not defined in the client")
	print("%s: %d symbols reach the jump table; end_ = 0x%08x (own segment %d)"
	      % (cl.path.split('/')[-1], nlib, seen.get("end_", 0),
		 seen.get("end_", 0) >> 24))
	return fail


def main():
	mode = sys.argv[1]
	lib = Lout(sys.argv[2])
	if mode == "lib":
		fail = checklib(lib)
	else:
		fail = checkclient(lib, Lout(sys.argv[3]))
	if fail:
		print("FAILED:")
		for m in fail:
			print("  " + m)
		sys.exit(1)
	print("slcheck: ok")


main()
SLCHECK_EOF
python3 "$OUT/slcheck.py" lib "$OUT/lib/libc.sl"
echo ""

# ---------------------------------------------------------------------------
# 5.  Clients.  Link a command both ways and report text+data.
#
# `libc.sl' goes LAST, like an archive, and the static archive is kept after it
# so anything the library does not export still resolves.
# ---------------------------------------------------------------------------
CC0="$BE/build/z8001/cc0-z8001"; CC1="$BE/build/z8001/cc1-z8001"; CC2="$BE/build/z8001/cc2-z8001"
VAR="${CCZ_VAR:-800000020800}"

compile() {			# compile() src obj
	s="$1"; o="$2"; b=$(basename "$s" .c)
	c900_buildlog "$s"
	"$CC0" $VAR "$s" "$OUT/obj/$b.z0" 2>/dev/null
	"$CC1" $VAR "$OUT/obj/$b.z0" "$OUT/obj/$b.z1" 2>/dev/null
	"$CC2" 0010 "$OUT/obj/$b.z1" "$o" "$OUT/obj/$b.scr" 0 2>/dev/null
}

linkboth() {			# linkboth() name obj...
	lbn="$1"; shift
	"$LD" -n -i -s -o "$OUT/bin-static/$lbn" "$BE/build/libc-z8001/crt0.o" "$@" \
		"$BE/build/libc-z8001/libc-z8001.a" 2>/dev/null || return 1
	"$LD" -n -i -s -o "$OUT/bin-shared/$lbn" "$OUT/lib/crt0sl.o" "$@" \
		"$OUT/lib/libc.sl" "$BE/build/libc-z8001/libc-z8001.a" 2>/dev/null || return 1
	return 0
}

echo ""
echo "command          static   shared    saved"
tot_s=0; tot_h=0; n=0
if [ "$sweep" = 1 ]; then
	set -- $(ls "$OS"/base/cmd/*.c)
else
	set -- "$OS/base/cmd/sync.c" "$OS/base/cmd/echo.c" "$OS/base/cmd/cat.c" "$OS/base/cmd/wc.c" "$OS/base/cmd/date.c" "$OS/base/cmd/ls.c"
fi
for src in "$@"; do
	b=$(basename "$src" .c)
	compile "$src" "$OUT/obj/$b.o" 2>/dev/null || continue
	linkboth "$b" "$OUT/obj/$b.o" || continue
	s=$(wc -c < "$OUT/bin-static/$b"); h=$(wc -c < "$OUT/bin-shared/$b")
	tot_s=$((tot_s+s)); tot_h=$((tot_h+h)); n=$((n+1)); vfy="$b"
	[ "$sweep" = 1 ] || printf "%-14s %7d  %7d  %7d\n" "$b" "$s" "$h" "$((s-h))"
done
saved=$((tot_s-tot_h))
echo ""
# One client verified against the library: LF_SLREF set, every libc symbol
# landing on a jump slot and nowhere else in segment 1, and end_ resolved to
# the CLIENT's bss rather than the library's.  Unstripped, so it has symbols.
"$LD" -n -i -o "$OUT/bin-shared/verify.dbg" "$OUT/lib/crt0sl.o" "$OUT/obj/$vfy.o" \
	"$OUT/lib/libc.sl" "$BE/build/libc-z8001/libc-z8001.a"
python3 "$OUT/slcheck.py" client "$OUT/lib/libc.sl" "$OUT/bin-shared/verify.dbg"

echo ""
echo "$n commands: $tot_s B static -> $tot_h B shared, saved $saved B"
echo "less the $SLSZ B library: net $((saved-SLSZ)) B"
[ "$n" -gt 0 ] && echo "mean saving per binary: $((saved/n)) B"

# ---------------------------------------------------------------------------
# 6.  Execution tests -- built here, run on the target.
# ---------------------------------------------------------------------------
if [ "$tests" = 1 ]; then
	mkdir -p "$OUT/src"
	# sltest -- everything a client depends on the library for, in order of
	# what each one proves:
	#   printf   the library's stdio, and its private data copy
	#   getenv   environ_, which crt0sl.s stored into LIBRARY data
	#   malloc   __end_, likewise, and sbrk against THIS program's break
	#   strcpy   an assembler member, reached through the same jump table
	#   errno    the absolute at 0:0xFFFE, shared by construction
	cat > "$OUT/src/sltest.c" <<'CEOF'
#include <stdio.h>

extern	char	*malloc(), *getenv(), *strcpy();
extern	int	errno;

main(argc, argv)
int argc;
char *argv[];
{
	char *p;
	char *h;
	int fd;

	printf("sltest: argc=%d\n", argc);
	h = getenv("HOME");
	printf("sltest: HOME=%s\n", h==(char *)0 ? "(unset)" : h);
	if ((p = malloc(300)) == (char *)0) {
		printf("sltest: malloc FAILED\n");
		return (1);
	}
	strcpy(p, "malloc+strcpy through the jump table");
	printf("sltest: %s\n", p);
	printf("sltest: brk moved to %lx\n", (long)p);
	errno = 0;
	if ((fd = open("/no/such/file", 0)) >= 0)
		printf("sltest: open of a missing file SUCCEEDED?\n");
	else
		printf("sltest: errno=%d (expect 2)\n", errno);
	printf("sltest: ok\n");
	return (0);
}
CEOF
	# slfork -- the case most likely to fail: segdup() must refcount the
	# shared segment and COPY the private one, so parent and child must not
	# see each other's libc data.  Each half mallocs again and prints; the
	# two blocks must differ, and both halves must keep working stdio.
	cat > "$OUT/src/slfork.c" <<'CEOF'
#include <stdio.h>

extern	char	*malloc();

main()
{
	char *a, *b;
	int pid;
	int st;

	if ((a = malloc(200)) == (char *)0) {
		printf("slfork: first malloc FAILED\n");
		return (1);
	}
	printf("slfork: parent pre-fork block %lx\n", (long)a);
	fflush(stdout);
	if ((pid = fork()) == 0) {
		b = malloc(200);
		printf("slfork: child  block %lx (must differ from the parent's)\n",
			(long)b);
		fflush(stdout);
		_exit(0);
	}
	if (pid < 0) {
		printf("slfork: fork FAILED\n");
		return (1);
	}
	b = malloc(200);
	printf("slfork: parent block %lx\n", (long)b);
	wait(&st);
	printf("slfork: child status %x, ok\n", st);
	return (0);
}
CEOF
	for t in sltest slfork; do
		compile "$OUT/src/$t.c" "$OUT/obj/$t.o"
		linkboth "$t" "$OUT/obj/$t.o"
		printf "%-8s static %6d  shared %6d\n" "$t" \
			"$(wc -c < "$OUT/bin-static/$t")" "$(wc -c < "$OUT/bin-shared/$t")"
	done
fi

echo ""
echo "build-slib: output in $OUT"
