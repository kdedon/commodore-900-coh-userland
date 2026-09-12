/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * portholder.c -- take a TCP port and keep it, so that nothing else can bind it.
 *
 *	portholder [-w wait] [-t hold] port
 *
 * The other half of what a server must do when its listening socket is gone:
 * bind a fresh one, and STOP asking when the port is somebody else's.  A port
 * held by another program does not come back by being asked, and a daemon that
 * asked for ever would write a line every turn for as long as the machine was
 * up -- so a test that only proved the recovery would leave the retry
 * unbounded.  This program is the something else.
 *
 * NO socket() CALL, and it must not be one: libsocket configures every socket
 * NWTC_SHARED, and the stack lets two SHARED descriptors name one local port
 * (that is how accept() opens its replacement listener while the first channel
 * carries a connection).  A SHARED claim would therefore not stop anybody.
 * What does is NWTC_EXCL, which tcp_setconf() refuses to place beside any other
 * descriptor on the same port and refuses to place a SHARED one beside:
 * EADDRINUSE, both ways round.  So this claim cannot be taken while a server
 * still holds the port, and no server can take the port back while this claim
 * stands -- which is exactly the pair of facts the test needs.
 *
 * IT RETRIES, once a second up to `wait' seconds, because the moment it is
 * meant to catch is the one where the port has just been given up.  A single
 * attempt would have to be timed against another program's failure, and this
 * one says on its own output whether it got the port and how long that took.
 * Then it holds for `hold' seconds and gives it back, so a run leaves the
 * machine as it found it.
 *
 * Every line starts with "portholder:" so a scripted run can pick it out.
 */
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <net/gen/in.h>
#include <net/gen/tcp.h>
#include <net/gen/tcp_io.h>
#include <net/hton.h>
#include <net/ioctl.h>

extern int errno;

#define WAIT	60		/* seconds spent trying for the port	*/
#define HOLD	120		/* seconds it is held once taken	*/

main(argc, argv)
int argc;
char **argv;
{
	struct nwio_tcpconf conf;
	int wait = WAIT, hold = HOLD;
	int ai = 1, port, fd, s;

	while (ai + 1 < argc && argv[ai][0] == '-' && argv[ai][2] == '\0')
	{
		if (argv[ai][1] == 'w')
			wait = atoi(argv[ai + 1]);
		else if (argv[ai][1] == 't')
			hold = atoi(argv[ai + 1]);
		else
			break;
		ai += 2;
	}
	if (argc - ai != 1)
	{
		fprintf(stderr,
			"usage: portholder [-w wait] [-t hold] port\n");
		exit(2);
	}
	port = atoi(argv[ai]);
	if (port <= 0)
	{
		fprintf(stderr, "portholder: %s is not a port\n", argv[ai]);
		exit(2);
	}

	if ((fd = open("/dev/tcp", O_RDWR)) < 0)
	{
		printf("portholder: /dev/tcp: errno %d\n", errno);
		exit(1);
	}
	memset((char *)&conf, 0, sizeof(conf));
	/*
	 * EXCL and a named local port, with no remote end named: this asks for
	 * the port itself and for nothing else, which is what a claim is.
	 */
	conf.nwtc_flags = NWTC_EXCL | NWTC_LP_SET | NWTC_UNSET_RA |
		NWTC_UNSET_RP;
	conf.nwtc_locport = htons((tcpport_t)port);

	for (s = 0; s <= wait; s++)
	{
		if (ioctl(fd, NWIOSTCPCONF, (char *)&conf) == 0)
		{
			printf("portholder: port %d taken after %d seconds,"
				" holding it for %d\n", port, s, hold);
			fflush(stdout);
			sleep((unsigned)hold);
			close(fd);
			printf("portholder: port %d released\n", port);
			exit(0);
		}
		sleep(1);
	}
	printf("portholder: port %d could not be taken in %d seconds"
		" (errno %d): something else still holds it\n",
		port, wait, errno);
	exit(1);
}
