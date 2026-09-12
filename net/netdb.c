/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * netdb.c -- LOCAL host and service lookup for the BSD socket veneer.
 *
 * getservbyname/getservbyport, gethostname/sethostname, and the /etc/hosts half
 * of the host lookups.  It reads files and a built-in table and touches no
 * network, so nothing here refers to the DNS resolver in net/resolv.
 *
 * THAT ABSENCE IS THE POINT, and it is why gethostbyname/gethostbyaddr are in a
 * file of their own (gethostby.c) rather than here beside getservbyname.  An
 * archive member is the unit `ld' pulls, so a member that both looks up a port
 * and calls the resolver gives the resolver -- 5272 bytes of static query
 * buffers and some 8 KB of text -- to inetd, telnetd, fingerd, smtpd and every
 * other program that only ever wanted a port number.  Keep every reference to
 * net/resolv out of this file, and the linker charges the resolver to the
 * programs that resolve and to no others, with nothing for a consumer to
 * configure.
 *
 * `_host_answer' and `_hosts_lookup' are the local half, shared with
 * gethostby.c across that seam; the leading underscore marks them internal to
 * the library.  Both return a pointer and are declared in netdb_priv.h, which
 * any caller must include: K&R defaults an undeclared function to int, and that
 * drops the segment half of a far pointer.
 *
 * Both lookups fall back to a built-in table when the file is missing.  That is
 * not a convenience: a machine whose /etc/services has not been installed would
 * otherwise fail to reach even the well-known ports, and the failure would
 * surface as a connection refused rather than as a missing file.
 *
 * Everything returned points into static storage, as BSD's do -- a second call
 * overwrites the first.  Callers of this vintage expect that.
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
 * h_errno lives HERE and not in gethostby.c, even though only the host lookups
 * set it: the resolver's own files set it too, and it must not be the symbol
 * that drags either of them in.
 */
int h_errno;

/*
 * Split off the next whitespace-separated field, in place.
 *
 * strtok() would do this, but it is not in libc -- it lives in the games
 * link-line shim (games/lib/src) -- and pulling that in would give every
 * program that links both this library and that shim a duplicate symbol.  A
 * library other people link has no business dragging in a string package.
 *
 * `*pp' walks the line; the returned field is NUL-terminated in place.
 */
static char *field(pp)
char **pp;
{
	char *p= *pp, *start;

	while (*p == ' ' || *p == '\t' || *p == '\n')
		p++;
	if (*p == '\0')
	{
		*pp= p;
		return (char *)0;
	}
	start= p;
	while (*p && *p != ' ' && *p != '\t' && *p != '\n')
		p++;
	if (*p)
		*p++= '\0';
	*pp= p;
	return start;
}

/* One alias list, shared: it is always empty, and nothing may write to it. */
static char *no_aliases[1];

/*
 * Hosts.
 */
static struct hostent host_ent;
static char host_name[64];
static unsigned long host_addr;
static char *host_addrs[2];

struct hostent *_host_answer(name, addr)
char *name;
unsigned long addr;
{
	strncpy(host_name, name, sizeof(host_name) - 1);
	host_name[sizeof(host_name) - 1] = '\0';
	host_addr = addr;
	host_addrs[0] = (char *)&host_addr;
	host_addrs[1] = (char *)0;
	host_ent.h_name = host_name;
	host_ent.h_aliases = no_aliases;
	host_ent.h_addrtype = AF_INET;
	host_ent.h_length = 4;
	host_ent.h_addr_list = host_addrs;
	return &host_ent;
}

/*
 * Scan /etc/hosts for `name' (or, if name is null, for `addr').
 *
 *	address		name [aliases...]
 *
 * Aliases ARE matched even though they are not reported: a file that names a
 * host twice should answer to both spellings.
 */
