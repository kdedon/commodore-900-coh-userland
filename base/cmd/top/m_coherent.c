/*
 * top - a top users display for Unix
 *
 * SYNOPSIS:  COHERENT 3.x on the Zilog Z8001 (Commodore 900)
 *
 * DESCRIPTION:
 * The kernel is read the way ps(1) reads it: the namelist comes from the
 * kernel image /coherent, the process chain and the segment queue from
 * /dev/kmem, and a command line from the process's own u-area through
 * /dev/mem or /dev/swap.  All three nodes are 600 root, so /bin/top is setuid
 * root; machine_init() opens them and then does setuid(getuid()), and every
 * read after that is a read through a descriptor already held.
 *
 * The namelist file must be the kernel that is running.  A namelist from a
 * different link of the kernel resolves the same names to addresses that now
 * hold something else, and the walk then reports whatever is at those
 * addresses.  The chain walk refuses a pointer outside the arena, so the usual
 * outcome is a complaint that the image does not match rather than a plausible
 * table, but that check is a guard and not a proof.
 *
 * WHERE THE ARENA COPY LIVES.  A sample reads the alloc arena in one go and
 * walks it in this process's own memory, so no kernel pointer is ever
 * followed where the kernel could free what it points at.  That copy is a
 * local of sample(): it is on the STACK, which exec commits whole -- all
 * MADSIZE bytes of it, painted, whatever the process puts there (exstack(),
 * sys/z8001/src/exec.c) -- so the space costs nothing beyond what every
 * process already pays, and it is gone when the sample is over.  In bss or on
 * the heap the same bytes are memory this process is charged for from the
 * moment it starts until it exits, and the arena is ALLSIZE bytes whatever the
 * machine is doing: it is fixed at boot from a kernel configuration constant,
 * not from the load.
 *
 * The copy is a WINDOW, not a requirement.  WINSIZE is what fits beside the
 * frames above it, so a kernel whose arena is larger than that is read through
 * the window for the part that fits and one structure at a time for the rest.
 * getkval() is where those two meet, and no caller of it knows which it got.
 *
 * WHAT THE COPY DOES NOT GIVE, and what does.  read(2) takes an int count, so
 * an arena of this size is two reads with a window between them where the
 * kernel runs: the copy is not one instant of the kernel's memory and never
 * was, and a block freed and handed to another tenant between the two reads is
 * in it as readily as a live one.  relproc() (sys/coh/proc.c) unlinks a PROC
 * and frees it, and free() (sys/coh/alloc.c) only marks the block, so the
 * bytes stay as they were until some later alloc() reuses the block and clears
 * what it hands out.  A stale pointer therefore leads to one of three things:
 *
 *	the process is still there		-- the ordinary case;
 *	it is unlinked but the block is not yet reused -- the fields still
 *		read as they did, and its p_nback still names a process that
 *		is in the arena, so the walk carries on across it;
 *	the block has been reused		-- the contents are another
 *		tenant's, and believing them is the failure to avoid.
 *
 * So every step is verified against the step before it: the chain is doubly
 * linked, and the node reached through p_nback must point back through p_nforw
 * at the node it was reached from.  A reused block matches that only by
 * holding, at exactly the offset of p_nforw, the four bytes of the previous
 * node's far address.  The same rule walks the segment queue through s_forw
 * and s_back.
 *
 * A step that fails says the arena moved while it was being copied, so the
 * sample is taken again -- a fresh copy and a fresh walk, MAXTRY times.  The
 * node that failed is never used.  If it is still moving after that, the
 * process table for that refresh STOPS at the point of the inconsistency and
 * is short by the processes beyond it; rows are independent, so a short table
 * is missing rows rather than holding wrong ones.  The memory line is a sum
 * and cannot be short without being wrong, so it keeps the previous complete
 * figure instead.
 *
 * Nothing else about the arena is assumed: the range check below refuses any
 * pointer that does not land inside it, which is what catches a namelist that
 * does not belong to the running kernel.
 *
 * LIBS: -ltermcap
 */

#include "os.h"
#include <ctype.h>
#include <pwd.h>
#include <const.h>
#include <l.out.h>
#include <proc.h>
#include <sched.h>
#include <seg.h>
#include <uproc.h>

#include "top.h"
#include "machine.h"
#include "utils.h"

#define VMUNIX	"/coherent"
#define KMEM	"/dev/kmem"
#define MEM	"/dev/mem"
#define SWAP	"/dev/swap"

#define ARGSIZE	512			/* Argument bytes read per process */
#define CMDLEN	20			/* Command column width */
/*
 * The process table grows to whatever the chain holds.  This kernel has no
 * process limit to size it from: a PROC is kalloc()ed out of the single alloc
 * arena (sys/coh/proc.c process(), sys/coh/alloc.c), so the ceiling is however
 * many fit beside every other arena tenant, and it moves with them.  PENTSTEP
 * is the growth quantum only; a sample larger than the tables holds grows them
 * before it fills them, and a process that arrives when the growth fails still
 * counts in the totals and in the CPU-state figures.
 */
