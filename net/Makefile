# Makefile -- COHERENT networking (net).
#
# Builds the TCP/IP daemon (inet/inet), the super-server (inetd), the SLIP and
# PPP serial link daemons, the client socket library (libsocket.a), and a socket
# test client.  Traditional make; builds natively on the C900 with cc/as/ar.
# Cross-build on a host with make CC=ccz (see hostbuild/).
#
# inet and inetd are two different programs.  inet/ is the STACK -- the ported
# Minix inet server, which owns every connection this machine has.  inetd is an
# ordinary client of it: it listens on the ports named in /etc/inetd.conf and
# runs a program per connection.
#
# The stack is the ported Minix 2.0.4 inet server; the source is de-ANSI'd K&R,
# so it compiles directly through the MWC cc -- no preprocessing step.

# Declare the suffixes this makefile's rules use.  The cross-build's top-level
# makefile sets --no-builtin-rules (so make's built-in %: %.c cannot compile a
# guest program with the host cc), and that also empties .SUFFIXES -- which
# silently disables the .c.o rule below.  Declaring them here keeps this
# makefile working either way.
.SUFFIXES:
.SUFFIXES: .c .s .o

CC	= cc
AS	= as
AR	= ar
CFLAGS	= -O -Iinclude
LFLAGS	= -i			# separated I/D

# The channel client + raw-syscall stubs that every net client links.
CHAN =	inet_chan.o rawsys.o

# The DNS resolver (resolv/).  It is part of libsocket.a rather than a library
# of its own because gethostby.c's gethostbyname calls into it: the archive every
# net client links has to be able to satisfy its own references.
#
# gethostby.o is the ONLY member that refers to it, and that is what keeps these
# six objects -- 5272 bytes of static query buffers and some 8 KB of text -- out
# of every program that links libsocket for a socket or a port number and never
# resolves a name.  Keep it that way: a resolver call in any other member gives
# the resolver to all of them.
RESOLV = resolv/res_init.o resolv/res_mkquery.o resolv/res_comp.o \
	resolv/res_send.o resolv/res_query.o resolv/gethnmadr.o

all:	inet-daemon inetd slip ppp libsocket.a ifconfig sntp echoclient echoserver udpecho \
	udpserver udppoll netdbtest acceptmany chanmax sockcycle ephport discotime devtcp sntpsrv rlecho \
	talkping dropclient repeatclient fdhog portholder

# TCP/IP daemon (its own directory).  The target is `inet-daemon' and not
# `inet': the product is inet/inet, and a target spelled `inet' names the
# DIRECTORY that holds it, which make would find already existing and never
# descend into.
inet-daemon:
	cd inet; make CC="$(CC)"

# The super-server (inetd.conf: listen, accept, fork, exec).  Linked against
# libsocket like any other client of the stack -- it is not part of it.
#
# talkserv.o is the ntalk(518/udp) service, which inetd answers itself: it used
# to be /etc/talkd, and it is here rather than in cmd/ because the only thing
# that can hold a datagram socket on this system is the process that bound it.
inetd:	inetd.o talkserv.o libsocket.a
	$(CC) $(LFLAGS) -o inetd inetd.o talkserv.o libsocket.a

# SLIP / PPP serial link daemons (bridge a tty to the daemon's psip channel).
slip:	coh_slip.o $(CHAN)
	$(CC) $(LFLAGS) -o slip coh_slip.o $(CHAN)

ppp:	coh_ppp.o $(CHAN)
	$(CC) $(LFLAGS) -o ppp coh_ppp.o $(CHAN)

# The r-commands' shared half: rcmd() sets up a connection from a reserved
# port, iruserok() answers whether the peer may act as a local user.  netlib.h
# has always DECLARED both as library calls; rcmd.c used to sit in cmd/rlogin,
# where the second program to need it could not reach it, and iruserok() had no
# implementation at all.
RCMD =	rcmd.o ruserok.o

# Client socket library (BSD socket() veneer).
#
# gethostby.o comes BEFORE $(RESOLV) and $(RESOLV) before netdb.o, which is the
# order that satisfies the chain in one direction: gethostby.o wants the
# resolver, the resolver wants netdb.o's getservbyname and h_errno, and netdb.o
# wants neither of them back.
libsocket.a:	libsocket.o gethostby.o netdb.o $(CHAN) $(RESOLV) $(RCMD)
	rm -f libsocket.a
	$(AR) r libsocket.a libsocket.o gethostby.o $(RESOLV) netdb.o \
		$(CHAN) $(RCMD)

