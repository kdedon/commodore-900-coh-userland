/*
 * mkkmem.c -- forge a /dev/kmem image for top(1) and ps(1) to walk.
 *
 * top reads a namelist from a kernel image, seeks /dev/kmem to the low half
 * of each symbol's address, and follows a chain of far pointers through the
 * kernel's alloc arena.  On a host there is no kernel and no /dev/kmem, so
 * this writes a file that answers those seeks: it takes its offsets from a
 * REAL kernel image through the same nlist() call top makes, and lays out the
 * process chain with the same <proc.h> the kernel and top are compiled
 * against, so the record layout is the compiler's and not a guess.
 *
 * The process table it forges is fixed and known, which is what makes the
 * output checkable: pid 1 has used 1 second and sleeps, pid 42 has used 30
 * and is runnable, pid 7 has used none and is runnable, and pid 0 is the idle
 * process that top must leave out.  Two runnable processes is one more than
 * the processor can be running, so the load a reader computes from this table
 * is 1.00 even though no time passes between reads of a file.
 *
 * The SEGMENTS are forged too, and they are half of what a reader does with
 * this image: each process but the idle one owns a data segment and a stack,
 * which is the SIZE its row prints, and the memory segment queue holds all of
 * them, which is the `used' figure on the memory line.  Both come out at fixed
 * numbers -- 40 K a process, 120 K used of a 896 K pool -- so a reader's
 * arithmetic over the queue is checkable and not merely non-empty.  Without
 * them the queue walk found nothing and every reader printed a blank memory
 * line, which no assertion could tell from a right one.
 *
 *	mkkmem kernel out            a chain top must accept
 *	mkkmem -bad kernel out       a chain whose first link leaves the arena
 *	mkkmem -stale kernel out     an image laid out by a different link
 *	mkkmem -short kernel out     an image that stops before the arena
 *	mkkmem -reused kernel out    a chain entry whose block is another
 *				     structure's now
 *
 * The four broken images are the negative controls: an image a reader cannot
 * tell from a good one until it uses it must produce that reader's diagnostic
 * and no table.  Without them a run that printed nothing would look like a
 * pass.  Each is a defect the namelist route really has -- a chain pointer out
 * of bounds, a namelist resolving to addresses that now hold something else, a
 * device that answers a seek but not the read behind it, and a process that
 * left the chain between the read of the pointer to it and the read of the
 * structure it points at.
 *
 * -reused is the last of those, held still.  The chain is read one structure at
 * a time, so a block freed and handed to another tenant between two reads is
 * the hazard, and what stands against it is that the chain is doubly linked:
 * the entry reached through p_nback must point back through p_nforw at the
 * entry it was reached from.  Here the third entry does not, and the table must
 * STOP there -- short by the entries beyond it -- rather than report them.
 *
 * -stale moves every datum a fixed distance, which is what a relink of the
 * kernel does to it, and leaves the addresses this namelist names holding the
 * zeros of unused kernel data.  The arena size a reader picks up is then 0.
 *
 * Build (host cross toolchain, run under n2z8001 -runexec):
 *	ccz -s -i -L -I include -I include/sys -o mkkmem mkkmem.c
 */
#include <stdio.h>
#include <const.h>
#include <l.out.h>
#include <proc.h>
#include <seg.h>
#include <stat.h>

#define NFAKE	4			/* Processes in the forged chain */

/*
 * The symbols top and ps look up, in top's order.
 */
struct nlist nl[] ={
	"procq_",	0,	0,
	"utimer_",	0,	0,
	"stimer_",	0,	0,
	"asize_",	0,	0,
	"end_",		0,	0,
	"segmq_",	0,	0,
	"msize_",	0,	0,
	"corebot_",	0,	0,
	"coretop_",	0,	0,
	""
};

#define	aprocq		nl[0].n_value
#define	autime		nl[1].n_value
#define aasize		nl[3].n_value
#define aend		nl[4].n_value
#define asegmq		nl[5].n_value

/*
 * The core the forged machine has.  A reader divides the segment sizes below
 * by 1024 and subtracts them from the pool, so both numbers it prints are
 * fixed by these.
 */
#define MSIZE	1024			/* K of core */
#define COREBOT	0x010000L
#define CORETOP	0x0F0000L		/* 896 K of pool */

/*
 * Two segments per process that has any: what ps(1) counts as its size.  The
 * stack is what exec commits, and the data segment is the rest.
 */
#define NSEG	((NFAKE-1)*2)		/* pid 0 has none */
#define SEGDATA	8192L
#define SEGSTK	32768L

