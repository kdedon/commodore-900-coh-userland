/*
 * deepstack.c -- does a process get the whole stack allowance, whatever size
 * its frames are, and does it fault cleanly when the allowance runs out?
 *
 *	deepstack [frame-bytes]		default 16 (16, 256, 512 or 4096)
 *
 * WHAT THIS IS FOR.  Not "how deep can a program recurse": that number falls
 * out of the allowance divided by the frame, and no test is needed to find it.
 * What is being decided is that the answer does not depend on the FRAME SIZE,
 * which is where this kernel has twice been wrong.
 *
 *   1. THE ALLOWANCE.  exec commits MADSIZE (32 KB, machz8001.h) to the stack
 *	segment.  A recursion must therefore reach the neighbourhood of 32 KB
 *	whether its frames are 16 bytes or 4096, and the same at every size in
 *	between.  Two separate defects have made this false, and both showed up
 *	only at some frame sizes: a click/byte confusion in stviol() that stopped
 *	the stack at the 4 KB exec handed out, and -- after that was fixed --
 *	demand growth that worked for frames small enough to walk down through
 *	the MMU's 256-byte write-warning page and did nothing at all for frames
 *	that stepped over it.  512-byte frames died at 8 levels while 16-byte
 *	frames ran to 1087, so a single frame size proves nothing here.
 *   2. A CLEAN FAULT AT THE ALLOWANCE.  The last frame is the interesting one.
 *	The correct outcome is that the offending process, and only it, dies of
 *	SIGSEGV.  The failures are a kernel that hands out stack past what the
 *	hardware can address (silent corruption of whatever is mapped next), one
 *	that panics, and one that wedges the machine with the faulting process
 *	unkillable.  All three are indistinguishable from inside the process
 *	that faults, which is why the verdict is computed somewhere else.
 *
 * The recursion happens in a CHILD -- it always ends in a fault, so a
 * verdict printed by the recursing process itself could never run -- and the
 * parent computes the verdict from two things that outlive the child: the
 * record file it wrote on its way down, and the status wait(2) reports.
 * Each level writes its depth AND the address of its own frame with an
 * unbuffered write(2), so the first and last records in the file bracket
 * exactly how much stack the child really had, with no arithmetic about
 * frame sizes and no reliance on the child living long enough to report.
 *
 * `frame-bytes' picks between four real frame sizes: one under the warning
 * page, one exactly its size, and two over it.
 */
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>		/* long strtol(): K&R would truncate it to int */
#include <unistd.h>		/* long lseek(): the same */

#define MAXDEPTH	20000	/* a stop, not a target: see CAPPED below	*/
#define ISTSIZE		4096	/* the argument headroom exec adds (z8001 conf)	*/
#define ALLOWANCE	32768L	/* MADSIZE: the whole stack a process may have	*/
#define CEILING		65536L	/* one segment: the hard architectural limit	*/
#define OUTFILE		"/deepstack.out"
#define RECLEN		20	/* one fixed-width record, "%6d %012lx\n"	*/
#define DEADLINE	120	/* seconds the parent will wait for the child	*/

int	depth;			/* global: readable from a core file */
int	fd;
int	pad_bytes = 16;
int	kid;
int	fails;

/*
 * The recursion, once per frame size.  There are no variable-length arrays in
 * this dialect, so the padding HAS to be a compile-time constant: one
 * function per size, each really carrying the frame its name says.
 *
 * Every byte of the pad is written, so nothing can leave it unallocated, and
 * the record carries &pad so the parent can measure the extent directly.
 * RECLEN is fixed so the parent can find the last record by seeking.
 */
#define DOWN(name, bytes)					\
	static void name(n)					\
	int n;							\
	{							\
		char	pad[bytes];				\
		char	buf[RECLEN + 8];			\
		int	i;					\
								\
		depth = n;					\
		for (i = 0; i < (int)sizeof(pad); i++)		\
			pad[i] = (char)n;			\
		/* Unbuffered and fixed width, so the last	\
		 * complete record on the disk is the last	\
		 * level the child reached. */			\
		sprintf(buf, "%6d %012lx\n", n, (long)&pad[0]);	\
		write(fd, buf, RECLEN);				\
		if (n < MAXDEPTH)				\
			name(n + 1);				\
	}

