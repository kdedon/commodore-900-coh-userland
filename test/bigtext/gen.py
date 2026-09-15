#!/usr/bin/env python3
"""gen.py [outdir] -- generate the N-segment text probes t3 and t4.

t3 matches ttycity's size profile: ~190K of text, three hardware segments
(3,4,5) with the third nearly full, data in segment 6.  t4 crosses into a
fourth text segment (3,4,5,6), data in segment 7.  Both are self-checking:
every cross-segment route (near call, far call into the last segment, call
from the last segment back into the first, function pointer taken in one
segment and called from another) computes a value that main compares against
the same computation done by a duplicate function in main's own module.
A probe that merely loads and dies cannot fake a pass.

Markers go out with raw write(2), never stdio: a probe that dies mid-way
must not lose output to an unflushed buffer.

Module placement is by link order; Makefile comments give the intended
segment of each special module.
"""
import os
import sys

PER = 45        # functions per filler module, ~5.5K text each

FUNC = """f%(i)d(a, b)
int a, b;
{
	int t;
	t = a * %(m)d + b;
	t = t ^ (a << 3);
	t = t + (b >> 1);
	t = t - (a & 0x5A5A);
	t = t | (b + %(i)d);
	return t;
}
"""


def filler(fi):
    return "".join(FUNC % {"i": fi * PER + j, "m": ((fi * PER + j) % 97) + 1}
                   for j in range(PER))


# fmid lands in the SECOND text segment; callthru calls a function pointer
# handed to it -- the pointer is taken in another segment.
MID = """fmid(a, b)
int a, b;
{
	int t;
	t = a * 51 + b * 3;
	t = t ^ (b << 2);
	return t - (a & 0x0F0F);
}

callthru(fp, a, b)
int (*fp)();
int a, b;
{
	return (*fp)(a, b);
}
"""

# tail lands in the LAST text segment.  fback calls back into the first
# (f0) and the second (fmid) segments; getfp takes a pointer to a
# second-segment function inside the last segment.
TAIL = """extern int f0();
extern int fmid();

ftail(a, b)
int a, b;
{
	int t;
	t = a * 77 + b * 5;
	t = t ^ (a << 4);
	return t + (b & 0x3333);
}

fback(a, b)
int a, b;
{
	return f0(a, b) + fmid(a, b) + 777;
}

int (*
getfp())()
{
	return fmid;
}
"""

# t4-only chain links: c1 in segment 4, c2 in segment 5, c3 in segment 6.
C1 = """extern int t4h2();

t4h1(a, b)
int a, b;
{
	return t4h2(a, b) + 31;
}
"""

C2 = """extern int f0();
extern int fmid();
extern int t4h3();

static int (*t4tab[3])() = { f0, fmid, t4h3 };

t4h2(a, b)
int a, b;
{
	return t4h3(a, b) + 17;
}

t4fpsum(a, b)
int a, b;
{
	register int i;
	register int s;

	s = 0;
	for (i = 0; i < 3; i++)
		s += (*t4tab[i])(a, b);
	return s;
}
"""

C3 = """extern int f0();

t4h3(a, b)
int a, b;
{
	return f0(a, b) + 7;
}
"""

# Duplicates of f0/fmid/ftail for main's own module, so every cross-segment
# result is compared against the same arithmetic done by a near call.
DUPS = """static
dup0(a, b)
int a, b;
{
	int t;
	t = a * 1 + b;
	t = t ^ (a << 3);
	t = t + (b >> 1);
	t = t - (a & 0x5A5A);
	t = t | (b + 0);
	return t;
}

static
dupmid(a, b)
int a, b;
{
	int t;
	t = a * 51 + b * 3;
	t = t ^ (b << 2);
	return t - (a & 0x0F0F);
}

static
duptail(a, b)
int a, b;
{
	int t;
	t = a * 77 + b * 5;
	t = t ^ (a << 4);
	return t + (b & 0x3333);
}
"""

CHECK = """
static
ok(s, n)
char *s;
int n;
{
	write(1, s, n);
}

static int nfail;

static
bad(s, n)
char *s;
int n;
{
	nfail++;
	write(1, s, n);
}
"""

