/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * netdbtest.c -- the lookups and socket queries a BSD net program does first.
 *
 *	netdbtest
 *
 * gethostbyname, getservbyname, getsockname and setsockopt: the four calls
 * hunt(6) makes before it sends a single packet, and that telnet and ftp will
 * make after it.  None of them touches the network, so this runs anywhere.
 *
 * WHAT IS AND IS NOT UNDER TEST HERE.  Some of these calls are answered
 * entirely inside libsocket.c and never reach the inet daemon, so a check on
 * them tests the shim's own bookkeeping and nothing else.  That is worth having
 * -- the shim is code, and its contract is what every BSD program here is
 * written against -- but it must not be mistaken for evidence about the stack,
 * so those checks are labelled "[shim]" when they print.  setsockopt is the
 * clearest case: libsocket.c has no per-option control to offer, so its
 * setsockopt is a switch statement over a list of names, and checking it
 * exercises exactly that switch.
 *
 * The one query here that only the STACK can answer is the ephemeral port.  A
 * socket bound to port 0 has no port of its own to remember, so a non-zero
 * answer from getsockname() can only have come back over the channel
 * (NWIOGUDPOPT, via udp_learnlocal) -- and if that ioctl fails, libsocket
 * silently keeps the zero it was given, which is exactly the shape the answer
 * must not have.  Those checks are labelled "[stack]".
 *
 * Two of the checks are about being WRONG in the right way:
 *
 *	getsockname(0) must FAIL.  A BSD daemon asks it to find out whether
 *	inetd handed it a connection on its standard input; an answer of any
 *	kind for a plain fd would make every such daemon believe it was
 *	inetd-spawned and start reading a protocol off the terminal.  Here the
 *	honest answer is always "no" -- /etc/inetd relays over a pipe rather
 *	than handing over the socket, and tells a service who it is talking to
 *	through the environment (net/inetd.c) -- so this check is what keeps
 *	the failure a failure.
 *
 *	setsockopt of an option this stack does not have must fail too, rather
 *	than returning 0 and doing nothing -- the caller cannot otherwise tell
 *	"enabled" from "ignored", and for SO_BROADCAST that difference is
 *	whether hunt's discovery reaches anyone.
 *
 * Every line starts with "netdb:" so a scripted run can pick it out.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <errno.h>

extern int errno;
extern char *inet_ntoa();

static int fails;

static void ck(what, ok)
char *what;
int ok;
{
	printf("netdb: %-40s %s\n", what, ok ? "ok" : "FAIL");
	if (!ok)
		fails++;
	fflush(stdout);
}

