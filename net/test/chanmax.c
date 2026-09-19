/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * chanmax.c -- how many connections can one PROCESS have open at once?
 *
 *	chanmax [n [least]]		default 24 channels, at least 6
 *
 * One /dev/tcp descriptor is one connection, and every one of them is a channel
 * to the inet daemon.  There are TWO limits on how many can exist, and this
 * program can only reach the nearer one:
 *
 *   - THE MACHINE'S.  The daemon holds one descriptor per channel (the request
 *     FIFO; the reply direction goes through inet/coh_fdc.c, which keeps a
 *     descriptor only while there are spare ones), so with NOFILE 20 it can
 *     carry fifteen channels.  The daemon prints that number when it starts:
 *     "inet: ready, 15 connections".  Exhaustion here answers ENFILE.
 *   - THIS PROCESS'S.  A channel costs the CLIENT two descriptors -- the
 *     request FIFO it writes and the reply FIFO it reads, which is also the
 *     socket descriptor -- so one process holds at most eight of them whatever
 *     the daemon can do.  Exhaustion here answers EMFILE, and it comes first
 *     unless the daemon is already nearly full.
 *
 * So a run of this program alone measures the per-process limit.  To see the
 * machine's, run it in several shells at once and add the counts up:
 *
 *	chanmax 8 6 & chanmax 8 6 & chanmax 8 6 &
 *
 * IT IS A CHECK AS WELL AS A MEASUREMENT.  Printing the number and exiting 0
 * whatever it was left nothing that could ever fail: a daemon that refused the
 * FIRST channel, or one that never refused at all, both read as a successful
 * run.  So the answer is scored:
 *
 *	- a refusal must arrive.  No refusal within `n' means either the ceiling
 *	  is above what was asked for -- in which case say so and ask for more
 *	  -- or channels are being handed out that nothing can service.
 *	- it must arrive with ENFILE (the daemon has no descriptors left --
 *	  coh_sr.c sr_refuse) or EMFILE (this process has none left).  Any other
 *	  errno is a failure, not a limit, and a ceiling measured from one is
 *	  nobody's ceiling.  Which of the two arrived is printed, because it
 *	  says which limit was reached.
 *	- at least `least' channels must have been open when it came.  Eight is
 *	  what a healthy process reaches; a run that manages one or two has found
 *	  a defect, not a limit -- and a telnetd on a machine that can hold one
 *	  connection is not a telnetd.
 *	- every one of them must close again.
 *
 * `least' is deliberately below eight: the exact number depends on how many
 * descriptors the shell handed this process and on how full the daemon already
 * is, and what this guards against is a collapse, not a drift.
 *
 * Every line starts with "chanmax:" so a scripted run can pick it out.
 */
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>

#define MAXFD	24
#define LEAST	6		/* fewer than this is a defect, not a ceiling */
/*
 * COHERENT's ENFILE (include/errno.h), which is what ichan_fail() leaves in
 * errno when the daemon answers sr_refuse().  Spelled out rather than taken
 * from <errno.h>: this file is built with -Iinclude, where net/include/errno.h
 * is the stack's MINIX header and its ENFILE is the NEGATIVE status the daemon
 * puts on the wire, not the number a client sees.
 */
#define ENFILE_COH	23
#define EMFILE_COH	24		/* this process's own descriptors */

extern int errno;

int main(argc, argv)
int argc;
char **argv;
{
	int fd[MAXFD];
	int want, least, i, n, e, fails;

	want = argc > 1 ? atoi(argv[1]) : MAXFD;
	if (want > MAXFD)
		want = MAXFD;
	least = argc > 2 ? atoi(argv[2]) : LEAST;
	fails = 0;
	e = 0;

	for (n = 0; n < want; n++)
	{
		if ((fd[n] = open("/dev/tcp", O_RDWR)) < 0)
		{
			e = errno;
			printf("chanmax: %d open, next refused with errno %d\n",
				n, e);
			fflush(stdout);
			break;
		}
		/* No `>': the harness takes `#' or `>' as a prompt. */
		printf("chanmax: channel %d is fd %d\n", n, fd[n]);
		fflush(stdout);
	}

	if (n == want)
	{
		printf("chanmax: FAIL -- %d open and no refusal; the ceiling is"
			" above %d, so nothing was measured\n", n, want);
		fails++;
	}
	else
	{
		if (e == ENFILE_COH)
			printf("chanmax: the DAEMON is full (ENFILE): this is"
				" the machine's ceiling\n");
		else if (e == EMFILE_COH)
			printf("chanmax: THIS PROCESS is full (EMFILE): two"
				" descriptors a channel, so the machine may"
				" have more to give\n");
		else
		{
			printf("chanmax: FAIL -- refused with errno %d, which"
				" is neither ENFILE (%d) nor EMFILE (%d): that"
				" is a failure, not a ceiling\n",
				e, ENFILE_COH, EMFILE_COH);
			fails++;
		}
		if (n < least)
		{
			printf("chanmax: FAIL -- only %d channel(s) before the"
				" refusal, wanted at least %d\n", n, least);
			fails++;
		}
	}

	for (i = 0; i < n; i++)
		if (close(fd[i]) < 0)
		{
			printf("chanmax: FAIL -- channel %d would not close,"
				" errno %d\n", i, errno);
			fails++;
		}
	printf("chanmax: closed %d -- %s (ceiling %d)\n", n,
		fails ? "FAIL" : "PASS", n);
	return fails ? 1 : 0;
}
