/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * swap.c -- does a process that was pushed out to disk come back intact?
 *
 * The kernel swapper (sys/coh/swap.c) moves whole SEGMENTS, and this machine
 * maps them a click at a time with no page table to fix up afterwards: a
 * segment that comes back at a different physical click base is only usable
 * if every descriptor derived from the old base has been rebuilt.  Nothing
 * about that is visible from userland except its result -- memory that reads
 * back the way it was written, or memory that reads back as somebody else's.
 * So this program writes a signature it can check byte for byte, arranges to
 * be swapped out while it is not running, and checks it again on the far side.
 *
 * The shape:
 *
 *	NSLEEP sleeper children.  Each fills its private data with a pattern
 *	  keyed to its own index, fills a stack region the same way, announces
 *	  itself, and then sleeps.  A sleeping process is not runnable, which
 *	  is exactly what makes it the swapper's first choice to evict.
 *	NHOG hog children, forked once every sleeper is asleep.  Each one
 *	  costs a full copy of the data segment, which is the memory demand
 *	  that forces the sleepers out.
 *	The sleepers wake, re-read every byte, and report.
 *
 * The pattern is a function of both the child's index and the offset, so a
 * segment restored at the wrong base, restored short, or restored from
 * another process's swap image all read as mismatches rather than as zeros --
 * and the first mismatch is printed with its offset, which says WHICH of
 * those it was.  Zeros mean the segment never came back; another child's
 * index means the disk queue handed out an overlapping extent.
 *
 * Sizing.  The point is to need more memory than the machine has: with the
 * defaults each child carries FILL bytes of data, so NSLEEP+NHOG+1 copies
 * have to exceed the free memory the boot banner reports (about 900 KB in the
 * emulator).  The program prints the arithmetic it is relying on, because a
 * run that never provoked a swap-out passes for the wrong reason -- if the
 * total is under the banner's figure, raise the counts rather than believe it.
 *
 * A machine WITHOUT a swapper fails this differently and recognisably: it
 * never gets past the hog forks.  Measured, it does not report ENOMEM there
 * either -- salloc()'s other fallback, krunch() compaction, physically copies
 * segments on every failed allocation, and the run simply stops making
 * progress.  Either way the give-away is the swap area itself: run the same
 * test on a kernel with no swapper and not one block of it is written.
 *
 * A MACHINE THAT STOPS MAKING PROGRESS IS THIS TEST'S OWN WORST OUTCOME.  The
 * paragraph above says it plainly: on a kernel with no swapper the run does not
 * report ENOMEM, it simply stops -- and every one of the parent's three waits is
 * unbounded.  The ready pipe has no writer that will ever close while any
 * sleeper is stuck, the result pipe is the same, and the final wait() loop waits
 * on children that may never be scheduled again.  So the run is under a
 * deadline: a test that hangs has no verdict, which is worse than one that
 * fails, and "no progress" is itself the answer here rather than an accident.
 *
 * Usage: swap [nsleep [nhog [snooze]]]
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>

extern int errno;

#define NSLEEP	8			/* sleepers to fork */
#define NHOG	8			/* hogs forked to squeeze them out */
#define SNOOZE	20			/* seconds a sleeper stays asleep */
#define HOGNAP	8			/* seconds a hog holds its memory */

/*
 * Long enough for the whole shape to play out -- SNOOZE plus HOGNAP plus the
 * time a loaded machine needs to move seventeen segments to disk and back --
 * and short enough that "it stopped making progress" is an answer and not an
 * afternoon.
 */
#define DEADLINE 300			/* seconds for the whole run */

/*
 * Private data.  One object, just under 64K: the standard test build
 * (hostbuild/build-tests.sh) links without the large model, so the whole
 * data segment has to fit one hardware segment.  Cost per process is raised
 * by carrying more of them rather than by carrying more per process.
 */
#define FILL	60000

char	blk[FILL];

#define STKFILL	2000			/* stack bytes a sleeper signs */

char	*who = "swap";
int	nsleep = NSLEEP;
int	nhog = NHOG;
int	snooze = SNOOZE;
int	rdyfd[2];			/* children -> parent: I am ready */
int	resfd[2];			/* children -> parent: my verdict */
int	nready;				/* sleepers that announced themselves */
int	nback;				/* sleepers that reported a verdict */

