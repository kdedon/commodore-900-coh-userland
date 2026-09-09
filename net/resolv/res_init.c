/*-
 * Copyright (c) 1985, 1989 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted provided
 * that: (1) source distributions retain this entire copyright notice and
 * comment, and (2) distributions including binaries display the following
 * acknowledgement:  ``This product includes software developed by the
 * University of California, Berkeley and its contributors'' in the
 * documentation or other materials provided with the distribution and in
 * all advertising materials mentioning features or use of this software.
 * Neither the name of the University nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	@(#)res_init.c	6.14 (Berkeley) 6/27/90
 */

/*
 * res_init.c -- the resolver's state, and /etc/resolv.conf.
 *
 * _res holds everything the query path needs: the nameserver list, the retry
 * count and interval, the option flags and the domain search list.  res_init()
 * fills it in from defaults, then from the file, and every res_* entry point
 * calls it once (RES_INIT in _res.options is the "done" flag).
 *
 * /etc/resolv.conf lines understood:
 *	domain NAME		default domain, searched alone
 *	search NAME...		explicit search list, up to MAXDNSRCH entries
 *	nameserver A.B.C.D	up to MAXNS of them, tried in order
 * $LOCALDOMAIN overrides `domain'/`search'.  With no nameserver line the
 * server is 127.0.0.1, which fails fast rather than hanging on a wire.
 *
 * The nameserver port comes from getservbyname("domain") and is kept in
 * network order, ready for the sockaddr res_send() builds.
 */
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ansi.h>
#include <net/hton.h>
#include <net/gen/in.h>
#include <net/gen/inet.h>
#include <net/gen/nameser.h>
#include <net/gen/netdb.h>
#include <net/gen/resolv.h>

#define index(s,c) strchr(s,c)

extern int gethostname();

/*
 * Resolver state
 */
struct state _res;

/*
 * Set up default settings.  If the configuration file exist, the values
 * there will have precedence.  Otherwise, the server address is set to
 * 127.0.0.1 (localhost) and the default domain name comes from gethostname().
 *
 * The configuration file should only be used if you want to redefine your
 * domain or run without a server on your machine.
 *
 * Return 0 if completes successfully, -1 on error
 */
