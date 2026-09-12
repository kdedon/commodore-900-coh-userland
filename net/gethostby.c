/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * gethostby.c -- gethostbyname/gethostbyaddr: the host lookups that can reach
 * the NETWORK.
 *
 * A host name is resolved in this order: a dotted quad, /etc/hosts, and finally
 * the DNS resolver in net/resolv.  The file comes first so that a machine's own
 * names, its peer over the serial link, and localhost answer without a packet;
 * the network is consulted only for what the file does not know.
 *
 * The resolver is asked only when /etc/resolv.conf exists.  Without it res_init()
 * would default to a nameserver at 127.0.0.1, where nothing listens, and every
 * failing lookup would cost the full retry schedule -- a minute of wall clock
 * that reads as a hang.  No file means no nameserver means HOST_NOT_FOUND, at
 * once, which is what this machine did before DNS existed for it.
 *
 * WHY THIS IS A FILE OF ITS OWN.  These two functions are the only callers of
 * net/resolv, and an archive member is the unit `ld' pulls: keeping them apart
 * from netdb.c's getservbyname is what confines the resolver -- 5272 bytes of
 * static query buffers and some 8 KB of text -- to programs that resolve a name.
 * A program that wants only a port number never references either symbol here,
 * so the linker leaves the resolver out with nothing to configure.  Do not put a
 * resolver call anywhere else in the library.
 *
 * The local half is netdb.c's, reached through netdb_priv.h.  The answers point
 * into its single static hostent, so a second call to either file's lookups
 * overwrites the first, exactly as BSD's do.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include "netdb_priv.h"

extern unsigned long inet_addr();

/*
 * The resolver's half of the two host lookups (net/resolv/gethnmadr.c).  Both
 * return a pointer, so both MUST be declared before they are called: K&R would
 * otherwise default them to int and truncate a 32-bit far pointer to 16 bits.
 */
extern struct hostent *_dns_byname();
extern struct hostent *_dns_byaddr();

/* Same file as net/gen/resolv.h's _PATH_RESCONF; <netdb.h> does not name it. */
#define	_PATH_RESCONF	"/etc/resolv.conf"

/*
 * Is there a resolver configuration at all?
 *
 * access(2) rather than a stat buffer: the question is only whether the file is
 * there to be read, and the answer costs one syscall and no stack.
 */
static int have_resolv()
{
	return access(_PATH_RESCONF, 4) == 0;
}

struct hostent *gethostbyname(name)
char *name;
{
	unsigned long a;
	struct hostent *hp;

	h_errno = 0;
	if (name == (char *)0)
	{
		h_errno = HOST_NOT_FOUND;
		return (struct hostent *)0;
	}
	/*
	 * A dotted quad first, and without touching the file.  An address needs
	 * neither database to be understood, and it has to work on a machine
	 * whose /etc/hosts does not exist.
	 */
	if ((a = inet_addr(name)) != (unsigned long)0xFFFFFFFFL)
		return _host_answer(name, a);
	if ((hp = _hosts_lookup(name, (unsigned long)0)) != (struct hostent *)0)
		return hp;
	if (have_resolv())
	{
		/* The resolver sets h_errno itself -- HOST_NOT_FOUND for a
		 * denial, TRY_AGAIN for a timeout -- and the difference is what
		 * a caller decides whether to retry on. */
		h_errno = HOST_NOT_FOUND;
		return _dns_byname(name);
	}
	h_errno = HOST_NOT_FOUND;
	return (struct hostent *)0;
}

struct hostent *gethostbyaddr(addr, len, type)
char *addr;
int len, type;
{
	unsigned long a;
	struct hostent *hp;
	char buf[16];

	h_errno = 0;
	if (len != 4 || type != AF_INET || addr == (char *)0)
	{
		h_errno = HOST_NOT_FOUND;
		return (struct hostent *)0;
	}
	memcpy((char *)&a, addr, 4);
	if ((hp = _hosts_lookup((char *)0, a)) != (struct hostent *)0)
		return hp;
	if (have_resolv())
	{
		hp = _dns_byaddr(addr, len, type);
		if (hp != (struct hostent *)0)
			return hp;
	}
	/* Neither the file nor the network: answer with the address written
	 * out, rather than failing.  A caller asking this question has the
	 * address already and wants something to print. */
	sprintf(buf, "%d.%d.%d.%d", (int)((a >> 24) & 0xFF),
		(int)((a >> 16) & 0xFF), (int)((a >> 8) & 0xFF),
		(int)(a & 0xFF));
	h_errno = 0;			/* the resolver's failure is not ours */
	return _host_answer(buf, a);
}