/*
 * What each forged process claims.  p_state is what top's state() reads;
 * PSRUN is what the load's run-queue term counts.
 */
struct fake {
	unsigned f_pid;
	unsigned f_uid;
	unsigned f_state;
	long	 f_utime;
	long	 f_stime;
} fakes[NFAKE] = {
	0,	0,	PSRUN,		0L,	0L,	/* idle: must not show */
	1,	0,	PSSLEEP,	60L,	40L,	/* 1.00 s */
	42,	100,	PSRUN,		2000L,	1000L,	/* 30.00 s */
	7,	100,	PSRUN,		0L,	0L,	/* no time at all */
};

/*
 * How far a relink moves the kernel's data.  -stale lays the whole image out
 * this far from where the namelist says it is.
 */
#define SHIFT	0x2000

int	fd;
char	*me;
unsigned shift;				/* Added to every offset written */

long	segat();

main(argc, argv)
int argc;
char *argv[];
{
	char *kern, *out;
	int bad, shortimage, reused;
	unsigned procqoff, endoff, tickoff, sizeoff;
	unsigned casize;
	unsigned tick;
	long base, qaddr, sbase, squeue;
	PROC hdr, p;
	SEG sq, sg;
	int i;

	me = argv[0];
	bad = 0;
	shortimage = 0;
	reused = 0;
	while (argc > 1 && argv[1][0] == '-') {
		if (strcmp(argv[1], "-bad") == 0)
			bad = 1;
		else if (strcmp(argv[1], "-stale") == 0)
			shift = SHIFT;
		else if (strcmp(argv[1], "-short") == 0)
			shortimage = 1;
		else if (strcmp(argv[1], "-reused") == 0)
			reused = 1;
		else
			break;
		argc--; argv++;
	}
	if (argc != 3) {
		fprintf(stderr,
	    "Usage: mkkmem [-bad] [-stale] [-short] [-reused] kernel out\n");
		exit(1);
	}
	kern = argv[1];
	out = argv[2];

	nlist(kern, nl);
	if (nl[0].n_type == 0) {
		fprintf(stderr, "%s: %s: no namelist\n", me, kern);
		exit(1);
	}
	/*
	 * The seek into /dev/kmem is the offset within the kernel's data
	 * segment, so the file is laid out by the low halves; the far
	 * pointers stored IN it keep the whole value, because that is what
	 * the arena bounds check compares against.
	 */
	procqoff = (unsigned)aprocq;
	tickoff	 = (unsigned)autime;
	sizeoff	 = (unsigned)aasize;
	endoff	 = (unsigned)aend;
	base	 = aend + (long)shift;
	qaddr	 = aprocq + (long)shift;
	sbase	 = base + (long)(NFAKE * sizeof (PROC));
	squeue	 = asegmq + (long)shift;
	casize	 = NFAKE * sizeof (PROC) + NSEG * sizeof (SEG);
	tick	 = 5000;

	printf("procq_ %u  utimer_ %u  asize_ %u  end_ %u  arena %u bytes\n",
		procqoff, tickoff, sizeoff, endoff, casize);

	if ((fd=creat(out, 0666)) < 0) {
		fprintf(stderr, "%s: cannot create %s\n", me, out);
		exit(1);
	}
	/*
	 * creat gives a zero-length file and every field written below is
	 * placed by an absolute seek, so the gaps between them read as zero
	 * -- which is what unused kernel data would be.
	 */
	put(sizeoff, (char *)&casize, sizeof (casize));
	put(tickoff, (char *)&tick, sizeof (tick));
	putcore();

	/*
	 * The queue header lives at procq_ and the walk follows p_nback.  A
	 * link is a pointer into the arena, whose first byte is end_.
	 */
	clear((char *)&hdr, sizeof (hdr));
	hdr.p_nback = (PROC *)(base + 0);
	hdr.p_nforw = (PROC *)(base + 0);
	hdr.p_lforw = (PROC *)qaddr;
	hdr.p_lback = (PROC *)qaddr;
	if (bad)
		hdr.p_nback = (PROC *)(base + (long)casize + 0x1000L);
	put(procqoff, (char *)&hdr, sizeof (hdr));

	/*
	 * -short stops here: the arena the header points into is past the end
	 * of the file, so the seek to it succeeds and the read behind it does
	 * not.
	 */
	for (i=0; shortimage == 0 && i<NFAKE; i++) {
		clear((char *)&p, sizeof (p));
		p.p_pid	  = fakes[i].f_pid;
		p.p_ppid  = 1;
		p.p_uid	  = fakes[i].f_uid;
		p.p_ruid  = fakes[i].f_uid;
		p.p_state = fakes[i].f_state;
		p.p_utime = fakes[i].f_utime;
		p.p_stime = fakes[i].f_stime;
		p.p_ttdev = NODEV;
		/*
		 * The last entry closes the ring on the header, which is how
		 * the walk knows it is done.  p_nforw closes it the other
		 * way: the kernel's chain is doubly linked, and a reader
		 * checks each step against the one before it by following
		 * p_nforw back to where it came from.
		 */
		if (i+1 < NFAKE)
			p.p_nback = (PROC *)(base + (long)(i+1)*sizeof (PROC));
		else
			p.p_nback = (PROC *)qaddr;
		if (i > 0)
			p.p_nforw = (PROC *)(base + (long)(i-1)*sizeof (PROC));
		else
			p.p_nforw = (PROC *)qaddr;
		/*
		 * -reused: the third entry's block has been handed to some
		 * other tenant since the pointer to it was taken, so its
		 * bytes are no longer a PROC and its backward link no longer
		 * names the entry the walk arrived from.  Everything in that
		 * entry and beyond is another structure's, and a reader that
		 * believes it prints numbers it has invented.
		 */
		if (reused && i == 2)
			p.p_nforw = (PROC *)(base + (long)casize - 2L);
		p.p_lforw = (PROC *)qaddr;
		p.p_lback = (PROC *)qaddr;
		/*
		 * Every process but the idle one owns a data segment and a
		 * stack, which is what its SIZE column adds up and what the
		 * memory line sums over the queue.
		 */
		if (i > 0) {
			p.p_segp[SIPDATA] = (SEG *)segat(sbase, (i-1)*2);
			p.p_segp[SISTACK] = (SEG *)segat(sbase, (i-1)*2+1);
		}
		put(endoff + i*sizeof (PROC), (char *)&p, sizeof (p));
	}

	/*
	 * The memory segment queue, doubly linked through s_forw and s_back
	 * the way the kernel keeps it, with its header at segmq_.
	 */
	if (shortimage == 0) {
		clear((char *)&sq, sizeof (sq));
		sq.s_forw = (SEG *)segat(sbase, 0);
		sq.s_back = (SEG *)segat(sbase, NSEG-1);
		put((unsigned)asegmq, (char *)&sq, sizeof (sq));
		for (i=0; i<NSEG; i++) {
			clear((char *)&sg, sizeof (sg));
			sg.s_flags = SFCORE;
			sg.s_urefc = 1;
			sg.s_size = (i & 1) ? SEGSTK : SEGDATA;
			sg.s_paddr = COREBOT + (long)i * SEGSTK;
			sg.s_forw = i+1 < NSEG ? (SEG *)segat(sbase, i+1)
					       : (SEG *)squeue;
			sg.s_back = i > 0 ? (SEG *)segat(sbase, i-1)
					  : (SEG *)squeue;
			put((unsigned)(sbase - base) + endoff + i*sizeof (SEG),
				(char *)&sg, sizeof (sg));
		}
	}
	close(fd);
	printf("%s: %d processes, %d segments, sizeof(PROC) %u sizeof(SEG) %u\n",
		out, NFAKE, shortimage ? 0 : NSEG,
		(unsigned)sizeof (PROC), (unsigned)sizeof (SEG));
	exit(0);
}

