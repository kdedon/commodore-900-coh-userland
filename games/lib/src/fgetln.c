/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/* fgetln() -- K&R emulation over fgets with a static buffer; the returned
 * line INCLUDES the newline and is valid until the next call (BSD contract;
 * quiz's lines fit LSIZE). */
#include <stdio.h>

#define	FGL_MAX	512

char *
fgetln(fp, lenp)
FILE *fp;
unsigned *lenp;
{
	static char buf[FGL_MAX];
	register int n;
	char *fgets();

	if (fgets(buf, FGL_MAX, fp) == NULL)
		return ((char *)0);
	for (n = 0; buf[n] != '\0'; n++)
		;
	*lenp = n;
	return (buf);
}