# Interface configuration: address + default route.  Linked against libsocket
# for inet_addr (which lives there, with the socket veneer); the ioctls go over
# the daemon channel, not through a socket.
ifconfig:	coh_ifconfig.o libsocket.a
	$(CC) $(LFLAGS) -o ifconfig coh_ifconfig.o libsocket.a

# sntp(8) -- set the clock from an RFC 4330 server.  A TOOL, not a probe: with
# read_cmos() returning 0 this machine boots at the epoch every time, so this is
# its only automatic clock until the M58321 driver exists.  Linked against
# libsocket for the socket veneer, gethostbyname and getservbyname.
sntp:	sntp.o libsocket.a
	$(CC) $(LFLAGS) -o sntp sntp.o libsocket.a

# Socket test client, linked against the library.
echoclient:	test/echoclient.o libsocket.a
	$(CC) $(LFLAGS) -o echoclient test/echoclient.o libsocket.a

# Socket test SERVER.  With both ends on one machine the stack routes the
# connection to its own address internally, so TCP can be exercised with no
# serial line, no slip and no host peer -- which is the difference between
# "TCP is wrong" and "the packet never got there".
echoserver:	test/echoserver.o libsocket.a
	$(CC) $(LFLAGS) -o echoserver test/echoserver.o libsocket.a

# UDP round trip with both ends here: no wire, no slip, no peer.  Checks that
# recvfrom() reports the right sender, which is what per-datagram addressing is
# for -- not merely that the bytes arrived.
udpecho:	test/udpecho.o libsocket.a
	$(CC) $(LFLAGS) -o udpecho test/udpecho.o libsocket.a

# The wire version: the guest serves and the HOST initiates, so the end that
# can retransmit is the end that speaks first (UDP retransmits nothing).
udpserver:	test/udpserver.o libsocket.a
	$(CC) $(LFLAGS) -o udpserver test/udpserver.o libsocket.a

# Does poll() on a socket mean what a BSD program expects?  Every select-driven
# network program waits before it reads, and a request/reply channel has nothing
# to wait on until a request has been sent -- so this checks the arming that
# closes that gap, in loopback, where it needs no wire.
udppoll:	test/udppoll.o libsocket.a
	$(CC) $(LFLAGS) -o udppoll test/udppoll.o libsocket.a

# The lookups and socket queries a BSD net program makes before it sends a
# packet: gethostbyname, getservbyname, getsockname, setsockopt.  Touches no
# network, so it runs anywhere.
netdbtest:	test/netdbtest.o libsocket.a
	$(CC) $(LFLAGS) -o netdbtest test/netdbtest.o libsocket.a

# Can a server accept more than ONE connection?  One descriptor is one
# connection in the device model, so this is the whole difference between a
# test client and a daemon -- huntd, telnetd, ftpd.  Forks its own clients and
# loops back internally, so it needs no wire.
acceptmany:	test/acceptmany.o libsocket.a
	$(CC) $(LFLAGS) -o acceptmany test/acceptmany.o libsocket.a

# How many connections can exist at once?  Every one is two FIFOs the daemon
# holds open, so the daemon's own NOFILE -- not the channel table -- is what
# limits a machine to N sockets, and that number decides how many logins
# telnetd can serve.
chanmax:	test/chanmax.o libsocket.a
	$(CC) $(LFLAGS) -o chanmax test/chanmax.o libsocket.a

# Does a socket give its memory back?  libsocket keeps a socket's state on the
# heap, so a closed socket must free its block or a server grows by 624 bytes a
# connection.  Opens and closes repeatedly and watches the break address; needs
# the stack running and no wire.
sockcycle:	test/sockcycle.o libsocket.a
	$(CC) $(LFLAGS) -o sockcycle test/sockcycle.o libsocket.a

# The far end of an rlogin session: answers the rcmd(3) handshake and echoes.
# There is no rlogind here, so without it the client cannot be exercised at all
# -- and what needs exercising is that ONE process carries both directions of a
# session over a channel two processes cannot share.
rlecho:	test/rlecho.o libsocket.a
	$(CC) $(LFLAGS) -o rlecho test/rlecho.o libsocket.a

