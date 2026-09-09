/*
 * Copyright (c) 1985, 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that: (1) source distributions retain this entire copyright
 * notice and comment, and (2) distributions including binaries display
 * the following acknowledgement:  ``This product includes software
 * developed by the University of California, Berkeley and its contributors''
 * in the documentation or other materials provided with the distribution
 * and in all advertising materials mentioning features or use of this
 * software. Neither the name of the University nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	@(#)gethostnamadr.c	6.41 (Berkeley) 6/1/90
 */

/*
 * gethnmadr.c -- turn a DNS answer into a struct hostent.
 *
 * _dns_byname()	 res_search() for an A record, then getanswer().
 * _dns_byaddr()	 res_query() for the PTR record of d.c.b.a.in-addr.arpa.
 *
 * These are the NETWORK half of gethostbyname/gethostbyaddr only.  The public
 * entry points live in net/netdb.c, which reads /etc/hosts first and comes here
 * when the file has no answer; that keeps a machine with no nameserver, or with
 * no /etc/resolv.conf, from paying a query for every lookup.
 *
 * getanswer() walks the answer section, following CNAMEs into the alias list
 * and collecting every A record of the first type and class it sees.  The names
 * and the addresses are packed end to end into one static buffer, with the
 * addresses aligned so that a caller may read one as a 32-bit word.
 */
#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <ansi.h>
#include <net/hton.h>
#include <net/gen/nameser.h>
#include <net/gen/netdb.h>
#include <net/gen/in.h>
#include <net/gen/inet.h>
#include <net/gen/resolv.h>
#include <net/gen/socket.h>

#define	MAXALIASES	35
#define	MAXADDRS	35

static char *h_addr_ptrs[MAXADDRS + 1];

struct in_addr
{
	ipaddr_t s_addr;
};
typedef u32_t u_long;
typedef u16_t u_short;
typedef u8_t u_char;
union querybuf;

#define getshort _getshort
#define bcmp memcmp
#define bcopy(s, d, l) memcpy(d, s, l)

static struct hostent *getanswer _ARGS(( union querybuf *answer, int anslen,
	int iquery ));

static struct hostent host;
static char *host_aliases[MAXALIASES];
static char hostbuf[BUFSIZ+1];
static struct in_addr host_addr;

#if PACKETSZ > 1024
#define	MAXPACKET	PACKETSZ
#else
#define	MAXPACKET	1024
#endif

typedef union querybuf
{
	dns_hdr_t hdr;
	u_char buf[MAXPACKET];
} querybuf_t;

typedef union align {
    long al;
    char ac;
} align_t;

/*
 * The answer scratch is static, not automatic: it is a kilobyte and the
 * initial user stack is 4 KB.
 */
static querybuf_t qbuf;

static struct hostent *
getanswer(answer, anslen, iquery)
	querybuf_t *answer;
	int anslen;
	int iquery;
{
	register dns_hdr_t *hp;
	register u_char *cp;
	register int n;
	u_char *eom;
	char *bp, **ap;
	int type, class, buflen, ancount, qdcount;
	int haveanswer, getclass = C_ANY;
	char **hap;

	eom = answer->buf + anslen;
	/*
	 * find first satisfactory answer
	 */
	hp = &answer->hdr;
	ancount = ntohs(hp->dh_ancount);
	qdcount = ntohs(hp->dh_qdcount);
	bp = hostbuf;
	buflen = sizeof(hostbuf);
	cp = answer->buf + sizeof(dns_hdr_t);
	if (qdcount) {
		if (iquery) {
			if ((n = dn_expand((u_char *)answer->buf, eom,
			     cp, (u_char *)bp, buflen)) < 0) {
				h_errno = NO_RECOVERY;
				return ((struct hostent *) 0);
			}
			cp += n + QFIXEDSZ;
			host.h_name = bp;
			n = strlen(bp) + 1;
			bp += n;
			buflen -= n;
		} else
			cp += dn_skipname(cp, eom) + QFIXEDSZ;
		while (--qdcount > 0)
			cp += dn_skipname(cp, eom) + QFIXEDSZ;
	} else if (iquery) {
		if (hp->dh_flag1 & DHF_AA)
			h_errno = HOST_NOT_FOUND;
		else
			h_errno = TRY_AGAIN;
		return ((struct hostent *) 0);
	}
	ap = host_aliases;
	*ap = (char *)0;
	host.h_aliases = host_aliases;
	hap = h_addr_ptrs;
	*hap = (char *)0;
	host.h_addr_list = h_addr_ptrs;
	haveanswer = 0;
	while (--ancount >= 0 && cp < eom) {
		if ((n = dn_expand((u_char *)answer->buf, eom, cp, (u_char *)bp,
			buflen)) < 0)
			break;
		cp += n;
		type = getshort(cp);
 		cp += sizeof(u_short);
		class = getshort(cp);
 		cp += sizeof(u_short) + sizeof(u_long);
		n = getshort(cp);
		cp += sizeof(u_short);
		if (type == T_CNAME) {
			cp += n;
			if (ap >= &host_aliases[MAXALIASES-1])
				continue;
			*ap++ = bp;
			n = strlen(bp) + 1;
			bp += n;
			buflen -= n;
			continue;
		}
		if (iquery && type == T_PTR) {
			if ((n = dn_expand((u8_t *)answer->buf, eom,
			    cp, (u8_t *)bp, buflen)) < 0) {
				cp += n;
				continue;
			}
			cp += n;
			host.h_name = bp;
			return(&host);
		}
		if (iquery || type != T_A)  {
#ifdef DEBUG
			if (_res.options & RES_DEBUG)
				printf("unexpected answer type %d, size %d\n",
					type, n);
#endif
			cp += n;
			continue;
		}
		if (haveanswer) {
			if (n != host.h_length) {
				cp += n;
				continue;
			}
			if (class != getclass) {
				cp += n;
				continue;
			}
		} else {
			host.h_length = n;
			getclass = class;
			host.h_addrtype = (class == C_IN) ? AF_INET : AF_UNSPEC;
			if (!iquery) {
				host.h_name = bp;
				bp += strlen(bp) + 1;
			}
		}

		/*
		 * Round the address up to an align_t boundary within hostbuf,
		 * so a caller may read it as a 32-bit word.  The offset from
		 * hostbuf is what is rounded: a far pointer's bits 16..23 are
		 * a segment field, not part of an address, so the pointer
		 * itself is not a number to take a remainder of.
		 */
		bp += (sizeof(align_t) - ((bp - hostbuf) % sizeof(align_t)))
			% sizeof(align_t);

		if (bp + n >= &hostbuf[sizeof(hostbuf)]) {
#ifdef DEBUG
			if (_res.options & RES_DEBUG)
				printf("size (%d) too big\n", n);
#endif
			break;
		}
		bcopy(cp, *hap++ = bp, n);
		bp +=n;
		cp += n;
		haveanswer++;
	}
	if (haveanswer) {
		*ap = (char *)0;
		*hap = (char *)0;
		return (&host);
	} else {
		h_errno = TRY_AGAIN;
		return ((struct hostent *) 0);
	}
}

