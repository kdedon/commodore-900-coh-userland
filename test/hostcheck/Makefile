# hostcheck/Makefile -- negative controls for the test probes.
#
# The programs in test are compiled here VERBATIM by the host cc and linked
# against kshim.c, a stand-in for the parts of COHERENT they measure that can be
# told to misbehave -- so that every check they make can be SEEN to fail against
# a broken system as well as pass against a working one.  A check that has never
# been seen to fail is decoration; this is where it is made to fail.
#
# Nothing here is built for the C900 and nothing here ships: it is host-only
# scaffolding, modelled on net/test/hostcheck, which asks the same question of
# the network tests.
#
#	make -C tests/hostcheck check		the whole matrix
#	make -C tests/hostcheck check A=sbrkzero	one probe
#
# -fno-builtin matters: tests/strnlong's subject is strncpy/strncmp/strncat, and
# the shim's broken versions of them are only reached if the compiler calls them
# instead of expanding them inline.
CC	= gcc
CFLAGS	= -std=gnu89 -w -g -I. -include hostcompat.h -fno-builtin
LDFLAGS	= -ldl -rdynamic		# -rdynamic: the shim finds tests/swap's `blk' by name
T	= ..

PROGS	= ddtentry-h sbrkzero-h tickrate-h lptest-h strnlong-h \
	  pollhup-h pollfifo-h pollpipe-h pollexit-h polltty-h selectlong-h \
	  openexcl-h kalloc-h termio-h hrglyph-h deepstack-h swap-h pty-h

all:	$(PROGS)

# One rule for all of them: the probe in tests/<name>/<name>.c, compiled with no
# edit of its own, plus the shim.  The prerequisite needs the stem TWICE, which
# only survives a second expansion.
.SECONDEXPANSION:
$(PROGS): %-h: $(T)/$$*/$$*.c kshim.c hostcompat.h termio.h sgtty.h
	$(CC) $(CFLAGS) -o $@ $(T)/$*/$*.c kshim.c $(LDFLAGS)

check:	all
	sh run.sh $(A)

clean:
	rm -f $(PROGS) *.o
	rm -rf work
