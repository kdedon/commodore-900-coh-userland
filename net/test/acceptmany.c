/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * acceptmany.c -- can a server accept more than one connection?
 *
 *	acceptmany [port [clients]]	default port 7030, 3 clients
 *
 * In the underlying device model one descriptor IS one connection: the passive
 * open turns the listening descriptor into the connected one.  So accept()
 * used to return the descriptor it was given, and a server could accept
 * exactly once -- which is no server at all, and is what stood between this
 * machine and huntd, telnetd or ftpd.
 *
 * The test is the whole point in one line: accept TWICE on the same listening
 * descriptor and talk to both peers.  If the second accept returns the same
 * descriptor as the first, or the first connection stops working once the
 * second arrives, the swap underneath the caller is wrong.
 *
 * THE BYTES ARE COMPARED, on both sides, and that is what makes this a test of
 * crosswiring rather than of liveness.  Distinct descriptors and a non-empty
 * read say only that two connections exist; they say nothing about whether each
 * one is carrying its OWN peer's stream.  Each client therefore sends a payload
 * that names it and requires exactly that payload back, and the server requires
 * the N payloads it collects to be the N distinct ones it expects, each arriving
 * once.  An accept that attached connection A's stream to connection B passes
 * every structural check above and fails both of these.
 *
 * Server and clients are both here, forked, so it needs no wire, no slip and
 * no host peer -- ip_write loops a packet addressed to our own interface back
 * internally.  Each child connects, sends its number and reads the answer.
 *
 * Every line starts with "acceptmany:" so a scripted run can pick it out.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>

extern int errno;
extern unsigned long inet_addr();

static char *me = "10.0.0.2";

static int child(port, n)
int port, n;
{
	int s, k;
	struct sockaddr_in sin;
	char buf[64], mine[64];

	sleep(2 + n);			/* let the server reach accept() */
	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("acceptmany: client %d: socket errno %d\n", n, errno);
		return 1;
	}
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = inet_addr(me);
	if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		printf("acceptmany: client %d: connect errno %d\n", n, errno);
		return 1;
	}
	sprintf(mine, "client%d\n", n);
	if (write(s, mine, strlen(mine)) < 0)
	{
		printf("acceptmany: client %d: write errno %d\n", n, errno);
		return 1;
	}
	if ((k = read(s, buf, sizeof(buf) - 1)) <= 0)
	{
		printf("acceptmany: client %d: read %d errno %d\n", n, k,
			errno);
		return 1;
	}
	buf[k] = '\0';
	printf("acceptmany: client %d got back [%s]\n", n, buf);
	fflush(stdout);
	close(s);
	/*
	 * Its OWN payload, whole.  A crosswired accept gives this client the
	 * bytes another one sent -- which is a passing read, a plausible-looking
	 * line of output, and the exact defect this program exists to catch.
	 */
	if (k != (int)strlen(mine) || strcmp(buf, mine) != 0)
	{
		printf("acceptmany: FAIL -- client %d sent [%s] and got"
			" back [%s]\n", n, mine, buf);
		fflush(stdout);
		return 1;
	}
	return 0;
}

