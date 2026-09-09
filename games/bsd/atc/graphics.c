/*	$NetBSD: graphics.c,v 1.10 2003/08/07 09:36:54 agc Exp $	*/

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ed James.
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
 * Copyright (c) 1987 by Ed James, UC Berkeley.  All rights reserved.
 *
 * Copy permission is hereby granted provided that this notice is
 * retained on all partial or complete copies.
 *
 * For more info on this and all of my stuff, mail edjames@berkeley.edu.
 */


#include "include.h"

#define C_TOPBOTTOM		'-'
#define C_LEFTRIGHT		'|'
#define C_AIRPORT		'='
#define C_LINE			'+'
#define C_BACKROUND		'.'
#define C_BEACON		'*'
#define C_CREDIT		'*'

WINDOW	*radar, *cleanradar, *credit, *input, *planes;

int
getAChar()
{
	int c;

	errno = 0;
	while ((c = getchar()) == EOF && errno == EINTR) {
		errno = 0;
		clearerr(stdin);
	}
	return(c);
}

void
erase_all()
{
	PLANE	*pp;

	for (pp = air.head; pp != NULL; pp = pp->next) {
		wmove(cleanradar, pp->ypos, pp->xpos * 2);
		wmove(radar, pp->ypos, pp->xpos * 2);
		waddch(radar, winch(cleanradar));
		wmove(cleanradar, pp->ypos, pp->xpos * 2 + 1);
		wmove(radar, pp->ypos, pp->xpos * 2 + 1);
		waddch(radar, winch(cleanradar));
	}
}

void
draw_all()
{
	PLANE	*pp;

	for (pp = air.head; pp != NULL; pp = pp->next) {
		if (pp->status == S_MARKED)
			wstandout(radar);
		wmove(radar, pp->ypos, pp->xpos * 2);
		waddch(radar, name(pp));
		waddch(radar, '0' + pp->altitude);
		if (pp->status == S_MARKED)
			wstandend(radar);
	}
	wrefresh(radar);
	planewin();
	wrefresh(input);		/* return cursor */
	fflush(stdout);
}

/* A window this game cannot do without.  newwin() answers NULL for a size the
 * terminal cannot hold as well as for a failed malloc, so a terminal too small
 * for the radar arrives here rather than as a fault in the first wmove(). */
void
nowindow(what)
	char	*what;
{
	endwin();
	fprintf(stderr, "atc: no room for the %s window on a %dx%d screen.\n",
		what, COLS, LINES);
	exit(1);
}

void
init_gr()
{
	static char	buffer[BUFSIZ];

	initscr();
	setbuf(stdout, buffer);
	if (LINES < INPUT_LINES + 4 || COLS < PLANE_COLS + 20) {
		endwin();
		fprintf(stderr,
		    "atc: needs at least a %dx%d screen; this one is %dx%d.\n",
		    PLANE_COLS + 20, INPUT_LINES + 4, COLS, LINES);
		exit(1);
	}
	input = newwin(INPUT_LINES, COLS - PLANE_COLS, LINES - INPUT_LINES, 0);
	if (input == NULL)
		nowindow("input");
	credit = newwin(INPUT_LINES, PLANE_COLS, LINES - INPUT_LINES,
		COLS - PLANE_COLS);
	if (credit == NULL)
		nowindow("credit");
	planes = newwin(LINES - INPUT_LINES, PLANE_COLS, 0, COLS - PLANE_COLS);
	if (planes == NULL)
		nowindow("plane-list");
}

void
setup_screen(scp)
	 C_SCREEN	*scp;
{
	int	i, j;
	char	str[3];
	 char *airstr;

	str[2] = '\0';

	/* The radar is 2 columns per scenario column, so a 40-wide scenario
	 * wants 80 and will not fit beside the plane list. */
	if (scp->height > LINES - INPUT_LINES ||
	    scp->width * 2 > COLS - PLANE_COLS) {
		endwin();
		fprintf(stderr,
		    "atc: \"%s\" wants a %dx%d radar; this screen holds %dx%d.\n",
		    file, scp->width * 2, scp->height, COLS - PLANE_COLS,
		    LINES - INPUT_LINES);
		exit(1);
	}

	if (radar != NULL)
		delwin(radar);
	radar = newwin(scp->height, scp->width * 2, 0, 0);
	if (radar == NULL)
		nowindow("radar");

	if (cleanradar != NULL)
		delwin(cleanradar);
	cleanradar = newwin(scp->height, scp->width * 2, 0, 0);
	if (cleanradar == NULL)
		nowindow("radar backing");

	/* minus one here to prevent a scroll */
	for (i = 0; i < PLANE_COLS - 1; i++) {
		wmove(credit, 0, i);
		waddch(credit, C_CREDIT);
		wmove(credit, INPUT_LINES - 1, i);
		waddch(credit, C_CREDIT);
	}
	wmove(credit, INPUT_LINES / 2, 1);
	waddstr(credit, AUTHOR_STR);

	for (i = 1; i < scp->height - 1; i++) {
		for (j = 1; j < scp->width - 1; j++) {
			wmove(radar, i, j * 2);
			waddch(radar, C_BACKROUND);
		}
	}

	/*
	 * Draw the lines first, since people like to draw lines
	 * through beacons and exit points.
	 */
	str[0] = C_LINE;
	for (i = 0; i < scp->num_lines; i++) {
		str[1] = ' ';
		draw_line(radar, scp->line[i].p1.x, scp->line[i].p1.y,
			scp->line[i].p2.x, scp->line[i].p2.y, str);
	}

	str[0] = C_TOPBOTTOM;
	str[1] = C_TOPBOTTOM;
	wmove(radar, 0, 0);
	for (i = 0; i < scp->width - 1; i++)
		waddstr(radar, str);
	waddch(radar, C_TOPBOTTOM);

	str[0] = C_TOPBOTTOM;
	str[1] = C_TOPBOTTOM;
	wmove(radar, scp->height - 1, 0);
	for (i = 0; i < scp->width - 1; i++)
		waddstr(radar, str);
	waddch(radar, C_TOPBOTTOM);

	for (i = 1; i < scp->height - 1; i++) {
		wmove(radar, i, 0);
		waddch(radar, C_LEFTRIGHT);
		wmove(radar, i, (scp->width - 1) * 2);
		waddch(radar, C_LEFTRIGHT);
	}

	str[0] = C_BEACON;
	for (i = 0; i < scp->num_beacons; i++) {
		str[1] = '0' + i;
		wmove(radar, scp->beacon[i].y, scp->beacon[i].x * 2);
		waddstr(radar, str);
	}

	for (i = 0; i < scp->num_exits; i++) {
		wmove(radar, scp->exit[i].y, scp->exit[i].x * 2);
		waddch(radar, '0' + i);
	}

	airstr = "^?>?v?<?";
	for (i = 0; i < scp->num_airports; i++) {
		str[0] = airstr[scp->airport[i].dir];
		str[1] = '0' + i;
		wmove(radar, scp->airport[i].y, scp->airport[i].x * 2);
		waddstr(radar, str);
	}
	
	overwrite(radar, cleanradar);
	wrefresh(radar);
	wrefresh(credit);
	fflush(stdout);
}

