/*
 * Copyright (c) 2026 Michal Pleban.
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * mem -- report physical memory usage.
 *
 * Walks the kernel's in-core segment queue (segmq, seg.c) the way ps(1) walks
 * the process list: nlist() the addresses out of /coherent, snapshot the
 * kalloc arena (where the SEG nodes live) through /dev/kmem, then chase the
 * list inside the snapshot.  Each step is checked against the one before it
 * through s_back, so a queue caught mid-edit is reported rather than followed.
 *
 * The queue is kept in memory-address order -- seggrow() depends on that to
 * find a segment's neighbours -- so the gaps between consecutive segments are
 * exactly the free holes.  User memory on the Z8001 is physically contiguous
 * per segment, so fragmentation matters as much as the total: a program can
 * fail to load with plenty of free memory in scattered holes.  Hence
 * "largest".
 *
 * s_paddr and s_size are physical BYTE quantities (paddr_t/fsize_t), and
 * corebot/coretop are physical byte addresses; every figure below is
 * converted to Kb for printing and nothing is summed in an int.
 *
 *	mem		totals, then free/largest
 *	mem -l		also list every in-core segment
 */
#include <stdio.h>
#include <l.out.h>
#include <seg.h>

#define range(p)	((long)(p) >= (long)aend && \
			 (long)(p) - (long)aend < (long)casize)
#define map(p)		((SEG *)&allp[(long)(p) - (long)aend])

#define	kb(n)		((long)(n) / 1024L)

#define	asegmq		nl[0].n_value
#define	acorebot	nl[1].n_value
#define	acoretop	nl[2].n_value
#define	aasize		nl[3].n_value
#define	aend		nl[4].n_value

/*
 * A complete initializer group terminates the table, not a bare "": a
 * trailing partial group is dropped, which leaves the array one element short
 * and nlist() scans (and zeroes) past its end.
 */
struct nlist nl[] ={
	"segmq_",	0,	0,
	"corebot_",	0,	0,
	"coretop_",	0,	0,
	"asize_",	0,	0,
	"end_",		0,	0,
	"",		0,	0
};

char	*names[] = {
	"segmq", "corebot", "coretop", "asize", "end"
};

int	lflag;				/* -l: list every segment */
int	kfd;				/* /dev/kmem */
char	*allp;				/* arena snapshot */
unsigned casize;			/* arena size */

char	*malloc();

main(argc, argv)
char *argv[];
{
	register SEG *sp;
	register SEG *sg;
	register int i;
	SEG head;
	SEG *from;
	long corebot, coretop, prev;
	long total, used, sharedk, savedk, stackk, systk;
	long gap, biggap;
	unsigned nseg, nshared, ngap;
	char *cp;

	for (i = 1; i < argc; i++)
		for (cp = argv[i]; *cp; cp++)
			switch (*cp) {
			case '-':
				continue;
			case 'l':
				lflag++;
				continue;
			default:
				fprintf(stderr, "Usage: mem [-l]\n");
				exit(1);
			}

	nlist("/coherent", nl);
	for (i = 0; i < sizeof (names) / sizeof (names[0]); i++)
		if (nl[i].n_type == 0)
			fail("/coherent exports no `%s'", names[i]);
	if ((kfd = open("/dev/kmem", 0)) < 0)
		fail("cannot open /dev/kmem", (char *)0);
	kread((long)aasize, (char *)&casize, sizeof (casize));
	if ((allp = malloc(casize)) == NULL)
		fail("out of memory", (char *)0);
	kread((long)aend, allp, (int)casize);
	kread((long)asegmq, (char *)&head, sizeof (head));
	kread((long)acorebot, (char *)&corebot, sizeof (corebot));
	kread((long)acoretop, (char *)&coretop, sizeof (coretop));

	total = coretop - corebot;
	used = sharedk = savedk = stackk = systk = 0;
	nseg = nshared = ngap = 0;
	biggap = 0;
	prev = corebot;

	if (lflag)
		printf(" BASEK SIZEK FLAGS REF\n");
	from = (SEG *)asegmq;
	for (sp = head.s_forw; sp != (SEG *)asegmq; sp = sg->s_forw) {
		if (range(sp) == 0)
			fail("segment list left the kalloc arena (rerun)",
				(char *)0);
		sg = map(sp);
		/*
		 * The step back.  Nothing below is believed before this: the
		 * snapshot is taken without the segmentation lock, so a queue
		 * caught mid-edit must be reported, not walked.
		 */
		if (sg->s_back != from)
			fail("segment list changed under the read (rerun)",
				(char *)0);
		nseg++;
		used += sg->s_size;
		if (sg->s_flags & SFSHRX) {
			nshared++;
			sharedk += sg->s_size;
			if (sg->s_urefc > 1)
				savedk += (sg->s_urefc - 1) * sg->s_size;
		}
		if (sg->s_flags & SFDOWN)
			stackk += sg->s_size;
		if (sg->s_flags & SFSYST)
			systk += sg->s_size;
		if (sg->s_paddr > prev) {
			gap = sg->s_paddr - prev;
			ngap++;
			if (gap > biggap)
				biggap = gap;
		}
		prev = sg->s_paddr + sg->s_size;
		if (lflag)
			printf("%6ld %5ld %c%c%c%c  %3d\n",
				kb(sg->s_paddr - corebot), kb(sg->s_size),
				sg->s_flags & SFSHRX ? 'x' : '-',
				sg->s_flags & SFTEXT ? 't' : '-',
				sg->s_flags & SFDOWN ? 's' : '-',
				sg->s_flags & SFSYST ? 'k' : '-',
				sg->s_urefc);
		from = sp;
	}
	if (coretop > prev) {
		gap = coretop - prev;
		ngap++;
		if (gap > biggap)
			biggap = gap;
	}

	/*
	 * Everything below corebot is fixed at boot: the kernel image, the
	 * kalloc arena, the inode table, the disk buffer cache and the
	 * clists (commodore.c).  The arena is broken out because it is the
	 * one pool whose exhaustion is reported as EAGAIN from fork(2) or
	 * open(2) rather than as a shortage of memory.
	 */
	printf("kernel  %5ldK  including a %ldK alloc arena\n",
		kb(corebot), kb(casize));
	printf("user    %5ldK\n", kb(total));
	printf("used    %5ldK  in %u segments\n", kb(used), nseg);
	if (nshared != 0) {
		printf("shared  %5ldK  in %u segments", kb(sharedk), nshared);
		if (savedk != 0)
			printf(", saving %ldK", kb(savedk));
		printf("\n");
	}
	if (stackk != 0)
		printf("stacks  %5ldK\n", kb(stackk));
	if (systk != 0)
		printf("system  %5ldK\n", kb(systk));
	printf("free    %5ldK  in %u holes, largest %ldK\n",
		kb(total - used), ngap, kb(biggap));
	exit(0);
}

fail(s, a)
char *s;
char *a;
{
	fprintf(stderr, "mem: ");
	fprintf(stderr, s, a);
	fprintf(stderr, "\n");
	exit(1);
}

kread(s, bp, n)
long s;
char *bp;
{
	/* kernel data addresses are 16-bit offsets into /dev/kmem */
	unsigned x;

	x = s;
	s = x;
	lseek(kfd, s, 0);
	if (read(kfd, bp, n) != n)
		fail("kernel memory read error", (char *)0);
}
