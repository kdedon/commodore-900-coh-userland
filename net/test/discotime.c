/*
 * discotime.c -- how long does hunt's driver discovery actually take?
 *
 *	discotime [seconds]		default 30
 *
 * hunt finds a game driver by sending one UDP datagram to port 26740 and
 * waiting for a reply -- with `poll(set, 1, 1000)', a single ONE-SECOND
 * window.  Miss it and `list_drivers()' returns an empty list, which every
 * caller reads as "no driver anywhere" and reports by printing nothing at all.
 *
 * That one second is a LAN assumption from a machine whose stack lived in the
 * kernel.  Here a datagram crosses a fifo to the inet daemon, comes back, and
 * the driver's reply makes the same trip again -- on a 6 MHz Z8001.  Whether
 * that fits in a second is a question about this machine, not about hunt, and
 * it is the difference between "the driver is broken" and "the driver was
 * still answering".
 *
 * So: send the same probe hunt sends, then poll in TENTH-of-a-second slices
 * and say how many elapsed.  The same binary runs under the instruction-level
 * emulator and under the simulator, so the two numbers are directly comparable
 * -- which is the only way to tell a slow machine from a broken one.
 *
 * Tenths, not whole seconds, because the margin is the question.  A
 * one-second slice can only report "inside hunt's window" or "outside it",
 * and the first run of this said "slice 1" on the emulator -- which reads as
 * "took a second" and actually means "somewhere in the first thousand
 * milliseconds".  Whether that is 20 ms or 900 ms decides whether hunt has
 * room to spare on real hardware or is passing by luck.
 *
 * Needs a driver running (/usr/games/lib/huntd).  Every line starts with
 * "discotime:" so a scripted run can pick it out.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>

extern int errno;
extern unsigned long inet_addr();

static char *me = "10.0.0.2";

#define TEST_PORT	26740		/* ('h'<<8)|'t', huntd/pathname.c */
#define C_SCORES	3		/* answered whether or not anyone plays */

int main(argc, argv)
int argc;
char **argv;
{
	int s, i, n, limit, slices, alen;
	unsigned short msg, reply;
	struct sockaddr_in to, from;
	struct pollfd set[1];

	limit = argc > 1 ? atoi(argv[1]) : 30;
	slices = limit * 10;		/* the wait, in tenths */

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		printf("discotime: socket errno %d\n", errno);
		return 1;
	}
	memset((char *)&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_port = htons(TEST_PORT);
	to.sin_addr.s_addr = inet_addr(me);

	msg = htons(C_SCORES);
	if (sendto(s, (char *)&msg, sizeof(msg), 0,
	    (struct sockaddr *)&to, sizeof(to)) < 0)
	{
		printf("discotime: sendto errno %d\n", errno);
		return 1;
	}
	printf("discotime: probe sent to port %d, waiting up to %d s\n",
		TEST_PORT, limit);
	fflush(stdout);

	set[0].fd = s;
	set[0].events = POLLIN;
	for (i = 1; i <= slices; i++)
	{
		errno = 0;
		n = poll(set, (unsigned long)(1), 100);
		if (n < 0 && errno != EINTR)
		{
			printf("discotime: poll errno %d\n", errno);
			return 1;
		}
		if (n <= 0)
			continue;
		alen = sizeof(from);
		n = recvfrom(s, (char *)&reply, sizeof(reply), 0,
			(struct sockaddr *)&from, &alen);
		if (n < 0)
		{
			printf("discotime: recvfrom errno %d\n", errno);
			return 1;
		}
		/*
		 * The reply is the tcp port the driver wants us on.  Print it
		 * unsigned: int is 16 bits and the stack hands out high ports,
		 * so %d turns 49152 into -16384 and reads like a failure.
		 */
		/*
		 * Printed as seconds.tenths rather than as milliseconds: int
		 * is 16 bits here, so `i * 100' overflows at a 33-second wait
		 * and the measurement would go negative exactly when it was
		 * reporting something interesting.
		 */
		printf("discotime: reply after %d.%d s: %d bytes, port %u\n",
			i / 10, i % 10, n, (unsigned)ntohs(reply));
		printf("discotime: %s\n",
			i <= 10 ? "PASS -- inside hunt's 1000 ms discovery window"
				: "SLOW -- hunt's 1000 ms window would have MISSED this");
		close(s);
		return i <= 10 ? 0 : 2;
	}
	printf("discotime: no reply in %d s -- no driver is answering at all\n",
		limit);
	close(s);
	return 1;
}
