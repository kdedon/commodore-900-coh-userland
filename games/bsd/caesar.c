/*	$NetBSD: caesar.c,v 1.14 2004/01/27 20:30:29 jsm Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Adams.
 *
 * Authors:
 *	Stan King, John Eldridge, based on algorithm suggested by
 *	Bob Morris
 * 29-Sep-82
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



#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>

/* K&R: pointer/long-returning libc routines (their ANSI headers are
 * stripped by the port; undeclared they default to int, truncating far
 * pointers and sign-extending longs). */
extern char *malloc(), *calloc(), *realloc(), *getenv();
extern long atol(), time();
extern unsigned long strtoul();


#define	LINELENGTH	2048
#define	ROTATE(ch, perm) \
	isupper(ch) ? ('A' + (ch - 'A' + perm) % 26) : \
	    islower(ch) ? ('a' + (ch - 'a' + perm) % 26) : ch

/*
 * letter frequencies (taken from some unix(tm) documentation)
 * (unix is a trademark of Bell Laboratories)
 */
/* ln(freq%) + ln(26/100), PRECOMPUTED -- the upstream computed this at
 * startup with libm log(); folding it here removes the libm dependency. */
double stdf[26] = {
	0.728611, -1.046969, -0.063366, 0.217367, 1.168201,
	-0.648939, -0.968637, 0.154779, 0.507661, -4.565949,
	-2.214574, -0.009444, -0.357532, 0.431263, 0.593106,
	-0.278921, -3.872802, 0.544531, 0.824263, 0.922988,
	-0.383899, -1.557795, -0.715802, -2.816750, -0.619525,
	-4.160484
};


int	main();
void	printit();

int
main(argc, argv)
	int argc;
	char **argv;
{
	int ch, i, nread;
	double dot, winnerdot;
	char *inbuf;
	int obs[26], try, winner;

	/* revoke setgid privileges */

	winnerdot = 0;
	if (argc > 1)
		printit(argv[1]);

	if (!(inbuf = malloc(LINELENGTH)))
		{ fprintf(stderr, "caesar: out of memory\n"); exit(1); }

	/* zero out observation table */
	memset(obs, 0, 26 * sizeof(int));

	if ((nread = read(0, inbuf, LINELENGTH)) < 0)
		{ fprintf(stderr, "reading from stdin\n"); exit(1); }
	for (i = nread; i--;) {
		ch = inbuf[i];
		if (islower(ch))
			++obs[ch - 'a'];
		else if (isupper(ch))
			++obs[ch - 'A'];
	}

	/*
	 * now "dot" the freqs with the observed letter freqs
	 * and keep track of best fit
	 */
	for (try = winner = 0; try < 26; ++try) { /* += 13) { */
		dot = 0;
		for (i = 0; i < 26; i++)
			dot += obs[i] * stdf[(i + try) % 26];
		/* initialize winning score */
		if (try == 0)
			winnerdot = dot;
		if (dot > winnerdot) {
			/* got a new winner! */
			winner = try;
			winnerdot = dot;
		}
	}

	for (;;) {
		for (i = 0; i < nread; ++i) {
			ch = inbuf[i];
			putchar(ROTATE(ch, winner));
		}
		if (nread < LINELENGTH)
			break;
		if ((nread = read(0, inbuf, LINELENGTH)) < 0)
			{ fprintf(stderr, "reading from stdin\n"); exit(1); }
	}
	exit(0);
}

void
printit(arg)
	char *arg;
{
	int ch, rot;

	if ((rot = atoi(arg)) < 0)
		{ fprintf(stderr, "bad rotation value.\n"); exit(1); }
	while ((ch = getchar()) != EOF)
		putchar(ROTATE(ch, rot));
	exit(0);
}
