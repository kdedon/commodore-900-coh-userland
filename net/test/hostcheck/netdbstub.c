/*
 * netdbstub.c -- the name and service lookups netdbtest expects, and libsocket's
 * setsockopt, supplied here so netdbtest.c can be run on the build machine.
 *
 * The host's /etc/hosts has no `peer' and its /etc/services has no `hunt', so
 * the lookups are answered from the same table the target's /etc files hold.
 * setsockopt is libsocket.c's switch, reproduced -- which is the point of
 * labelling those checks "[shim]" in netdbtest: what they exercise is a list of
 * names, and it is a list of names here too.
 *
 * h_addr holds the address in the order the TARGET's netdb.c leaves it, which
 * on a big-endian Z8001 is indistinguishable from network order.  On a
 * little-endian host the two differ, and netdbtest reads the four bytes with
 * memcpy and compares them as a number -- so the bytes are laid out the way the
 * target lays them out, or the check would fail here for a reason that has
 * nothing to do with what it is testing.
 *
 * Host-only scaffolding.  Nothing here is compiled for the C900.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifndef SO_USELOOPBACK
#define SO_USELOOPBACK	0x0040
#endif

static unsigned long hostaddr;
static char *hostlist[2];
static struct hostent he;

struct hostent *gethostbyname(name)
const char *name;
{
	unsigned int a, b, c, d;

	if (strcmp(name, "peer") == 0 || strcmp(name, "10.0.0.1") == 0)
		hostaddr = 0x0A000001;
	else if (sscanf(name, "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
		hostaddr = (a << 24) | (b << 16) | (c << 8) | d;
	else
		return (struct hostent *)0;	/* unknown: fail, do not hang */
	hostlist[0] = (char *)&hostaddr;
	hostlist[1] = (char *)0;
	he.h_name = (char *)name;
	he.h_aliases = &hostlist[1];
	he.h_addrtype = AF_INET;
	he.h_length = 4;
	he.h_addr_list = hostlist;
	return &he;
}

/* The entries netdbtest names, from the target's /etc/services. */
static struct { char *nm, *pr; int port; } svc[] = {
	{ "telnet",	"tcp",	23 },
	{ "hunt",	"udp",	26740 },
	{ "echo",	"udp",	7 },
	{ "echo",	"tcp",	7 },
	{ "login",	"tcp",	513 },
	{ (char *)0,	(char *)0, 0 }
};

static struct servent se;

static struct servent *fill(i)
int i;
{
	static char *nolist[1] = { (char *)0 };

	se.s_name = svc[i].nm;
	se.s_aliases = nolist;
	se.s_port = htons(svc[i].port);
	se.s_proto = svc[i].pr;
	return &se;
}

struct servent *getservbyname(name, proto)
const char *name, *proto;
{
	int i;

	for (i = 0; svc[i].nm; i++)
		if (strcmp(svc[i].nm, name) == 0 &&
		    (!proto || strcmp(svc[i].pr, proto) == 0))
			return fill(i);
	return (struct servent *)0;
}

struct servent *getservbyport(port, proto)
int port;
const char *proto;
{
	int i;

	for (i = 0; svc[i].nm; i++)
		if (htons(svc[i].port) == (unsigned short)port &&
		    (!proto || strcmp(svc[i].pr, proto) == 0))
			return fill(i);
	return (struct servent *)0;
}

/* libsocket.c's setsockopt, verbatim in behaviour. */
int setsockopt(int s, int level, int name, const void *val, socklen_t len)
{
	struct sockaddr_in sin;
	socklen_t sl;

	sl = sizeof(sin);
	if (getsockname(s, (struct sockaddr *)&sin, &sl) < 0)
		return -1;			/* not a socket: EBADF/ENOTSOCK */
	if (level != SOL_SOCKET)
		return -1;
	switch (name) {
	case SO_BROADCAST:
	case SO_USELOOPBACK:
	case SO_REUSEADDR:
	case SO_KEEPALIVE:
		return 0;
	}
	return -1;
}