#define MAXKID	256
int	kid[MAXKID];			/* every child forked, for the deadline */
int	nkid;

/*
 * The deadline expired.  Which of the two counts is short says WHERE it
 * stopped: short of nready, the sleepers never got their memory signed and
 * asleep; short of nback, they went out to disk and did not come back.
 */
static void
hung()
{
	int i;

	printf("%s: FAIL -- no verdict within %d s: %d of %d sleepers ready,"
		" %d reported back.\n", who, DEADLINE, nready, nsleep, nback);
	printf("%s: A kernel with no swapper does not refuse the hogs, it stops"
		" making\n", who);
	printf("%s: progress -- which is this outcome, reported rather than"
		" hung.\n", who);
	fflush(stdout);
	/* By pid, never kill(0, ...): this program's process group holds the
	 * shell that started it. */
	for (i = 0; i < nkid; i++)
		(void)kill(kid[i], SIGKILL);
	exit(1);
}

/*
 * The signature byte for offset `o' of child `ix'.  Both terms matter: the
 * index alone would let two children's images be swapped for each other
 * without notice, and the offset alone would hide a segment restored at the
 * wrong base within itself.
 */
static int
sigbyte(ix, o)
long o;
{
	return ((int)(((long)(ix+1)*7L + o*3L + (o>>8)) & 0xFF));
}

/*
 * Sign the data blocks.
 */
static void
signdata(ix)
{
	register long i;
	register char *p;

	p = blk;
	for (i = 0; i < FILL; i++)
		p[i] = sigbyte(ix, i);
}

/*
 * Check the data blocks.  Returns 0 when every byte reads back, else the
 * 1-based offset of the first byte that did not, with the two values printed.
 */
static long
ckdata(ix)
{
	register long i;
	register char *p;
	int want, got;

	p = blk;
	for (i = 0; i < FILL; i++) {
		want = sigbyte(ix, i);
		got = p[i] & 0xFF;
		if (got != want) {
			printf("%s: child %d data at %ld: want %02x got %02x\n",
				who, ix, i, want, got);
			return (i+1);
		}
	}
	return (0L);
}

/*
 * Announce, then sleep.  Called from the bottom of the signed stack so that
 * the frames above it are live -- and therefore have to survive the swap --
 * for the whole time the child is off the run queue.
 */
static int
snoozer(ix)
{
	char b;

	b = (char)ix;
	write(rdyfd[1], &b, 1);
	sleep(snooze);
	return (0);
}

/*
 * Sign a run of stack on the way down, sleep at the bottom, and check it on
 * the way back up.  The stack is its own segment here and travels to disk
 * separately from the data, so it needs its own signature -- and the
 * signature only means anything while the frames holding it are still live,
 * which is why the sleep happens at the bottom of this recursion rather than
 * after it has unwound.  Each frame is read back only once every deeper frame
 * has been discarded, so a stack restored at the wrong base is caught even
 * where the outermost frame happens to land correctly.
 *
 * Returns 0, or the 1-based stack offset of the first byte that changed.
 */
static int
stksign(ix, depth)
{
	char frame[100];
	register int i;
	int r;

	for (i = 0; i < 100; i++)
		frame[i] = sigbyte(ix, (long)(depth*100+i));

	if (depth+1 < STKFILL/100)
		r = stksign(ix, depth+1);
	else
		r = snoozer(ix);

	for (i = 0; i < 100; i++)
		if ((frame[i] & 0xFF) != sigbyte(ix, (long)(depth*100+i)))
			return (depth*100 + i + 1);
	return (r);
}

/*
 * One sleeper.  Never returns.
 */
static void
sleeper(ix)
{
	char b;
	long bad;
	int sbad;

	signdata(ix);

	/*
	 * Signs the stack, announces itself, sleeps, and checks the stack --
	 * everything up to and including the swap happens in here.
	 */
	sbad = stksign(ix, 0);

	bad = ckdata(ix);
	if (bad != 0 || sbad != 0) {
		if (sbad != 0)
			printf("%s: child %d stack differs at %d\n",
				who, ix, sbad-1);
		b = (char)(0x80 | ix);
	} else
		b = (char)ix;
	/*
	 * _exit(2) so a child does not flush stdio the parent buffered before
	 * the fork and print it a second time; the diagnostics above are
	 * pushed out by hand instead.
	 */
	fflush(stdout);
	write(resfd[1], &b, 1);
	_exit(0);
}

