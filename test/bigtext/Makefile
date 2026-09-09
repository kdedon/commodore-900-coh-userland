# tests/bigtext -- self-checking probes for user text spanning N 64K segments.
#
# t3: ~190K of text = hardware segments 3,4,5 with the third heavily used
#     (ttycity's profile: 189,806 B = 2.90 segments), data in segment 6.
# t4: ~226K of text = hardware segments 3,4,5,6, data in segment 7.
#
# Placement is by link order; the intended segment of each special module:
#     mid.c  second text segment (4)     fmid, callthru
#     c1.c   second text segment (4)     t4h1           (t4 only)
#     c2.c   third  text segment (5)     t4h2, t4fpsum  (t4 only)
#     tail.c LAST   text segment         ftail, fback, getfp
#     c3.c   LAST   text segment         t4h3           (t4 only)
# `make check` verifies the placement in the linked symbol tables.
#
# Both probes end with two BSS checks: `bssv' (runtime indexing -- the kernel
# mapping/clearing proof) then `bssc' (constant displacements >= 0x8000 --
# the compiler segment-borrow case).  bssc is last: a miscompiled
# displacement targets the wrong hardware segment and dies by SIGSEGV rather
# than by marker.
#
#	make            build both probes
#	make check      verify segment placement + header layout on the host
#	make stage      copy into the image staging bin dir
#
# Keep these OUT of shipped dist lists -- they are ~200KB test binaries.
include ../../hostbuild/toolchain.mk
include ../../hostbuild/gotools.mk
CCZ	= $(TC)/ccz
LFLAGS	= -i -L
GEN	= ./gen.py
STAGE	= ../../hostbuild/build/bin

FILL	:= $(patsubst %,g%.o,$(shell seq 0 39))

# Filler blocks: A fills out the first text segment, B the second, C the
# third; D (t4 only) carries the link across the fourth segment boundary.
FA	:= $(patsubst %,g%.o,$(shell seq 0 11))
FB	:= $(patsubst %,g%.o,$(shell seq 12 22))
FC	:= $(patsubst %,g%.o,$(shell seq 23 32))
FD	:= $(patsubst %,g%.o,$(shell seq 33 34))
FE	:= $(patsubst %,g%.o,$(shell seq 35 38))

T3OBJ	= main3.o $(FA) mid.o $(FB) tail.o $(FC)
T4OBJ	= main4.o $(FA) mid.o c1.o $(FB) c2.o $(FC) $(FD) tail.o c3.o $(FE)

all:	t3 t4

sources:
	$(GEN) .

t3:	main3.o mid.o tail.o $(FILL)
	$(CCZ) $(LFLAGS) -o t3 $(T3OBJ)

t4:	main4.o mid.o tail.o c1.o c2.o c3.o $(FILL)
	$(CCZ) $(LFLAGS) -o t4 $(T4OBJ)

check:	t3 t4 $(LOUTDIS)
	$(LOUTDIS) -hdr $(CURDIR)/t3
	$(LOUTDIS) -syms $(CURDIR)/t3 | grep -E "(fmid|ftail|fback|getfp|callthru|main)_"
	$(LOUTDIS) -hdr $(CURDIR)/t4
	$(LOUTDIS) -syms $(CURDIR)/t4 | grep -E "(fmid|ftail|fback|getfp|callthru|t4h1|t4h2|t4h3|t4fpsum|main)_"

stage:	t3 t4
	cp t3 t4 $(STAGE)/

# hostbuild/Makefile puts --no-builtin-rules in MAKEFLAGS and a sub-make
# inherits it, which empties .SUFFIXES; naming the two suffixes puts the
# .c.o rule back for this makefile alone.
.SUFFIXES: .c .o

.c.o:
	$(CCZ) -i -c -o $@ $<

clean:
	rm -f *.o t3 t4

.PHONY: all sources check stage clean
