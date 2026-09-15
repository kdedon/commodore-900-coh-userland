/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * stackhw.c -- how much of its 32 KB stack allowance has each live process
 * actually touched?
 *
 *	stackhw -q <procq_ offset> [-a] [-v] [tag]
 *	stackhw -q <procq_ offset> -c '<command>' [tag]
 *
 * The first form takes one sample of every process: -a includes the ones the
 * kernel runs on its own behalf, -v traces what was read, and `tag' labels the
 * sample so a transcript with several of them can be told apart.
 *
 * The second form measures ONE command, which is the only way to measure a
 * command that runs for less time than a sample of every process takes.  The
 * probe forks, the child execs the command (split on spaces, with its standard
 * output on /dev/null), and the parent samples that child and its children --
 * only those -- as fast as it can until the family is gone, then reports the
 * deepest mark it saw and how many rounds it managed.  A command that outran
 * the sampler entirely reports samples=0 and no mark rather than a mark of
 * zero.
 *
 * A sample can land between the fork and the exec, where the child still holds
 * a copy of this probe's stack and its mark is this probe's.  Those samples
 * carry the probe's own command name and are discarded, so `myname' has to be
 * this program's name as the u-area holds it.
 *
 * WHY A MEASUREMENT IS POSSIBLE WITHOUT INSTRUMENTING ANYTHING.  The kernel
 * already paints every stack segment: exstack() (sys/z8001/src/exec.c) takes
 * the whole MADSIZE allowance with salloc(MADSIZE, SFDOWN), and salloc()
 * (sys/coh/seg.c) ends with `if ((f&SFNCLR) == 0) pclear(sp->s_paddr, n)' --
 * SFNCLR is not passed, so all 32768 bytes are ZERO at exec time, every exec.
 * Nothing re-clears the segment while the process lives.  So the lowest
 * non-zero byte in a live process's stack segment is the deepest address any
 * frame of that process has ever written, and the distance from there to the
 * top of the segment is its stack high-water mark.  It persists for the whole
 * life of the process, which is what makes an idle daemon measurable long
 * after the deep path that made the mark.
 *
 * The read path is unprivileged: /dev/kmem for each PROC and SEG, read at its
 * own address rather than out of a copy of the whole arena, and /dev/mem at
 * SEG.s_paddr -- or /dev/swap at SEG.s_daddr -- for a segment's bytes.  s_size
 * comes from the kernel's own SEG, so the same line also answers what the
 * allowance really is at run time rather than what a header says.
 *
 * THE ADDRESS OF procq_ IS GIVEN ON THE COMMAND LINE: -q takes the offset of
 * procq_ in the kernel's data segment, read from the kernel's symbol table
 * (run.sh reads it with the distribution repository's dist.py).  A wrong
 * address walks a list of something else and reports a plausible table from it,
 * so the walk must come back to -q within MAXPROCS steps and must have passed
 * the probe's own pid, or nothing is printed.
 *
 * THE BIAS, STATED.  Zero is the paint, so a frame that leaves zeros in its
 * lowest bytes is invisible and the mark is an UNDER-estimate -- never an
 * over-estimate.  The size of that error is bounded by how long a run of zeros
 * sits inside the used region, so the largest interior zero run is measured
 * and printed (`maxz') beside every mark: a small maxz says the boundary
 * cannot be far wrong, and a large one says treat the mark as a floor.
 *
 * OUTPUT, all decimal bytes.  One line per process:
 *
 *   SHW <tag> <pid> <comm> size=<s_size> low=<lowest non-zero offset>
 *	 hw=<size-low> args=<arg region> frames=<hw-args> maxz=<max interior
 *	 zero run> st=<state> <core|swap>
 *
 * and, under -c, one line for the whole run, ending in the number of sampling
 * rounds and the command's wait(2) status instead of a state:
 *
 *   SHW <tag> <pid> <comm> size= low= hw= args= frames= maxz= samples= status=
 *
 * `args' is the argv/envp block exec wrote at the top of the segment, which is
 * real stack the process spends but is not frame depth; `frames' is the mark
 * with it taken off.  A process whose u-area cannot be read reports args=-1
 * and frames=-1 rather than guessing.
 */
#include <stdio.h>
#include <stdlib.h>		/* long strtol(): K&R would truncate it	*/
#include <string.h>
#include <unistd.h>		/* long lseek(): the same		*/
#include <sys/const.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/seg.h>
#include <sys/uproc.h>

#define CHUNK	1024		/* one read(2) of the segment: fits an int */
#define MAXPROCS 500		/* chain steps before the address is wrong */
#define MAXARGS	16		/* words in a -c command */

