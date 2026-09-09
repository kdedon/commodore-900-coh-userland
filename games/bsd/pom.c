/*	$NetBSD: pom.c,v 1.14 2004/01/27 20:30:30 jsm Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software posted to USENET.
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



/*
 * Phase of the Moon.  Calculates the current phase of the moon.
 * Based on routines from `Practical Astronomy with Your Calculator',
 * by Duffett-Smith.  Comments give the section from the book that
 * particular piece of code was adapted from.
 *
 * -- Keith E. Brandt  VIII 1984
 *
 * Updated to the Third Edition of Duffett-Smith's book, Paul Janzen, IX 1998
 *
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* K&R: routines returning a pointer or a long must be declared, or they
 * default to int -- 16 bits here, while a pointer is a 32-bit far pointer, so
 * the segment is lost. */
extern char *malloc(), *calloc(), *realloc(), *getenv();
extern char *fgets(), *memset(), *memcpy(), *ctime(), *asctime();
extern char *getlogin(), *getprogname(), *strerror(), *mktemp();
extern long atol(), time(), lseek(), strtol();
extern unsigned long strtoul();
extern char *optarg;
extern int optind, opterr;

#ifndef PI
#define	PI	  3.14159265358979323846
#endif

/*
 * The EPOCH in the third edition of the book is 1990 Jan 0.0 TDT.
 * In this program, we do not bother to correct for the differences
 * between UTC (as shown by the UNIX clock) and TDT.  (TDT = TAI + 32.184s;
 * TAI-UTC = 32s in Jan 1999.)
 */
/* 7304 days, and 7304 * 86400 is 631065600: the scaling must be long, or the
 * constant expression is computed in a 16-bit int and wraps. */
#define EPOCH_MINUS_1970	(20L * 365 + 5 - 1) /* 20 years, 5 leaps, back 1 day to Jan 0 */
#define	EPSILONg  279.403303	/* solar ecliptic long at EPOCH */
#define	RHOg	  282.768422	/* solar ecliptic long of perigee at EPOCH */
#define	ECCEN	  0.016713	/* solar orbit eccentricity */
#define	lzero	  318.351648	/* lunar mean long at EPOCH */
#define	Pzero	  36.340410	/* lunar mean long of perigee at EPOCH */
#define	Nzero	  318.510107	/* lunar mean long of node at EPOCH */

void	adj360();
double	dtor();
int	main();
double	potm();
time_t	parsetime();
void	fmttime();
long	tmtotime();
void	badformat();

int
main(argc, argv)
	int argc;
	char *argv[];
{
	time_t tmpt, now;
	double days, today, tomorrow;
	char buf[1024];

	/* Revoke setgid privileges */
	/* setgid privileges: not used here */

	if (time(&now) == (time_t)-1L)
		err(1, "time");
	if (argc > 1) {
		tmpt = parsetime(argv[1]);
		fmttime(buf, localtime(&tmpt));
		printf("%s:  ", buf);
	} else {
		tmpt = now;
	}
	days = (tmpt - EPOCH_MINUS_1970 * 86400L) / 86400.0;
	today = potm(days) + .5;
	if (tmpt < now)
		(void)printf("The Moon was ");
	else if (tmpt == now)
		(void)printf("The Moon is ");
	else
		(void)printf("The Moon will be ");
	if ((int)today == 100)
		(void)printf("Full\n");
	else if (!(int)today)
		(void)printf("New\n");
	else {
		tomorrow = potm(days + 1);
		if ((int)today == 50)
			(void)printf("%s\n", tomorrow > today ?
			    "at the First Quarter" : "at the Last Quarter");
			/* today is 0.5 too big, but it doesn't matter here
			 * since the phase is changing fast enough
			 */
		else {
			today -= 0.5;		/* Now it might matter */
			(void)printf("%s ", tomorrow > today ?
			    "Waxing" : "Waning");
			if (today > 50)
				(void)printf("Gibbous (%1.0f%% of Full)\n",
				    today);
			else if (today < 50)
				(void)printf("Crescent (%1.0f%% of Full)\n",
				    today);
		}
	}
	exit(0);
}

/*
 * potm --
 *	return phase of the moon
 */
double
potm(days)
	double days;
{
	double N, Msol, Ec, LambdaSol, l, Mm, Ev, Ac, A3, Mmprime;
	double A4, lprime, V, ldprime, D, Nm;

	N = 360 * days / 365.242191;				/* sec 46 #3 */
	adj360(&N);
	Msol = N + EPSILONg - RHOg;				/* sec 46 #4 */
	adj360(&Msol);
	Ec = 360 / PI * ECCEN * sin(dtor(Msol));		/* sec 46 #5 */
	LambdaSol = N + Ec + EPSILONg;				/* sec 46 #6 */
	adj360(&LambdaSol);
	l = 13.1763966 * days + lzero;				/* sec 65 #4 */
	adj360(&l);
	Mm = l - (0.1114041 * days) - Pzero;			/* sec 65 #5 */
	adj360(&Mm);
	Nm = Nzero - (0.0529539 * days);			/* sec 65 #6 */
	adj360(&Nm);
	Ev = 1.2739 * sin(dtor(2*(l - LambdaSol) - Mm));	/* sec 65 #7 */
	Ac = 0.1858 * sin(dtor(Msol));				/* sec 65 #8 */
	A3 = 0.37 * sin(dtor(Msol));
	Mmprime = Mm + Ev - Ac - A3;				/* sec 65 #9 */
	Ec = 6.2886 * sin(dtor(Mmprime));			/* sec 65 #10 */
	A4 = 0.214 * sin(dtor(2 * Mmprime));			/* sec 65 #11 */
	lprime = l + Ev + Ec - Ac + A4;				/* sec 65 #12 */
	V = 0.6583 * sin(dtor(2 * (lprime - LambdaSol)));	/* sec 65 #13 */
	ldprime = lprime + V;					/* sec 65 #14 */
	D = ldprime - LambdaSol;				/* sec 67 #2 */
	return(50.0 * (1 - cos(dtor(D))));			/* sec 67 #3 */
}

