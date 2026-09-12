/*
 * Copyright (c) 1977-1995 Robert Swartz.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
	closedir -- close a directory stream

	last edit:	25-Apr-1987	D A Gwyn
*/

#ifdef COHERENT
#include	<errno.h>
#else
#include	<sys/errno.h>
#endif

#include	<sys/types.h>
#include	"dirent.h"

typedef char	*pointer;		/* (void *) if you have it */

extern void	free();
extern int	close();

extern int	errno;

#ifndef NULL
#define	NULL	0
#endif

int
closedir( dirp )
	register DIR	*dirp;		/* stream from opendir() */
	{
	if ( dirp == NULL || dirp->dd_buf == NULL )
		{
		errno = EFAULT;
		return -1;		/* invalid pointer */
		}

	free( (pointer)dirp->dd_buf );
	free( (pointer)dirp );
	return close( dirp->dd_fd );
	}