#define PENTSTEP 16			/* Table entries added per growth */

/*
 * How much of the arena a sample copies at once.  It is a frame of sample(),
 * so this plus everything called from there has to fit under the stack a
 * process is given, and test/stackhw measures what a run of top actually
 * reaches.  It is not a claim about the kernel: an arena larger than this is
 * read through the window as far as it goes and one structure at a time
 * beyond it.
 */
#define WINSIZE	24576

/*
 * A chain that does not come back to its own header within this many steps is
 * not the chain it was taken for.  The bound is on the walk, not on the
 * system: it is what makes a wrong namelist stop rather than run forever.
 */
#define MAXLINK	512

/*
 * Walks attempted when the chain changes under the walk.
 */
#define MAXTRY	4

/*
 * Is a kernel pointer inside the alloc arena?  The subtraction is done in
 * long: the arena size is an unsigned, and `aend + casize' computed in a
 * pointer would wrap where the arena ends at the top of the kernel's data
 * segment and admit every address below it.
 */
#define range(p)	((long)(p) >= aend && (long)(p) - aend < (long)casize)

/*
 * Namelist indices.
 */
#define X_PROCQ		0
#define X_UTIMER	1
#define X_STIMER	2
#define X_ASIZE		3
#define X_MSIZE		4
#define X_COREBOT	5
#define X_CORETOP	6
#define X_SEGMQ		7
#define X_AVENRUN	8
#define X_END		9

static struct nlist nl[] = {
	"procq_",	0,	0,
	"utimer_",	0,	0,
	"stimer_",	0,	0,
	"asize_",	0,	0,
	"msize_",	0,	0,
	"corebot_",	0,	0,
	"coretop_",	0,	0,
	"segmq_",	0,	0,
	"avenrun_",	0,	0,
	"end_",		0,	0,
	""
};

#define aprocq		nl[X_PROCQ].n_value
#define autimer		nl[X_UTIMER].n_value
#define astimer		nl[X_STIMER].n_value
#define asegmq		nl[X_SEGMQ].n_value
#define aavenrun	nl[X_AVENRUN].n_value
#define aend		nl[X_END].n_value

/*
 * The per-process area of the display.
 */
static char header[] =
  "  PID X        NICE  SIZE   RES STATE   TIME   %CPU COMMAND";
/* 0123456   -- field to fill in starts at header+6 */
#define UNAME_START 6

#define Proc_format \
	"%5u %-8.8s %4d %5s %5s %-5s %6s %6s %s"

/*
 * Process states.  These are top's states, not the kernel's: the kernel has
 * three (PSSLEEP, PSRUN, PSDEAD) and distinguishes waiting and stopped by
 * the sleep channel and by PFSTOP, exactly as ps(1) does when it prints its
 * state letter.
 */
#define S_NONE	0
#define S_SLEEP	1
#define S_WAIT	2
#define S_RUN	3
#define S_STOP	4
#define S_ZOMB	5
#define S_ODD	6
#define NSTATES	7

char *state_abbrev[NSTATES] = {
	"", "sleep", "wait", "run", "stop", "zomb", "?"
};

int process_states[NSTATES];
char *procstatenames[] = {
	"", " sleeping, ", " waiting, ", " running, ", " stopped, ",
	" zombie, ", " unknown, ",
	NULL
};

/*
 * CPU states.  The kernel charges each process user and system ticks and
 * counts every tick in utimer; what utimer counted and no process claimed is
 * what the machine spent doing nothing.
 */
#define CPUSTATES 3

int cpu_states[CPUSTATES];
char *cpustatenames[] = {
	"user", "system", "idle", NULL
};

int memory_stats[4];
char *memorynames[] = {
	"K total, ", "K used, ", "K free, ", "K kernel", NULL
};

/*
 * What the command column needs from a process besides its segments: which of
 * the names that stand in for a command line this process gets, if any.
 */
#define K_USER	0			/* Its own argv, out of its memory */
#define K_EXIT	1			/* Exiting */
#define K_SWAP	2			/* The swapper */
#define K_SLIB	3			/* Shared library */
#define K_KERN	4			/* Kernel process */

/*
 * Where a segment's bytes are, taken while the arena copy is in hand.  The
 * copy is a frame of sample() and is gone by the time a row is formatted, so
 * what the command column will need is carried out of it rather than looked
 * up again.
 */
struct sdesc {
	int	 d_ok;			/* There is a segment here */
	short	 d_flags;		/* s_flags */
	paddr_t	 d_paddr;		/* s_paddr */
	daddr_t	 d_daddr;		/* s_daddr */
};