int main(argc, argv)
int argc;
char **argv;
{
	int ls, n, i, k, alen, port, nclients, fails, st, seen;
	int conn[4];
	int got[4];			/* got[j]: how many times client j's	*/
					/* payload arrived, over all conns	*/
	struct sockaddr_in sin, from;
	char buf[64], want[64];

	port = argc > 1 ? atoi(argv[1]) : 7030;
	nclients = argc > 2 ? atoi(argv[2]) : 3;
	if (nclients > 4)
		nclients = 4;
	fails = 0;
	for (i = 0; i < 4; i++)
	{
		conn[i] = -1;		/* so the tidy-up cannot close a	*/
		got[i] = 0;		/* descriptor that was never opened	*/
	}

	if ((ls = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("acceptmany: socket errno %d\n", errno);
		return 1;
	}
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = inet_addr(me);
	if (bind(ls, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		printf("acceptmany: bind errno %d\n", errno);
		return 1;
	}
	if (listen(ls, 5) < 0)
	{
		printf("acceptmany: listen errno %d\n", errno);
		return 1;
	}
	printf("acceptmany: listening on port %d as fd %d\n", port, ls);
	fflush(stdout);

	for (i = 0; i < nclients; i++)
	{
		if ((n = fork()) == 0)
			return child(port, i);
		if (n < 0)
		{
			printf("acceptmany: fork errno %d\n", errno);
			return 1;
		}
	}

	for (i = 0; i < nclients; i++)
	{
		alen = sizeof(from);
		if ((conn[i] = accept(ls, (struct sockaddr *)&from, &alen)) < 0)
		{
			printf("acceptmany: accept %d failed errno %d\n", i,
				errno);
			fails++;
			break;
		}
		printf("acceptmany: accept %d -> fd %d (listener still %d)\n",
			i, conn[i], ls);
		fflush(stdout);
		if (conn[i] == ls)
		{
			printf("acceptmany: FAIL -- accept returned the"
				" LISTENING descriptor\n");
			fails++;
			break;
		}
		if ((k = read(conn[i], buf, sizeof(buf) - 1)) <= 0)
		{
			printf("acceptmany: read %d got %d errno %d\n", i, k,
				errno);
			fails++;
			continue;
		}
		buf[k] = '\0';
		printf("acceptmany: conn %d said [%s]", i, buf);
		fflush(stdout);

		/*
		 * WHOSE stream is this?  The connections arrive in whatever
		 * order the clients get there, so the payload is not required
		 * to match the accept index -- but it must be one of the
		 * clients' payloads, and the tally below requires each to turn
		 * up exactly once.  Two connections fed from the same stream
		 * (the crosswire) makes one payload arrive twice and another
		 * not at all.
		 */
		seen = -1;
		for (n = 0; n < nclients; n++)
		{
			sprintf(want, "client%d\n", n);
			if (strcmp(buf, want) == 0)
			{
				seen = n;
				got[n]++;
				break;
			}
		}
		if (seen < 0)
		{
			printf("acceptmany: FAIL -- conn %d carried [%s],"
				" which no client sent\n", i, buf);
			fails++;
		}
		if (write(conn[i], buf, k) != k)
		{
			printf("acceptmany: echo %d failed errno %d\n", i,
				errno);
			fails++;
		}
	}

	/*
	 * Every connection must still be distinct at the end.  Two accepts
	 * that returned the same descriptor would have looked fine above if
	 * the first had already been closed.
	 */
	for (i = 0; i < nclients; i++)
		for (k = i + 1; k < nclients; k++)
			if (conn[i] >= 0 && conn[i] == conn[k])
			{
				printf("acceptmany: FAIL -- conn %d and %d are"
					" the same descriptor\n", i, k);
				fails++;
			}

	/* Each client's payload exactly once: no duplicates, none missing. */
	for (n = 0; n < nclients; n++)
		if (got[n] != 1)
		{
			printf("acceptmany: FAIL -- client %d's payload arrived"
				" %d times, wanted 1\n", n, got[n]);
			fails++;
		}

	for (i = 0; i < nclients; i++)
		if (conn[i] >= 0)
			close(conn[i]);
	close(ls);

	/*
	 * The clients check their OWN payload came back and exit non-zero if it
	 * did not.  Nothing collected those statuses, so every client-side
	 * verdict was discarded -- including the one that sees a crosswire.
	 */
	for (i = 0; i < nclients; i++)
	{
		st = 0;
		if (wait(&st) < 0)
		{
			printf("acceptmany: FAIL -- wait errno %d\n", errno);
			fails++;
			break;
		}
		if (st != 0)
		{
			printf("acceptmany: FAIL -- a client exited %d\n",
				(st >> 8) & 0xFF);
			fails++;
		}
	}

	printf("acceptmany: %s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