MAIN3 = """extern int f0();
extern int fmid();
extern int ftail();
extern int fback();
extern int (*getfp())();
extern int callthru();

static int (*sfp)() = ftail;

static char big[40000];

%(dups)s%(check)s
main()
{
	register int x, y;
	register unsigned j;
	int nz;
	int (*fp)();

	write(1, "t3 start\\n", 9);

	x = f0(3, 5);
	if (x == dup0(3, 5))
		ok("t3 near ok\\n", 11);
	else
		bad("t3 FAIL near\\n", 13);

	x = ftail(7, 9);
	if (x == duptail(7, 9))
		ok("t3 far ok\\n", 10);
	else
		bad("t3 FAIL far\\n", 12);

	x = fback(11, 13);
	y = dup0(11, 13) + dupmid(11, 13) + 777;
	if (x == y)
		ok("t3 back ok\\n", 11);
	else
		bad("t3 FAIL back\\n", 13);

	x = callthru(sfp, 6, 8);
	if (x == duptail(6, 8))
		ok("t3 fp1 ok\\n", 10);
	else
		bad("t3 FAIL fp1\\n", 12);

	fp = getfp();
	x = (*fp)(9, 4);
	if (x == dupmid(9, 4))
		ok("t3 fp2 ok\\n", 10);
	else
		bad("t3 FAIL fp2\\n", 12);

	/* bssv: the 40K BSS through runtime indexing only -- clearing,
	   then a write/read at both ends and the middle */
	nz = 0;
	for (j = 0; j < 40000; j += 997)
		if (big[j] != 0)
			nz++;
	j = 0;
	big[j] = 0x5A;
	j = 19999;
	big[j] = 0x3C;
	j = 39999;
	big[j] = 0x69;
	x = 0;
	j = 0;
	if (big[j] == 0x5A) x++;
	j = 19999;
	if (big[j] == 0x3C) x++;
	j = 39999;
	if (big[j] == 0x69) x++;
	if (nz == 0 && x == 3)
		ok("t3 bssv ok\\n", 11);
	else
		bad("t3 FAIL bssv\\n", 13);

	/* bssc: the same cells through CONSTANT displacements past 0x8000.
	   Kept LAST: a miscompiled displacement targets the wrong hardware
	   segment and this dies by SIGSEGV rather than by marker */
	big[39999] = 0x77;
	j = 39999;
	if (big[j] == 0x77 && big[0x9C3F] == 0x77)
		ok("t3 bssc ok\\n", 11);
	else
		bad("t3 FAIL bssc\\n", 13);

	if (nfail == 0) {
		write(1, "t3 PASS\\n", 8);
		return 0;
	}
	return 1;
}
"""

MAIN4 = """extern int f0();
extern int fmid();
extern int ftail();
extern int fback();
extern int (*getfp())();
extern int callthru();
extern int t4h1();
extern int t4h3();
extern int t4fpsum();

static int (*sfp)() = ftail;

static char big[40000];

%(dups)s%(check)s
main()
{
	register int x, y;
	register unsigned j;
	int nz;
	int (*fp)();

	write(1, "t4 start\\n", 9);

	x = f0(3, 5);
	if (x == dup0(3, 5))
		ok("t4 near ok\\n", 11);
	else
		bad("t4 FAIL near\\n", 13);

	x = ftail(7, 9);
	if (x == duptail(7, 9))
		ok("t4 far ok\\n", 10);
	else
		bad("t4 FAIL far\\n", 12);

	x = fback(11, 13);
	y = dup0(11, 13) + dupmid(11, 13) + 777;
	if (x == y)
		ok("t4 back ok\\n", 11);
	else
		bad("t4 FAIL back\\n", 13);

	x = t4h1(5, 12);
	y = dup0(5, 12) + 7 + 17 + 31;
	if (x == y)
		ok("t4 chain ok\\n", 12);
	else
		bad("t4 FAIL chain\\n", 14);

	x = t4fpsum(8, 3);
	y = dup0(8, 3) + dupmid(8, 3) + (dup0(8, 3) + 7);
	if (x == y)
		ok("t4 fptab ok\\n", 12);
	else
		bad("t4 FAIL fptab\\n", 14);

	x = callthru(sfp, 6, 8);
	if (x == duptail(6, 8))
		ok("t4 fp1 ok\\n", 10);
	else
		bad("t4 FAIL fp1\\n", 12);

	fp = getfp();
	x = (*fp)(9, 4);
	if (x == dupmid(9, 4))
		ok("t4 fp2 ok\\n", 10);
	else
		bad("t4 FAIL fp2\\n", 12);

	nz = 0;
	for (j = 0; j < 40000; j += 997)
		if (big[j] != 0)
			nz++;
	j = 0;
	big[j] = 0x5A;
	j = 19999;
	big[j] = 0x3C;
	j = 39999;
	big[j] = 0x69;
	x = 0;
	j = 0;
	if (big[j] == 0x5A) x++;
	j = 19999;
	if (big[j] == 0x3C) x++;
	j = 39999;
	if (big[j] == 0x69) x++;
	if (nz == 0 && x == 3)
		ok("t4 bssv ok\\n", 11);
	else
		bad("t4 FAIL bssv\\n", 13);

	big[39999] = 0x77;
	j = 39999;
	if (big[j] == 0x77 && big[0x9C3F] == 0x77)
		ok("t4 bssc ok\\n", 11);
	else
		bad("t4 FAIL bssc\\n", 13);

	if (nfail == 0) {
		write(1, "t4 PASS\\n", 8);
		return 0;
	}
	return 1;
}
"""


def emit(d):
    for fi in range(40):
        open(os.path.join(d, "g%d.c" % fi), "w").write(filler(fi))
    open(os.path.join(d, "mid.c"), "w").write(MID)
    open(os.path.join(d, "tail.c"), "w").write(TAIL)
    open(os.path.join(d, "c1.c"), "w").write(C1)
    open(os.path.join(d, "c2.c"), "w").write(C2)
    open(os.path.join(d, "c3.c"), "w").write(C3)
    open(os.path.join(d, "main3.c"), "w").write(
        MAIN3 % {"dups": DUPS, "check": CHECK})
    open(os.path.join(d, "main4.c"), "w").write(
        MAIN4 % {"dups": DUPS, "check": CHECK})


if __name__ == "__main__":
    emit(sys.argv[1] if len(sys.argv) > 1 else ".")