void
draw_line(w, x, y, lx, ly, s)
	WINDOW	*w;
	int	 x, y, lx, ly;
	 char	*s;
{
	int	dx, dy;

	dx = SGN(lx - x);
	dy = SGN(ly - y);
	for (;;) {
		wmove(w, y, x * 2);
		waddstr(w, s);
		if (x == lx && y == ly)
			break;
		x += dx;
		y += dy;
	}
}

void
ioclrtoeol(pos)
	int pos;
{
	wmove(input, 0, pos);
	wclrtoeol(input);
	wrefresh(input);
	fflush(stdout);
}

void
iomove(pos)
	int pos;
{
	wmove(input, 0, pos);
	wrefresh(input);
	fflush(stdout);
}

void
ioaddstr(pos, str)
	int	 pos;
	 char	*str;
{
	wmove(input, 0, pos);
	waddstr(input, str);
	wrefresh(input);
	fflush(stdout);
}

void
ioclrtobot()
{
	wclrtobot(input);
	wrefresh(input);
	fflush(stdout);
}

void
ioerror(pos, len, str)
	int	 pos, len;
	 char	*str;
{
	int	i;

	wmove(input, 1, pos);
	for (i = 0; i < len; i++)
		waddch(input, '^');
	wmove(input, 2, 0);
	waddstr(input, str);
	wrefresh(input);
	fflush(stdout);
}

void
quit(dummy)
	int dummy ;
{
	int			c, y, x;

	getyx(input, y, x);
	wmove(input, 2, 0);
	waddstr(input, "Really quit? (y/n) ");
	wclrtobot(input);
	wrefresh(input);
	fflush(stdout);

	c = getchar();
	if (c == EOF || c == 'y') {
		/* disable timer */
		alarm(0);
		fflush(stdout);
		clear();
		refresh();
		endwin();
		log_score(0);
		exit(0);
	}
	wmove(input, 2, 0);
	wclrtobot(input);
	wmove(input, y, x);
	wrefresh(input);
	fflush(stdout);
}

void
planewin()
{
	PLANE	*pp;
	int	warning = 0;

	wmove(planes, 0, 0);
	wclrtobot(planes);
	wprintw(planes, "Time: %-4d Safe: %d", clck, safe_planes);
	wmove(planes, 2, 0);

	waddstr(planes, "pl dt  comm");
	for (pp = air.head; pp != NULL; pp = pp->next) {
		if (waddch(planes, '\n') == ERR) {
			warning++;
			break;
		}
		waddstr(planes, command(pp));
	}
	waddch(planes, '\n');
	for (pp = ground.head; pp != NULL; pp = pp->next) {
		if (waddch(planes, '\n') == ERR) {
			warning++;
			break;
		}
		waddstr(planes, command(pp));
	}
	if (warning) {
		wmove(planes, LINES - INPUT_LINES - 1, 0);
		waddstr(planes, "---- more ----");
		wclrtoeol(planes);
	}
	wrefresh(planes);
	fflush(stdout);
}

void
loser(p, s)
	 PLANE	*p;
	 char	*s;
{
	int			c;

	/* disable timer */
	alarm(0);

	wmove(input, 0, 0);
	wclrtobot(input);
	/* p may be NULL if we ran out of memory */
	if (p == NULL)
		wprintw(input, "%s\n\nHit space for top players list...", s);
	else
		wprintw(input, "Plane '%c' %s\n\nHit space for top players list...",
			name(p), s);
	wrefresh(input);
	fflush(stdout);
	while ((c = getchar()) != EOF && c != ' ')
		;
	clear();	/* move to top of screen */
	refresh();
	endwin();
	log_score(0);
	exit(0);
}

void
redraw()
{
	clear();
	refresh();

	touchwin(radar);
	wrefresh(radar);
	touchwin(planes);
	wrefresh(planes);
	touchwin(credit);
	wrefresh(credit);

	/* refresh input last to get cursor in right place */
	touchwin(input);
	wrefresh(input);
	fflush(stdout);
}

void
done_screen()
{
	clear();
	refresh();
	endwin();	  /* clean up curses */
}