int
res_init()
{
	register FILE *fp;
	register char *cp, **pp;
	char *ep;
	register int n;
	static char buf[BUFSIZ];
	int haveenv = 0;
	int havesearch = 0;
	struct servent *servent;
	u16_t nameserver_port;

	/* Resolver state default settings */
	_res.retrans = RES_TIMEOUT;	/* retransmition time interval */
	_res.retry = 4;			/* number of times to retransmit */
	_res.options = RES_DEFAULT;	/* options flags */
	_res.nscount = 0;		/* number of name servers */
	_res.defdname[0] = 0;		/* domain */

	servent= getservbyname("domain", (char *)0);
	if (!servent)
	{
		h_errno= NO_RECOVERY;
		return -1;
	}
	nameserver_port= servent->s_port;

	/* Allow user to override the local domain definition */
	if ((cp = getenv("LOCALDOMAIN")) != (char *)0) {
		(void)strncpy(_res.defdname, cp, sizeof(_res.defdname));
		haveenv++;
	}

	if ((fp = fopen(_PATH_RESCONF, "r")) != (FILE *)0) {
	    /* read the config file */
	    while (fgets(buf, sizeof(buf), fp) != (char *)0) {
		/* read default domain name */
		if (!strncmp(buf, "domain", sizeof("domain") - 1)) {
		    if (haveenv)	/* skip if have from environ */
			    continue;
		    cp = buf + sizeof("domain") - 1;
		    while (*cp == ' ' || *cp == '\t')
			    cp++;
		    if ((*cp == '\0') || (*cp == '\n'))
			    continue;
		    (void)strncpy(_res.defdname, cp, sizeof(_res.defdname) - 1);
		    if ((cp = index(_res.defdname, '\n')) != (char *)0)
			    *cp = '\0';
		    havesearch = 0;
		    continue;
		}
		/* set search list */
		if (!strncmp(buf, "search", sizeof("search") - 1)) {
		    if (haveenv)	/* skip if have from environ */
			    continue;
		    cp = buf + sizeof("search") - 1;
		    while (*cp == ' ' || *cp == '\t')
			    cp++;
		    if ((*cp == '\0') || (*cp == '\n'))
			    continue;
		    (void)strncpy(_res.defdname, cp, sizeof(_res.defdname) - 1);
		    if ((cp = index(_res.defdname, '\n')) != (char *)0)
			    *cp = '\0';
		    /*
		     * Set search list to be blank-separated strings
		     * on rest of line.
		     */
		    cp = _res.defdname;
		    pp = _res.dnsrch;
		    *pp++ = cp;
		    for (n = 0; *cp && pp < _res.dnsrch + MAXDNSRCH; cp++) {
			    if (*cp == ' ' || *cp == '\t') {
				    *cp = 0;
				    n = 1;
			    } else if (n) {
				    *pp++ = cp;
				    n = 0;
			    }
		    }
		    /* null terminate last domain if there are excess */
		    while (*cp != '\0' && *cp != ' ' && *cp != '\t')
			    cp++;
		    *cp = '\0';
		    *pp++ = (char *)0;
		    havesearch = 1;
		    continue;
		}
		/* read nameservers to query */
		if (!strncmp(buf, "nameserver", sizeof("nameserver") - 1) &&
		   _res.nscount < MAXNS) {
		    cp = buf + sizeof("nameserver") - 1;
		    while (*cp == ' ' || *cp == '\t')
			    cp++;
		    if ((*cp == '\0') || (*cp == '\n'))
			    continue;
		    /*
		     * End the address at the first white space.
		     *
		     * fgets() leaves the newline on the line, and this
		     * libsocket's inet_addr() is strict about what follows the
		     * last digit: it answers INADDR_NONE for anything that is
		     * not exactly four dotted numbers and then a NUL.  That
		     * strictness is deliberate -- a lax one made "no.such.host"
		     * parse as 0.0.0.0, so gethostbyname() could not tell a
		     * dotted quad from a name -- so the newline has to come off
		     * here.  Without this every `nameserver' line in
		     * /etc/resolv.conf was discarded, _res.nscount stayed 0,
		     * and res_init() fell back to its 127.0.0.1 default: every
		     * query went to a loopback address where nothing listens,
		     * no query ever reached the wire, and the lookup failed
		     * only after the whole retry schedule had run.
		     */
		    for (ep = cp; *ep && *ep != ' ' && *ep != '\t' &&
			*ep != '\n' && *ep != '\r'; ep++)
			    ;
		    *ep = '\0';
		    if (!inet_aton(cp, &_res.nsaddr_list[_res.nscount]))
			    continue;
		    _res.nsport_list[_res.nscount]= nameserver_port;
		    _res.nscount++;
		    continue;
		}
	    }
	    (void) fclose(fp);
	}
	if (_res.nscount == 0) {
		/* "localhost" is the default nameserver. */
		_res.nsaddr_list[0]= HTONL(0x7F000001L);
		_res.nsport_list[0]= nameserver_port;
		_res.nscount= 1;
	}
	if (_res.defdname[0] == 0) {
		if (gethostname(buf, sizeof(_res.defdname)) == 0 &&
		   (cp = index(buf, '.')))
			(void)strcpy(_res.defdname, cp + 1);
	}

	/* find components of local domain that might be searched */
	if (havesearch == 0) {
		pp = _res.dnsrch;
		*pp++ = _res.defdname;
		for (cp = _res.defdname, n = 0; *cp; cp++)
			if (*cp == '.')
				n++;
		cp = _res.defdname;
		for (; n >= LOCALDOMAINPARTS && pp < _res.dnsrch + MAXDFLSRCH;
		    n--) {
			cp = index(cp, '.');
			*pp++ = ++cp;
		}
		*pp++ = (char *)0;
	}
	_res.options |= RES_INIT;
	return (0);
}
