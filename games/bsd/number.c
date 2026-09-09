/*	$NetBSD: number.c,v 1.10 2004/11/05 21:30:32 dsl Exp $	*/

/*
 * Copyright (c) 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
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
#include <stdio.h>
#include <string.h>

/* K&R: pointer/long-returning libc routines (their ANSI headers are
 * stripped by the port; undeclared they default to int, truncating far
 * pointers and sign-extending longs). */
extern char *malloc(), *calloc(), *realloc(), *getenv();
extern long atol(), time();
extern unsigned long strtoul();


extern char *optarg;
extern int optind, opterr;

#define	MAXNUM		65		/* Biggest number we handle. */

static char	*name1[] = {
	"",		"one",		"two",		"three",
	"four",		"five",		"six",		"seven",
	"eight",	"nine",		"ten",		"eleven",
	"twelve",	"thirteen",	"fourteen",	"fifteen",
	"sixteen",	"seventeen",	"eighteen",	"nineteen",
},
		*name2[] = {
	"",		"ten",		"twenty",	"thirty",
	"forty",	"fifty",	"sixty",	"seventy",
	"eighty",	"ninety",
},
		*name3[] = {
	"hundred",	"thousand",	"million",	"billion",
	"trillion",	"quadrillion",	"quintillion",	"sextillion",
	"septillion",	"octillion",	"nonillion",	"decillion",
	"undecillion",	"duodecillion",	"tredecillion",	"quattuordecillion",
	"quindecillion",		"sexdecillion",	
	"septendecillion",		"octodecillion",
	"novemdecillion",		"vigintillion",
};

void	convert();
int	main();
int	number();
void	pfract();
int	unit();
void	usage();

int lflag;

int
main(argc, argv)
	int argc;
	char *argv[];
{
	int ch, first;
	char line[256];

	/* Revoke setgid privileges */

	lflag = 0;
	while ((ch = getopt(argc, argv, "l")) != -1)
		switch (ch) {
		case 'l':
			lflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (*argv == NULL)
		for (first = 1;
		    fgets(line, sizeof(line), stdin) != NULL; first = 0) {
			if (index(line, '\n') == NULL)
				{ fprintf(stderr, "line too long.\n"); exit(1); }
			if (!first)
				(void)printf("...\n");
			convert(line);
		}
	else
		for (first = 1; *argv != NULL; first = 0, ++argv) {
			if (!first)
				(void)printf("...\n");
			convert(*argv);
		}
	exit(0);
}

void
convert(line)
	char *line;
{
	int flen, len, rval, singular;
	char *p, *fraction;

	flen = 0;
	fraction = NULL;
	for (p = line; *p != '\0' && *p != '\n'; ++p) {
		if (*p == ' ' || *p == '\t') {
			if (p == line) {
				++line;
				continue;
			}
			goto badnum;
		}
		if (isdigit((unsigned char)*p))
			continue;
		switch (*p) {
		case '.':
			if (fraction != NULL)
				goto badnum;
			fraction = p + 1;
			*p = '\0';
			break;
		case '-':
			if (p == line)
				break;
			/* FALLTHROUGH */
		default:
badnum:			{ fprintf(stderr, "illegal number: %s\n", line); exit(1); }
			break;
		}
	}
	*p = '\0';

	if ((len = strlen(line)) > MAXNUM ||
	    (fraction != NULL && (flen = strlen(fraction)) > MAXNUM))
		{ fprintf(stderr, "number too large, max %d digits.\n", MAXNUM); exit(1); }

	if (*line == '-') {
		(void)printf("minus%s", lflag ? " " : "\n");
		++line;
		--len;
	}

	rval = len > 0 ? unit(len, line, &singular) : 0;
	if (fraction != NULL && flen != 0)
		for (p = fraction; *p != '\0'; ++p)
			if (*p != '0') {
				if (rval)
					(void)printf("%sand%s",
					    lflag ? " " : "",
					    lflag ? " " : "\n");
				if (unit(flen, fraction, &singular)) {
					if (lflag)
						(void)printf(" ");
					pfract(flen, singular);
					rval = 1;
				}
				break;
			}
	if (!rval)
		(void)printf("zero%s", lflag ? "" : ".\n");
	if (lflag)
		(void)printf("\n");
}

int
unit(len, p, singular)
	int len;
	char *p;
	int *singular;
{
	int off, rval;

	rval = 0;
	if (len > 3) {
		if (len % 3) {
			off = len % 3;
			len -= off;
			if (number(p, off, singular)) {
				rval = 1;
				(void)printf(" %s%s",
				    name3[len / 3], lflag ? " " : ".\n");
			}
			p += off;
		}
		for (; len > 3; p += 3) {
			len -= 3;
			if (number(p, 3, singular)) {
				rval = 1;
				(void)printf(" %s%s",
				    name3[len / 3], lflag ? " " : ".\n");
			}
		}
	}
	if (number(p, len, singular)) {
		if (rval)
			*singular = 0;
		if (!lflag)
			(void)printf(".\n");
		rval = 1;
	}
	return (rval);
}

int
number(p, len, singular)
	char *p;
	int len;
	int *singular;
{
	int val, rval;

	rval = 0;
	*singular = 1;
	switch (len) {
	case 3:
		if (*p != '0') {
			rval = 1;
			*singular = 0;
			(void)printf("%s hundred", name1[*p - '0']);
		}
		++p;
		/* FALLTHROUGH */
	case 2:
		val = (p[1] - '0') + (p[0] - '0') * 10;
		if (val) {
			if (rval)
				(void)printf(" ");
			if (val < 20)
				(void)printf("%s", name1[val]);
			else {
				(void)printf("%s", name2[val / 10]);
				if (val % 10)
					(void)printf("-%s", name1[val % 10]);
			}
			rval = 1;
		}
		if (val != 1)
			*singular = 0;
		break;
	case 1:
		if (*p != '0') {
			rval = 1;
			(void)printf("%s", name1[*p - '0']);
		}
		if (*p != '1')
			*singular = 0;
	}
	return (rval);
}

void
pfract(len, singular)
	int len;
	int singular;
{
	static char *pref[] = { "", "ten-", "hundred-" };

	switch(len) {
	case 1:
		(void)printf("tenth");
		break;
	case 2:
		(void)printf("hundredth");
		break;
	default:
		(void)printf("%s%sth", pref[len % 3], name3[len / 3]);
		break;
	}
	if (!singular) {
		printf("s");
	}
	printf(".\n");
}

void
usage()
{
	(void)fprintf(stderr, "usage: number [# ...]\n");
	exit(1);
}