/*
 * One process, as of one sample.
 */
struct pent {
	unsigned e_pid;			/* Process id */
	unsigned e_uid;			/* Real uid */
	int	 e_stat;		/* One of the S_ codes */
	int	 e_kern;		/* Runs on the kernel's own behalf */
	int	 e_kind;		/* One of the K_ codes */
	int	 e_nice;		/* Nice value, as nice(2) keeps it */
	struct sdesc e_useg;		/* Its u-area segment */
	struct sdesc e_aseg;		/* The segment its arguments are in */
	long	 e_size;		/* Segment bytes, ps(1)'s selection */
	long	 e_res;			/* Of those, the bytes in core */
	long	 e_time;		/* Cpu time used, in HZ */
	long	 e_delta;		/* Cpu time since the last sample */
	char	 e_cmd[CMDLEN+1];	/* Command */
};

/* get_process_info passes back a handle.  This is what it looks like: */
struct handle {
	struct pent **next_proc;	/* Points to the next entry to format */
	int	 remaining;		/* Entries left */
};

static int kmem = -1;
static int mem = -1;
static int swap = -1;

static unsigned casize;			/* Size of the kernel's alloc arena */

/*
 * The machine's memory, which is what it was when the machine came up:
 * msize_, corebot_ and coretop_ are set once, by the startup that sizes core,
 * so they are read once as well.
 */
static unsigned cmsize;			/* Core, in K */
static long ccorebot, ccoretop;		/* What the segment allocator has */

/*
 * The arena copy the sample in progress is walking, and the range of kernel
 * addresses it answers for.  NULL between samples, which is when getkval()
 * goes to the device for everything.
 */
static char *win;
static long wlo, whi;

/*
 * This sample, the subset of it being displayed, and the two fields of the
 * previous sample that deltas() needs -- a pid to match on and the time to
 * subtract.  All three are grown together by grow().
 */
static struct pent *cur;		/* This sample */
static struct pent **pref;		/* The subset being displayed */
static struct ptime {
	unsigned e_pid;			/* Process id */
	long	 e_time;		/* Cpu time used, in HZ */
} *prev;				/* The sample before this one */
static int npent;			/* Entries the three tables hold */
static int ncur, nprev;
static int pref_len;

static unsigned lasttick;		/* utimer at the previous sample */
static int primed;			/* An interval has been measured */
static long dticks;			/* Clock ticks since the last sample */

/*
 * Monotone accumulators for percentages().  Ticks charged to a process that
 * then exits leave the running sum, so the per-sample differences are taken
 * first and only their non-negative parts are accumulated: percentages()
 * reads a decrease as a counter wrap.
 */
static long cp_time[CPUSTATES];
static long cp_old[CPUSTATES];
static long cp_diff[CPUSTATES];
static long lastuser, lastsys;
static int havecpu;
static int haveload;			/* The kernel keeps a load average */

/*
 * The bytes the memory segment queue held at the last walk of it that came
 * back to the head of the queue.
 */
static long lastused;
static int haveused;

static struct handle handle;
static char fmt[MAX_COLS];		/* Where format_next_process builds */

char *format_time();
char *format_k();
char *printable();
static int segwalk();
static char *kmap();

machine_init(statics)

struct statics *statics;

{
	unsigned asize;

	nlist(VMUNIX, nl);
	if (nl[X_PROCQ].n_type == 0) {
		fprintf(stderr, "top: %s: no namelist -- not a kernel image\n",
			VMUNIX);
		return(-1);
	}
	if ((kmem = open(KMEM, 0)) < 0) {
		perror(KMEM);
		return(-1);
	}
	getkval(nl[X_ASIZE].n_value, (char *)&asize, sizeof (asize));
	if (asize == 0) {
		fprintf(stderr,
	"top: %s: alloc arena is empty -- kernel image does not match the running kernel\n",
			VMUNIX);
		return(-1);
	}
	casize = asize;
	getkval(nl[X_MSIZE].n_value, (char *)&cmsize, sizeof (cmsize));
	getkval(nl[X_COREBOT].n_value, (char *)&ccorebot, sizeof (ccorebot));
	getkval(nl[X_CORETOP].n_value, (char *)&ccoretop, sizeof (ccoretop));

	/*
	 * The command column needs a process's own memory, which is in core
	 * or on the swap device.  Neither is fatal: without them every row
	 * still carries its pid, state, size and time.
	 */
	mem = open(MEM, 0);
	swap = open(SWAP, 0);

	/*
	 * Turn off setuid privileges.  The three descriptors above are the
	 * whole reason for the bit -- all three nodes are 600 root -- and
	 * everything after this point, including ~/.toprc and the kill and
	 * renice commands, acts as the real user.
	 */
	setuid(getuid());

