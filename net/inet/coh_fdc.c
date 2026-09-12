/*
 * Cache reply FIFO descriptors for the inet daemon.
 * See coh_fdc.h for the interface.  The host checks compile this file
 * without the stack headers.
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
 * Count free descriptors by dup(), then close the probes.
 * Measure only when admitting a channel; inherited and device descriptors
 * make a fixed NOFILE calculation unreliable.
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

/*
 * Count cached descriptors available for reclamation.
 */
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

/*
 * Initialize the cache and the descriptor used for free-slot probes.
 */
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

/*
 * Register a channel and its reply FIFO, releasing any cached descriptor.
 */
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
 * Close the cached descriptor and unlink the channel's reply FIFO.
 * The daemon owns cleanup even when the client exits without closing.
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
 * Return a reply descriptor, or -1.  Evict at most one cached descriptor
 * to make room.  O_RDWR prevents SIGPIPE if the client has exited.
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