/*
 * A kernel pointer is a far seg:offset pair -- 0x3200406A, segment 0x3200 --
 * while /dev/kmem is addressed by the offset alone, so every pointer followed
 * here is reduced to its offset word.
 */
#define koff(p)		((unsigned)((long)(p) & 0xFFFFL))

unsigned aprocq;		/* procq_: the process queue's own PROC	*/

PROC	cprocq;			/* the queue header */
/* What the most recent report() read, for the -c loop to keep when a sample
 * turns out to be the deepest one. */
long	lastsize, lastargs, lastmaxz;
char	lastname[sizeof(((struct uproc *)0)->u_comm) + 1];
PROC	cur;			/* the process being reported */
int	kfd = -1, mfd = -1, sfd = -1;
struct	uproc u;
char	buf[CHUNK];
char	*tag = "-";
char	*cflag;			/* -c: the one command to measure */
char	*myname = "stackhw";	/* argv[0]'s last component */
int	aflag;			/* report kernel/idle processes too */
int	vflag;			/* trace what was read, to localise a fault */

/*
 * Zero a block.  Spelled out rather than taken from libc so that the probe
 * needs nothing of memset(3) on a K&R libc.
 */
static void
kclear(p, n)
char *p;
unsigned n;
{
	while (n-- != 0)
		*p++ = '\0';
}

static void
panic(s)
char *s;
{
	fflush(stdout);
	fprintf(stderr, "stackhw: %s\n", s);
	exit(1);
}

/*
 * Kernel memory, in CHUNK-sized reads: the arena is bigger than the largest
 * count read(2) can be given on this machine, so a single read of it would
 * pass a negative length and return nothing.
 */
static void
kread(s, bp, n)
long s;
char *bp;
unsigned n;
{
	int k;

	if (lseek(kfd, s, 0) != s)
		panic("cannot seek kernel memory");
	while (n != 0) {
		k = n > (unsigned)CHUNK ? CHUNK : (int)n;
		if (read(kfd, bp, k) != k)
			panic("kernel memory read error");
		bp += k;
		n -= (unsigned)k;
	}
}

/*
 * Physical memory.  lseek(2) on /dev/mem sets the position without reporting
 * it, so the read is what says whether the address was good.
 */
static int
mread(s, bp, n)
long s;
char *bp;
int n;
{
	int k;

	(void)lseek(mfd, s, 0);
	k = read(mfd, bp, n);
	if (k != n && vflag)
		printf("SHWV /dev/mem read %d at %lx returned %d\n", n, s, k);
	return (k == n);
}

/*
 * The swap device, for a segment the swapper has taken out of core.  A
 * daemon that has not run for a while is normally there, so without this the
 * marks that matter most are the ones that cannot be read.  s_daddr is a block
 * number, as it is in ps(1).
 */
static int
dread(s, bp, n)
long s;
char *bp;
int n;
{
	if (sfd < 0)
		return (0);
	(void)lseek(sfd, s, 0);
	return (read(sfd, bp, n) == n);
}

/*
 * Scan a stack segment.  Returns 1 on success and fills *lowp with the offset
 * of the lowest non-zero byte and *maxzp with the longest run of zeros lying
 * above it (the bound on how far *lowp can be wrong).  A segment of nothing
 * but zeros -- a process that has not run at all -- gives low == size.
 */
static int
scan(base, incore, size, lowp, maxzp)
long base, size;
int incore;
long *lowp, *maxzp;
{
	long off, low, run, maxz;
	int i, n, seen;

	low = size;
	maxz = 0;
	run = 0;
	seen = 0;
	for (off = 0; off < size; off += (long)CHUNK) {
		/* The comparison is in long: a 32768-byte segment does not fit
		 * an int, and narrowing the residue first makes it negative. */
		n = size - off > (long)CHUNK ? CHUNK : (int)(size - off);
		if ((incore ? mread(base + off, buf, n)
			    : dread(base + off, buf, n)) == 0)
			return (0);
		for (i = 0; i < n; i++) {
			if (buf[i] == '\0') {
				if (seen) {
					run++;
					if (run > maxz)
						maxz = run;
				}
				continue;
			}
			run = 0;
			if (seen == 0) {
				seen = 1;
				low = off + (long)i;
			}
		}
	}
	*lowp = low;
	*maxzp = maxz;
	return (1);
}

/*
 * The size of the argv/envp block exec left at the top of the stack: from
 * u_argp, the offset of argv[0], to the top of the segment.  That is stack the
 * process spends but never a frame, so it is reported separately.  -1 when the
 * u-area cannot be read, and the u-area is cleared first so that a failure
 * cannot leave the previous process's name and count in place.
 */