	/*
	 * A kernel older than the one that grew a load average has no
	 * avenrun_, and nlist() leaves its entry with no type.  Reading the
	 * address it did not resolve would report whatever is at zero as a
	 * load, so the field stays unavailable instead.
	 */
	haveload = nl[X_AVENRUN].n_type != 0;

	statics->procstate_names = procstatenames;
	statics->cpustate_names = cpustatenames;
	statics->memory_names = memorynames;
	return(0);
}

char *format_header(uname_field)

register char *uname_field;

{
	register char *ptr;

	ptr = header + UNAME_START;
	while (*uname_field != '\0')
		*ptr++ = *uname_field++;
	return(header);
}

/*
 * Make room for `want' entries in the three per-sample tables.  Returns 1 when
 * the tables hold that many.  A failure leaves the tables and their contents as
 * they were, so the sample in progress stays consistent and merely stops
 * short.
 */
static int
grow(want)
int want;
{
	int n;
	char *c, *p, *r;

	if (want <= npent)
		return(1);
	n = ((want + PENTSTEP - 1) / PENTSTEP) * PENTSTEP;
	c = cur == NULL ? malloc((unsigned)n * sizeof (struct pent))
			: realloc((char *)cur, (unsigned)n * sizeof (struct pent));
	if (c == NULL)
		return(0);
	cur = (struct pent *)c;
	p = prev == NULL ? malloc((unsigned)n * sizeof (struct ptime))
			 : realloc((char *)prev, (unsigned)n * sizeof (struct ptime));
	if (p == NULL)
		return(0);
	prev = (struct ptime *)p;
	r = pref == NULL ? malloc((unsigned)n * sizeof (struct pent *))
			 : realloc((char *)pref, (unsigned)n * sizeof (struct pent *));
	if (r == NULL)
		return(0);
	pref = (struct pent **)r;
	npent = n;
	return(1);
}

/*
 * Where the segment at kernel address `sp' keeps its bytes.  Returns 0, and
 * leaves the descriptor saying so, when there is no segment there or the
 * address is not in the arena -- which is what a namelist that does not match
 * the running kernel produces.
 */
static int
segdesc(sp, dp)
SEG *sp;
register struct sdesc *dp;
{
	register SEG *sg;
	SEG segbuf;

	dp->d_ok = 0;
	dp->d_flags = 0;
	dp->d_paddr = 0;
	dp->d_daddr = 0;
	if (sp == NULL || range(sp) == 0)
		return(0);
	if ((sg = (SEG *)kmap((long)sp, sizeof (SEG))) == NULL) {
		getkval((long)sp, (char *)&segbuf, sizeof (segbuf));
		sg = &segbuf;
	}
	dp->d_ok = 1;
	dp->d_flags = sg->s_flags;
	dp->d_paddr = sg->s_paddr;
	dp->d_daddr = sg->s_daddr;
	return(1);
}

/*
 * Walk the process chain and fill cur[].  Returns 1 when the walk reached the
 * head of the chain again and every step was verified against the one before
 * it, and 0 when a step was not: the caller takes the walk again.  A walk that
 * returns 0 has filled cur[] as far as it got and no further, and the node
 * that failed is not in it.
 */