/*
 * One hog: take a full copy of the data segment, touch all of it so nothing
 * can pretend the memory was never needed, hold it, and go.
 */
static void
hog(ix)
{
	signdata(ix);
	if (ckdata(ix) != 0)
		printf("%s: hog %d saw its own memory change\n", who, ix);
	fflush(stdout);
	sleep(HOGNAP);
	_exit(0);
}

main(argc, argv)
char **argv;
{
	int i, pid, n, got, bad, nhogged;
	char b;

	if (argc > 1)
		nsleep = atoi(argv[1]);
	if (argc > 2)
		nhog = atoi(argv[2]);
	if (argc > 3)
		snooze = atoi(argv[3]);

	printf("%s: %d sleepers + %d hogs + this process, %ld bytes of data each\n",
		who, nsleep, nhog, (long)FILL);
	printf("%s: %ld KB of data must be live at once -- compare that with the\n",
		who, ((long)(nsleep+nhog+1) * FILL) / 1024L);
	printf("%s: `KB free memory' the boot banner printed; if it is smaller,\n", who);
	printf("%s: nothing had to be swapped and a pass proves nothing.\n", who);

	(void)signal(SIGALRM, hung);
	(void)alarm(DEADLINE);

	if (pipe(rdyfd) < 0 || pipe(resfd) < 0) {
		printf("%s: FAIL cannot pipe (errno %d)\n", who, errno);
		exit(1);
	}

	/*
	 * Sleepers.
	 */
	for (i = 0; i < nsleep; i++) {
		if ((pid = fork()) < 0) {
			printf("%s: FAIL fork of sleeper %d refused (errno %d)\n",
				who, i, errno);
			exit(1);
		}
		if (pid == 0)
			sleeper(i);
		if (nkid < MAXKID)
			kid[nkid++] = pid;
	}

	/*
	 * Wait until every sleeper has signed its memory and gone to sleep.
	 * Reading the announcements is what makes the next phase's demand
	 * land on processes that are asleep rather than on ones still
	 * running, which are not the ones the swapper evicts.
	 */
	for (n = 0; n < nsleep; n++) {
		if (read(rdyfd[0], &b, 1) != 1) {
			printf("%s: FAIL only %d of %d sleepers reported ready\n",
				who, n, nsleep);
			exit(1);
		}
		nready = n + 1;
	}
	printf("%s: %d sleepers asleep, signed\n", who, nsleep);

	/*
	 * Hogs: the memory demand.
	 */
	nhogged = 0;
	for (i = 0; i < nhog; i++) {
		if ((pid = fork()) < 0) {
			printf("%s: hog %d refused (errno %d) -- no core, and the\n",
				who, i, errno);
			printf("%s: kernel did not swap to find any\n", who);
			break;
		}
		if (pid == 0)
			hog(nsleep + i);
		if (nkid < MAXKID)
			kid[nkid++] = pid;
		nhogged++;
	}
	printf("%s: %d of %d hogs running\n", who, nhogged, nhog);

	/*
	 * Verdicts.
	 */
	bad = 0;
	for (n = 0; n < nsleep; n++) {
		if (read(resfd[0], &b, 1) != 1) {
			printf("%s: FAIL only %d of %d sleepers reported back\n",
				who, n, nsleep);
			exit(1);
		}
		nback = n + 1;
		got = b & 0xFF;
		if ((got & 0x80) != 0) {
			printf("%s: child %d came back CORRUPT\n", who, got & 0x7F);
			bad++;
		}
	}

	while (wait((int *)0) > 0)
		;

	if (bad != 0 || nhogged != nhog) {
		printf("%s: FAIL (%d corrupt, %d of %d hogs)\n",
			who, bad, nhogged, nhog);
		exit(1);
	}
	printf("%s: PASS -- %d processes swapped out and resumed intact\n",
		who, nsleep);
	exit(0);
}
