/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * selectlong.c -- does select() actually wait as long as it was asked to?
 *
 *	selectlong [seconds]		default 40
 *
 * select() takes a struct timeval whose tv_sec is a LONG, and reaches the
 * kernel through poll(2), whose timeout is a plain int of milliseconds.  A
 * millisecond count only reaches 32.767 seconds in 16 bits, so every timeout
 * longer than that has to be served as more than one poll(2).  This measures
 * whether it is: the verdict is ELAPSED TIME, not the return value, because
 * the return value is the part that lies -- a select() that comes back after
 * 32 seconds saying "timed out" is indistinguishable, at the call site, from
 * one that waited the whole five minutes it was asked for.
 *
 * Every wait is measured with the guest's own time(2), and every bound is
 * stated in guest seconds.  Nothing here compares against a host clock: the
 * emulator and the simulator run the guest at their own speeds, so a wall
 * clock measures the instrument.  select()'s contract and time(2) are both
 * denominated in the same 100 Hz tick, so they scale together and the
 * quotient is a property of the port.
 *
 * TOLERANCE.  time(2) has one-second resolution, so a wait begun and ended
 * mid-second is measured up to a second short, and the kernel rounds
 * milliseconds up to whole ticks.  SLACK below is the allowance, and it only
 * has to be small compared with the DIFFERENCE the cases are separating: case
 * 1 asks for 40 s and a 16-bit clamp answers at 32, so the two verdicts are 8
 * seconds apart and a 2-second allowance cannot confuse them.  A slower or
 * faster machine moves both terms together and does not change that.
 *
 * The cases:
 *
 *	1  a timeout LONGER than 32.767 s on a descriptor that is never
 *	   ready.  Must return 0 having waited the whole time.  This is the
 *	   defect: a clamp answers 0 at 32 s.
 *	2  a SHORT timeout, same descriptor.  Must return 0 at about the time
 *	   asked -- a select() that always waits its longest is as wrong as
 *	   one that always waits its shortest.
 *	3  a long timeout with data arriving early.  Must return as soon as
 *	   the child writes, not at the end of the first chunk and not at the
 *	   end of the timeout: serving a long wait in pieces must not cost
 *	   the wakeup.
 *	4  a NEGATIVE timeout.  Must be refused with EINVAL rather than
 *	   treated as a wait of some other length.
 *
 * A run that hangs has no verdict, so the whole thing carries a deadline and
 * reports which case was outstanding when it fired.
 */
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>

extern int errno;

#define QUIET	"/sl.fifo"	/* a descriptor that is never ready	*/
#define SLACK	2		/* guest seconds of measurement error	*/
#define SHORT	3		/* case 2's timeout, in guest seconds	*/
#define EARLY	3		/* case 3: when the child writes	*/
#define LONGCAP	120		/* case 3's timeout: never reached	*/
#define SLOP	20		/* deadline margin over the whole run	*/

static char *step = "startup";
static int deadline;

static
hung()
{
	printf("FAIL selectlong: no verdict within %d s, waiting on: %s\n",
		deadline, step);
	printf("FAIL selectlong: a select() that never returns is as much a"
		" defect as one\n");
	printf("FAIL selectlong: that returns too early -- reported here"
		" rather than hung,\n");
	printf("FAIL selectlong: because a test that hangs has no verdict at"
		" all.\n");
	fflush(stdout);
	exit(1);
}

/*
 * Wait on `fd' for `secs' seconds and return the guest seconds that passed.
 * `rcp' takes select()'s own answer.  Both terms come from time(2).
 */
static long
timedwait(fd, secs, rcp)
int fd;
long secs;
int *rcp;
{
	fd_set rd;
	struct timeval tv;
	time_t t0, t1;

	FD_ZERO(&rd);
	FD_SET(fd, &rd);
	tv.tv_sec = secs;
	tv.tv_usec = 0L;
	t0 = time((time_t *)0);
	*rcp = select(fd + 1, &rd, (fd_set *)0, (fd_set *)0, &tv);
	t1 = time((time_t *)0);
	return (long)(t1 - t0);
}

