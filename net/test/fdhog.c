/*
 * fdhog.c -- run a program with almost every descriptor already taken.
 *
 *	fdhog [-f free] program [argument ...]
 *
 * A server that cannot open a channel is not a hypothetical: NOFILE is 20 here
 * and a socket costs two descriptors, so a switchboard with seven services is
 * at the ceiling with nothing to spare (net/inetd.c, MAXSERV).  What that
 * scarcity produces is accept() finding no room for the replacement listening
 * channel it must open before it hands a connection over, and there is no way
 * to ask a running program for it from outside: descriptors are per process, so
 * the pressure has to be INHERITED.  This program is that inheritance -- it
 * fills its own descriptor table, gives back exactly `free' of them, and execs
 * the program under test into what is left.
 *
 * `free', not a count to hold, and that is the whole design.  How many
 * descriptors a program starts with depends on the shell that ran it and on
 * this program's own standard files, so a fixed number of opens leaves an
 * amount of room nobody stated.  Opening until the table is full and then
 * closing `free' of them states it: the program that is exec'd has exactly
 * `free' descriptors and no other arithmetic can be wrong.
 *
 * THREE is the useful setting for a server, and it is the default.  Opening one
 * channel to the inet daemon takes three descriptors at once (the request FIFO,
 * the reply FIFO, and the rendezvous the open is announced on) and keeps two,
 * so a server given three can bind exactly one listening socket and is then one
 * descriptor short of ever opening a second channel -- which is the condition
 * its accept() path is written for and which nothing else here can produce.
 *
 * Every line starts with "fdhog:" so a scripted run can pick it out.
 */
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>

extern int errno;

#define HELDMAX		64	/* more than NOFILE on any target here */
#define FREE		3	/* one channel's worth, less the one it keeps */

static int held[HELDMAX];

main(argc, argv)
int argc;
char **argv;
{
	int nfree = FREE;
	int nheld = 0;
	int ai = 1;
	int fd, i;

	if (ai < argc && argv[ai][0] == '-' && argv[ai][1] == 'f'
			&& argv[ai][2] == '\0' && ai + 1 < argc)
	{
		nfree = atoi(argv[++ai]);
		ai++;
	}
	if (ai >= argc || nfree < 0)
	{
		fprintf(stderr,
			"usage: fdhog [-f free] program [argument ...]\n");
		exit(2);
	}

	/*
	 * /dev/null because it is the one file every target has and opening it
	 * costs nothing anywhere else: this is a descriptor to hold, not a file
	 * to read.
	 */
	while (nheld < HELDMAX && (fd = open("/dev/null", O_RDONLY)) >= 0)
		held[nheld++] = fd;
	if (nheld == 0)
	{
		fprintf(stderr, "fdhog: no descriptor to take: errno %d\n",
			errno);
		exit(1);
	}
	/*
	 * Given back from the END of what was taken, so that what is free is
	 * the top of the table and what is held is one block below it.  Which
	 * numbers those are does not matter to the program under test; that
	 * they are not scattered through the table does, since a server that
	 * reports its own descriptors is easier to read against a run of them.
	 */
	for (i = 0; i < nfree && nheld > 0; i++)
		close(held[--nheld]);
	if (i < nfree)
	{
		fprintf(stderr,
			"fdhog: only %d descriptors could be taken, %d asked"
			" to be left free\n", i, nfree);
		exit(1);
	}
	printf("fdhog: holding %d descriptors, %d free, running %s\n",
		nheld, nfree, argv[ai]);
	fflush(stdout);
	execv(argv[ai], &argv[ai]);
	fprintf(stderr, "fdhog: %s: errno %d\n", argv[ai], errno);
	exit(1);
}
