/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/* COHERENT.c -- the load average, read from the running kernel.
 *
 * The original of this file (Harry C. Pulley, IV, 1993, after Randy Wright's
 * dtime.c) asked a loadable driver called mz for two summed times through
 * ioctl.  That driver is not part of this system.  The kernel here keeps the
 * figure itself -- three decayed averages of the run queue, sampled on the
 * clock (coh/clock.c) -- so this reads the one-minute average out of
 * /dev/kmem, with a namelist from /coherent.  /dev/kmem is 600 root, so
 * mgrload is installed setuid root; setup() gives the privilege up as soon as
 * the open has answered, whether it succeeded or not, and mgrload calls
 * getload() once before it sets its window up so that happens first.
 *
 * /coherent must be the kernel that is running.  A namelist from a different
 * link resolves avenrun_ to an address that now holds something else, and a
 * kernel that keeps no load average has no such name at all; both answer
 * zero, which the graph draws as a flat line.
 *
 * The answer is an integer.  getload() returned a double and kept a floating
 * point decaying average of its own, and float support on this target is
 * incomplete; the kernel's value is fixed point, and the graph's partitions
 * are PSIZE=100 of what this returns -- 1.00 of a process -- so hundredths of
 * a process are two digits finer than anything it can show.
 */

#include <stdio.h>
#include <fcntl.h>
#include <l.out.h>
#include <const.h>
#include <sched.h>

#include "getload.h"

#define CENTI		100		/* Centiloads per unit of load */
#define MAXCENT		9900L		/* Load ceiling, 99.00 */

static struct nlist nl[] ={
	"avenrun_",	0,	0,
	""
};

static int	kfd = -1;		/* /dev/kmem */
static int	dead;			/* The kernel cannot be read */

/* lseek(2) answers a long, and an undeclared function answers an int: without
   this the failure test below reads half of the offset it just seeked to. */
extern long	lseek();

/*
 * Resolve the namelist and open /dev/kmem.  Returns 0 if anything is wrong,
 * and the caller then stops asking: the graph draws a flat zero rather than
 * the client exiting out of a window the user opened deliberately.
 *
 * It says which step failed first, once, on stderr -- which for a client is
 * its own window.  A flat line is also what a correctly working graph draws on
 * an idle machine, so the two are indistinguishable on the screen and the
 * difference has to be stated rather than inferred.
 */
static int
setup()
{
	int	ok;

	nlist("/coherent", nl);
	if (nl[0].n_type == 0) {
		fprintf(stderr,
		    "mgrload: /coherent has no avenrun_ -- not the running kernel\r\n");
		setuid(getuid());
		return (0);
	}
	ok = 1;
	if ((kfd=open("/dev/kmem", O_RDONLY)) < 0) {
		fprintf(stderr, "mgrload: /dev/kmem: cannot open\r\n");
		ok = 0;
	}
	/*
	 * Turn off setuid privileges.  The descriptor is the only thing this
	 * program needs root for; every window call and every other file it
	 * touches is the real user's.
	 */
	setuid(getuid());
	return (ok);
}

/*
 * The one-minute load average, in centiloads.
 *
 * avenrun[0] is scaled by FSCALE, so the conversion is one multiply and one
 * shift, in long because the product of a full-scale average and CENTI does
 * not fit in a word.  The seek is the offset within the kernel's data
 * segment, so the segment half of the namelist value is not part of it.
 */
int
getload()
{
	unsigned av[NLOADAV];
	long cent;

	if (dead)
		return (0);
	if (kfd < 0 && setup() == 0) {
		dead = 1;
		return (0);
	}
	if (lseek(kfd, (long)(unsigned)nl[0].n_value, 0) < 0
	 || read(kfd, (char *)av, sizeof (av)) != sizeof (av)) {
		fprintf(stderr, "mgrload: /dev/kmem: no read at avenrun_ (%u)\r\n",
		    (unsigned)nl[0].n_value);
		dead = 1;
		return (0);
	}
	cent = ((long)av[0] * CENTI) >> FSHIFT;
	if (cent > MAXCENT)
		cent = MAXCENT;
	return ((int)cent);
}
/* end of COHERENT.c*/
