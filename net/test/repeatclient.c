/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * repeatclient.c -- does a service answer the SECOND caller, and the TENTH?
 *
 *	repeatclient [-n rounds] host portspec [portspec ...]
 *
 * A switchboard hands each caller a connection and goes on listening, and the
 * going on listening is the half nothing else here measures.  echoclient proves
 * one connection; acceptmany proves a server of its own accepts twice; this asks
 * the question of the SHIPPED services, through their real ports, one caller at
 * a time and each one closed properly before the next opens.  A service that
 * answers once and refuses afterwards has lost its listening socket, and the
 * only thing that can see that from outside is a second caller.
 *
 * EACH CONNECTION IS CLOSED, and that is what separates this from dropclient:
 * there the case is a peer that goes away without saying so, here it is a peer
 * that does everything right.  Nothing is held, so nothing here can fail for
 * want of a descriptor, a process or a pty -- if round ten is refused it is
 * because the port stopped being served.
 *
 * A PORTSPEC SAYS WHAT THE SERVICE OWES A CALLER, because they differ:
 *
 *	port		connect, and that alone is the answer
 *	port:		connect and READ -- the service speaks first
 *	port:text	connect, write `text' and CRLF, then read
 *	uport		a DATAGRAM service: one talk(1) rendezvous request,
 *			and a well-formed reply is the answer
 *
 * `u' is ntalk, the one datagram line in the shipped table, and it is asked in
 * the only language it speaks: a talk_request, answered with a talk_reply.
 * There is no connection there to lose -- inetd hands the socket to a child on
 * the first datagram and binds a fresh one when that child retires -- so what
 * ten rounds ask of it is whether the port goes on being answered across that
 * hand-over, which is the same question in the form the service has.
 *
 * The read is what makes "answered" mean answered.  A refused connection and a
 * service that accepts and says nothing are different failures, and a test that
 * only connected would call the second one a pass.  It is bounded by a poll(2)
 * timeout, so a service that never speaks fails in WAIT ms rather than hanging:
 * a test that cannot fail is worse than no test.
 *
 * THE SCORE IS PER PORT AND IS PRINTED AS A FRACTION -- `port 7: 10/10' -- so a
 * judge can require the whole count rather than the last round, and the round
 * each port first failed in is printed with it.  Every line starts with
 * "repeatclient:" so a scripted run can pick it out.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "../talk.h"

extern int errno;
extern unsigned long inet_addr();

#define MAXPORT		8
#define BUFLEN		256
#define WAIT		30000		/* ms to wait for one answer	*/
#define ROUNDS		10

struct probe {
	int	p_port;
	int	p_udp;			/* a `u' was given: one datagram */
	int	p_read;			/* a ':' was given: read the answer */
	char	*p_text;		/* written first, or null	*/
	int	p_ok;			/* rounds answered		*/
	int	p_bad;			/* the first round that did not	*/
};

static struct probe probes[MAXPORT];
static int nprobe;

/*
 * One datagram caller.  The socket is bound to a port the stack chooses and
 * then asked which, because the reply comes back to the control address the
 * request carries and this program has to be able to name its own.
 *
 * LOOK_UP for an invitation nobody left: the answer is NOT_HERE, and NOT_HERE
 * is an answer -- what is being asked is whether the service is there at all,
 * and a lookup changes nothing on the machine, so ten rounds leave it as they
 * found it.
 */
static int oneshot_udp(pr, addr, why)
struct probe *pr;
unsigned long addr;
char *why;
{
	struct talk_request rq;
	struct talk_reply rp;
	struct sockaddr_in sin, serv;
	struct pollfd set[1];
	int s, n, len;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		sprintf(why, "no socket (errno %d)", errno);
		return 0;
	}
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = 0;
	sin.sin_addr.s_addr = addr;
	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		sprintf(why, "bind failed (errno %d)", errno);
		close(s);
		return 0;
	}
	len = sizeof(sin);
	if (getsockname(s, (struct sockaddr *)&sin, &len) < 0)
	{
		sprintf(why, "getsockname failed (errno %d)", errno);
		close(s);
		return 0;
	}
	memset((char *)&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_port = htons(pr->p_port);
	serv.sin_addr.s_addr = addr;

	memset((char *)&rq, 0, sizeof(rq));
	rq.version = TALK_VERSION;
	rq.type = LOOK_UP;
	rq.pid = (long)getpid();
	strncpy(rq.luser, "root", USER_SIZE);
	strncpy(rq.ruser, "guest", USER_SIZE);
	rq.addr.sa_family = htons(AF_INET);
	rq.addr.sin_port = htons(4000);
	rq.addr.sin_addr = addr;
	rq.ctl_addr.sa_family = htons(AF_INET);
	rq.ctl_addr.sin_port = sin.sin_port;
	rq.ctl_addr.sin_addr = addr;
	if (sendto(s, (char *)&rq, (int)sizeof(rq), 0,
			(struct sockaddr *)&serv, sizeof(serv)) < 0)
	{
		sprintf(why, "sendto failed (errno %d)", errno);
		close(s);
		return 0;
	}
	set[0].fd = s;
	set[0].events = POLLIN;
	set[0].revents = 0;
	if ((n = poll(set, (unsigned long)1, WAIT)) < 0)
	{
		sprintf(why, "poll failed (errno %d)", errno);
		close(s);
		return 0;
	}
	if (n == 0)
	{
		sprintf(why, "no answer in %d ms", WAIT);
		close(s);
		return 0;
	}
	if ((n = recvfrom(s, (char *)&rp, (int)sizeof(rp), 0,
			(struct sockaddr *)0, (int *)0)) != sizeof(rp))
	{
		sprintf(why, "a %d-byte answer is not a %d-byte reply",
			n, (int)sizeof(rp));
		close(s);
		return 0;
	}
	sprintf(why, "answered %d [type %d answer %d]", n, (int)rp.type,
		(int)rp.answer);
	close(s);
	return 1;
}

