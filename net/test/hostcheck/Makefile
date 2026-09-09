# hostcheck/Makefile -- negative controls for the net tests.
#
# The programs in net/test are compiled here VERBATIM by the host cc and
# linked against stand-ins that can be told to misbehave, so that every check
# they make can be shown to FAIL against a broken system as well as pass against
# a working one.  A check that has never been seen to fail is decoration; this
# is where it is made to fail.
#
# Nothing here is built for the C900 and nothing here ships: it is host-only
# scaffolding, the same shape as tests/sbrkzero's simulated non-zeroing kernel.
#
#	make -C net/test/hostcheck check
#
CC	= gcc
CFLAGS	= -std=gnu89 -w -g -I../../include -I. -include hostcompat.h
T	= ..

PROGS	= psipping-h sockprogs devtcp-h peers fdcprogs

all:	$(PROGS)

# --- psipping: a fake IP stack that can be told to answer packets it should
# --- have dropped.
psipping-h:	$(T)/psipping.c psipstub.c
	$(CC) $(CFLAGS) -o psipping-h $(T)/psipping.c psipstub.c

# --- the socket programs: real loopback sockets, with fault injection in the
# --- read/write/accept/getsockname path.
sockprogs:	acceptmany-h ephport-h echoserver-h echoclient-h netdbtest-h \
		chanmax-h rlecho-h udpserver-h udpecho-h udppoll-h \
		repeatclient-h

acceptmany-h:	$(T)/acceptmany.c sockshim.c
	$(CC) $(CFLAGS) -o acceptmany-h $(T)/acceptmany.c sockshim.c

ephport-h:	$(T)/ephport.c sockshim.c
	$(CC) $(CFLAGS) -o ephport-h $(T)/ephport.c sockshim.c

echoserver-h:	$(T)/echoserver.c sockshim.c
	$(CC) $(CFLAGS) -o echoserver-h $(T)/echoserver.c sockshim.c

echoclient-h:	$(T)/echoclient.c sockshim.c
	$(CC) $(CFLAGS) -o echoclient-h $(T)/echoclient.c sockshim.c

netdbtest-h:	$(T)/netdbtest.c sockshim.c netdbstub.c
	$(CC) $(CFLAGS) -DSO_USELOOPBACK=0x0040 -o netdbtest-h $(T)/netdbtest.c sockshim.c netdbstub.c

chanmax-h:	$(T)/chanmax.c sockshim.c
	$(CC) $(CFLAGS) -o chanmax-h $(T)/chanmax.c sockshim.c

rlecho-h:	$(T)/rlecho.c sockshim.c
	$(CC) $(CFLAGS) -o rlecho-h $(T)/rlecho.c sockshim.c

udpserver-h:	$(T)/udpserver.c sockshim.c
	$(CC) $(CFLAGS) -o udpserver-h $(T)/udpserver.c sockshim.c

# Both ends of the datagram round trip live in one process, so these two need
# no peer -- only a shim that can misreport a sender or a readiness.
udpecho-h:	$(T)/udpecho.c sockshim.c
	$(CC) $(CFLAGS) -o udpecho-h $(T)/udpecho.c sockshim.c

udppoll-h:	$(T)/udppoll.c sockshim.c
	$(CC) $(CFLAGS) -o udppoll-h $(T)/udppoll.c sockshim.c

# The service it calls is `peers serve'; nothing in net/test is that end.
# -D__osockaddr_defined keeps glibc's own `struct osockaddr' -- the 4.3BSD
# sockaddr it declares for exactly this protocol -- out of the way of net/talk.h's,
# which repeatclient reads for the ntalk rendezvous and which carries the wire's
# widths rather than this machine's.  Same struct, two declarations, one name.
repeatclient-h:	$(T)/repeatclient.c sockshim.c
	$(CC) $(CFLAGS) -D__osockaddr_defined=1 -o repeatclient-h \
		$(T)/repeatclient.c sockshim.c

# --- devtcp: a fake /dev/tcp that can report local port 0 or echo the wrong
# --- bytes back.
devtcp-h:	$(T)/devtcp.c devtcpstub.c
	$(CC) $(CFLAGS) -o devtcp-h $(T)/devtcp.c devtcpstub.c

# --- fdc: the descriptor policy that sets how many connections the machine
# --- has, compiled from the SHIPPED source and run at COHERENT's NOFILE.
# --- fdc-h is the real one; the other two are the policies it replaced and a
# --- version of it with the reply reserve taken out.
FDC	= ../../inet/coh_fdc.c

fdcprogs:	fdc-h fdcold-h fdcgreedy-h

fdc-h:	fdctest.c $(FDC)
	$(CC) $(CFLAGS) -o fdc-h fdctest.c $(FDC)

fdcold-h:	fdctest.c fdcold.c
	$(CC) $(CFLAGS) -o fdcold-h fdctest.c fdcold.c

# coh_fdc.c verbatim except that its fdc_room() is compiled under another name,
# so fdcgreedy.c's can take its place.
fdcgreedy-h:	fdctest.c fdcgreedy.c $(FDC)
	$(CC) $(CFLAGS) -Dfdc_room=fdc_room_reserved -c -o fdc_greedy.o $(FDC)
	$(CC) $(CFLAGS) -o fdcgreedy-h fdctest.c fdcgreedy.c fdc_greedy.o

check:	all
	sh run.sh

clean:
	rm -f psipping-h devtcp-h *-h *.o

# The other end, for the two programs that ARE the other end.
peers:	peers.c
	$(CC) -std=gnu99 -w -g -o peers peers.c

# The BEFORE picture.  Rebuilds each test as it stood at HEAD and runs it
# against the same broken systems, which is how "this check now discriminates"
# is told from "this check was always there": e.g. the old psipping exits 0
# against a stack that answers a bad-checksum packet and 1 against one that
# correctly ignores it.  Only for the files git still has; chanmax.c and
# rlecho.c are untracked, and their old form ended in an unconditional
# `return 0;'.
baseline:
	@for f in psipping acceptmany ephport devtcp netdbtest echoclient \
		 echoserver udpserver; do \
		git show HEAD:net/test/$$f.c > $$f-old.c 2>/dev/null \
		 || { echo "$$f: not in HEAD, skipped"; continue; }; \
		case $$f in \
		psipping) $(CC) $(CFLAGS) -o $$f-old $$f-old.c psipstub.c ;; \
		devtcp)   $(CC) $(CFLAGS) -o $$f-old $$f-old.c devtcpstub.c ;; \
		netdbtest) $(CC) $(CFLAGS) -DSO_USELOOPBACK=0x0040 -o $$f-old \
			$$f-old.c sockshim.c netdbstub.c ;; \
		*)        $(CC) $(CFLAGS) -o $$f-old $$f-old.c sockshim.c ;; \
		esac; \
		echo "built $$f-old"; \
	done