struct hostent *_hosts_lookup(name, addr)
char *name;
unsigned long addr;
{
	FILE *fp;
	char line[256], *cp, *p, *tok, *first;
	unsigned long a;

	if ((fp = fopen(_PATH_HOSTS, "r")) == (FILE *)0)
		return (struct hostent *)0;
	while (fgets(line, sizeof(line), fp) != (char *)0)
	{
		if ((cp = strchr(line, '#')) != (char *)0)
			*cp = '\0';
		p = line;
		if ((tok = field(&p)) == (char *)0)
			continue;
		a = inet_addr(tok);
		if (a == (unsigned long)0xFFFFFFFFL)
			continue;		/* not an address: skip */
		if ((first = field(&p)) == (char *)0)
			continue;		/* an address with no name */
		if (name == (char *)0)
		{
			if (a == addr)
			{
				fclose(fp);
				return _host_answer(first, a);
			}
			continue;
		}
		for (tok = first; tok != (char *)0; tok = field(&p))
		{
			if (strcmp(tok, name) == 0)
			{
				fclose(fp);
				return _host_answer(first, a);
			}
		}
	}
	fclose(fp);
	return (struct hostent *)0;
}

/*
 * gethostname/sethostname -- this machine's own name.
 *
 * COHERENT 3.x has no hostname: no syscall, no uname, nothing in the kernel
 * that knows one.  So it lives in a file, /etc/hostname, which is what the
 * BSDs of this era did before uname(2) spread.  A machine without the file
 * answers "localhost", which is true and resolvable rather than empty.
 */
int gethostname(name, len)
char *name;
int len;
{
	FILE *fp;
	char buf[64], *cp;

	if (name == (char *)0 || len <= 0)
		return -1;
	buf[0] = '\0';
	if ((fp = fopen(_PATH_HOSTNAME, "r")) != (FILE *)0)
	{
		if (fgets(buf, sizeof(buf), fp) == (char *)0)
			buf[0] = '\0';
		fclose(fp);
	}
	for (cp = buf; *cp; cp++)
		if (*cp == '\n' || *cp == ' ' || *cp == '\t')
		{
			*cp = '\0';
			break;
		}
	if (buf[0] == '\0')
		strcpy(buf, "localhost");
	strncpy(name, buf, len - 1);
	name[len - 1] = '\0';
	return 0;
}

int sethostname(name, len)
char *name;
int len;
{
	FILE *fp;

	if (name == (char *)0)
		return -1;
	if ((fp = fopen(_PATH_HOSTNAME, "w")) == (FILE *)0)
		return -1;
	fprintf(fp, "%.*s\n", len, name);
	fclose(fp);
	return 0;
}

/*
 * Services.
 */
static struct servent serv_ent;
static char serv_name[32];
static char serv_proto[8];

/*
 * The fallback table: enough to reach the well-known ports with no
 * /etc/services installed.  `hunt' and `hunt-back' are the game's own, and are
 * here because it looks them up before it will start.  `domain' is the
 * resolver's own port: res_init() asks for it by name before it can send a
 * query at all, so a missing /etc/services would leave the machine unable to
 * resolve anything.
 */
struct servdef {
	char *sd_name;
	int   sd_port;
	char *sd_proto;
};
static struct servdef builtin[] = {
	{ "echo",	7,	"tcp" },
	{ "echo",	7,	"udp" },
	{ "discard",	9,	"tcp" },
	{ "daytime",	13,	"tcp" },
	{ "ftp-data",	20,	"tcp" },
	{ "ftp",	21,	"tcp" },
	{ "telnet",	23,	"tcp" },
	{ "smtp",	25,	"tcp" },
	{ "time",	37,	"tcp" },
	{ "time",	37,	"udp" },
	{ "domain",	53,	"udp" },
	{ "domain",	53,	"tcp" },
	{ "finger",	79,	"tcp" },
	/* sntp(8) asks for this before it can set the clock, and the machine it
	 * runs on boots at the epoch -- so it is exactly the case a built-in
	 * entry is for: a host whose /etc/services was never installed can still
	 * find out what time it is. */
	{ "ntp",	123,	"udp" },
	{ "login",	513,	"tcp" },
	{ "shell",	514,	"tcp" },
	{ "talk",	517,	"udp" },
	{ "ntalk",	518,	"udp" },
	{ "hunt",	26740,	"udp" },
	{ "hunt-back",	26741,	"udp" },
	{ (char *)0,	0,	(char *)0 }
};

