/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * ephport.c -- can a server let the STACK choose its port, and then find out
 * which one it got?
 *
 *	ephport
 *
 * The BSD idiom for a daemon that does not want a fixed port is
 *
 *	bind(s, port 0);  listen(s);  getsockname(s, &me);
 *
 * and then it publishes me.sin_port somewhere a client can find it.  hunt's
 * driver does exactly this, twice -- one port for the game and one for the
 * score table -- and answers a client's discovery datagram with both.
 *
 * Two things had to be true and neither was.  bind() recorded the port and
 * listen() asked the stack for that literal port, so port 0 was requested as
 * port zero -- which the stack rejects (EBADMODE), leaving the socket never
 * listening at all.  And getsockname() answered from our own record of the
 * bind, so even had the stack chosen a port, the program could not learn it.
 * A driver therefore advertised itself on port 0; the client's list of drivers
 * is TERMINATED by a zero port, so the first entry looked like the end of the
 * list and hunt reported no game running on a machine that was running one.
 *
 * Server and client are both here, forked, so this needs no wire and no peer:
 * ip_write loops a packet addressed to our own interface back internally.
 *
 * The client compares the BYTES it reads against what the server wrote.  A read
 * that merely returned something was satisfied by any stream at all -- so this
 * checked that a connection reached the right port, and not that the connection
 * it reached it on was the one the server had accepted.
 *
 * Every line starts with "ephport:" so a scripted run can pick it out.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>

extern int errno;
extern unsigned long inet_addr();

static char *me = "10.0.0.2";

#define REPLY	"scores\n"
/* sizeof, not strlen(): no call, so no chance of a K&R width slip. */
#define REPLYLEN	((int)sizeof(REPLY) - 1)

/*
 * The client is told the port the same way hunt's is: over a channel of its
 * own.  A pipe stands in for the discovery datagram -- what is under test is
 * the port, not how it travels.
 *
 * The port travels as the raw network-order 16 bits rather than as text.  int
 * is 16 bits here and the stack hands out high ports, so 49152 printed with
 * %d reads as -16384 and atoi() of that overflows on the way back; the number
 * survives either way, but only because two wrongs cancel.  %u below is for
 * the same reason.
 */
static int child(pfd)
int pfd;
{
	int s, k;
	unsigned short netport;
	struct sockaddr_in sin;
	char buf[64];

	if (read(pfd, (char *)&netport, sizeof(netport)) != sizeof(netport))
	{
		printf("ephport: client: never told a port\n");
		return 1;
	}
	if (netport == 0)
	{
		printf("ephport: client: told port 0 -- FAIL\n");
		return 1;
	}
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("ephport: client: socket errno %d\n", errno);
		return 1;
	}
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = netport;
	sin.sin_addr.s_addr = inet_addr(me);
	if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		printf("ephport: client: connect to %u errno %d\n",
			(unsigned)ntohs(netport), errno);
		return 1;
	}
	printf("ephport: client: connected to %u\n",
		(unsigned)ntohs(netport));
	fflush(stdout);
	/*
	 * Read to END OF FILE, as `hunt -S' does: the server says its piece and
	 * closes, and the client has no length to go by.  A close that does not
	 * reach the peer leaves this blocked here for ever, so the test is as
	 * much about the shutdown as about the port.
	 */
	k = read(s, buf, sizeof(buf) - 1);
	if (k <= 0)
	{
		printf("ephport: client: read %d errno %d\n", k, errno);
		close(s);
		return 1;
	}
	buf[k] = '\0';
	printf("ephport: client got [%s]", buf);
	fflush(stdout);
	/*
	 * The BYTES, not merely that some arrived.  Reaching the right port is
	 * only half of what the ephemeral-port idiom has to get right: the
	 * connection also has to be the one this server accepted, carrying what
	 * that server wrote.  A read that returns something is satisfied by any
	 * stream at all, including another connection's.
	 */
	if (k != REPLYLEN || strcmp(buf, REPLY) != 0)
	{
		printf("ephport: FAIL -- wanted [%s] (%d bytes), got [%s]"
			" (%d bytes)\n", REPLY, REPLYLEN, buf, k);
		fflush(stdout);
		close(s);
		return 1;
	}
	k = read(s, buf, sizeof(buf) - 1);
	printf("ephport: client: second read %d (0 = clean EOF)\n", k);
	fflush(stdout);
	close(s);
	return k == 0 ? 0 : 1;
}

int main(argc, argv)
int argc;
char **argv;
{
	int ls, cs, n, alen, pfd[2], st, fails;
	unsigned short netport;
	struct sockaddr_in sin, from;

	fails = 0;
	if (pipe(pfd) < 0)
	{
		printf("ephport: pipe errno %d\n", errno);
		return 1;
	}
	if ((ls = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("ephport: socket errno %d\n", errno);
		return 1;
	}
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(0);		/* the stack's choice */
	sin.sin_addr.s_addr = inet_addr(me);
	if (bind(ls, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		printf("ephport: bind errno %d\n", errno);
		return 1;
	}
	if (listen(ls, 5) < 0)
	{
		printf("ephport: listen errno %d -- FAIL\n", errno);
		return 1;
	}
	alen = sizeof(sin);
	if (getsockname(ls, (struct sockaddr *)&sin, &alen) < 0)
	{
		printf("ephport: getsockname errno %d\n", errno);
		return 1;
	}
	netport = sin.sin_port;
	printf("ephport: listening on port %u (fd %d)\n",
		(unsigned)ntohs(netport), ls);
	fflush(stdout);
	if (netport == 0)
	{
		printf("ephport: FAIL -- the stack chose no port, or we"
			" cannot see the one it chose\n");
		return 1;
	}

	/*
	 * The child does NOT close the listening socket, though BSD practice
	 * says it should.  A socket here lives in the inet daemon, not in the
	 * kernel's file table: close() writes an NWR_CLOSE down the channel,
	 * and the channel is the SAME one the parent is listening on -- a fork
	 * duplicates the descriptors but there is only one socket behind them.
	 * The tidy-up therefore destroyed the parent's listener, and the
	 * connect below was refused (EIO, the stack's ECONNREFUSED).
	 */
	if ((n = fork()) == 0)
		return child(pfd[0]);
	if (n < 0)
	{
		printf("ephport: fork errno %d\n", errno);
		return 1;
	}
	write(pfd[1], (char *)&netport, sizeof(netport));

	alen = sizeof(from);
	if ((cs = accept(ls, (struct sockaddr *)&from, &alen)) < 0)
	{
		printf("ephport: accept errno %d -- FAIL\n", errno);
		return 1;
	}
	printf("ephport: accepted on fd %d\n", cs);
	fflush(stdout);
	n = write(cs, REPLY, REPLYLEN);
	if (n != REPLYLEN)
	{
		printf("ephport: FAIL -- write returned %d of %d, errno %d\n",
			n, REPLYLEN, errno);
		fails++;
	}
	close(cs);			/* the client must see EOF from this */

	st = 1;
	if (wait(&st) < 0)
	{
		printf("ephport: FAIL -- wait errno %d\n", errno);
		fails++;
	}
	else if (st != 0)
		fails++;		/* the client said so, and said why */
	printf("ephport: %s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
