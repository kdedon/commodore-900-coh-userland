/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * fdctest.c -- how many connections does this machine have, and can every one
 * of them be answered?
 *
 * The inet daemon is an ordinary process with twenty descriptors, so what
 * limits the number of sockets on the whole machine is how many descriptors a
 * channel costs it.  Held both ways -- request FIFO and reply FIFO -- a channel
 * costs two and the machine has eight connections, which `rc.net' spends before
 * the machine has finished booting.  Reaching the reply direction through
 * coh_fdc.c costs one and it has fifteen.
 *
 * That claim is arithmetic, and arithmetic about descriptors is exactly the
 * kind of claim that is wrong in practice: the count has to be taken from a
 * kernel that is really refusing, with real FIFOs, at a real limit.  So this
 * runs the SHIPPED coh_fdc.c (compiled verbatim, no copy) against
 * RLIMIT_NOFILE = 20 -- COHERENT's NOFILE -- with the daemon's own fixed
 * descriptors open, and models the daemon's accept loop:
 *
 *	while there is room: take a descriptor for a request FIFO, and hand
 *	the reply FIFO's path to the cache.
 *
 * and then asks the question that matters, which is not how many were admitted
 * but whether every admitted channel can still be REPLIED to.  A ceiling that
 * admits channels the daemon cannot answer is worse than a low one: the client
 * gets a socket and then waits for ever.
 *
 * Scored, so that it fails against a policy that is wrong in either direction:
 *
 *	- admission must stop.  A cache that never refuses is handing out
 *	  channels the descriptors cannot carry.
 *	- at least MINCHAN must be admitted.  Fourteen is the arithmetic
 *	  (twenty, less stdin/stdout/stderr and the rendezvous FIFO, less one
 *	  kept back so a reply can always be written) with a channel to spare.
 *	- every admitted channel must yield a descriptor that is open on ITS
 *	  reply FIFO -- checked by inode, not by the write succeeding, since a
 *	  write to the wrong channel's FIFO succeeds just as well -- and a
 *	  reply-sized record must go down it.
 *	- closing every channel must give every descriptor back.
 *
 * Host-only scaffolding; nothing here is built for the C900.  The machine-side
 * measurement of the same number is test/chanmax.c, which needs a running
 * daemon.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "coh_fdc.h"
#include "inet_ipc.h"

#define NOFILE_COH	20	/* include/sys/param.h:11			*/
#define MINCHAN		14	/* below this the fix has not taken	*/

static char dir[64];
static int reqfd[FDC_NCHAN];
static char replpath[FDC_NCHAN][FDC_PATHLEN];
static int bad;

static void
fail(what)
char *what;
{
	printf("fdc: FAIL -- %s\n", what);
	bad++;
}

/* Descriptors this process can still get, by asking the kernel. */
static int
canopen()
{
	int probe[NOFILE_COH + 4];
	int n, fd, got;

	for (n= 0; n < (int)(sizeof(probe)/sizeof(probe[0])); n++)
	{
		if ((fd= dup(1)) < 0)
			break;
		probe[n]= fd;
	}
	got= n;
	while (n > 0)
		(void)close(probe[--n]);
	return got;
}

static void
mkpath(buf, kind, chan)
char *buf;
char *kind;
int chan;
{
	sprintf(buf, "%s/%s%d", dir, kind, chan);
	if (strlen(buf) >= FDC_PATHLEN)
	{
		printf("fdc: FAIL -- path %s does not fit sh_repl[%d]\n",
			buf, FDC_PATHLEN);
		exit(2);
	}
	(void)unlink(buf);
	if (mkfifo(buf, 0600) < 0)
	{
		perror(buf);
		exit(2);
	}
}

int
main(argc, argv)
int argc;
char **argv;
{
	struct rlimit rl;
	struct stat sb, sf;
	char reqpath[FDC_PATHLEN];
	char tmpl[64];
	nwrepl_t rep;
	int i, n, rv, fd, free0, free1;

	strcpy(tmpl, "/tmp/fdcXXXXXX");	/* short: a path must fit sh_repl[40] */
	if (mkdtemp(tmpl) == 0)
	{
		perror("mkdtemp");
		return 2;
	}
	strcpy(dir, tmpl);

	rl.rlim_cur= NOFILE_COH;
	rl.rlim_max= NOFILE_COH;
	if (setrlimit(RLIMIT_NOFILE, &rl) < 0)
	{
		perror("setrlimit");
		return 2;
	}

	/*
	 * The daemon's fixed descriptors: stdin, stdout, stderr, and the
	 * rendezvous FIFO it listens on.  Everything the cache reports is
	 * relative to these, so they are opened before anything is measured --
	 * a count taken without them is a count of a daemon that does not exist.
	 */
	mkpath(reqpath, "rv", 0);
	if ((rv= open(reqpath, O_RDWR)) < 0)
	{
		perror(reqpath);
		return 2;
	}
	fdc_init(rv);

	free0= canopen();
	printf("fdc: %d descriptors free with the daemon's four open\n", free0);
	printf("fdc: ceiling reported %d\n", fdc_ceiling());

	for (i= 0; i < FDC_NCHAN; i++)
		reqfd[i]= -1;

	/*
	 * Admit channels the way sr_accept() does: ask for room, take the
	 * descriptor that is never given back (the request FIFO), and give the
	 * cache the reply FIFO's path.  O_RDWR on the request FIFO where the
	 * daemon uses O_RDONLY -- the daemon has a client holding the other end
	 * and this has not, and O_RDONLY would block for ever waiting for one.
	 * The descriptor cost, which is what is being measured, is the same.
	 */
	for (n= 0; n < FDC_NCHAN; n++)
	{
		if (!fdc_room())
			break;
		if (!fdc_spare())
			break;
		mkpath(reqpath, "q", n);
		mkpath(replpath[n], "r", n);
		if ((reqfd[n]= open(reqpath, O_RDWR)) < 0)
			break;
		fdc_hold(n, replpath[n]);
	}
	printf("fdc: admitted %d channels\n", n);

	if (n >= FDC_NCHAN)
		fail("admission never stopped: it ran out of table, not of"
			" descriptors -- the reserve is not being kept");
	if (n < MINCHAN)
	{
		printf("fdc: %d channels, wanted at least %d\n", n, MINCHAN);
		fail("the machine's connection count is below the arithmetic:"
			" a channel is still costing two descriptors");
	}

	/*
	 * Every admitted channel must be answerable, and answerable on its OWN
	 * FIFO.  Done in one pass over all of them so that the last channels --
	 * the ones admitted when descriptors were scarcest -- are asked for a
	 * descriptor while the earlier ones are still holding theirs.
	 */
	memset((char *)&rep, 0, sizeof(rep));
	for (i= 0; i < n; i++)
	{
		if ((fd= fdc_fd(i)) < 0)
		{
			printf("fdc: channel %d of %d\n", i, n);
			fail("an admitted channel cannot be answered: no"
				" descriptor for its reply FIFO");
			continue;
		}
		if (fstat(fd, &sf) < 0 || stat(replpath[i], &sb) < 0 ||
			sf.st_ino != sb.st_ino)
		{
			printf("fdc: channel %d\n", i);
			fail("the reply descriptor is open on another"
				" channel's FIFO");
			continue;
		}
		rep.nwr_status= i;
		rep.nwr_rop= 1;
		rep.nwr_dlen= 0;
		if (write(fd, (char *)&rep, sizeof(rep)) != (int)sizeof(rep))
		{
			printf("fdc: channel %d\n", i);
			fail("the reply could not be written");
		}
	}

	/* And they must all come back. */
	for (i= 0; i < n; i++)
	{
		fdc_forget(i);
		if (reqfd[i] >= 0)
			(void)close(reqfd[i]);
		(void)unlink(replpath[i]);
	}
	free1= canopen();
	if (free1 < free0)
	{
		printf("fdc: %d free before, %d after %d channels closed\n",
			free0, free1, n);
		fail("descriptors were not all given back");
	}

	printf("fdc: %s -- %d channels, %d descriptors recovered\n",
		bad ? "FAIL" : "ok", n, free1);
	return bad ? 1 : 0;
}
