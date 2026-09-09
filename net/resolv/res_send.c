/*
 * Copyright (c) 1985, 1989 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)res_send.c	6.27 (Berkeley) 2/24/91
 */

/*
 * res_send.c -- send a query to a nameserver and wait for the reply.
 *
 * Tries every server in _res.nsaddr_list, _res.retry times round, doubling the
 * per-attempt timeout each pass.  A reply whose id does not match the query is
 * a stale answer and is discarded without ending the wait.  A reply with the
 * truncation bit set is re-asked over TCP against the same server, which is
 * also the path taken when the query itself is longer than one datagram or when
 * the caller sets RES_USEVC.
 *
 * The transport is BSD sockets over libsocket: SOCK_DGRAM + sendto/recvfrom for
 * a query, SOCK_STREAM + connect for the circuit, and select() for the reply
 * timeout.  Over TCP a message is preceded by its 16-bit length, big-endian.
 *
 * A datagram socket is never bound: libsocket's sendto() takes an ephemeral
 * local port on first use, which is what a query socket wants.
 *
 * Errors are reported the way the resolver's callers read them: ECONNREFUSED
 * when no server answered at all, ETIMEDOUT when one was reached but never
 * replied, and the transport's own errno when a circuit failed.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ansi.h>
#include <net/netlib.h>
#include <net/gen/in.h>
#include <net/gen/inet.h>
#include <net/gen/netdb.h>
#include <net/gen/nameser.h>
#include <net/gen/resolv.h>
#include <net/gen/tcp.h>
#include <net/gen/udp.h>

typedef u16_t u_short;

extern int errno;

/*
 * The longest select() timeout this libc can express.  select() reduces its
 * struct timeval to a millisecond count in an int, and int is 16 bits: a
 * timeout of 32.767 s or more comes out negative, which poll() reads as "block
 * forever".  The resolver's own backoff reaches 40 s on its fourth try.
 */
#define MAXTIMEOUT	30L

static int tcp_connect _ARGS(( ipaddr_t host, tcpport_t port, int *terrno ));
static int tcpip_writeall _ARGS(( int fd, char *buf, int siz ));
static int udp_socket _ARGS(( void ));
static int udp_sendto _ARGS(( int fd, char *buf, int buflen, ipaddr_t addr,
				udpport_t port ));
static int udp_receive _ARGS(( int fd, char *buf, int buflen, long timeout ));

static int s = -1;	/* socket used for communications */

