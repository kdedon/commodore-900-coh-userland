/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*
 *  Top users/processes display for Unix
 *  Version 3
 *
 *  This file is the COHERENT/Z8001 member of top's per-system glue header.
 *  The distribution's own os.h selects between a BSD and a System V spelling
 *  of the string and memory routines; this system needs neither branch, so it
 *  states what it has.
 */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#define memzero(a, b)		memset((a), 0, (b))

/*
 * The opaque handle get_process_info() hands back.  <sys/types.h> here has
 * no caddr_t.
 */
typedef char *caddr_t;

/*
 * setbuf() here takes the buffer and nothing else: the stdio buffer length is
 * the compiled-in BUFSIZ, not a caller's choice.
 */
#define setbuffer(f, b, s)	setbuf((f), (b))

/*
 * Signal handlers return int here, as they do everywhere else in this
 * userland.
 */
typedef int sigret_t;

char *getenv();
char *malloc();
char *realloc();
