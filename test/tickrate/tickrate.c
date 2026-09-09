/*
 * tickrate.c -- how much CPU work fits in one of the guest's own seconds?
 *
 *	tickrate [seconds]		default 5
 *
 * The number this prints needs NO host clock, which is the whole point.  Both
 * quantities come from inside the guest: work done, and the guest's own idea
 * of how long that took.  On a machine where the 100 Hz tick and the CPU are
 * in their correct proportion the answer is a property of the PORT, so the
 * emulator and the simulator must agree.  Where they disagree, the difference
 * is the emulation, not COHERENT.
 *
 * That matters because a wall-clock measurement cannot tell the two apart.
 * `sleep 5' took 140 s under the simulator, which reads as "the tick is 28x
 * slow" -- but a container running the guest at 10-22% of real speed already
 * accounts for a factor of 5 to 10 of that, and host load for some more.
 * Dividing one unknown by another is not a measurement.  Here, a LARGER number
 * than the emulator's means the tick is slow relative to the CPU: more work
 * fitted into what the guest was told was a second.
 *
 * The inner loop is deliberately trivial integer work and the outer loop calls
 * time(2) once per 1000 of them, so the syscall is ~0.1% of the sample rather
 * than the thing being measured.  `vol' is written every iteration and printed
 * at the end so no optimiser can delete the loop.
 *
 * Every line starts with "tickrate:" so a scripted run can pick it out.
 *
 * The number itself is a measurement, not a verdict -- it means something
 * only when compared with the same number from another instrument
 * (hostbuild/tickrate-test.py does that), so no "correct" rate is hard-coded
 * here.  But a rate is a QUOTIENT, and three failure modes make it
 * meaningless; they are refused, being the only three this program can
 * decide by itself:
 *
 *	the clock never advanced	t1 == t0: the sample took no time at
 *					all, and the rate would divide by it
 *	the clock ran backwards		t1 < t0.  The sampling loop breaks
 *					out on it, because its own exit test
 *					`t1 - t0 < secs' is never satisfied
 *					by a backwards clock
 *	no work was done		units == 0: the loop never completed
 *					a single 1000-iteration batch inside
 *					the whole sample
 *
 * Anything else is reported and left to the comparison.
 *
 * THE CLOCK-NEVER-ADVANCED CASE IS A SPIN, NOT A RETURN.  `while (time() ==
 * mark) ;' is how the sample is put on a tick boundary, and on a machine whose
 * tick never fires that loop never ends -- the check below for t1 == t0 is
 * downstream of a loop that never reaches it.  So the run carries a deadline,
 * and its expiry says which of the two it was: a stopped clock, or a sample
 * that was simply asked to be longer than the deadline.
 */
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>

#define INNER	1000
#define SLACK	60		/* seconds allowed on top of the sample */

long	vol;
int	deadline;

/*
 * The deadline expired.
 */
static
hung()
{
	printf("tickrate: FAIL -- no result within %d s.  Either the guest"
		" clock never\n", deadline);
	printf("tickrate: advanced (the boundary spin at the top never ends"
		" then, and the\n");
	printf("tickrate: t1 == t0 check below it is never reached), or the"
		" sample asked\n");
	printf("tickrate: for is longer than the deadline.\n");
	fflush(stdout);
	exit(1);
}

int main(argc, argv)
int argc;
char **argv;
{
	long	units;
	int	i, secs;
	time_t	t0, t1, mark;

	secs = argc > 1 ? atoi(argv[1]) : 5;
	if (secs < 1) {
		printf("tickrate: FAIL -- a sample of %d seconds measures"
			" nothing\n", secs);
		return 1;
	}

	deadline = secs + SLACK;
	(void)signal(SIGALRM, hung);
	(void)alarm(deadline);

	/*
	 * Start on a tick boundary.  time(2) has one-second resolution, so a
	 * sample begun mid-second is short by up to a whole second -- 20% of a
	 * five-second run, which is larger than the effect being looked for.
	 */
	mark = time((time_t *)0);
	while (time((time_t *)0) == mark)
		;
	t0 = time((time_t *)0);

	units = 0;
	while ((t1 = time((time_t *)0)) - t0 < (time_t)secs)
	{
		/* A clock that runs BACKWARDS never satisfies this loop's own
		 * exit condition, so without this the check for it below is
		 * unreachable -- the loop would still be turning when the
		 * deadline fired, and the specific answer would be lost behind
		 * the general one. */
		if (t1 < t0)
			break;
		for (i = 0; i < INNER; i++)
			vol = vol + i;
		units++;
	}

	if ((long)(t1 - t0) < 0L) {
		printf("tickrate: FAIL -- the guest clock read %ld then %ld:"
			" it ran BACKWARDS\n", (long)t0, (long)t1);
		return 1;
	}
	if ((long)(t1 - t0) == 0L) {
		printf("tickrate: FAIL -- the guest clock read %ld then %ld:"
			" no elapsed time to divide by\n",
			(long)t0, (long)t1);
		return 1;
	}
	if (units == 0L) {
		printf("tickrate: FAIL -- no work completed in %ld guest s\n",
			(long)(t1 - t0));
		return 1;
	}
	printf("tickrate: %ld kloop in %ld guest s = %ld kloop/guest-s\n",
		units, (long)(t1 - t0), units / (long)(t1 - t0));
	printf("tickrate: (vol %ld -- printed so the loop cannot be elided)\n",
		vol);
	return 0;
}