DOWN(down16, 16)
DOWN(down256, 256)
DOWN(down512, 512)
DOWN(down4096, 4096)

/*
 * The child.  Never returns on a working kernel: it recurses until the segment
 * ceiling faults it.
 */
static void
recurse()
{
	if (pad_bytes == 4096)
		down4096(1);
	else if (pad_bytes == 512)
		down512(1);
	else if (pad_bytes == 256)
		down256(1);
	else
		down16(1);
	/* Reached only if MAXDEPTH stopped it first, which the parent reads as
	 * CAPPED: the ceiling was never approached and nothing was decided. */
	_exit(3);
}

/*
 * The parent's deadline.  A stack fault that wedges the faulting process
 * instead of killing it is one of the defects being looked for, and from here
 * it is a wait(2) that never returns -- so it has to be a reported failure and
 * not a hang.
 */
static
hung()
{
	printf("deepstack: FAIL -- the child neither died nor finished within"
		" %d s.\n", DEADLINE);
	printf("deepstack: a process wedged in the stack-growth path is exactly"
		" the defect this\n");
	printf("deepstack: test exists to catch; it is not a deadline to"
		" raise.\n");
	fflush(stdout);
	if (kid > 0)
		(void)kill(kid, SIGKILL);
	exit(1);
}

/*
 * Read the record at byte offset `off'.  Returns 1 and fills *dp (the depth)
 * and *ap (the frame address), or 0.
 */
static int
record(f, off, dp, ap)
int f;
long off;
int *dp;
long *ap;
{
	char buf[RECLEN + 1];

	if (lseek(f, off, 0) != off)
		return 0;
	if (read(f, buf, RECLEN) != RECLEN)
		return 0;
	buf[RECLEN] = '\0';
	*dp = atoi(buf);
	*ap = strtol(buf + 7, (char **)0, 16);
	return *dp > 0;
}

static void
ck(what, got, want)
char *what;
long got, want;
{
	printf("deepstack: %-40s got %ld want %ld  %s\n", what, got, want,
		got == want ? "ok" : "FAIL");
	if (got != want)
		fails++;
	fflush(stdout);
}

