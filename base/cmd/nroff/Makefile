# Makefile -- COHERENT nroff (the 3.x troff-class formatter, nroff image only).
#
# Traditional make; builds natively on the C900 with cc.  The troff/fwtable
# images and their font tables are intentionally not built here (they need the
# font/PostScript machinery -- a separate port).  nroff is >64K of text, so it
# links large model (-i -L, the ld options cc passes through).  Cross-build on a
# host: make CC=ccz.

CC	= cc
CFLAGS	= -O -I.
LFLAGS	= -i -L

OBJ =	div.o env.o expr.o extern.o getf.o hyphen.o hyptab.o main.o misc.o \
	process.o reg.o req1.o req2.o request.o table.o tty.o

nroff:	$(OBJ)
	$(CC) $(LFLAGS) -o nroff $(OBJ)

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f nroff $(OBJ)