/*
 * The address of the `i'th forged segment.
 */
long
segat(sbase, i)
long sbase;
int i;
{
	return (sbase + (long)i * (long)sizeof (SEG));
}

/*
 * What the machine has: the size of core and the pool the segment allocator
 * gives out of.  Fixed, so the memory line a reader prints is fixed too.
 */
putcore()
{
	unsigned msize;
	long v;

	msize = MSIZE;
	put((unsigned)nl[6].n_value, (char *)&msize, sizeof (msize));
	v = COREBOT;
	put((unsigned)nl[7].n_value, (char *)&v, sizeof (v));
	v = CORETOP;
	put((unsigned)nl[8].n_value, (char *)&v, sizeof (v));
}

/*
 * Write `n' bytes at absolute offset `off'.
 */
put(off, bp, n)
unsigned off;
char *bp;
int n;
{
	off += shift;
	if (lseek(fd, (long)off, 0) < 0) {
		fprintf(stderr, "%s: seek to %u failed\n", me, off);
		exit(1);
	}
	if (write(fd, bp, n) != n) {
		fprintf(stderr, "%s: write of %d at %u failed\n", me, n, off);
		exit(1);
	}
}

clear(bp, n)
register char *bp;
register int n;
{
	while (n-- > 0)
		*bp++ = '\0';
}
