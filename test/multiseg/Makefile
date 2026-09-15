# tests/multiseg -- probes for user text spanning more than one 64K segment.
#
# These isolate multi-segment text mapping from everything else about a large
# program: same toolchain flags, no stdio, no BSS unless asked, markers via
# raw write(2).  s2 (2 segments) is the control; s3 (3 segments) calls into
# the third; s3near maps three segments but never calls into the third, which
# separates "mapping" failures from "calling" ones.
#
#	make            build all three probes
#	make stage      copy them into the image staging bin dir
#
# Keep them OUT of the shipped lists (they are 90-160KB test binaries).
include ../../hostbuild/toolchain.mk
CCZ	= $(TC)/ccz
LFLAGS	= -i -L
GEN	= ./gen.py
STAGE	= ../../hostbuild/build/bin

MODS	= bt0.o bt1.o bt2.o bt3.o bt4.o
M2OBJ	= bt0.o bt1.o bt2.o
PROBES	= s2 s3 s3near

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