static int
walk(userp, sysp)
long *userp, *sysp;
{
	register PROC *pp;
	register PROC *pr;
	register struct pent *ep;
	PROC prbuf;
	PROC *from;
	int steps;

	ncur = 0;
	*userp = *sysp = 0;
	memzero((char *)process_states, sizeof (process_states));
	getkval((long)aprocq, (char *)&prbuf, sizeof (prbuf));
	from = (PROC *)aprocq;
	pp = prbuf.p_nback;
	for (steps = 0; pp != (PROC *)aprocq; steps++) {
		if (steps >= MAXLINK)
			return(0);
		if (range(pp) == 0) {
			fprintf(stderr,
	"top: process chain leaves the arena -- kernel image does not match the running kernel\n");
			quit(1);
		}
		if ((pr = (PROC *)kmap((long)pp, sizeof (PROC))) == NULL) {
			getkval((long)pp, (char *)&prbuf, sizeof (prbuf));
			pr = &prbuf;
		}
		/*
		 * The step back.  Everything below reads *pr, so nothing is
		 * believed before this.
		 */
		if (pr->p_nforw != from)
			return(0);
		/*
		 * Process 0 is the idle process.  Its time is the machine's
		 * idle time, which the CPU-state line reports as such, so
		 * counting it here would charge idleness to a process.
		 */
		if (pr->p_pid == 0) {
			from = pp;
			pp = pr->p_nback;
			continue;
		}
		*userp += pr->p_utime;
		*sysp += pr->p_stime;
		/*
		 * A process the tables have no room for still counts in the
		 * totals and in the CPU-state figures; it is only left out of
		 * the table.
		 */
		if (grow(ncur + 1) != 0) {
			ep = &cur[ncur++];
			ep->e_pid = pr->p_pid;
			ep->e_uid = pr->p_ruid;
			ep->e_stat = state(pr, pp);
			ep->e_kern = (pr->p_flags & PFKERN) != 0 ||
				     pr->p_event == (char *)astimer;
			ep->e_kind = kind(pr);
			/*
			 * The kernel's nice runs MINNICE..MAXNICE from a
			 * default of zero -- unice() clamps to that range and
			 * procq starts at 0 -- so nothing is subtracted from
			 * it.  DEFNICE is defined in <proc.h> and is not what
			 * this kernel starts a process at.
			 */
			ep->e_nice = (int)pr->p_nice;
			psize(pr, ep);
			ep->e_time = pr->p_utime + pr->p_stime;
			ep->e_delta = 0;
			(void) segdesc(pr->p_segp[SIUSERP], &ep->e_useg);
			(void) segdesc(pr->p_segp[SISTACK] != NULL
					? pr->p_segp[SISTACK]
					: pr->p_segp[SIPDATA], &ep->e_aseg);
			/*
			 * The command comes out of the process's own memory,
			 * which costs a u-area read and an argument read
			 * each.  Only the rows that reach the screen pay for
			 * it, so it is left until the row is formatted.
			 */
			ep->e_cmd[0] = '\0';
			process_states[ep->e_stat]++;
		}
		from = pp;
		pp = pr->p_nback;
	}
	return(1);
}

/*
 * Take one sample.
 *
 * The arena copy is a frame of this function and nothing outside it reads the
 * copy: the row the display will draw, and the segments the command column
 * will need, are taken out of it here.
 *
 * Both walks run against one copy, and a step either of them cannot verify
 * means the arena moved while it was being copied -- so the answer is another
 * copy, not another walk of the same bytes.
 */
static
sample()
{
	/*
	 * Longs, not chars: a PROC in the copy is used where it lies, and the
	 * processor takes a word only at an even address.
	 */
	long arena[WINSIZE / sizeof (long)];
	unsigned tick;
	long user, sys, du, ds, idle, used;
	int try, okseg, okwalk;

	getkval((long)autimer, (char *)&tick, sizeof (tick));
	okseg = okwalk = 0;
	for (try = 0; try < MAXTRY; try++) {
		snap((char *)arena);
		/*
		 * A walk that has already come out consistent keeps its
		 * answer: the two are independent of each other, and each is
		 * consistent with the copy it ran on.
		 */
		if (okseg == 0)
			okseg = segwalk(&used);
		if (okwalk == 0)
			okwalk = walk(&user, &sys);
		if (okseg != 0 && okwalk != 0)
			break;
	}
	win = NULL;
	/*
	 * A sum that stops early is not a smaller answer, it is a wrong one,
	 * so an incomplete walk leaves the previous complete figure standing
	 * rather than replacing it.
	 */
	if (okseg != 0) {
		lastused = used;
		haveused = 1;
	}
	deltas();

	/*
	 * utimer is a 16-bit tick counter, so it wraps every 655 seconds and
	 * unsigned subtraction is what recovers the interval.  A gap longer
	 * than one wrap cannot be told from a short one.
	 */
	dticks = (long)(unsigned)(tick - lasttick);
	lasttick = tick;

	du = user - lastuser;
	ds = sys - lastsys;
	lastuser = user;
	lastsys = sys;
	if (primed == 0) {
		primed = 1;
		dticks = 0;
		return;
	}
	if (du < 0)
		du = 0;
	if (ds < 0)
		ds = 0;
	idle = dticks - du - ds;
	if (idle < 0)
		idle = 0;
	cp_time[0] += du;
	cp_time[1] += ds;
	cp_time[2] += idle;
	if (du + ds + idle > 0)
		havecpu = 1;
}

/*
 * Copy as much of the arena as `bp' holds, and point the window at it.  The
 * window is cleared first so that the copy itself is read from the device
 * rather than from the copy it is replacing.
 */
static
snap(bp)
char *bp;
{
	unsigned n;

	win = NULL;
	n = casize > (unsigned)WINSIZE ? (unsigned)WINSIZE : casize;
	getkval(aend, bp, n);
	win = bp;
	wlo = aend;
	whi = aend + (long)n;
}

/*
 * Charge each process the time it has used since the previous sample.
 */
static
deltas()
{
	register struct pent *ep;
	register struct ptime *pp;
	register int i, j;

	for (i = 0; i < ncur; i++) {
		ep = &cur[i];
		for (j = 0; j < nprev; j++) {
			pp = &prev[j];
			if (pp->e_pid != ep->e_pid)
				continue;
			if (ep->e_time > pp->e_time)
				ep->e_delta = ep->e_time - pp->e_time;
			break;
		}
	}
}

