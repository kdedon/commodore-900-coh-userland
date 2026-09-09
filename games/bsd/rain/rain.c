/*	$NetBSD: rain.c,v 1.17 2004/05/02 21:31:23 christos Exp $	*/

/*
 * Copyright (c) 1980, 1993
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



/*
 * rain 11/3/1980 EPS/CITHEP
 * cc rain.c -o rain -O -ltermlib
 */

#include <sys/types.h>
#include <curses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <termio.h>
#include <unistd.h>
#include <errno.h>

/* K&R: routines returning a pointer or a long must be declared, or they
 * default to int -- 16 bits here, while a pointer is a 32-bit far pointer, so
 * the segment is lost. */
extern char *malloc(), *calloc(), *realloc(), *getenv();
extern char *strcpy(), *strncpy(), *strcat(), *strncat();
extern char *strchr(), *strrchr(), *index(), *rindex(), *strtok();
extern char *fgets(), *memset(), *memcpy(), *ctime(), *asctime();
extern char *getlogin(), *getprogname(), *strerror(), *mktemp();
extern long atol(), time(), lseek(), strtol();
extern unsigned long strtoul();
extern char *optarg;
extern int optind, opterr;

static  int sig_caught = 0;

int main();
static void onsig();


int
main(argc, argv)
int argc;
char **argv;
{
	int x, y, j;
	int cols, lines;
	/* usleep() takes an unsigned long: a 16-bit int here would pass two
	 * bytes where four are read, and 999 ms in microseconds does not fit in
	 * an int anyway. */
	unsigned long delay = 0;
	unsigned long val = 0;
	int ch;
	char *ep;
	int xpos[5], ypos[5];

	setprogname(argv[0]);

	while ((ch = getopt(argc, argv, "d:")) != -1)
		switch (ch) {
		case 'd':
			val = strtoul(optarg, &ep, 0);
			if (ep == optarg || *ep)
				errx(1, "Invalid delay `%s'", optarg);
			if (val >= 1000)
				errx(1, "Invalid delay `%s' (1-999)", optarg);
			delay = val * 1000L;  /* ms -> us */
			break;
		default:
			(void)fprintf(stderr, "Usage: %s [-d delay]\n",
			    getprogname());
			return 1;
		}

	initscr();
	cols = COLS - 4;
	lines = LINES - 4;

	(void)signal(SIGHUP, onsig);
	(void)signal(SIGINT, onsig);
	(void)signal(SIGTERM, onsig);

	curs_set(0);
	for (j = 4; j >= 0; --j) {
		xpos[j] = rand() % cols + 2;
		ypos[j] = rand() % lines + 2;
	}
	for (j = 0;;) {
		if (sig_caught) {
			endwin();
			exit(0);
		}
		x = rand() % cols + 2;
		y = rand() % lines + 2;
		mvaddch(y, x, '.');
		mvaddch(ypos[j], xpos[j], 'o');
		if (!j--)
			j = 4;
		mvaddch(ypos[j], xpos[j], 'O');
		if (!j--)
			j = 4;
		mvaddch(ypos[j] - 1, xpos[j], '-');
		mvaddstr(ypos[j], xpos[j] - 1, "|.|");
		mvaddch(ypos[j] + 1, xpos[j], '-');
		if (!j--)
			j = 4;
		mvaddch(ypos[j] - 2, xpos[j], '-');
		mvaddstr(ypos[j] - 1, xpos[j] - 1, "/ \\");
		mvaddstr(ypos[j], xpos[j] - 2, "| O |");
		mvaddstr(ypos[j] + 1, xpos[j] - 1, "\\ /");
		mvaddch(ypos[j] + 2, xpos[j], '-');
		if (!j--)
			j = 4;
		mvaddch(ypos[j] - 2, xpos[j], ' ');
		mvaddstr(ypos[j] - 1, xpos[j] - 1, "   ");
		mvaddstr(ypos[j], xpos[j] - 2, "     ");
		mvaddstr(ypos[j] + 1, xpos[j] - 1, "   ");
		mvaddch(ypos[j] + 2, xpos[j], ' ');
		xpos[j] = x;
		ypos[j] = y;
		refresh();
		if (delay)
			usleep(delay);
		else
			usleep(20000L);
	}
}

static void
onsig(dummy)
int dummy;
{
	sig_caught = 1;
}
