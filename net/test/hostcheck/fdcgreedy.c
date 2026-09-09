/*
 * fdcgreedy.c -- the real cache with its reserve removed.
 *
 * coh_fdc.c is compiled alongside this with its own fdc_room() renamed away,
 * and this one put in its place: admit a channel whenever the table has a slot,
 * without keeping a descriptor back for the reply.  Every descriptor then ends
 * up being a request FIFO nobody can answer through -- the client is given a
 * socket and waits for a reply for ever, which is worse than being refused.
 *
 * It exists so that the ceiling test can be shown to fail on a policy that
 * admits MORE than the right number, not just one that admits fewer.  A test
 * that only knows "too few" would rate this as an improvement.
 *
 * Host-only.  Nothing here ships.
 */
#include "coh_fdc.h"

int
fdc_room()
{
	return 1;
}
