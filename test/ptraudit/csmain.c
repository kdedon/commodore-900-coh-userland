/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * Callee-saved registers across the assembly string routines.  Each cl*()
 * wrapper (calleesave.s) plants sentinels in R6..R12, calls one routine, and
 * returns the number of the first register that came back changed.
 */
#include <stdio.h>
#include <string.h>

static	char	src[32];
static	char	dst[32];

main()
{
	strcpy(src, "hello world");
	printf("strlen %d (len %d)\n", clstrlen(src), strlen(src));
	printf("index  %d\n", clindex(src, 'w'));
	printf("strcpy %d\n", clstrcpy(dst, src));
	printf("strcmp %d (cmp %d)\n", clstrcmp(dst, src), strcmp(dst, src));
	return (0);
}