get_system_info(si)

struct system_info *si;

{
	register int i;

	/* keep what the next sample's deltas need, then take a new one */
	for (i = 0; i < ncur; i++) {
		prev[i].e_pid = cur[i].e_pid;
		prev[i].e_time = cur[i].e_time;
	}
	nprev = ncur;
	sample();

	/*
	 * There is no last pid: the kernel keeps no counter of the last
	 * process created, and -1 is how the display is told to leave the
	 * field out rather than invent one.
	 */
	si->last_pid = -1;

	loadavg(si->load_avg);

	if (havecpu)
		(void) percentages(CPUSTATES, cpu_states, cp_time, cp_old,
				   cp_diff);
	si->cpustates = cpu_states;
	si->procstates = process_states;

	memory();
	si->memory = memory_stats;
}

/*
 * The three load averages, in hundredths.
 *
 * The kernel keeps them as unsigned fixed point with FSHIFT fraction bits and
 * does no scaling of its own -- there is no floating point in that path -- so
 * the conversion to hundredths is done here, in long, and the fraction is
 * shifted out last so that nothing is thrown away before the multiply.
 *
 * A kernel without them leaves LOAD_NONE, which prints as dashes.  That is a
 * kernel older than the one that grew the counter, not an error: one sampled
 * from userland instead would begin at zero on each invocation, see nothing of
 * the run queue between samples, and count top itself.
 */
static
loadavg(la)

long *la;

{
	unsigned av[NLOADAV];
	register int i;

	for (i = 0; i < NUM_AVERAGES; i++)
		la[i] = LOAD_NONE;
	if (haveload == 0)
		return;
	getkval((long)aavenrun, (char *)av, sizeof (av));
	for (i = 0; i < NUM_AVERAGES && i < NLOADAV; i++)
		la[i] = ((long)av[i] * LOAD_SCALE) >> FSHIFT;
}

/*
 * Total, used, free and kernel memory, in K.
 *
 * `used' is the memory segment queue: every segment resident in core, each
 * counted once however many processes share it.  `free' is what is left of
 * the core the segment allocator has to give out.  `kernel' is the alloc
 * arena, which is not part of that pool.
 *
 * The queue itself is walked by sample(), out of the arena copy; this is the
 * arithmetic on what that walk summed.  The three figures the machine's own
 * size gives -- total core, and the bottom and top of the pool -- are what
 * they were at boot, and are read once, at startup.
 */
static
memory()
{
	long used, pool;

	used = haveused ? lastused : 0;
	pool = ccoretop - ccorebot;
	memory_stats[0] = cmsize;
	memory_stats[1] = (int)(used / 1024L);
	memory_stats[2] = (int)((pool - used) / 1024L);
	if (memory_stats[2] < 0)
		memory_stats[2] = 0;
	memory_stats[3] = (int)((long)casize / 1024L);
}

/*
 * Walk the memory segment queue, summing the bytes it holds.  Returns 1 when
 * the walk came back to the head of the queue with every step verified against
 * the one before it, as the process walk verifies its own.
 */
static int
segwalk(usedp)
long *usedp;
{
	register SEG *sp;
	register SEG *sg;
	SEG segbuf;
	SEG *from;
	int steps;

	*usedp = 0;
	getkval((long)asegmq, (char *)&segbuf, sizeof (segbuf));
	from = (SEG *)asegmq;
	sp = segbuf.s_forw;
	for (steps = 0; sp != (SEG *)asegmq; steps++) {
		if (steps >= MAXLINK || range(sp) == 0)
			return(0);
		if ((sg = (SEG *)kmap((long)sp, sizeof (SEG))) == NULL) {
			getkval((long)sp, (char *)&segbuf, sizeof (segbuf));
			sg = &segbuf;
		}
		/*
		 * The step back.  Nothing below is believed before this.
		 */
		if (sg->s_back != from)
			return(0);
		*usedp += sg->s_size;
		from = sp;
		sp = sg->s_forw;
	}
	return(1);
}

caddr_t get_process_info(si, sel, compare)

struct system_info *si;
struct process_select *sel;
int (*compare)();