static struct servent *serv_answer(name, port, proto)
char *name;
int port;
char *proto;
{
	strncpy(serv_name, name, sizeof(serv_name) - 1);
	serv_name[sizeof(serv_name) - 1] = '\0';
	strncpy(serv_proto, proto ? proto : "tcp", sizeof(serv_proto) - 1);
	serv_proto[sizeof(serv_proto) - 1] = '\0';
	serv_ent.s_name = serv_name;
	serv_ent.s_aliases = no_aliases;
	serv_ent.s_port = htons(port);	/* network order, as BSD returns */
	serv_ent.s_proto = serv_proto;
	return &serv_ent;
}

/*
 * Scan /etc/services.
 *
 *	name	port/proto	[aliases...]
 *
 * A null `proto' matches any, which is what BSD does and what hunt relies on
 * when it asks for "smtp" without naming one.
 */
static struct servent *serv_file(name, port, proto)
char *name;
int port;
char *proto;
{
	FILE *fp;
	char line[256], *cp, *q, *tok, *sname, *pp;
	int p;

	if ((fp = fopen(_PATH_SERVICES, "r")) == (FILE *)0)
		return (struct servent *)0;
	while (fgets(line, sizeof(line), fp) != (char *)0)
	{
		if ((cp = strchr(line, '#')) != (char *)0)
			*cp = '\0';
		q = line;
		if ((sname = field(&q)) == (char *)0)
			continue;
		if ((tok = field(&q)) == (char *)0)
			continue;
		if ((pp = strchr(tok, '/')) == (char *)0)
			continue;		/* not port/proto */
		*pp++ = '\0';
		p = atoi(tok);
		if (proto != (char *)0 && strcmp(pp, proto) != 0)
			continue;
		if (name != (char *)0)
		{
			/* the official name, then the aliases */
			if (strcmp(sname, name) != 0)
			{
				for (tok = field(&q);
				     tok != (char *)0 && strcmp(tok, name) != 0;
				     tok = field(&q))
					;
				if (tok == (char *)0)
					continue;
			}
		}
		else if (p != port)
			continue;
		fclose(fp);
		return serv_answer(sname, p, pp);
	}
	fclose(fp);
	return (struct servent *)0;
}

static struct servent *serv_builtin(name, port, proto)
char *name;
int port;
char *proto;
{
	struct servdef *sd;

	for (sd = builtin; sd->sd_name != (char *)0; sd++)
	{
		if (proto != (char *)0 && strcmp(sd->sd_proto, proto) != 0)
			continue;
		if (name != (char *)0)
		{
			if (strcmp(sd->sd_name, name) == 0)
				return serv_answer(sd->sd_name, sd->sd_port,
					sd->sd_proto);
		}
		else if (sd->sd_port == port)
			return serv_answer(sd->sd_name, sd->sd_port,
				sd->sd_proto);
	}
	return (struct servent *)0;
}

struct servent *getservbyname(name, proto)
char *name, *proto;
{
	struct servent *sp;

	if (name == (char *)0)
		return (struct servent *)0;
	if ((sp = serv_file(name, 0, proto)) != (struct servent *)0)
		return sp;
	return serv_builtin(name, 0, proto);
}

struct servent *getservbyport(port, proto)
int port;
char *proto;
{
	struct servent *sp;

	/* BSD takes this port in NETWORK order; the tables are in host order. */
	port = ntohs(port);
	if ((sp = serv_file((char *)0, port, proto)) != (struct servent *)0)
		return sp;
	return serv_builtin((char *)0, port, proto);
}