# Can the STACK choose a server's port, and can the server then find out which?
# bind(0) + listen + getsockname is how every BSD daemon that does not want a
# fixed port publishes itself -- huntd does it twice.
ephport:	test/ephport.o libsocket.a
	$(CC) $(LFLAGS) -o ephport test/ephport.o libsocket.a

# The /dev/tcp shim, exercised the way the Minix net clients use it: open the
# device and drive it with NWIO* ioctls, no socket() call anywhere.  That is the
# whole API telnet, ftp and the resolver need from the transport.
devtcp:	test/devtcp.o libsocket.a
	$(CC) $(LFLAGS) -o devtcp test/devtcp.o libsocket.a

# How long does hunt's driver discovery take on THIS machine?  hunt allows one
# second; the question is whether a datagram can cross to the inet daemon and
# back twice in that time here.
discotime:	test/discotime.o libsocket.a
	$(CC) $(LFLAGS) -o discotime test/discotime.o libsocket.a

# Does the ntalk service answer, and does it REMEMBER?  The invitation table is
# state between datagrams, so this asks for something that was minted while a
# DIFFERENT datagram was being answered -- the one assertion no service started
# per datagram could pass, and the reason talkserv.c is shaped as it is.  Loops
# back internally, so it needs no wire and no second machine.
talkping:	test/talkping.o libsocket.a
	$(CC) $(LFLAGS) -o talkping test/talkping.o libsocket.a

# A client that loses its connections instead of closing them: it holds them
# open long enough for a `ps' to see what was started for them, then _exit(2)s
# with every socket still open.  That is the case every service under the
# switchboard has to end on -- the relay closes the program's standard input,
# and a program that does not act on end of file there is one process per
# dropped connection.
dropclient:	test/dropclient.o libsocket.a
	$(CC) $(LFLAGS) -o dropclient test/dropclient.o libsocket.a

# The other half of the same question: a client that does everything right, one
# caller at a time, each connection closed before the next is opened.  What it
# measures is the listening socket a service is supposed to still have after it
# has served somebody -- a port that answers once and refuses afterwards is
# invisible to any test that connects once.
repeatclient:	test/repeatclient.o libsocket.a
	$(CC) $(LFLAGS) -o repeatclient test/repeatclient.o libsocket.a

# A program run with almost every descriptor already taken: it fills its own
# table, gives back exactly as many as were asked for, and execs the program
# under test into what is left.  Descriptors are per process, so scarcity a
# server has to be measured under can only be INHERITED -- there is no way to
# take one from a running program.  No socket of its own, so no libsocket.
fdhog:	test/fdhog.o
	$(CC) $(LFLAGS) -o fdhog test/fdhog.o

# The something else that holds a port.  Claims it EXCL through /dev/tcp, which
# is the one claim the stack will not put beside another (libsocket's sockets
# are all SHARED and two of those may share a port), holds it, and gives it
# back -- so a server's rebinding can be shown to stop as well as to work.
portholder:	test/portholder.o libsocket.a
	$(CC) $(LFLAGS) -o portholder test/portholder.o libsocket.a

# A fixed-answer SNTP server, so sntp(8) can be checked against a timestamp
# this tree states rather than against whatever a real server says.  Loops back
# internally like udpecho, so it needs no wire.
sntpsrv:	test/sntpsrv.o libsocket.a
	$(CC) $(LFLAGS) -o sntpsrv test/sntpsrv.o libsocket.a

# IP traffic test: injects an ICMP echo on the psip channel and verifies the
# stack's reply.  Needs no serial line, so it works under the emulator.
psipping:	test/psipping.o $(CHAN)
	$(CC) $(LFLAGS) -o psipping test/psipping.o $(CHAN)

# Control-channel handshake probe: walks ichan_open's syscalls one at a time,
# printing errno after each.  Linked exactly as ifconfig is, so the failure it
# reproduces is the one ifconfig hits.
ichanprobe:	test/ichanprobe.o libsocket.a
	$(CC) $(LFLAGS) -o ichanprobe test/ichanprobe.o libsocket.a

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

.s.o:
	$(AS) -o $@ $<

clean:
	rm -f *.o test/*.o resolv/*.o inetd slip ppp ifconfig sntp echoclient echoserver udpecho udpserver udppoll netdbtest acceptmany chanmax sockcycle ephport discotime sntpsrv rlecho dropclient repeatclient fdhog portholder ichanprobe psipping libsocket.a
	cd inet; make clean