{
	register int i;
	register struct pent *ep;
	register struct pent **prefp;
	int show_idle, show_uid, show_system;

	show_idle = sel->idle;
	show_system = sel->system;
	show_uid = sel->uid != -1;

	prefp = pref;
	for (i = 0; i < ncur; i++) {
		ep = &cur[i];
		/*
		 * The kernel processes are the swapper and anything else the
		 * kernel runs on its own behalf; -S shows them, as it does
		 * everywhere else.
		 */
		if (!show_system && ep->e_kern)
			continue;
		if (!show_idle && ep->e_delta == 0 && ep->e_stat != S_RUN)
			continue;
		if (show_uid && ep->e_uid != (unsigned)sel->uid)
			continue;
		*prefp++ = ep;
	}
	pref_len = prefp - pref;

	if (compare != NULL)
		qsort((char *)pref, pref_len, sizeof (struct pent *), compare);

	si->p_total = ncur;
	si->p_active = pref_len;

	handle.next_proc = pref;
	handle.remaining = pref_len;
	return((caddr_t)&handle);
}

char *format_next_process(hndl, get_userid)

caddr_t hndl;
char *(*get_userid)();

{
	register struct pent *ep;
	struct handle *hp;
	long tenths;
	char pbuf[8];

	hp = (struct handle *)hndl;
	ep = *(hp->next_proc++);
	hp->remaining--;
	if (ep->e_cmd[0] == '\0')
		getcmd(ep);

	/*
	 * The share of the interval this process held the processor, in
	 * tenths of a percent.  dticks names its own denominator: it is the
	 * clock ticks that actually elapsed between the two samples, not the
	 * requested delay.
	 */
	tenths = 0;
	if (dticks > 0)
		tenths = (ep->e_delta * 1000L) / dticks;
	if (tenths > 1000L)
		tenths = 1000L;
	(void) sprintf(pbuf, "%3ld.%ld", tenths / 10L, tenths % 10L);

	(void) sprintf(fmt, Proc_format,
		ep->e_pid,
		(*get_userid)((int)ep->e_uid),
		ep->e_nice,
		format_k(ep->e_size / 1024L),
		format_k(ep->e_res / 1024L),
		state_abbrev[ep->e_stat],
		format_time((ep->e_time + HZ / 2) / HZ),
		pbuf,
		printable(ep->e_cmd));
	return(fmt);
}

/*
 * Rank by the time used since the last sample, then by total time so that a
 * run of idle processes keeps a stable order.
 */
proc_compare(pp1, pp2)

struct pent **pp1, **pp2;

{
	register struct pent *e1, *e2;

	e1 = *pp1;
	e2 = *pp2;
	if (e1->e_delta != e2->e_delta)
		return(e1->e_delta > e2->e_delta ? -1 : 1);
	if (e1->e_time != e2->e_time)
		return(e1->e_time > e2->e_time ? -1 : 1);
	return(0);
}

/*
 * The real uid of the process with this pid, or -1 if this sample has no
 * such process.  It is what decides whether a kill is this user's to make.
 */
proc_owner(pid)

int pid;

{
	register int i;

	for (i = 0; i < ncur; i++)
		if (cur[i].e_pid == (unsigned)pid)
			return((int)cur[i].e_uid);
	return(-1);
}

/*
 * The size of a process, counting the segments ps(1) counts without -r:
 * everything but the u-area and the auxiliaries.  e_res is the part of that
 * which is resident.
 *
 * s_size is a byte count on this machine, so it is summed as it stands and
 * divided by 1024 once, at print time.  ctob() would scale it by the click
 * size a second time and hand back the byte count under a K heading, and a
 * sum accumulated in an int would wrap at 64K -- an /etc/init of 48K read as
 * 49152K, and /bin/sh at 80896 bytes as 15360.
 */
static
psize(pp, ep)

register PROC *pp;
register struct pent *ep;

{
	register SEG *sg;
	register int n;
	SEG segbuf;

	ep->e_size = 0;
	ep->e_res = 0;
	for (n = 0; n < NUSEG+1; n++) {
		if (n == SIUSERP || n == SIAUXIL)
			continue;
		if (pp->p_segp[n] == NULL)
			continue;
		if (range(pp->p_segp[n]) == 0)
			return;
		if ((sg = (SEG *)kmap((long)pp->p_segp[n], sizeof (SEG)))
		    == NULL) {
			getkval((long)pp->p_segp[n], (char *)&segbuf,
				sizeof (segbuf));
			sg = &segbuf;
		}
		ep->e_size += sg->s_size;
		if ((sg->s_flags & SFCORE) != 0)
			ep->e_res += sg->s_size;
	}
}

/*
 * The state of a process.  `at' is its address in the kernel, which is the
 * sleep channel a process waiting for a child sleeps on.
 */
static
state(pp, at)

register PROC *pp;
PROC *at;

{
	register int s;

	s = pp->p_state;
	if (s == PSSLEEP) {
		if ((PROC *)pp->p_event == at)
			return(S_WAIT);
		if ((pp->p_flags & PFSTOP) != 0)
			return(S_STOP);
		return(S_SLEEP);
	}
	if (s == PSRUN)
		return(S_RUN);
	if (s == PSDEAD)
		return(S_ZOMB);
	return(S_ODD);
}

