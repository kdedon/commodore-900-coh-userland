/*
 * udppoll.c -- does poll() on a socket mean what a BSD program expects?
 *
 *	udppoll [addr]			default 10.0.0.2
 *
 * Two bound UDP sockets on this machine, as udpecho.c does, but the receiving
 * end WAITS with poll() instead of blocking in recvfrom().  That is the order
 * every select/poll-driven network program uses -- hunt(6), and every server --
 * and it is the order the channel protocol does not naturally support: a
 * request/reply channel has nothing on its reply FIFO until a request has been
 * sent, so poll() on a socket reported "not readable" no matter how much data
 * had arrived.  libsocket now keeps a READ armed to close that gap.
 *
 * Three things are checked, in the order they can fail:
 *
 *	1. poll() with a timeout RETURNS on an idle socket.  If arming were
 *	   broken the other way -- readable when nothing has arrived -- test 2
 *	   would pass anyway, so the idle case has to be checked first.
 *	2. poll() reports readable once a datagram is sent, and the data is
 *	   there when read.
 *	3. it goes back to not-readable afterwards, rather than spinning: a
 *	   poll loop that always fires is as useless as one that never does.
 *
 * Every line starts with "udppoll:" so a scripted run can pick it out.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <errno.h>

extern int errno;
extern unsigned long inet_addr();

#define PORT_A	7011
#define PORT_B	7012
#define MSG	"poll-me"

static int bindport(port, addr)
int port;
unsigned long addr;
{
	int s;
	struct sockaddr_in sin;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		printf("udppoll: socket(%d) failed errno %d\n", port, errno);
		return -1;
	}
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = addr;
	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		printf("udppoll: bind(%d) failed errno %d\n", port, errno);
		return -1;
	}
	return s;
}

/*
 * poll one fd for `ms' milliseconds; returns the count, and revents by ref.
 *
 * The (unsigned long) cast is REQUIRED.  poll(2)'s second argument is an
 * unsigned long in this ABI -- the kernel's upoll() declares it so, and the
 * syscall table budgets 2+4+2 argument bytes -- and K&R has no prototype to
 * widen a literal `1' for us.  Passing an int pushes two bytes where the kernel
 * reads four, so it takes msec's bytes as the count's high half, sees a huge
 * number of fds and returns EINVAL on every call.  That is what this test
 * reported the first time it ran, and it was the test that was wrong.
 *
 * Every vendored program that calls poll() needs the same cast (libc's
 * select() carries it); hunt(6) calls poll(set, 2, INFTIM) in three places.
 */
static int waitfd(fd, ms, revents)
int fd, ms;
int *revents;
{
	struct pollfd set[1];
	int n;

	set[0].fd = fd;
	set[0].events = POLLIN;
	set[0].revents = 0;
	n = poll(set, (unsigned long)1, ms);
	*revents = set[0].revents;
	return n;
}

int main(argc, argv)
int argc;
char **argv;
{
	int a, b, n, alen, rev, fails;
	unsigned long me;
	struct sockaddr_in to, from;
	char buf[128];

	me = inet_addr(argc > 1 ? argv[1] : "10.0.0.2");
	fails = 0;

	if ((a = bindport(PORT_A, me)) < 0 || (b = bindport(PORT_B, me)) < 0)
		return 1;
	printf("udppoll: bound ports %d and %d\n", PORT_A, PORT_B);
	fflush(stdout);

	/* 1. idle: poll must time out rather than report data or fail. */
	n = waitfd(b, 1000, &rev);
	printf("udppoll: idle poll returned %d revents 0x%x (errno %d)\n",
		n, rev, errno);
	fflush(stdout);
	if (n != 0)
	{
		printf("udppoll: FAIL -- idle socket did not time out\n");
		fails++;
	}

	memset((char *)&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_port = htons(PORT_B);
	to.sin_addr.s_addr = me;
	n = sendto(a, MSG, sizeof(MSG) - 1, 0, (struct sockaddr *)&to,
		sizeof(to));
	printf("udppoll: sent %d (errno %d)\n", n, errno);
	fflush(stdout);

	/* 2. a datagram has arrived: poll must say so, and the data be there. */
	n = waitfd(b, 5000, &rev);
	printf("udppoll: poll after send returned %d revents 0x%x\n", n, rev);
	fflush(stdout);
	if (n != 1 || !(rev & POLLIN))
	{
		printf("udppoll: FAIL -- poll did not report the datagram\n");
		fails++;
	}
	else
	{
		alen = sizeof(from);
		n = recvfrom(b, buf, sizeof(buf) - 1, 0,
			(struct sockaddr *)&from, &alen);
		if (n <= 0)
		{
			printf("udppoll: FAIL -- recvfrom got %d errno %d\n",
				n, errno);
			fails++;
		}
		else
		{
			buf[n] = '\0';
			printf("udppoll: read [%s] from port %d\n", buf,
				ntohs(from.sin_port));
			if (strcmp(buf, MSG) != 0)
			{
				printf("udppoll: FAIL -- wanted [%s]\n", MSG);
				fails++;
			}
		}
	}
	fflush(stdout);

	/* 3. and back to quiet: a loop that always fires is no better than one
	 * that never does. */
	n = waitfd(b, 1000, &rev);
	printf("udppoll: poll after reading returned %d revents 0x%x\n",
		n, rev);
	if (n != 0)
	{
		printf("udppoll: FAIL -- still readable with nothing to read\n");
		fails++;
	}

	printf("udppoll: %s\n", fails ? "FAIL" : "PASS");
	close(a);
	close(b);
	return fails ? 1 : 0;
}
