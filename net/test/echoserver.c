/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * echoserver.c -- a TCP echo server over libsocket, for testing WITHOUT a wire.
 *
 *	echoserver [port [seconds]]	default 7; the second argument is the
 *					deadline below
 *
 * Its point is to make the TCP layer testable on its own.  Every other network
 * test here needs the serial line, a host peer and the simulator (two minutes to
 * boot, twenty to run), so a failed handshake leaves two very different
 * explanations open at once: TCP is wrong, or the packet never reached it.
 * Running a client and this server on the SAME machine takes the wire, slip and
 * the host peer out of the picture entirely -- the stack routes a connection to
 * its own address internally -- and answers just the first question.
 *
 * It accepts one connection, echoes what it reads until the peer closes, and
 * exits.  Every line it prints starts with "echoserver:" so a scripted run can
 * pick it out of a shared console.
 *
 * WHAT THIS CAN AND CANNOT CHECK.  It is the PEER in somebody else's test, so
 * the verdict on whether the bytes survived the round trip belongs to the
 * client, which is the end that knows what it sent -- echoclient(8) makes that
 * comparison.  What this end owes is an honest exit status: every failure it
 * detects (accept, a short or failed echo, a connection that carried nothing)
 * now leaves a non-zero status behind instead of the unconditional 0 it used
 * to exit with, which let a scripted run see a broken server as a working one.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

extern int errno;
extern unsigned long inet_addr();

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
	write(2, "echoserver: FAIL -- timed out with nothing more arriving\n", 57);
	_exit(1);
}

int main(argc, argv)
int argc;
char **argv;
{
	int s, c, n, total, fails, dl;
	struct sockaddr_in sin;
	char buf[256];

	fails = 0;
	dl = argc > 2 ? atoi(argv[2]) : DEADLINE;

	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(argc > 1 ? atoi(argv[1]) : 7);
	sin.sin_addr.s_addr = 0;		/* any local address */

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("echoserver: socket failed\n");
		return 1;
	}
	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		printf("echoserver: bind failed\n");
		return 1;
	}
	if (listen(s, 1) < 0)
	{
		printf("echoserver: listen failed\n");
		return 1;
	}
	printf("echoserver: listening on port %d\n", ntohs(sin.sin_port));
	fflush(stdout);
	signal(SIGALRM, expired);
	alarm(dl);

	/*
	 * accept() returns a NEW descriptor for the connection and leaves
	 * `s' listening, as BSD does; reading from `s' here reads the
	 * LISTENING socket and gets nothing.
	 */
	if ((c = accept(s, (struct sockaddr *)0, (int *)0)) < 0)
	{
		printf("echoserver: accept failed\n");
		return 1;
	}
	printf("echoserver: connected\n");
	fflush(stdout);

	total = 0;
	alarm(dl);
	while ((n = read(c, buf, sizeof(buf) - 1)) > 0)
	{
		alarm(dl);
		total += n;
		buf[n] = '\0';
		printf("echoserver: read %d [%s]\n", n, buf);
		fflush(stdout);
		if (write(c, buf, n) != n)
		{
			printf("echoserver: FAIL -- echo of %d bytes short or"
				" refused\n", n);
			fails++;
			break;
		}
	}
	alarm(0);
	if (n < 0)
	{
		printf("echoserver: FAIL -- read errno %d\n", errno);
		fails++;
	}
	/*
	 * A connection that carried nothing is not a successful run: the peer
	 * connected and closed without a byte crossing, which is exactly what a
	 * stack that establishes but never delivers data looks like.
	 */
	if (total == 0)
	{
		printf("echoserver: FAIL -- the connection carried no data\n");
		fails++;
	}
	printf("echoserver: done, %d bytes echoed -- %s\n", total,
		fails ? "FAIL" : "PASS");
	close(c);
	close(s);
	return fails ? 1 : 0;
}