main(argc, argv)
int argc;
char **argv;
{
	int quiet, rc, p[2], kid;
	long want, took;
	char buf[8];

	want = argc > 1 ? (long)atoi(argv[1]) : 40L;
	if (want <= 33L) {
		printf("FAIL selectlong: a %ld s timeout is inside the 32.767 s"
			" a 16-bit millisecond\n", want);
		printf("FAIL selectlong: count reaches, so it cannot tell a"
			" clamp from correct waiting\n");
		return 1;
	}

	deadline = (int)want + SHORT + EARLY + SLOP;
	(void)signal(SIGALRM, hung);
	(void)alarm(deadline);

	/*
	 * An empty named FIFO held open for reading and writing by this
	 * process alone: nothing can ever make it readable, so every wait on
	 * it runs to its own end and the elapsed time is select()'s alone.
	 */
	(void)unlink(QUIET);
	if (mknod(QUIET, 010000|0666, 0) < 0 ||
	    (quiet = open(QUIET, O_RDWR)) < 0) {
		printf("FAIL selectlong: cannot make a quiet descriptor\n");
		return 1;
	}

	/* ---- case 1: longer than a 16-bit millisecond count reaches ---- */
	printf("1 waiting %ld s on a descriptor nothing will ever ready\n",
		want);
	step = "case 1, the long timeout (THE SUSPECT)";
	took = timedwait(quiet, want, &rc);
	printf("1 select returned %d after %ld guest s\n", rc, took);
	if (rc != 0) {
		printf("FAIL selectlong: nothing can ready that descriptor,"
			" so the only correct\n");
		printf("FAIL selectlong: answer is 0; got %d (errno %d)\n",
			rc, errno);
		return 1;
	}
	if (took < want - (long)SLACK) {
		printf("FAIL selectlong: asked for %ld s, reported a TIMEOUT"
			" after %ld s.\n", want, took);
		printf("FAIL selectlong: the caller is told its deadline"
			" passed when it has not:\n");
		printf("FAIL selectlong: %ld seconds of the wait were never"
			" waited.\n", want - took);
		return 1;
	}
	printf("2 long timeout waited its full term\n");

	/* ---- case 2: a short timeout is still short ---- */
	step = "case 2, the short timeout";
	took = timedwait(quiet, (long)SHORT, &rc);
	printf("3 short select returned %d after %ld guest s\n", rc, took);
	if (rc != 0) {
		printf("FAIL selectlong: short wait answered %d, wanted 0\n",
			rc);
		return 1;
	}
	if (took > (long)(SHORT + SLACK)) {
		printf("FAIL selectlong: asked for %d s, waited %ld s -- a"
			" wait must not be\n", SHORT, took);
		printf("FAIL selectlong: rounded up to whatever unit it is"
			" served in\n");
		return 1;
	}
	printf("4 short timeout is still short\n");

	/* ---- case 3: a long timeout must not cost the wakeup ---- */
	if (pipe(p) < 0) {
		printf("FAIL selectlong: pipe\n");
		return 1;
	}
	if ((kid = fork()) < 0) {
		printf("FAIL selectlong: fork\n");
		return 1;
	}
	if (kid == 0) {
		sleep(EARLY);
		(void)write(p[1], "E", 1);
		_exit(0);
	}
	printf("5 child forked; selecting with a %d s cap for a write at %d s\n",
		LONGCAP, EARLY);
	step = "case 3, the early write under a long timeout";
	took = timedwait(p[0], (long)LONGCAP, &rc);
	printf("5 select returned %d after %ld guest s\n", rc, took);
	/* One descriptor is in the set, so a positive count is that one. */
	if (rc != 1) {
		printf("FAIL selectlong: the child wrote at %d s and select"
			" answered %d\n", EARLY, rc);
		return 1;
	}
	if (took > (long)(EARLY + SLACK)) {
		printf("FAIL selectlong: the child wrote at %d s but select"
			" did not return for\n", EARLY);
		printf("FAIL selectlong: %ld s -- a long wait served in pieces"
			" must still wake on\n", took);
		printf("FAIL selectlong: the event, not at the end of a"
			" piece\n");
		return 1;
	}
	(void)read(p[0], buf, sizeof(buf));
	printf("6 an early write still wakes a long wait\n");

	/* ---- case 4: a timeout that is not a length is refused ---- */
	step = "case 4, the negative timeout";
	took = timedwait(quiet, -1L, &rc);
	printf("7 negative select returned %d errno %d after %ld guest s\n",
		rc, errno, took);
	if (rc != -1 || errno != EINVAL) {
		printf("FAIL selectlong: a negative timeout is not a length."
			" It must be refused\n");
		printf("FAIL selectlong: with EINVAL (%d), not served as some"
			" other wait: got rc %d\n", EINVAL, rc);
		printf("FAIL selectlong: errno %d after %ld s\n", errno, took);
		return 1;
	}
	printf("8 a negative timeout is refused, not reinterpreted\n");

	printf("PASS selectlong\n");
	return 0;
}
