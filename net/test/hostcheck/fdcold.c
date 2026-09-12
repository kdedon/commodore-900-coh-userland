/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * fdcold.c -- the descriptor policy the daemon had before coh_fdc.c: hold the
 * reply FIFO open from the moment the channel is accepted until it goes away.
 *
 * This is the defect the ceiling test exists to catch, written out as a policy
 * with the same interface, so that the test can be shown to FAIL against it.
 * It is not a broken version of the cache -- it is what sr_accept() used to do,
 * two descriptors a channel, and the number it produces (eight) is the number
 * the machine really had.
 *
 * Host-only.  Nothing here ships.
 */
#include "coh_fdc.h"

#include <fcntl.h>

static int old_fd[FDC_NCHAN];
static char old_path[FDC_NCHAN][FDC_PATHLEN];
static int old_ref = 1;

static int
old_free()
{
	int probe[FDC_NCHAN + 8];
	int n, fd, got;

	for (n= 0; n < (int)(sizeof(probe)/sizeof(probe[0])); n++)
	{
		if ((fd= dup(old_ref)) < 0)
			break;
		probe[n]= fd;
	}
	got= n;
	while (n > 0)
		(void)close(probe[--n]);
	return got;
}

void
fdc_init(reffd)
int reffd;
{
	int i;

	for (i= 0; i < FDC_NCHAN; i++)
	{
		old_fd[i]= -1;
		old_path[i][0]= '\0';
	}
	old_ref= (reffd >= 0) ? reffd : 1;
}

/* The old accept: the reply FIFO is opened here and kept. */
void
fdc_hold(chan, path)
int chan;
char *path;
{
	int n;

	for (n= 0; n < FDC_PATHLEN - 1 && path[n]; n++)
		old_path[chan][n]= path[n];
	old_path[chan][n]= '\0';
	old_fd[chan]= open(old_path[chan], O_RDWR);
}

void
fdc_forget(chan)
int chan;
{
	if (old_fd[chan] >= 0)
		(void)close(old_fd[chan]);
	old_fd[chan]= -1;
}

int
fdc_fd(chan)
int chan;
{
	return old_fd[chan];
}

/* Two descriptors a channel: one for the request FIFO, one for the reply. */
int
fdc_room()
{
	return old_free() >= 2;
}

int
fdc_spare()
{
	return old_free() > 0;
}

int
fdc_ceiling()
{
	return old_free() / 2;
}