int main(argc, argv)
int argc;
char **argv;
{
	long size, hi, lo, used;
	int f, st, sig, dhi, dlo;

	/* The argument selects a frame size; anything else is refused rather
	 * than silently rounded, so a run of one size cannot be read as a
	 * run of another. */
	if (argc > 1) {
		pad_bytes = atoi(argv[1]);
		if (pad_bytes != 16 && pad_bytes != 256 && pad_bytes != 512
		 && pad_bytes != 4096) {
			printf("deepstack: frame padding must be 16, 256, 512"
				" or 4096 (asked for %d)\n", pad_bytes);
			return 2;
		}
	}
	if ((fd = creat(OUTFILE, 0644)) < 0) {
		printf("deepstack: cannot create %s\n", OUTFILE);
		return 1;
	}
	printf("deepstack: recursing in a child, %d bytes of frame padding\n",
		pad_bytes);
	fflush(stdout);

	if ((kid = fork()) < 0) {
		printf("deepstack: FAIL -- cannot fork\n");
		return 1;
	}
	if (kid == 0)
		recurse();		/* never returns */

	(void)signal(SIGALRM, hung);
	(void)alarm(DEADLINE);
	st = 0;
	if (wait(&st) != kid) {
		printf("deepstack: FAIL -- wait did not report the child\n");
		return 1;
	}
	(void)alarm(0);
	(void)close(fd);

	/*
	 * THE DEATH.  A process that runs its stack segment off the end must be
	 * killed by a signal, and the signal must be the memory fault.  Exit
	 * status 3 is the child's own report that MAXDEPTH stopped it first,
	 * which means the ceiling was never reached and nothing below has been
	 * decided either way.
	 */
	sig = st & 0177;
	if (sig == 0 && ((st >> 8) & 0xFF) == 3) {
		printf("deepstack: CAPPED -- MAXDEPTH (%d) stopped the child"
			" before the segment\n", MAXDEPTH);
		printf("deepstack: ceiling.  Nothing about growth or faulting"
			" was decided; raise MAXDEPTH.\n");
		return 1;
	}
	printf("deepstack: child status 0x%x (signal %d, core %d)\n",
		st, sig, (st & 0200) != 0 ? 1 : 0);
	ck("the child died of a signal", sig != 0 ? 1L : 0L, 1L);
	/* SIGSEGV is the clean answer.  A bus error, an illegal instruction or
	 * a kill from anywhere else all mean the ceiling was met some other
	 * way than the stack-limit path. */
	ck("  and the signal is SIGSEGV", (long)sig, (long)SIGSEGV);

	/*
	 * THE EXTENT.  The first and last records bracket the stack the child
	 * actually had.  Both come off the disk, so they survive the child's
	 * death.
	 */
	if ((f = open(OUTFILE, O_RDONLY)) < 0) {
		printf("deepstack: FAIL -- cannot reread %s\n", OUTFILE);
		return 1;
	}
	size = lseek(f, 0L, 2);
	printf("deepstack: %ld bytes of records (%ld levels)\n",
		size, size / (long)RECLEN);
	ck("the child wrote records at all",
		size >= (long)RECLEN ? 1L : 0L, 1L);
	if (size < (long)(2 * RECLEN)) {
		(void)close(f);
		printf("deepstack: FAIL -- fewer than two levels reached; the"
			" recursion never started\n");
		return 1;
	}
	if (!record(f, 0L, &dlo, &hi) ||
	    !record(f, ((size / (long)RECLEN) - 1L) * (long)RECLEN, &dhi, &lo)) {
		(void)close(f);
		printf("deepstack: FAIL -- the record file is unreadable\n");
		return 1;
	}
	(void)close(f);
	/* The stack grows DOWN, so the first frame holds the high address. */
	used = hi - lo;
	printf("deepstack: level %d at 0x%lx, level %d at 0x%lx: %ld bytes\n",
		dlo, hi, dhi, lo, used);

	/* Records are written one per level with no gaps, so the last depth and
	 * the record count must agree: a mismatch means writes were lost, and
	 * the extent above was then measured from the wrong frame. */
	ck("every level reached the disk", (long)dhi, size / (long)RECLEN);

	/* 1 -- THE ALLOWANCE WAS HANDED OUT.  A kernel that leaves the process
	 * with what exec set up stops inside the first few kilobytes. */
	ck("the stack ran past the initial segment",
		used > (long)ISTSIZE ? 1L : 0L, 1L);

	/* And it ran to the neighbourhood of the whole allowance, not merely
	 * past ISTSIZE.  This is the assertion that frame size cannot change:
	 * a stack that is grown only when the frames happen to tread on the
	 * warning page gets a few kilobytes at some sizes and everything at
	 * others, and clears the check above either way. */
	ck("  and reached most of the allowance",
		used > ALLOWANCE / 2 ? 1L : 0L, 1L);

	/* 2 -- THE ALLOWANCE HELD.  More than it means the kernel handed out
	 * stack it never promised, and more than one segment means memory the
	 * process cannot reach without wrapping the offset -- silent corruption
	 * rather than depth. */
	ck("and stopped within the allowance", used <= ALLOWANCE ? 1L : 0L, 1L);
	ck("  which is inside one segment", used < CEILING ? 1L : 0L, 1L);

	/* 3 -- and the fault took the child alone.  This parent still running
	 * is the whole of that evidence, and it is worth saying out loud: the
	 * failure it rules out is a machine that stops. */
	printf("deepstack: the parent survived the child's fault\n");

	printf("deepstack: %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
	fflush(stdout);
	return fails ? 1 : 0;
}