/*
 * One caller: connect, say what this service is owed, read the answer if it has
 * one, and close.  Returns 1 if the service answered.  What went wrong is
 * printed only for the first round that fails, by the caller -- a port that has
 * stopped being served fails every round after it and the log is read by a
 * person.
 */
static int oneshot(pr, addr, why, whylen)
struct probe *pr;
unsigned long addr;
char *why;
int whylen;
{
	struct sockaddr_in sin;
	struct pollfd set[1];
	char buf[BUFLEN];
	int s, n, i;

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		sprintf(why, "no socket (errno %d)", errno);
		return 0;
	}
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(pr->p_port);
	sin.sin_addr.s_addr = addr;
	if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		sprintf(why, "connect failed (errno %d)", errno);
		close(s);
		return 0;
	}
	if (pr->p_text != (char *)0)
	{
		n = strlen(pr->p_text);
		if (n > BUFLEN - 2)
			n = BUFLEN - 2;
		memcpy(buf, pr->p_text, n);
		buf[n++] = '\r';
		buf[n++] = '\n';
		if (write(s, buf, n) != n)
		{
			sprintf(why, "write failed (errno %d)", errno);
			close(s);
			return 0;
		}
	}
	if (!pr->p_read)
	{
		close(s);
		return 1;
	}
	set[0].fd = s;
	set[0].events = POLLIN;
	set[0].revents = 0;
	/* The (unsigned long) cast is REQUIRED: upoll() declares that argument
	 * unsigned long and K&R has no prototype to widen an int for it. */
	if ((n = poll(set, (unsigned long)1, WAIT)) < 0)
	{
		sprintf(why, "poll failed (errno %d)", errno);
		close(s);
		return 0;
	}
	if (n == 0)
	{
		sprintf(why, "no answer in %d ms", WAIT);
		close(s);
		return 0;
	}
	if ((n = read(s, buf, sizeof(buf) - 1)) <= 0)
	{
		sprintf(why, "read returned %d (errno %d)", n, errno);
		close(s);
		return 0;
	}
	buf[n] = '\0';
	for (i = 0; i < n; i++)
		if (buf[i] < ' ' && buf[i] != '\t')
			buf[i] = ' ';
	if (whylen > 0)
		sprintf(why, "answered %d [%s]", n, buf);
	close(s);
	return 1;
}

int main(argc, argv)
int argc;
char **argv;
{
	unsigned long addr;
	char *host, *colon;
	char why[BUFLEN + 64];
	int rounds, ai, i, r, all;

	rounds = ROUNDS;
	ai = 1;
	while (ai < argc && argv[ai][0] == '-' && argv[ai][1] != '\0')
	{
		if (argv[ai][1] == 'n' && ai + 1 < argc)
			rounds = atoi(argv[++ai]);
		else
			break;
		ai++;
	}
	if (argc - ai < 2 || rounds < 1)
	{
		fprintf(stderr,
		"usage: repeatclient [-n rounds] host [u]port[:[text]] ...\n");
		return 1;
	}
	host = argv[ai++];
	if ((addr = inet_addr(host)) == (unsigned long)-1)
	{
		printf("repeatclient: %s is not an address\n", host);
		return 1;
	}
	for (; ai < argc && nprobe < MAXPORT; ai++)
	{
		if (argv[ai][0] == 'u')
		{
			probes[nprobe].p_udp = 1;
			argv[ai]++;
		}
		colon = strchr(argv[ai], ':');
		if (colon != (char *)0)
		{
			*colon = '\0';
			probes[nprobe].p_read = 1;
			probes[nprobe].p_text = (colon[1] != '\0')
				? colon + 1 : (char *)0;
		}
		probes[nprobe].p_port = atoi(argv[ai]);
		probes[nprobe].p_bad = 0;
		nprobe++;
	}

	printf("repeatclient: %s, %d ports, %d rounds\n", host, nprobe, rounds);
	fflush(stdout);
	/*
	 * ROUND BY ROUND AND NOT PORT BY PORT, so that a port which has lost its
	 * listener is seen to have lost it while its neighbours still answer:
	 * running one port to exhaustion first would leave the others' rounds
	 * minutes later, when whatever is being blamed has moved on.
	 */
	for (r = 1; r <= rounds; r++)
	{
		for (i = 0; i < nprobe; i++)
		{
			why[0] = '\0';
			if (probes[i].p_udp
				? oneshot_udp(&probes[i], addr, why)
				: oneshot(&probes[i], addr, why,
					(int)sizeof(why)))
			{
				probes[i].p_ok++;
				printf("repeatclient: round %d port %d: %s\n",
					r, probes[i].p_port,
					why[0] ? why : "connected");
			}
			else
			{
				if (probes[i].p_bad == 0)
					probes[i].p_bad = r;
				printf("repeatclient: round %d port %d: FAILED %s\n",
					r, probes[i].p_port, why);
			}
			fflush(stdout);
		}
	}

	all = 1;
	for (i = 0; i < nprobe; i++)
	{
		printf("repeatclient: port %d: %d/%d",
			probes[i].p_port, probes[i].p_ok, rounds);
		if (probes[i].p_bad != 0)
		{
			printf(", first failed in round %d", probes[i].p_bad);
			all = 0;
		}
		printf("\n");
	}
	printf("repeatclient: %s\n", all ? "every port answered every round"
		: "a port stopped answering");
	fflush(stdout);
	return all ? 0 : 1;
}
