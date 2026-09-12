/*
 * Reply-descriptor cache for the inet daemon.
 *
 * Keep request FIFOs open for select().  Cache reply descriptors while
 * there is room, evicting the least recently used when another is needed.
 * Measure available descriptors with dup() when admitting a channel.
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
