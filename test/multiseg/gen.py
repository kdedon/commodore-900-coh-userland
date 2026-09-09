#!/usr/bin/env python3
"""gen.py <outdir> -- generate the multi-segment text probes.

Emits NFILE modules of PER near-identical functions each, sized so that a link
of N modules crosses a chosen number of 64K text segments, plus a main for each
probe that prints a marker before and after each call.  Markers are written with
raw write(2), never printf: a probe that dies mid-way must not lose its output
to an unflushed stdio buffer.

The functions are deliberately trivial-but-not-identical (the multiplier varies
with the index) so no two collapse into one, and so a cross-segment call's
return value can be checked rather than merely observed not to crash.
"""
import os
import sys

PER = 180

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


def module(fi):
    return "".join(FUNC % {"i": fi * PER + j, "m": ((fi * PER + j) % 97) + 1}
                   for j in range(PER))


MAIN = """extern int f0();
%(extern)s
main()
{
	int x, y;

	write(1, "%(tag)s-start\\n", %(n1)d);
	x = f0(3, 5);
	write(1, "%(tag)s-near-ok\\n", %(n2)d);
%(far)s	write(1, "%(tag)s-done\\n", %(n3)d);
	return 0;
}
"""

FAR = """	y = f%(last)d(3, 5);
	write(1, "%(tag)s-far-ok\\n", %(n)d);
	if (x == y)
		write(1, "%(tag)s-SAME\\n", %(ns)d);
	else
		write(1, "%(tag)s-DIFF\\n", %(nd)d);
"""


def main_c(tag, last, far):
    ext = "extern int f%d();\n" % last if far else ""
    body = ""
    if far:
        body = FAR % {"last": last, "tag": tag,
                      "n": len(tag) + 8, "ns": len(tag) + 6,
                      "nd": len(tag) + 6}
    return MAIN % {"extern": ext, "tag": tag, "far": body,
                   "n1": len(tag) + 7, "n2": len(tag) + 9,
                   "n3": len(tag) + 6}


def emit(d):
    for fi in range(10):
        open(os.path.join(d, "bt%d.c" % fi), "w").write(module(fi))
    # 2 segments (works), 3 segments calling far, 3 segments never calling far
    open(os.path.join(d, "m2.c"), "w").write(main_c("S2", 539, True))
    open(os.path.join(d, "m3.c"), "w").write(main_c("S3", 899, True))
    open(os.path.join(d, "m3near.c"), "w").write(main_c("S3N", 899, False))


if __name__ == "__main__":
    emit(sys.argv[1] if len(sys.argv) > 1 else ".")