static long
argregion(pp, size)
PROC *pp;
long size;
{
	SEG seg;

	kclear((char *)&u, sizeof(u));
	if (pp->p_segp[SIUSERP] == NULL)
		return (-1L);
	kread((long)koff(pp->p_segp[SIUSERP]), (char *)&seg, sizeof(seg));
	if ((seg.s_flags & SFCORE) != 0) {
		if (mread((long)seg.s_paddr, (char *)&u, sizeof(u)) == 0)
			return (-1L);
	} else if (dread((long)seg.s_daddr * (long)BSIZE, (char *)&u,
			 sizeof(u)) == 0)
		return (-1L);
	if (u.u_argc <= 0)
		return (-1L);
	return (size - (long)(unsigned)(u.u_argp - u.u_segl[SISTACK].sr_base));
}

/*
 * The head of a PROC as words, for reading the layout off the machine rather
 * than off a header: p_lforw, p_lback, p_nforw, p_nback, p_segp[0..8], p_pid.
 */
static void
dump(pp)
PROC *pp;
{
	unsigned *wp;
	int i;

	wp = (unsigned *)pp;
	printf("SHWD");
	for (i = 0; i < 30; i++)
		printf(" %x", wp[i]);
	printf("\n");
}

/*
 * One process's mark.  Returns the high-water in bytes, or -1 when the segment
 * could not be read.  `quiet' reports nothing, for the sampling loop, which
 * prints one line for the whole run rather than one per sample.
 */
static long
report(pp, quiet)
PROC *pp;
int quiet;
{
	SEG seg;
	long size, low, maxz, args, hw, base;
	char name[sizeof(u.u_comm) + 1];
	int i, incore;

	if (pp->p_segp[SISTACK] == NULL)
		return (-1L);
	kread((long)koff(pp->p_segp[SISTACK]), (char *)&seg, sizeof(seg));
	size = (long)seg.s_size;
	incore = (seg.s_flags & SFCORE) != 0;
	base = incore ? (long)seg.s_paddr : (long)seg.s_daddr * (long)BSIZE;
	if (scan(base, incore, size, &low, &maxz) == 0) {
		if (quiet == 0)
			printf("SHW %s %u ? size=%ld %s=%lx unreadable\n", tag,
				pp->p_pid, size, incore ? "paddr" : "daddr",
				base);
		return (-1L);
	}
	args = argregion(pp, size);	/* fills u, so read the name after */
	for (i = 0; i < (int)sizeof(u.u_comm); i++)
		name[i] = u.u_comm[i];
	name[sizeof(u.u_comm)] = '\0';
	if (name[0] == '\0')
		strcpy(name, "?");
	hw = size - low;
	if (quiet == 0) {
		printf("SHW %s %u %s size=%ld low=%ld hw=%ld args=%ld frames=%ld maxz=%ld st=%u %s\n",
			tag, pp->p_pid, name, size, low, hw, args,
			args < 0 ? -1L : hw - args, maxz, pp->p_state,
			incore ? "core" : "swap");
		fflush(stdout);
	} else {
		lastsize = size;
		lastargs = args;
		lastmaxz = maxz;
		for (i = 0; i <= (int)sizeof(u.u_comm); i++)
			lastname[i] = name[i];
	}
	return (hw);
}

/*
 * -c: run one command and report the deepest mark it or a child of it reached.
 *
 * The command is exec'd directly, split on spaces, so the process measured is
 * the command itself and not a shell that forked it; a command that forks --
 * make, man -- is followed one generation, which is why every sample walks the
 * chain for the whole family rather than looking up one pid.  The child's
 * standard output goes to /dev/null: this runs on a console whose transcript is
 * the result.
 */