struct hostent *
_dns_byname(name)
	char *name;
{
	register char *cp;
	int n;

	/*
	 * disallow names consisting only of digits/dots, unless
	 * they end in a dot.
	 */
	if (isdigit(name[0]))
		for (cp = name;; ++cp) {
			if (!*cp) {
				if (*--cp == '.')
					break;
				/*
				 * All-numeric, no dot at the end.
				 * Fake up a hostent as if we'd actually
				 * done a lookup.
				 */
				host_addr.s_addr = inet_addr(name);
				if (host_addr.s_addr == (ipaddr_t)0xFFFFFFFFL) {
					h_errno = HOST_NOT_FOUND;
					return((struct hostent *) 0);
				}
				host.h_name = name;
				host.h_aliases = host_aliases;
				host_aliases[0] = (char *)0;
				host.h_addrtype = AF_INET;
				host.h_length = sizeof(ipaddr_t);
				h_addr_ptrs[0] = (char *)&host_addr;
				h_addr_ptrs[1] = (char *)0;
				host.h_addr_list = h_addr_ptrs;
				return (&host);
			}
			if (!isdigit(*cp) && *cp != '.')
				break;
		}

	if ((n = res_search(name, C_IN, T_A, qbuf.buf, sizeof(qbuf))) < 0) {
#ifdef DEBUG
		if (_res.options & RES_DEBUG)
			printf("res_search failed\n");
#endif
		return ((struct hostent *) 0);
	}
	return (getanswer(&qbuf, n, 0));
}

struct hostent *
_dns_byaddr(addr, len, type)
	char *addr;
	int len, type;
{
	int n;
	register struct hostent *hp;
	char nbuf[MAXDNAME];

	if (type != AF_INET)
		return ((struct hostent *) 0);
	(void)sprintf(nbuf, "%u.%u.%u.%u.in-addr.arpa",
		((unsigned)addr[3] & 0xff),
		((unsigned)addr[2] & 0xff),
		((unsigned)addr[1] & 0xff),
		((unsigned)addr[0] & 0xff));
	n = res_query(nbuf, C_IN, T_PTR, qbuf.buf, sizeof(qbuf));
	if (n < 0) {
#ifdef DEBUG
		if (_res.options & RES_DEBUG)
			printf("res_query failed\n");
#endif
		return ((struct hostent *) 0);
	}
	hp = getanswer(&qbuf, n, 1);
	if (hp == (struct hostent *)0)
		return ((struct hostent *) 0);
	hp->h_addrtype = type;
	hp->h_length = len;
	h_addr_ptrs[0] = (char *)&host_addr;
	h_addr_ptrs[1] = (char *)0;
	/* memcpy, not a struct assignment: `addr' may be at an odd address,
	 * and a 32-bit load on this machine may not be. */
	bcopy(addr, (char *)&host_addr, sizeof(host_addr));
	return(hp);
}
