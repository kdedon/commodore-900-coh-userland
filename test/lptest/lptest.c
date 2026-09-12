/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * lptest -- is the Centronics printer driver dispatched on major 3?
 *
 * Opens /dev/rlp and reports what came back.  Three outcomes tell three
 * different things apart:
 *
 *	ENXIO	major 3 has no driver -- the slot in wdcon.c's drvl[] is empty.
 *	EDATTN	the driver answered and found BUSY asserted, i.e. no printer is
 *		attached.  That is the right answer on a bare machine: the
 *		Centronics inputs idle high with nothing on the cable.
 *	success	a printer is ready.
 *
 * The RAW minor is the one to open: in cooked mode lpopen and lpclose each
 * queue a '\r', and the close then drains the buffer by sleeping until the
 * printer's acknowledge interrupt arrives -- so on a machine with no printer
 * even an open/close pair never returns.  Nothing is written here for the same
 * reason; writing needs hardware, not a second test.
 *
 * That paragraph is also the reason for the deadline.  The distinction between
 * the raw and cooked minors is a property of the driver being tested: a raw
 * open that waits for an acknowledge interrupt that no printer will ever send
 * is precisely the defect, and without a bound it stops the run instead of
 * being reported by it.
 */
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#define DEADLINE 30

/*
 * The deadline expired.
 */
hung()
{
	printf("FAIL lp: /dev/rlp did not answer within %d s -- the RAW open"
		" is waiting for\n", DEADLINE);
	printf("FAIL lp: an acknowledge interrupt, which is the cooked minor's"
		" behaviour and\n");
	printf("FAIL lp: not this one's.\n");
	fflush(stdout);
	exit(1);
}

main()
{
	int fd;

	signal(SIGALRM, hung);
	alarm(DEADLINE);
	errno = 0;
	if ((fd = open("/dev/rlp", O_WRONLY)) >= 0) {
		printf("PASS lp: /dev/rlp opened -- printer ready\n");
		close(fd);
		exit(0);
	}
	if (errno == EDATTN) {
		printf("PASS lp: driver answered EDATTN -- no printer attached\n");
		exit(0);
	}
	if (errno == ENXIO) {
		printf("FAIL lp: ENXIO -- major 3 has no driver\n");
		exit(1);
	}
	printf("FAIL lp: open failed with errno %d\n", errno);
	exit(1);
}
