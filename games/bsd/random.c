/*	$NetBSD: random.c,v 1.9 2004/01/27 20:30:30 jsm Exp $	*/

/*
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Guy Harris at Network Appliance Corp.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */



#include <sys/time.h>

#include <errno.h>
#include <stdio.h>

/* K&R: pointer/long-returning libc routines (their ANSI headers are
 * stripped by the port; undeclared they default to int, truncating far
 * pointers and sign-extending longs). */
extern char *malloc(), *calloc(), *realloc(), *getenv();
extern long atol(), time();
extern unsigned long strtoul();


extern char *optarg;
extern int optind, opterr;

#define MAXRANDOM	2147483647

int  main();
void usage();

int
main(argc, argv)
	int argc;
	char *argv[];
{
	double denom;
	int ch, random_exit, selected, unbuffer_output;
	char *ep;

	/* Revoke setgid privileges */

	denom = 0;
	random_exit = unbuffer_output = 0;
	while ((ch = getopt(argc, argv, "er")) != -1)
		switch (ch) {
		case 'e':
			random_exit = 1;
			break;
		case 'r':
			unbuffer_output = 1;
			break;
		default:
		case '?':
			usage();
			/* NOTREACHED */
		}

	argc -= optind;
	argv += optind;

	switch (argc) {
	case 0:
		denom = 2;
		break;
	case 1:
		errno = 0;
		denom = (double)atol(*argv);
		ep = *argv;
		while (*ep >= '0' && *ep <= '9')
			ep++;
		if (errno == ERANGE)
			{ fprintf(stderr, "%s\n", *argv); exit(1); }
		if (denom == 0 || *ep != '\0')
			{ fprintf(stderr, "denominator is not valid.\n"); exit(1); }
		break;
	default:
		usage(); 
		/* NOTREACHED */
	}

	srand((unsigned)(time((long *)0) + getpid()));

	/* Compute a random exit status between 0 and denom - 1. */
	if (random_exit)
		return ((denom * rand()) / MAXRANDOM);

	/*
	 * Act as a filter, randomly choosing lines of the standard input
	 * to write to the standard output.
	 */
	if (unbuffer_output)
		setbuf(stdout, NULL);
	
	/*
	 * Select whether to print the first line.  (Prime the pump.)
	 * We find a random number between 0 and denom - 1 and, if it's
	 * 0 (which has a 1 / denom chance of being true), we select the
	 * line.
	 */
	selected = (int)(denom * rand() / MAXRANDOM) == 0;
	while ((ch = getchar()) != EOF) {
		if (selected)
			(void)putchar(ch);
		if (ch == '\n') {
			/* End of that line.  See if we got an error. */
			if (ferror(stdout))
				{ fprintf(stderr, "random: stdout\n"); exit(2); }

			/* Now see if the next line is to be printed. */
			selected = (int)(denom * rand() / MAXRANDOM) == 0;
		}
	}
	if (ferror(stdin))
		{ fprintf(stderr, "random: stdin\n"); exit(2); }
	exit (0);
}

void
usage()
{

	(void)fprintf(stderr, "usage: random [-er] [denominator]\n");
	exit(1);
}