/*
 * dtor --
 *	convert degrees to radians
 */
double
dtor(deg)
	double deg;
{
	return(deg * PI / 180);
}

/*
 * adj360 --
 *	adjust value so 0 <= deg <= 360
 */
void
adj360(deg)
	double *deg;
{
	for (;;)
		if (*deg < 0)
			*deg += 360;
		else if (*deg > 360)
			*deg -= 360;
		else
			break;
}

/*
 * fmttime and tmtotime stand in for strftime(3) and mktime(3), which this libc
 * does not have.  Only pom's two uses are covered: one fixed date format, and
 * turning a struct tm the user just edited back into a time_t (which is also
 * what fills in tm_yday, the field potm() ultimately needs).  Everything is UTC
 * -- localtime() here is gmtime() with the TZ offset applied, and pom's own
 * arithmetic is epoch-relative anyway.
 */
static char *wdayname[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static char *monname[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

void
fmttime(buf, tm)
	char *buf;
	struct tm *tm;
{
	sprintf(buf, "%s %d %s %2d %02d:%02d:%02d (UTC)",
	    wdayname[tm->tm_wday % 7], tm->tm_year + 1900,
	    monname[tm->tm_mon % 12], tm->tm_mday,
	    tm->tm_hour, tm->tm_min, tm->tm_sec);
}

/* Days in each month, non-leap. */
static int mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

#define	LEAP(y)	(((y) % 4 == 0 && (y) % 100 != 0) || (y) % 400 == 0)

long
tmtotime(tm)
	struct tm *tm;
{
	long days;
	int y, m;

	if (tm->tm_year < 70 || tm->tm_mon < 0 || tm->tm_mon > 11)
		return (-1L);
	days = 0;
	for (y = 1970; y < tm->tm_year + 1900; y++)
		days += LEAP(y) ? 366 : 365;
	for (m = 0; m < tm->tm_mon; m++)
		days += mdays[m] + ((m == 1 && LEAP(tm->tm_year + 1900)) ? 1 : 0);
	tm->tm_yday = (int) (days - 0);		/* set below, after the year part */
	days += tm->tm_mday - 1;
	/* tm_yday is the day within THIS year: recompute it from the month. */
	tm->tm_yday = tm->tm_mday - 1;
	for (m = 0; m < tm->tm_mon; m++)
		tm->tm_yday += mdays[m] +
		    ((m == 1 && LEAP(tm->tm_year + 1900)) ? 1 : 0);
	/* 1 Jan 1970 was a Thursday. */
	tm->tm_wday = (int) ((days + 4) % 7);
	return (((days * 24L + tm->tm_hour) * 60L + tm->tm_min) * 60L
	    + tm->tm_sec);
}

#define	ATOI2(ar)	((ar)[0] - '0') * 10 + ((ar)[1] - '0'); (ar) += 2;
time_t
parsetime(p)
	char *p;
{
	struct tm *lt;
	int bigyear;
	int yearset = 0;
	time_t tval;
	unsigned char *t;
	
	for (t = (unsigned char *)p; *t; ++t) {
		if (isdigit(*t))
			continue;
		badformat();
	}

	tval = time((long *) 0);
	lt = localtime(&tval);
	lt->tm_sec = 0;
	lt->tm_min = 0;

	switch (strlen(p)) {
	case 10:				/* yyyy */
		bigyear = ATOI2(p);
		lt->tm_year = bigyear * 100 - 1900;
		yearset = 1;
		/* FALLTHROUGH */
	case 8:					/* yy */
		if (yearset) {
			lt->tm_year += ATOI2(p);
		} else {
			lt->tm_year = ATOI2(p);
			if (lt->tm_year < 69)		/* hack for 2000 */
				lt->tm_year += 100;
		}
		/* FALLTHROUGH */
	case 6:					/* mm */
		lt->tm_mon = ATOI2(p);
		if ((lt->tm_mon > 12) || !lt->tm_mon)
			badformat();
		--lt->tm_mon;			/* time struct is 0 - 11 */
		/* FALLTHROUGH */
	case 4:					/* dd */
		lt->tm_mday = ATOI2(p);
		if ((lt->tm_mday > 31) || !lt->tm_mday)
			badformat();
		/* FALLTHROUGH */
	case 2:					/* HH */
		lt->tm_hour = ATOI2(p);
		if (lt->tm_hour > 23)
			badformat();
		break;
	default:
		badformat();
	}
	/* The calling code needs a valid tm_ydays and this is the easiest
	 * way to get one */
	if ((tval = tmtotime(lt)) == -1L)
		errx(1, "specified date is outside allowed range");
	return (tval);
}

void
badformat()
{
	warnx("illegal time format");
	(void)fprintf(stderr, "usage: pom [[[[[cc]yy]mm]dd]HH]\n");
	exit(1);
}
