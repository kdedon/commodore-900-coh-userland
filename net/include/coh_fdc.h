/*
 * coh_fdc.h -- the inet daemon's reply-descriptor cache.
 *
 * A channel is two FIFOs.  The daemon must watch the request FIFO all the time,
 * because that is where the next request appears and select() needs a
 * descriptor for it, but it needs the reply FIFO only for as long as a write to
 * it takes.  Holding both costs two of the daemon's twenty descriptors per
 * channel and puts the machine's whole connection count at eight; holding the
 * request FIFO and reaching the reply FIFO through this cache costs one, and
 * doubles it.
 *
 * The cache does hold reply descriptors while there are spare ones -- a held
 * descriptor is what makes a reply a single write() rather than open/write/
 * close -- but every one of them is reclaimable, so a descriptor is never
 * unavailable for a new channel merely because an idle channel is sitting on
 * it.  Least recently used goes first, so the channels carrying traffic keep
 * theirs and the idle ones pay the open.
 *
 * The limit is measured, not assumed: fdc_room() asks the kernel how many
 * descriptors this process can still get (dup until it refuses).  Nothing here
 * knows NOFILE, so a daemon started with fewer descriptors, or one that has
 * opened /dev/eth ports, gets the right answer rather than a compiled-in one.
 */
#ifndef COH_FDC_H
#define COH_FDC_H

/* Channels the daemon can describe.  The descriptor budget is the smaller
 * limit on every machine this runs on (twenty descriptors give fifteen), so
 * this is the size of a table, not a policy. */
#define FDC_NCHAN	16

/* A reply FIFO path, sr_hello_t's sh_repl verbatim. */
#define FDC_PATHLEN	40

void fdc_init();		/* (reffd) -- forget everything		*/
void fdc_hold();		/* (chan, path) -- chan's reply FIFO	*/
void fdc_forget();		/* (chan) -- channel gone		*/
int fdc_fd();			/* (chan) -> writable fd, or -1		*/
int fdc_room();			/* -> can another channel be admitted?	*/
int fdc_spare();		/* -> is a descriptor free for the caller? */
int fdc_ceiling();		/* -> channels this daemon can hold	*/

#endif /* COH_FDC_H */