int res_send(buf, buflen, answer, anslen)
	char *buf;
	int buflen;
	char *answer;
	int anslen;
{
	register int n;
	int try, v_circuit, resplen, ns;
	int gotsomewhere = 0;
	int connreset = 0;
	u16_t id, len;
	char *cp;
	long timeout;
	dns_hdr_t *hp = (dns_hdr_t *) buf;
	dns_hdr_t *anhp = (dns_hdr_t *) answer;
	int terrno = ETIMEDOUT;
	static char junk[512];

#ifdef DEBUG
	if (_res.options & RES_DEBUG) {
		printf("res_send()\n");
	}
#endif /* DEBUG */
	if (!(_res.options & RES_INIT))
		if (res_init() == -1) {
			return(-1);
		}

	resplen = -1;
	v_circuit = (_res.options & RES_USEVC) || buflen > PACKETSZ;
	id = hp->dh_id;
	/*
	 * Send request, RETRY times, or until successful
	 */
	for (try = 0; try < _res.retry; try++) {
	   for (ns = 0; ns < _res.nscount; ns++) {
#ifdef DEBUG
		if (_res.options & RES_DEBUG)
			printf("Querying server (# %d) address = %s\n", ns+1,
			      inet_ntoa(_res.nsaddr_list[ns]));
#endif /* DEBUG */
	usevc:
		if (v_circuit) {
			int truncated = 0;
			int nbytes;

			/*
			 * Use virtual circuit;
			 * at most one attempt per server.
			 */
			try = _res.retry;
			if (s < 0)
			{
				s= tcp_connect(_res.nsaddr_list[ns],
					_res.nsport_list[ns], &terrno);
				if (s == -1)
					continue;
			}
			/*
			 * Send length & message
			 */
			len = htons((u_short)buflen);
			nbytes= tcpip_writeall(s, (char *)&len, sizeof(len));
			if (nbytes != sizeof(len))
			{
				terrno= errno;
#ifdef DEBUG
				if (_res.options & RES_DEBUG)
					fprintf(stderr, "write failed: %s\n",
					strerror(terrno));
#endif /* DEBUG */
				close(s);
				s= -1;
				continue;
			}
			nbytes= tcpip_writeall(s, buf, buflen);
			if (nbytes != buflen)
			{
				terrno= errno;
#ifdef DEBUG
				if (_res.options & RES_DEBUG)
					fprintf(stderr, "write failed: %s\n",
					strerror(terrno));
#endif /* DEBUG */
				close(s);
				s= -1;
				continue;
			}
			/*
			 * Receive length & response
			 */
			cp = answer;
			len = sizeof(u16_t);
			while (len != 0)
			{
				n = read(s, (char *)cp, (int)len);
				if (n <= 0)
					break;
				cp += n;
				len -= n;
			}
			if (len) {
				terrno = errno;
#ifdef DEBUG
				if (_res.options & RES_DEBUG)
					fprintf(stderr, "read failed: %s\n",
						strerror(terrno));
#endif /* DEBUG */
				close(s);
				s= -1;
				/*
				 * A long running process might get its TCP
				 * connection reset if the remote server was
				 * restarted.  Requery the server instead of
				 * trying a new one.  When there is only one
				 * server, this means that a query might work
				 * instead of failing.  We only allow one reset
				 * per query to prevent looping.
				 */
				if (terrno == ECONNRESET && !connreset) {
					connreset = 1;
					ns--;
				}
				continue;
			}
			cp = answer;
			if ((resplen = ntohs(*(u_short *)cp)) > anslen) {
#ifdef DEBUG
				if (_res.options & RES_DEBUG)
					fprintf(stderr, "response truncated\n");
#endif /* DEBUG */
				len = anslen;
				truncated = 1;
			} else
				len = resplen;
			while (len != 0)
			{
				n= read(s, (char *)cp, (int)len);
				if (n <= 0)
					break;
				cp += n;
				len -= n;
			}
			if (len) {
				terrno = errno;
#ifdef DEBUG
				if (_res.options & RES_DEBUG)
					fprintf(stderr, "read failed: %s\n",
						strerror(terrno));
#endif /* DEBUG */
				close(s);
				s= -1;
				continue;
			}
			if (truncated) {
				/*
				 * Flush rest of answer
				 * so connection stays in synch.
				 */
				anhp->dh_flag1 |= DHF_TC;
				len = resplen - anslen;
				while (len != 0) {
					n = (len > sizeof(junk) ?
					    sizeof(junk) : len);
					if ((n = read(s, junk, n)) > 0)
						len -= n;
					else
						break;
				}
			}
		} else {
			/*
			 * Use datagrams.
			 */
			if (s < 0) {
				s = udp_socket();
				if (s < 0) {
					terrno = errno;
#ifdef DEBUG
					if (_res.options & RES_DEBUG)
					    perror("udp_socket failed");
#endif /* DEBUG */
					continue;
				}
			}
			if (udp_sendto(s, buf, buflen, _res.nsaddr_list[ns],
				_res.nsport_list[ns]) != buflen) {
#ifdef DEBUG
				if (_res.options & RES_DEBUG)
					perror("sendto");
#endif /* DEBUG */
				continue;
			}

			/*
			 * Wait for reply.  The shift is done in long: the
			 * fourth try of a 5-second retransmit is 40 seconds,
			 * and select() cannot express more than MAXTIMEOUT.
			 */
			timeout= (long)_res.retrans << try;
			if (try > 0)
				timeout /= _res.nscount;
			if (timeout <= 0)
				timeout= 1;
			if (timeout > MAXTIMEOUT)
				timeout= MAXTIMEOUT;
wait:
			if ((resplen= udp_receive(s, answer, anslen, timeout))
				== -1)
			{
				if (errno == EINTR)
				{
				/*
				 * timeout
				 */
#ifdef DEBUG
					if (_res.options & RES_DEBUG)
						printf("timeout\n");
#endif /* DEBUG */
					gotsomewhere = 1;
				}
				else
				{
#ifdef DEBUG
				if (_res.options & RES_DEBUG)
					perror("udp_receive");
#endif /* DEBUG */
				}
				continue;
			}
			gotsomewhere = 1;
			if (id != anhp->dh_id) {
				/*
				 * response from old query, ignore it
				 */
#ifdef DEBUG
				if (_res.options & RES_DEBUG)
					printf("old answer\n");
#endif /* DEBUG */
				goto wait;
			}
			if (!(_res.options & RES_IGNTC) &&
				(anhp->dh_flag1 & DHF_TC)) {
				/*
				 * get rest of answer;
				 * use TCP with same server.
				 */
#ifdef DEBUG
				if (_res.options & RES_DEBUG)
					printf("truncated answer\n");
#endif /* DEBUG */
				(void) close(s);
				s = -1;
				v_circuit = 1;
				goto usevc;
			}
		}
#ifdef DEBUG
		if (_res.options & RES_DEBUG) {
			printf("got answer\n");
		}
#endif /* DEBUG */
		/*
		 * If using virtual circuits, we assume that the first server
		 * is preferred * over the rest (i.e. it is on the local
		 * machine) and only keep that one open.
		 * If we have temporarily opened a virtual circuit,
		 * or if we haven't been asked to keep a socket open,
		 * close the socket.
		 */
		if ((v_circuit &&
		    ((_res.options & RES_USEVC) == 0 || ns != 0)) ||
		    (_res.options & RES_STAYOPEN) == 0) {
			(void) close(s);
			s = -1;
		}
		return (resplen);
	   }
	}
	if (s >= 0) {
		(void) close(s);
		s = -1;
	}
	if (v_circuit == 0)
		if (gotsomewhere == 0)
			errno = ECONNREFUSED;	/* no nameservers found */
		else
			errno = ETIMEDOUT;	/* no answer obtained */
	else
		errno = terrno;
	return (-1);
}

