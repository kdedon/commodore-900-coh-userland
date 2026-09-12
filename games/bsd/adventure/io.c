/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*	$NetBSD: io.c,v 1.15 2003/09/19 10:01:53 itojun Exp $	*/

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * The game adventure was originally written in Fortran by Will Crowther
 * and Don Woods.  It was later translated to C and enhanced by Jim
 * Gillogly.  This code is derived from software contributed to Berkeley
 * by Jim Gillogly at The Rand Corporation.
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


/*      Re-coding of advent in C: file i/o and user i/o                 */

#include <stdio.h>

/* K&R: pointer/long-returning libc routines (their ANSI headers are
 * stripped by the port; undeclared they default to int, truncating far
 * pointers and sign-extending longs). */
extern char *malloc(), *calloc(), *realloc(), *getenv();
extern long atol(), time();
extern unsigned long strtoul();

#include "hdr.h"
#include "extern.h"


void
getin(wrd1, wrd2)		/* get command from user        */
	char  **wrd1, **wrd2;	/* no prompt, usually           */
{
	char   *s;
	static char wd1buf[MAXSTR], wd2buf[MAXSTR];
	int     first, numch;

	*wrd1 = wd1buf;				/* return ptr to internal str */
	*wrd2 = wd2buf;
	wd2buf[0] = 0;				/* in case it isn't set here */
	for (s = wd1buf, first = 1, numch = 0;;) {
		if ((*s = getchar()) >= 'A' && *s <= 'Z')
			*s = *s - ('A' - 'a');
		/* convert to upper case */
		switch (*s) {			/* start reading from user */
		case '\n':
			*s = 0;
			return;
		case ' ':
			if (s == wd1buf || s == wd2buf)	/* initial blank */
				continue;
			*s = 0;
			if (first) {		/* finished 1st wd; start 2nd */
				first = numch = 0;
				s = wd2buf;
				break;
			} else {		/* finished 2nd word */
				FLUSHLINE;
				*s = 0;
				return;
			}
		case EOF:
			printf("user closed input stream, quitting...\n");
			exit(0);
		default:
			if (++numch >= MAXSTR) {	/* string too long */
				printf("Give me a break!!\n");
				wd1buf[0] = wd2buf[0] = 0;
				FLUSHLINE;
				return;
			}
			s++;
		}
	}
}

int
yes(x, y, z)			/* confirm with rspeak          */
	int     x, y, z;
{
	int     result = TRUE;	/* pacify gcc */
	int    ch;
	for (;;) {
		rspeak(x);	/* tell him what we want */
		if ((ch = getchar()) == 'y')
			result = TRUE;
		else if (ch == 'n')
			result = FALSE;
		else if (ch == EOF) {
			printf("user closed input stream, quitting...\n");
			exit(0);
		}
		FLUSHLINE;
		if (ch == 'y' || ch == 'n')
			break;
		printf("Please answer the question.\n");
	}
	if (result == TRUE)
		rspeak(y);
	if (result == FALSE)
		rspeak(z);
	return (result);
}

int
yesm(x, y, z)			/* confirm with mspeak          */
	int     x, y, z;
{
	int     result = TRUE;	/* pacify gcc */
	int    ch;
	for (;;) {
		mspeak(x);	/* tell him what we want */
		if ((ch = getchar()) == 'y')
			result = TRUE;
		else if (ch == 'n')
			result = FALSE;
		else if (ch == EOF) {
			printf("user closed input stream, quitting...\n");
			exit(0);
		}
		FLUSHLINE;
		if (ch == 'y' || ch == 'n')
			break;
		printf("Please answer the question.\n");
	}
	if (result == TRUE)
		mspeak(y);
	if (result == FALSE)
		mspeak(z);
	return (result);
}
/* FILE *inbuf,*outbuf; */

/*
 * The startup database scan (rdata/rdesc/rtrav/rvoc/rlocs/rdflt/rliq/
 * rhints and the next()/rnum() readers) runs on the HOST at build time
 * (games/bsd/adventure -- the dumper); the game loads its results from
 * the generated tables.c and reads message text from the file the
 * offsets index (init.c loadtables).  Only the iotape decryption the
 * messages still carry survives here.
 */
