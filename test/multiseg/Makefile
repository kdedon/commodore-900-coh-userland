# tests/multiseg -- probes for user text spanning more than one 64K segment.
#
# These isolate multi-segment text mapping from everything else about a large
# program: same toolchain flags, no stdio, no BSS unless asked, markers via
# raw write(2).  s2 (2 segments) is the control; s3 (3 segments) calls into
# the third; s3near maps three segments but never calls into the third, which
# separates "mapping" failures from "calling" ones.
#
# t3 and t4 are self-checking probes whose text spans three segments: every
# cross-segment route (near call, far call, call back into the first segment,
# a function pointer taken in one segment and called from another) is checked
# against the same computation in main's own module.  Their sources were
# written by bigtext.py and are kept in the tree, each with its licence header,
# so bigtext.py is not part of `make sources'.  Placement is by link order:
# mid.c (fmid, callthru) and c1.c (t4h1) follow filler block A, c2.c (t4h2,
# t4fpsum) follows B, and tail.c (ftail, fback, getfp) and c3.c (t4h3) come
# after C in t4 and before it in t3.  Both end with two BSS checks: `bssv'
# (runtime indexing -- the kernel mapping/clearing proof) then `bssc' (constant
# displacements >= 0x8000 -- the compiler segment-borrow case).  bssc is last:
# a miscompiled displacement targets the wrong hardware segment and dies by
# SIGSEGV rather than by marker.
#
#	make            build all five probes
#	make stage      copy them into the image staging bin dir
#
# Keep them OUT of the shipped lists (they are 100-220KB test binaries);
# testing.list stages t3 and t4.
include ../../hostbuild/toolchain.mk
CCZ	= $(TC)/ccz
LFLAGS	= -i -L
GEN	= ./gen.py
STAGE	= ../../hostbuild/build/bin

MODS	= bt0.o bt1.o bt2.o bt3.o bt4.o
M2OBJ	= bt0.o bt1.o bt2.o
PROBES	= s2 s3 s3near t3 t4

# t3/t4 filler blocks, in link order; D and E are t4's alone.
FA	:= $(patsubst %,g%.o,$(shell seq 0 11))
FB	:= $(patsubst %,g%.o,$(shell seq 12 22))
FC	:= $(patsubst %,g%.o,$(shell seq 23 32))
FD	:= $(patsubst %,g%.o,$(shell seq 33 34))
FE	:= $(patsubst %,g%.o,$(shell seq 35 38))
T3OBJ	= main3.o $(FA) mid.o $(FB) tail.o $(FC)
T4OBJ	= main4.o $(FA) mid.o c1.o $(FB) c2.o $(FC) $(FD) tail.o c3.o $(FE)

all:	$(PROBES)

sources:
	$(GEN) .

# The module sources are GENERATED and not checked in; generate them on
# demand so the directory builds from a fresh checkout.
GENSRC	= m2.c m3.c m3near.c $(MODS:.o=.c)
$(GENSRC) &: $(GEN)
	$(GEN) .

# 2 text segments: ~90K.  The control -- this one works.
s2:	m2.o $(M2OBJ)
	$(CCZ) $(LFLAGS) -o s2 m2.o $(M2OBJ)

# 3 text segments: ~156K, with a call into the third.
s3:	m3.o $(MODS)
	$(CCZ) $(LFLAGS) -o s3 m3.o $(MODS)

# 3 text segments, but main never calls into the third -- separates "mapping
# three segments" from "calling into the third".
s3near:	m3near.o $(MODS)
	$(CCZ) $(LFLAGS) -o s3near m3near.o $(MODS)

t3:	$(T3OBJ)
	$(CCZ) $(LFLAGS) -o t3 $(T3OBJ)

t4:	$(T4OBJ)
	$(CCZ) $(LFLAGS) -o t4 $(T4OBJ)

stage:	$(PROBES)
	cp $(PROBES) $(STAGE)/

# hostbuild/Makefile puts --no-builtin-rules in MAKEFLAGS and a sub-make
# inherits it, which empties .SUFFIXES; naming the two suffixes puts the
# .c.o rule back for this makefile alone.
.SUFFIXES: .c .o

.c.o:
	$(CCZ) -i -c -o $@ $<

clean:
	rm -f *.o $(PROBES)

.PHONY: all sources stage clean