/*
 * This routine is for closing the socket if a virtual circuit is used and
 * the program wants to close it.  This provides support for endhostent()
 * which expects to close the socket.
 *
 * This routine is not expected to be user visible.
 */
void
_res_close()
{
	if (s != -1) {
		(void) close(s);
		s = -1;
	}
}

/*
 * Open a stream socket to `host' port `port', both already in network order.
 * Returns the descriptor, or -1 with the failing errno in *terrno.
 */
static int tcp_connect(host, port, terrno)
ipaddr_t host;
tcpport_t port;
int *terrno;
{
	struct sockaddr_in sin;
	int fd;

	fd= socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1)
	{
		*terrno= errno;
		return -1;
	}
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family= AF_INET;
	sin.sin_addr.s_addr= host;
	sin.sin_port= port;
	if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) == -1)
	{
		*terrno= errno;
		close(fd);
		return -1;
	}
	*terrno= 0;
	return fd;
}

/*
 * Write `siz' bytes, however many writes that takes.  Returns the number
 * written, which is short of `siz' only on error or end of file.
 */
static int tcpip_writeall(fd, buf, siz)
int fd;
char *buf;
int siz;
{
	int siz_org;
	int nbytes;

	siz_org= siz;

	while (siz)
	{
		nbytes= write(fd, buf, siz);
		if (nbytes <= 0)
			return siz_org-siz;
		buf += nbytes;
		siz -= nbytes;
	}
	return siz_org;
}

/*
 * A datagram socket for queries.  Deliberately unbound: libsocket's sendto()
 * takes an ephemeral local port on first use.
 */
static int udp_socket()
{
	return socket(AF_INET, SOCK_DGRAM, 0);
}

static int udp_sendto(fd, buf, buflen, addr, port)
int fd;
char *buf;
int buflen;
ipaddr_t addr;
udpport_t port;
{
	struct sockaddr_in sin;

	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family= AF_INET;
	sin.sin_addr.s_addr= addr;
	sin.sin_port= port;
	return sendto(fd, buf, buflen, 0, (struct sockaddr *)&sin,
		sizeof(sin));
}

/*
 * Wait up to `timeout' seconds for a datagram, then read it.
 *
 * A timeout returns -1 with errno EINTR, which is how the caller tells "the
 * server was reached but said nothing" from "the transport failed".
 */
static int udp_receive(fd, buf, buflen, timeout)
int fd;
char *buf;
int buflen;
long timeout;
{
	struct timeval tv;
	fd_set rfds;
	int n;

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	tv.tv_sec= timeout;
	tv.tv_usec= 0L;
	n= select(fd+1, &rfds, (fd_set *)0, (fd_set *)0, &tv);
	if (n < 0)
		return -1;
	if (n == 0)
	{
		errno= EINTR;
		return -1;
	}
	return recvfrom(fd, buf, buflen, 0, (struct sockaddr *)0, (int *)0);
}
