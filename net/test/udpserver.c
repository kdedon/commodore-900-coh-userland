/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * udpserver.c -- a UDP echo server, for exercising datagrams over the wire.
 *
 *	udpserver [port [count [seconds]]]  default port 7, 4 datagrams; the
 *					third argument is the deadline below
 *
 * Binds a port, blocks in recvfrom(), prints who sent each datagram, and sends
 * it straight back to that sender.  The host end is udp-test.py.
 *
 * The guest SERVES rather than initiates, which is the opposite of echoclient
 * and deliberate.  UDP has no retransmission: a datagram the serial line loses
 * is simply gone, and whichever end speaks first must be the one able to say it
 * again.  That is the host -- it can resend on a timer for as long as it likes,
 * while a guest blocked in recvfrom() cannot do anything at all.  Losing a
 * twenty-minute simulator run to one dropped frame is not a useful test.
 *
 * It answers `count' datagrams instead of one so a host retransmission that
 * crosses the reply in flight is absorbed rather than left queued.
 *
 * What is under test is per-datagram addressing: the reply goes to the address
 * and port recvfrom() reported, so a wrong sender does not merely misprint, it
 * sends the echo somewhere the host is not listening.
 *
 * WHAT THIS CAN AND CANNOT CHECK.  The datagrams' contents are whatever the
 * host end chose to send, so this end cannot say whether they arrived intact --
 * only the host, which knows what it sent, can, and udp-test.py makes that
 * comparison.  What this end can judge, and now does, is everything it can see
 * for itself: that the sender recvfrom() reported is a real address and port
 * (a zero either way means the per-datagram addressing this test exists for did
 * not happen, and the echo went nowhere), that every echo left whole, and that
 * `count' datagrams actually arrived.  All of those used to be printed and
 * then discarded by an unconditional exit 0.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

extern int errno;

/*
 * A DEADLINE.  Every read below blocks with nothing to interrupt it, so a peer
 * that connects and then says less than this server expects leaves this
 * process waiting for ever -- and a test that hangs has no verdict at all,
 * which is worse than one that fails.  60 seconds is far longer than any
 * exchange here takes and far shorter than a person's patience.  The alarm is
 * re-armed before each blocking wait, so a long healthy session is not cut off.
 */
#define DEADLINE	60

static void expired()
{
	/* write(2), not printf: this runs from a signal, and the verdict has to
	 * get out even if stdio was in the middle of something. */
	write(2, "udpserver: FAIL -- timed out with nothing more arriving\n", 56);
	_exit(1);
}

/*
 * Printed here rather than with inet_ntoa(), which takes a struct in_addr BY
 * VALUE.  A by-value struct argument in a file with no prototypes is a
 * recurring width-bug shape, so a test avoids it.
 */
static char *ipstr(a)
unsigned long a;
{
	static char buf[16];

	sprintf(buf, "%d.%d.%d.%d", (int)((a >> 24) & 0xFF),
		(int)((a >> 16) & 0xFF), (int)((a >> 8) & 0xFF),
		(int)(a & 0xFF));
	return buf;
}

int main(argc, argv)
int argc;
char **argv;
{
	int s, n, i, len, alen, port, count, fails, done, dl;
	struct sockaddr_in sin, from;
	char buf[512];

	port = argc > 1 ? atoi(argv[1]) : 7;
	count = argc > 2 ? atoi(argv[2]) : 4;
	dl = argc > 3 ? atoi(argv[3]) : DEADLINE;
	fails = 0;
	done = 0;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		printf("udpserver: socket failed errno %d\n", errno);
		return 1;
	}
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = 0;		/* whatever we are configured as */
	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		printf("udpserver: bind(%d) failed errno %d\n", port, errno);
		return 1;
	}
	printf("udpserver: listening on port %d\n", port);
	fflush(stdout);
	signal(SIGALRM, expired);

	for (i = 0; i < count; i++)
	{
		alen = sizeof(from);
		alarm(dl);
		n = recvfrom(s, buf, sizeof(buf) - 1, 0,
			(struct sockaddr *)&from, &alen);
		if (n < 0)
		{
			printf("udpserver: recvfrom failed errno %d\n", errno);
			return 1;
		}
		buf[n] = '\0';
		printf("udpserver: got %d [%s] from %s port %d\n", n, buf,
			ipstr(from.sin_addr.s_addr), ntohs(from.sin_port));
		fflush(stdout);

		/*
		 * THE SENDER, which is the whole point of recvfrom() over
		 * read().  A zero address or a zero port is not a misprint: it
		 * is the reply being addressed to nobody, and the host waiting
		 * out its timeout for an echo that was sent into the void.
		 */
		if (from.sin_addr.s_addr == 0 || from.sin_port == 0)
		{
			printf("udpserver: FAIL -- datagram %d reported sender"
				" %s port %d; the echo has nowhere to go\n", i,
				ipstr(from.sin_addr.s_addr),
				ntohs(from.sin_port));
			fails++;
		}

		len = n;		/* what we owe the sender back */
		n = sendto(s, buf, len, 0, (struct sockaddr *)&from,
			sizeof(from));
		printf("udpserver: echoed %d (errno %d)\n", n, errno);
		fflush(stdout);
		if (n != len)
		{
			printf("udpserver: FAIL -- sendto returned %d of %d,"
				" errno %d\n", n, len, errno);
			fails++;
		}
		done++;
	}
	alarm(0);
	if (done != count)
	{
		printf("udpserver: FAIL -- %d datagrams of %d\n", done, count);
		fails++;
	}
	printf("udpserver: done, %d datagrams echoed -- %s\n", done,
		fails ? "FAIL" : "PASS");
	close(s);
	return fails ? 1 : 0;
}
