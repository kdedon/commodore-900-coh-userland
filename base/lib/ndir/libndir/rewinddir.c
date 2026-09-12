/*
 * Copyright (c) 1977-1995 Robert Swartz.
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
	rewinddir -- rewind a directory stream

	last edit:	25-Apr-1987	D A Gwyn

	This is not simply a call to seekdir(), because seekdir()
	will use the current buffer whenever possible and we need
	rewinddir() to forget about buffered data.
*/


#include	<sys/types.h>
#include	"dirent.h"

#ifdef COHERENT
#include	<errno.h>
/* off_t comes from <sys/types.h> on this system. */

#else
#include	<sys/errno.h>
#endif

extern int	errno;

long	lseek();

#ifndef NULL
#define	NULL	0
#endif

#ifndef SEEK_SET
#define	SEEK_SET	0
#endif

void
rewinddir( dirp )
	register DIR		*dirp;	/* stream from opendir() */
	{
	if ( dirp == NULL || dirp->dd_buf == NULL )
		{
		errno = EFAULT;
		return;			/* invalid pointer */
		}

	dirp->dd_loc = dirp->dd_size = 0;	/* invalidate buffer */
	(void)lseek( dirp->dd_fd, (off_t)0, SEEK_SET );	/* may set errno */
	}