char    iotape[] = "Ax3F'\003tt$8h\315qer*h\017nGKrX\207:!l";
char   *tape = iotape;		/* pointer to encryption tape   */

/*
 * getmsg() -- fetch one message's bytes from the message file into a
 * malloc'd buffer (caller frees).  The PDP-11 original kept the
 * database on disk exactly like this; NetBSD virtualized it into a
 * 56 KB in-memory blob, four times what a shared Z8001 text+data
 * budget can spare.
 */
char *
getmsg(msg)
	struct text *msg;
{
	register char *buf;
	register int n;
	char *malloc();
	long lseek();

	if (msg->seekadr < 0 || msg->txtlen <= 0)
		return (0);
	if ((buf = malloc(msg->txtlen + 1)) == 0) {
		printf("adventure: out of memory\n");
		exit(1);
	}
	lseek(datfd, msg->seekadr, 0);
	n = read(datfd, buf, msg->txtlen + 1);
	if (n < msg->txtlen) {
		printf("adventure: message file read error\n");
		exit(1);
	}
	return (buf);
}

void
rspeak(msg)
	int     msg;
{
	if (msg != 0)
		speak(&rtext[msg]);
}


void
mspeak(msg)
	int     msg;
{
	if (msg != 0)
		speak(&mtext[msg]);
}


void
speak(msg)			/* read, decrypt, and print a message (not
				 * ptext)      */
	struct text *msg;	/* msg is a pointer to seek address and length
				 * of mess */
{
	char   *s, *b, nonfirst;
	if ((b = getmsg(msg)) == 0)
		return;
	s = b;
	nonfirst = 0;
	while (s - b < msg->txtlen) {	/* read a line at a time */
		tape = iotape;	/* restart decryption tape      */
		while ((*s++ ^ *tape++) != TAB);	/* read past loc num       */
		/* assume tape is longer than location number           */
		/* plus the lookahead put together                    */
		if ((*s ^ *tape) == '>' &&
		    (*(s + 1) ^ *(tape + 1)) == '$' &&
		    (*(s + 2) ^ *(tape + 2)) == '<')
			break;
		if (blklin && !nonfirst++)
			putchar('\n');
		do {
			if (*tape == 0)
				tape = iotape;	/* rewind decryp tape */
			putchar(*s ^ *tape);
		} while ((*s++ ^ *tape++) != LF);	/* better end with LF   */
	}
	free(b);
}


void
pspeak(m, skip)			/* read, decrypt an print a ptext message              */
	int     m;		/* msg is the number of all the p msgs for
				 * this place  */
	int     skip;		/* assumes object 1 doesn't have prop 1, obj 2
				 * no prop 2 &c */
{
	char   *s, nonfirst;
	char   *numst, save;
	struct text *msg;
	char   *tbuf;

	msg = &ptext[m];
	if ((tbuf = getmsg(msg)) == 0)
		return;
	s = tbuf;

	nonfirst = 0;
	while (s - tbuf < msg->txtlen) {	/* read line at a time */
		tape = iotape;	/* restart decryption tape      */
		for (numst = s; (*s ^= *tape++) != TAB; s++);	/* get number  */

		save = *s;	/* Temporarily trash the string (cringe) */
		*s++ = 0;	/* decrypting number within the string          */

		if (atoi(numst) != 100 * skip && skip >= 0) {
			while ((*s++ ^ *tape++) != LF)	/* flush the line    */
				if (*tape == 0)
					tape = iotape;
			continue;
		}
		if ((*s ^ *tape) == '>' && (*(s + 1) ^ *(tape + 1)) == '$' &&
		    (*(s + 2) ^ *(tape + 2)) == '<')
			break;
		if (blklin && !nonfirst++)
			putchar('\n');
		do {
			if (*tape == 0)
				tape = iotape;
			putchar(*s ^ *tape);
		} while ((*s++ ^ *tape++) != LF);	/* better end with LF   */
		if (skip < 0)
			break;
	}
	free(tbuf);
}