static int
measure(cmd)
char *cmd;
{
	char *av[MAXARGS];
	char *cp;
	unsigned off;
	int kid, st, n, nsamp, i, ac, alive, steps, fd;
	long hw, best, bestsize, bestargs, bestmaxz;
	char bestname[sizeof(((struct uproc *)0)->u_comm) + 1];

	ac = 0;
	for (cp = cmd; *cp != '\0'; ) {
		while (*cp == ' ')
			cp++;
		if (*cp == '\0')
			break;
		if (ac >= MAXARGS - 1)
			panic("too many words in the -c command");
		av[ac++] = cp;
		while (*cp != '\0' && *cp != ' ')
			cp++;
		if (*cp == ' ')
			*cp++ = '\0';
	}
	av[ac] = (char *)0;
	if (ac == 0)
		panic("-c was given no command");

	fflush(stdout);
	if ((kid = fork()) < 0)
		panic("cannot fork");
	if (kid == 0) {
		if ((fd = open("/dev/null", 1)) >= 0) {
			(void)close(1);
			(void)dup(fd);
			(void)close(fd);
		}
		execv(av[0], av);
		_exit(127);
	}
	best = -1L;
	bestsize = bestargs = bestmaxz = 0L;
	bestname[0] = '\0';
	nsamp = 0;
	for (;;) {
		kread((long)aprocq, (char *)&cprocq, sizeof(cprocq));
		alive = 0;
		steps = 0;
		off = koff(cprocq.p_nback);
		while (off != aprocq) {
			if (off == 0 || ++steps > MAXPROCS)
				break;
			kread((long)off, (char *)&cur, sizeof(cur));
			off = koff(cur.p_nback);
			if ((int)cur.p_pid != kid && (int)cur.p_ppid != kid)
				continue;
			if (cur.p_state != PSDEAD)
				alive = 1;
			hw = report(&cur, 1);
			if (hw <= best || strcmp(lastname, myname) == 0)
				continue;
			/* The deepest sample is the answer, so its size,
			 * argument block, zero run and name are the ones
			 * kept. */
			best = hw;
			bestsize = lastsize;
			bestargs = lastargs;
			bestmaxz = lastmaxz;
			for (i = 0; i <= (int)sizeof(lastname) - 1; i++)
				bestname[i] = lastname[i];
		}
		if (alive == 0)
			break;
		nsamp++;
	}
	st = 0;
	while ((n = wait(&st)) != kid && n != -1)
		;
	if (best < 0L) {
		printf("SHW %s %d %s samples=0 outran-the-sampler status=0x%x\n",
			tag, kid, cmd, st);
		fflush(stdout);
		return (1);
	}
	printf("SHW %s %d %s size=%ld low=%ld hw=%ld args=%ld frames=%ld maxz=%ld samples=%d status=0x%x\n",
		tag, kid, bestname, bestsize, bestsize - best, best, bestargs,
		bestargs < 0 ? -1L : best - bestargs, bestmaxz, nsamp, st);
	fflush(stdout);
	return (0);
}

int main(argc, argv)
int argc;
char **argv;
{
	unsigned off;
	char *cp;
	int i, me, seen, steps;

	/* u_comm holds the last component of what was exec'd, truncated to its
	 * own width, which is what a sample has to be compared against. */
	for (cp = argv[0]; *cp != '\0'; cp++)
		if (*cp == '/')
			myname = cp + 1;
	if (myname[0] == '\0')
		myname = "stackhw";

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-a") == 0)
			aflag = 1;
		else if (strcmp(argv[i], "-v") == 0)
			vflag = 1;
		else if (strcmp(argv[i], "-q") == 0 && i+1 < argc)
			aprocq = (unsigned)strtol(argv[++i], (char **)0, 0);
		else if (strcmp(argv[i], "-c") == 0 && i+1 < argc)
			cflag = argv[++i];
		else
			tag = argv[i];
	}
	if (aprocq == 0)
		panic("usage: stackhw -q procq_ [-a] [-v] [-c command] [tag]");
	if ((kfd = open("/dev/kmem", 0)) < 0)
		panic("cannot open /dev/kmem");
	if ((mfd = open("/dev/mem", 0)) < 0)
		panic("cannot open /dev/mem");
	/* A segment the swapper has taken out of core is read from the swap
	 * device instead; without it, a daemon that has been idle for a while
	 * is exactly the one that cannot be measured. */
	sfd = open("/dev/swap", 0);
	kread((long)aprocq, (char *)&cprocq, sizeof(cprocq));
	if (vflag) {
		printf("SHWV procq at %u in segment %lx, nforw=%lx nback=%lx\n",
			aprocq, ((long)cprocq.p_nforw >> 16) & 0xFFFFL,
			(long)cprocq.p_nforw, (long)cprocq.p_nback);
		dump(&cprocq);
	}

	if (cflag != NULL)
		return (measure(cflag));

	me = getpid();
	seen = 0;
	steps = 0;
	off = koff(cprocq.p_nback);
	while (off != aprocq) {
		if (off == 0)
			panic("the process list runs into a null pointer");
		if (++steps > MAXPROCS)
			panic("the process list does not come back to -q");
		kread((long)off, (char *)&cur, sizeof(cur));
		if (vflag)
			dump(&cur);
		if ((int)cur.p_pid == me)
			seen = 1;
		if ((aflag != 0 || (cur.p_flags & PFKERN) == 0) &&
		    cur.p_state != PSDEAD)
			(void)report(&cur, 0);
		off = koff(cur.p_nback);
	}
	/*
	 * A wrong -q walks a list of something else and reports it.  The probe
	 * is itself a process, so it must have found its own pid; that is what
	 * says the chain being read is the process chain.
	 */
	if (seen == 0)
		panic("this process is not in the list at -q: wrong address");
	return (0);
}
