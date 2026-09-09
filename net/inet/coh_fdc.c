/*
coh_fdc.c -- the inet daemon's reply-descriptor cache.

See ../include/coh_fdc.h for what this is for.  Nothing here includes the
stack's headers: the whole file is descriptors and paths, so it compiles on its
own and the host harness (../test/hostcheck) exercises this source verbatim
against a real descriptor limit.

open/close/dup are left to K&R's implicit int declaration on purpose -- a
declaration here would conflict with the host <fcntl.h> prototypes the harness
compiles against.
*/

#include "coh_fdc.h"

#include <fcntl.h>

struct fdc_chan {
	int	fc_fd;			/* held reply descriptor, -1 if not */
	long	fc_use;			/* when it was last written to	*/
	int	fc_active;		/* a channel occupies this slot	*/
	char	fc_path[FDC_PATHLEN];	/* its reply FIFO		*/
};

static struct fdc_chan fdc_chans[FDC_NCHAN];
static long fdc_stamp;
static int fdc_ref = 1;		/* a descriptor to dup when probing	*/

/*
 * How many more descriptors this process can get.
 *
 * Asked of the kernel rather than worked out from NOFILE: the daemon does not
 * know how many descriptors it was started with, and the /dev/eth ports open
 * after this file is initialised, so an arithmetic answer would be wrong in
 * both directions.  dup() until it refuses, then hand them all back.
 *
 * Only the accept path asks, so the dozen-odd system calls this costs are paid
 * once per new channel and never per request.
 */
static int
fdc_free()
{
	int probe[FDC_NCHAN + 4];
	int n, fd, got;

	for (n= 0; n < (int)(sizeof(probe)/sizeof(probe[0])); n++)
	{
		if ((fd= dup(fdc_ref)) < 0)
			break;
		probe[n]= fd;
	}
	got= n;
	while (n > 0)
		(void)close(probe[--n]);
	return got;
}

/* Descriptors the cache is holding, all of which it can give back. */
static int
fdc_held()
{
	int i, n;

	for (i= n= 0; i < FDC_NCHAN; i++)
		if (fdc_chans[i].fc_fd >= 0)
			n++;
	return n;
}

/*
 * Close the reply descriptor of the channel that has gone longest without
 * writing one, and say whether there was one to close.  `except' is the
 * channel that wants the descriptor, which must not be the one robbed.
 */
static int
fdc_evict(except)
int except;
{
	int i, victim;

	victim= -1;
	for (i= 0; i < FDC_NCHAN; i++)
	{
		if (i == except || fdc_chans[i].fc_fd < 0)
			continue;
		if (victim < 0 || fdc_chans[i].fc_use < fdc_chans[victim].fc_use)
			victim= i;
	}
	if (victim < 0)
		return 0;
	(void)close(fdc_chans[victim].fc_fd);
	fdc_chans[victim].fc_fd= -1;
	return 1;
}

void
fdc_init(reffd)
int reffd;
{
	int i;

	for (i= 0; i < FDC_NCHAN; i++)
	{
		fdc_chans[i].fc_fd= -1;
		fdc_chans[i].fc_use= 0;
		fdc_chans[i].fc_active= 0;
		fdc_chans[i].fc_path[0]= '\0';
	}
	fdc_stamp= 0;
	fdc_ref= (reffd >= 0) ? reffd : 1;
}

void
fdc_hold(chan, path)
int chan;
char *path;
{
	char *p;
	int n;

	if (chan < 0 || chan >= FDC_NCHAN)
		return;
	if (fdc_chans[chan].fc_fd >= 0)
		(void)close(fdc_chans[chan].fc_fd);
	fdc_chans[chan].fc_fd= -1;
	fdc_chans[chan].fc_use= 0;
	fdc_chans[chan].fc_active= 1;
	p= fdc_chans[chan].fc_path;
	for (n= 0; n < FDC_PATHLEN - 1 && path[n]; n++)
		p[n]= path[n];
	p[n]= '\0';
}

/*
 * The channel is over: give the descriptor back and REMOVE THE FIFO.
 *
 * The name has to survive until here because fdc_fd() reopens it by path
 * whenever the descriptor has been reclaimed, so this is the first moment
 * nothing can want it again.  Removing it is not tidiness: a client that exits
 * rather than closing its socket never runs ichan_close(), so the pair of
 * inodes it made in /tmp was abandoned.  The daemon is the one party that
 * always outlives the channel, so it is the one that can be sure.
 */
void
fdc_forget(chan)
int chan;
{
	if (chan < 0 || chan >= FDC_NCHAN)
		return;
	if (fdc_chans[chan].fc_fd >= 0)
		(void)close(fdc_chans[chan].fc_fd);
	if (fdc_chans[chan].fc_path[0])
		(void)unlink(fdc_chans[chan].fc_path);
	fdc_chans[chan].fc_fd= -1;
	fdc_chans[chan].fc_active= 0;
	fdc_chans[chan].fc_path[0]= '\0';
}

/*
 * A descriptor open on `chan's reply FIFO, or -1 if the channel cannot be
 * answered at all.
 *
 * One eviction is enough to make room, because an eviction frees exactly the
 * one descriptor an open needs; a second failure is the path being gone, not
 * the table being full, and retrying then would close every held descriptor in
 * pursuit of a FIFO that no longer exists.
 *
 * O_RDWR, not O_WRONLY: the client opens both FIFOs before it announces itself
 * and holds them for as long as it lives, so there is a reader either way, but
 * being its own reader is what stops a reply to a client that has just died
 * from raising SIGPIPE in the daemon.
 */
int
fdc_fd(chan)
int chan;
{
	int fd;

	if (chan < 0 || chan >= FDC_NCHAN || !fdc_chans[chan].fc_active)
		return -1;
	if (fdc_chans[chan].fc_fd < 0)
	{
		fd= open(fdc_chans[chan].fc_path, O_RDWR);
		if (fd < 0 && fdc_evict(chan))
			fd= open(fdc_chans[chan].fc_path, O_RDWR);
		if (fd < 0)
			return -1;
		fdc_chans[chan].fc_fd= fd;
	}
	fdc_chans[chan].fc_use= ++fdc_stamp;
	return fdc_chans[chan].fc_fd;
}

/*
 * Is there a descriptor free for the caller's own open?  Reclaims one if the
 * only ones left are held here.
 */
int
fdc_spare()
{
	if (fdc_free() > 0)
		return 1;
	return fdc_evict(-1);
}

/*
 * May another channel be admitted?
 *
 * A channel costs one descriptor that is never given back -- its request FIFO,
 * which has to stay in the select set -- so admitting one must still leave a
 * descriptor for a reply to be written through.  Two available (free, or held
 * here and therefore reclaimable) is that: one for the new request FIFO, one
 * left over.
 *
 * Admitting the last one instead is a deadlock, not a tight fit: every channel
 * would be holding a request descriptor, none would be evictable, and no reply
 * could be written to any of them ever again.
 */
int
fdc_room()
{
	return (fdc_free() + fdc_held()) >= 2;
}

/* Channels this daemon can hold, counting those it already has: what the
 * machine should say about itself at boot. */
int
fdc_ceiling()
{
	int i, active;

	for (i= active= 0; i < FDC_NCHAN; i++)
		if (fdc_chans[i].fc_active)
			active++;
	i= active + fdc_free() + fdc_held() - 1;
	return (i > FDC_NCHAN) ? FDC_NCHAN : i;
}
