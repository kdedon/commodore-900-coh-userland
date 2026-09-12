/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * sockcycle.c -- does a socket opened and closed again give its memory back?
 *
 *	sockcycle [cycles [per-cycle]]		default 40 cycles of 4
 *
 * libsocket keeps a socket's state -- 624 bytes, 512 of them the channel's hold
 * buffer -- on the heap for as long as the socket is open.  SO A CLOSED SOCKET
 * HAS TO GIVE THE BLOCK BACK, and that is what this measures.  A close that
 * releases the table slot but not the memory is invisible to every other test on
 * this machine: the sockets keep working and the ceiling stays where it is, and
 * the only symptom is a daemon that grows by 624 bytes a connection until it
 * dies, days later, of an 863 KB machine.
 *
 * The instrument is the break address.  sbrk(0) is where the heap ends; if a
 * cycle's sockets are freed, the next cycle's come out of the same blocks and the
 * break does not move.  A leak moves it by 624 bytes a socket -- forty cycles of
 * four is 100 KB, which is neither missable nor mistakable for allocator noise.
 *
 * The FIRST cycle is not part of the comparison: it is the one entitled to grow
 * the heap, since its blocks never existed before, and a baseline taken ahead of
 * it would read the allocator working as a leak.  The baseline is the break after
 * cycle 1, and every cycle from there on must be free.
 *
 * It also scores the count, because a leak and a lost slot look alike from
 * outside: if `per-cycle' sockets could be opened the first time round, exactly
 * that many must be available every time round.  A count that decays says slots
 * are being consumed and not returned, which is the same defect one level up.
 *
 * Every line starts with "sockcycle:" so a scripted run can pick it out.
 */
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

#define CYCLES	40
#define PERCYC	4		/* two descriptors each, so four is safe at NUFILE 20 */
#define MAXPER	8

extern int errno;

int main(argc, argv)
int argc;
char **argv;
{
	int fd[MAXPER];
	unsigned long base, end;
	int cycles, per, first, c, i, n, fails;

	cycles = argc > 1 ? atoi(argv[1]) : CYCLES;
	per = argc > 2 ? atoi(argv[2]) : PERCYC;
	if (per > MAXPER)
		per = MAXPER;
	fails = 0;
	first = -1;
	base = 0L;

	/*
	 * Print before the first measurement: stdio's buffer is a malloc(BUFSIZ)
	 * on the first write to the stream, and taking the baseline ahead of it
	 * would charge that one buffer to the sockets.
	 */
	printf("sockcycle: %d cycles of %d\n", cycles, per);
	fflush(stdout);

	for (c = 1; c <= cycles; c++)
	{
		for (n = 0; n < per; n++)
			if ((fd[n] = open("/dev/tcp", O_RDWR)) < 0)
				break;
		if (n == 0)
		{
			printf("sockcycle: FAIL -- cycle %d opened nothing,"
				" errno %d\n", c, errno);
			fails++;
			break;
		}
		for (i = 0; i < n; i++)
			if (close(fd[i]) < 0)
			{
				printf("sockcycle: FAIL -- cycle %d, socket %d"
					" would not close, errno %d\n",
					c, i, errno);
				fails++;
			}
		if (first < 0)
		{
			/* Cycle 1: the heap grows for real here.  Record what
			 * it reached and what the machine could give. */
			first = n;
			base = (unsigned long)sbrk(0);
			printf("sockcycle: cycle 1 opened %d, break %lu\n",
				first, base);
			fflush(stdout);
			continue;
		}
		if (n != first)
		{
			printf("sockcycle: FAIL -- cycle %d opened %d, cycle 1"
				" opened %d: slots are not coming back\n",
				c, n, first);
			fails++;
			break;
		}
	}

	end = (unsigned long)sbrk(0);
	printf("sockcycle: break %lu -> %lu after %d cycles\n",
		base, end, cycles);
	if (base != 0L && end != base)
	{
		printf("sockcycle: FAIL -- the heap grew by %ld bytes across"
			" cycles 2..%d; a socket is not giving its block back\n",
			(long)end - (long)base, cycles);
		fails++;
	}
	printf("sockcycle: %d cycles of %d sockets, heap flat -- %s\n",
		cycles, first < 0 ? 0 : first, fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