int main(argc, argv)
int argc;
char **argv;
{
	struct hostent *hp;
	struct servent *sp;
	struct sockaddr_in sin;
	unsigned long a;
	int s, alen, opt;

	/* A dotted quad resolves with no file at all. */
	hp = gethostbyname("10.0.0.1");
	ck("gethostbyname(10.0.0.1)", hp != (struct hostent *)0);
	if (hp)
	{
		memcpy((char *)&a, hp->h_addr, 4);
		printf("netdb:   -> %lu.%lu.%lu.%lu (len %d, af %d)\n",
			(a >> 24) & 0xFF, (a >> 16) & 0xFF, (a >> 8) & 0xFF,
			a & 0xFF, hp->h_length, hp->h_addrtype);
		ck("  address is 10.0.0.1", a == 0x0A000001L);
	}

	/* A name, which needs /etc/hosts. */
	hp = gethostbyname("peer");
	ck("gethostbyname(peer) [/etc/hosts]", hp != (struct hostent *)0);
	if (hp)
	{
		memcpy((char *)&a, hp->h_addr, 4);
		ck("  peer is 10.0.0.1", a == 0x0A000001L);
	}

	hp = gethostbyname("no.such.host.here");
	ck("unknown name fails, not hangs", hp == (struct hostent *)0);

	/* Services: from the file, and by port. */
	sp = getservbyname("telnet", "tcp");
	ck("getservbyname(telnet, tcp)", sp != (struct servent *)0);
	if (sp)
	{
		printf("netdb:   -> port %d proto %s\n", ntohs(sp->s_port),
			sp->s_proto);
		ck("  telnet is port 23", ntohs(sp->s_port) == 23);
	}
	sp = getservbyname("hunt", "udp");
	ck("getservbyname(hunt, udp)", sp != (struct servent *)0);
	if (sp)
		ck("  hunt is port 26740", ntohs(sp->s_port) == 26740);
	sp = getservbyport(htons(7), "udp");
	ck("getservbyport(7, udp)", sp != (struct servent *)0
		&& strcmp(sp->s_name, "echo") == 0);
	sp = getservbyname("nosuchservice", "tcp");
	ck("unknown service fails", sp == (struct servent *)0);

	/* getsockname on a real socket, and on something that is not one. */
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		printf("netdb: socket failed errno %d\n", errno);
		return 1;
	}
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(7021);
	sin.sin_addr.s_addr = 0;
	ck("bind(7021)", bind(s, (struct sockaddr *)&sin, sizeof(sin)) == 0);

	memset((char *)&sin, 0, sizeof(sin));
	alen = sizeof(sin);
	ck("getsockname(socket)",
		getsockname(s, (struct sockaddr *)&sin, &alen) == 0);
	printf("netdb:   -> port %d\n", ntohs(sin.sin_port));
	ck("  reports the bound port [shim]", ntohs(sin.sin_port) == 7021);

	alen = sizeof(sin);
	ck("getsockname(0) FAILS [not inetd] [shim]",
		getsockname(0, (struct sockaddr *)&sin, &alen) < 0);

	/* setsockopt: granted where the stack really grants it.  All four of
	 * these are answered by a switch statement in libsocket.c; none of them
	 * reaches the daemon.  See the header comment. */
	opt = 1;
	ck("setsockopt(SO_BROADCAST) [shim]",
		setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char *)&opt,
			sizeof(opt)) == 0);
	ck("setsockopt(SO_USELOOPBACK) [shim]",
		setsockopt(s, SOL_SOCKET, SO_USELOOPBACK, (char *)&opt,
			sizeof(opt)) == 0);
	ck("setsockopt(unsupported) FAILS [shim]",
		setsockopt(s, SOL_SOCKET, 0x7FFF, (char *)&opt,
			sizeof(opt)) < 0);
	ck("setsockopt(non-socket) FAILS [shim]",
		setsockopt(0, SOL_SOCKET, SO_BROADCAST, (char *)&opt,
			sizeof(opt)) < 0);

	close(s);

	/*
	 * THE EPHEMERAL PORT -- the only query on this page whose answer the
	 * program cannot have supplied itself.
	 *
	 * bind() to port 0 gives libsocket nothing to remember, so a port that
	 * comes back from getsockname() afterwards must have arrived over the
	 * channel.  udp_learnlocal() asks NWIOGUDPOPT for it and keeps the
	 * socket's existing value if that ioctl fails -- so a stack that cannot
	 * answer leaves the zero in place, and zero is precisely what a caller
	 * must never publish: hunt's list of drivers is TERMINATED by a zero
	 * port, and a driver that advertised itself on port 0 made the client
	 * report no game running on a machine that was running one.
	 */
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		printf("netdb: socket failed errno %d\n", errno);
		return 1;
	}
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(0);		/* the stack's choice */
	sin.sin_addr.s_addr = 0;
	ck("bind(port 0) [stack]",
		bind(s, (struct sockaddr *)&sin, sizeof(sin)) == 0);
	memset((char *)&sin, 0, sizeof(sin));
	alen = sizeof(sin);
	ck("getsockname after bind(0) [stack]",
		getsockname(s, (struct sockaddr *)&sin, &alen) == 0);
	printf("netdb:   -> port %u\n", (unsigned)ntohs(sin.sin_port));
	ck("  the stack chose a NON-ZERO port [stack]", sin.sin_port != 0);
	close(s);
	printf("netdb: %s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