/*
 * Which of the names that stand in for a command line this process gets.
 */
static
kind(pp)

register PROC *pp;

{
	if (pp->p_state == PSDEAD)
		return(K_EXIT);
	if (pp->p_event == (char *)astimer)
		return(K_SWAP);
	if ((pp->p_flags & PFSLIB) != 0)
		return(K_SLIB);
	if ((pp->p_flags & PFKERN) != 0)
		return(K_KERN);
	return(K_USER);
}

/*
 * A process's command name, out of its own memory.  The u-area and the
 * argument block are read onto the stack: the stack segment is committed
 * whole at exec whatever is on it, so a buffer there costs nothing, while one
 * in bss is memory this process is charged for from the moment it starts.
 */
static
getcmd(ep)

register struct pent *ep;

{
	register char *cp;
	register int c, n;
	struct uproc u;
	char argp[ARGSIZE];

	(void) strcpy(ep->e_cmd, "?");
	switch (ep->e_kind) {
	case K_EXIT:
		(void) strcpy(ep->e_cmd, "<exiting>");
		return;
	case K_SWAP:
		(void) strcpy(ep->e_cmd, "<swap>");
		return;
	case K_SLIB:
		(void) strcpy(ep->e_cmd, "<slib>");
		return;
	case K_KERN:
		(void) strcpy(ep->e_cmd, "<kernel>");
		return;
	}
	if (segread(&ep->e_useg, (unsigned)0, (char *)&u, sizeof (u)) == 0)
		return;
	if (segread(&ep->e_aseg,
		    (unsigned)(u.u_argp - u.u_segl[SISTACK].sr_base),
		    argp, sizeof (argp)) == 0)
		return;
	if (u.u_argc <= 0)
		return;
	cp = argp;
	for (n = 0; n < CMDLEN; n++) {
		if ((c = cp[n]) == '\0')
			break;
		if (!isascii(c) || !isprint(c))
			return;
		ep->e_cmd[n] = c;
	}
	ep->e_cmd[n] = '\0';
	if (n == 0)
		(void) strcpy(ep->e_cmd, "?");
}

/*
 * Read `n' bytes at offset `s' of the segment `dp' describes into `bp'.
 */
static
segread(dp, s, bp, n)

register struct sdesc *dp;
unsigned s;
char *bp;
int n;

{
	if (dp->d_ok == 0)
		return(0);
	if ((dp->d_flags & SFCORE) != 0) {
		if (mem < 0)
			return(0);
		return(pread(mem, dp->d_paddr + (long)s, bp, n));
	}
	if (swap < 0)
		return(0);
	return(pread(swap, dp->d_daddr * BSIZE + (long)s, bp, n));
}

/*
 * A pointer to `n' bytes of kernel memory at `offset', where they lie in the
 * arena copy, or NULL when the copy does not answer for that address.  The
 * copy is the kernel's own layout, so a structure in it is read where it lies
 * rather than moved somewhere first.
 */
static char *
kmap(offset, n)
long offset;
unsigned n;
{
	if (win == NULL || offset < wlo || offset + (long)n > whi)
		return(NULL);
	return(win + (unsigned)(offset - wlo));
}

/*
 * Read `n' bytes of kernel memory at `offset' into `bp'.  Out of the arena
 * copy when the copy answers for that address, and off the device when it
 * does not -- which is every address outside the arena, every address past
 * the end of the window, and everything at all between samples.
 */
static
getkval(offset, bp, n)

long offset;
char *bp;
unsigned n;

{
	unsigned x;

	if (win != NULL && offset >= wlo && offset + (long)n <= whi) {
		memcpy(bp, win + (unsigned)(offset - wlo), (int)n);
		return(1);
	}
	/*
	 * /dev/kmem is addressed by the offset within the kernel's data
	 * segment, so the segment half of a namelist value is not part of the
	 * seek.
	 */
	x = offset;
	if (pread(kmem, (long)x, bp, n) == 0) {
		fprintf(stderr, "top: short read from %s at %u\n", KMEM, x);
		quit(1);
	}
	return(1);
}

/*
 * Read `n' bytes at `off' from `fd'.  Returns 0 on any short read.
 *
 * The arena copy is larger than an int, and read(2) returns its count in one,
 * so a single call for the whole of it would come back negative.  The transfer
 * is broken into pieces no larger than that.
 */
#define	IOCHUNK	16384

static
pread(fd, off, bp, n)

int fd;
long off;
char *bp;
unsigned n;

{
	register unsigned part;

	if (lseek(fd, off, 0) < 0)
		return(0);
	while (n != 0) {
		part = n > IOCHUNK ? IOCHUNK : n;
		if (read(fd, bp, (int)part) != (int)part)
			return(0);
		bp += part;
		n -= part;
	}
	return(1);
}
