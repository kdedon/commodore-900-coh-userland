/*	$NetBSD: worm.c,v 1.25 2004/01/27 20:30:31 jsm Exp $	*/

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
 * Worm.  Written by Michael Toy
 * UCSC
 */

#include <ctype.h>
#include <curses.h>
#include <signal.h>

#define newlink() (struct body *) malloc(sizeof (struct body));
#define HEAD '@'
#define BODY 'o'
#define LENGTH 7
#define RUNLEN 8
#define CNTRL(p) (p-'A'+1)

WINDOW *tv;
WINDOW *stw;
struct body {
	int x;
	int y;
	struct body *prev;
	struct body *next;
} *head, *tail, goody;
int growing = 0;
int running = 0;
int slow = 0;
int score = 0;
int start_len = LENGTH;
int visible_len;
int lastch;
char outbuf[BUFSIZ];

void	crash();
void	display();
int	main();
void	leave();
void	life();
void	newpos();
void	process();
void	prize();
int	rnd();
void	setup();
void	wake();

int
main(argc, argv)
	int argc;
	char **argv;
{

	/* Revoke setgid privileges */

	setbuf(stdout, outbuf);
	srand(getpid());
	signal(SIGALRM, wake);
	signal(SIGINT, leave);
	signal(SIGQUIT, leave);
	initscr();
	cbreak();
	noecho();
#ifdef KEY_LEFT
	keypad(stdscr, TRUE);
#endif
	slow = (baudrate() <= 1200);
	clear();
	if (COLS < 18 || LINES < 5) {
		/*
		 * Insufficient room for the line with " Worm" and the
		 * score if fewer than 18 columns; insufficient room for
		 * anything much if fewer than 5 lines.
		 */
		endwin();
		{ fprintf(stderr, "screen too small\n"); exit(1); }
	}
	if (argc == 2)
		start_len = atoi(argv[1]);
	if ((start_len <= 0) || (start_len > ((LINES-3) * (COLS-2)) / 3))
		start_len = LENGTH;
	stw = newwin(1, COLS-1, 0, 0);
	tv = newwin(LINES-1, COLS-1, 1, 0);
	box(tv, '*', '*');
	scrollok(tv, FALSE);
	scrollok(stw, FALSE);
	wmove(stw, 0, 0);
	wprintw(stw, " Worm");
	refresh();
	wrefresh(stw);
	wrefresh(tv);
	life();			/* Create the worm */
	prize();		/* Put up a goal */
	while(1)
	{
		if (running)
		{
			running--;
			process(lastch);
		}
		else
		{
		    fflush(stdout);
		    process(getch());
		}
	}
}

void
life()
{
	struct body *bp, *np;
	int i, j = 1;

	np = NULL;
	head = newlink();
	if (head == NULL)
		{ fprintf(stderr, "worm: out of memory\n"); exit(1); }
	head->x = start_len % (COLS-5) + 2;
	head->y = LINES / 2;
	head->next = NULL;
	display(head, HEAD);
	for (i = 0, bp = head; i < start_len; i++, bp = np) {
		np = newlink();
		if (np == NULL)
			{ fprintf(stderr, "worm: out of memory\n"); exit(1); }
		np->next = bp;
		bp->prev = np;
		if (((bp->x <= 2) && (j == 1)) || ((bp->x >= COLS-4) && (j == -1))) {
			j *= -1;
			np->x = bp->x;
			np->y = bp->y + 1;
		} else {
			np->x = bp->x - j;
			np->y = bp->y;
		}
		display(np, BODY);
	}
	tail = np;
	tail->prev = NULL;
	visible_len = start_len + 1;
}

void
display(pos, chr)
	struct body *pos;
	char chr;
{
	wmove(tv, pos->y, pos->x);
	waddch(tv, chr);
}

void
leave(dummy)
	int dummy;
{
	endwin();

	if (dummy == 0){	/* called via crash() */
		printf("\nWell, you ran into something and the game is over.\n");
		printf("Your final score was %d\n\n", score);
	}
	exit(0);
}

void
wake(dummy)
	int dummy ;
{
	signal(SIGALRM, wake);
	fflush(stdout);
	process(lastch);
}

int
rnd(range)
	int range;
{
	return abs((rand()>>5)+(rand()>>5)) % range;
}

void
newpos(bp)
	struct body * bp;
{
	if (visible_len == (LINES-3) * (COLS-3) - 1) {
		endwin();

		printf("\nYou won!\n");
		printf("Your final score was %d\n\n", score);
		exit(0);
	}
	do {
		bp->y = rnd(LINES-3)+ 1;
		bp->x = rnd(COLS-3) + 1;
		wmove(tv, bp->y, bp->x);
	} while(winch(tv) != ' ');
}

void
prize()
{
	int value;

	value = rnd(9) + 1;
	newpos(&goody);
	waddch(tv, value+'0');
	wrefresh(tv);
}

void
process(ch)
	int ch;
{
	int x,y;
	struct body *nh;

	alarm(0);
	x = head->x;
	y = head->y;
	switch(ch)
	{
#ifdef KEY_LEFT
		case KEY_LEFT:
#endif
		case 'h':
			x--; break;

#ifdef KEY_DOWN
		case KEY_DOWN:
#endif
		case 'j':
			y++; break;

#ifdef KEY_UP
		case KEY_UP:
#endif
		case 'k':
			y--; break;

#ifdef KEY_RIGHT
		case KEY_RIGHT:
#endif
		case 'l':
			x++; break;

		case 'H': x--; running = RUNLEN; ch = tolower(ch); break;
		case 'J': y++; running = RUNLEN/2; ch = tolower(ch); break;
		case 'K': y--; running = RUNLEN/2; ch = tolower(ch); break;
		case 'L': x++; running = RUNLEN; ch = tolower(ch); break;
		case '\f': setup(); return;

		case ERR:
		case CNTRL('C'):
		case CNTRL('D'):
			crash();
			return;

		default: if (! running) alarm(1);
			   return;
	}
	lastch = ch;
	if (growing == 0)
	{
		display(tail, ' ');
		tail->next->prev = NULL;
		nh = tail->next;
		free(tail);
		tail = nh;
		visible_len--;
	}
	else growing--;
	display(head, BODY);
	wmove(tv, y, x);
	if (isdigit(ch = winch(tv)))
	{
		growing += ch-'0';
		prize();
		score += growing;
		running = 0;
		wmove(stw, 0, COLS - 12);
		wprintw(stw, "Score: %3d", score);
		wrefresh(stw);
	}
	else if(ch != ' ') crash();
	nh = newlink();
	if (nh == NULL)
		{ fprintf(stderr, "worm: out of memory\n"); exit(1); }
	nh->next = NULL;
	nh->prev = head;
	head->next = nh;
	nh->y = y;
	nh->x = x;
	display(nh, HEAD);
	head = nh;
	visible_len++;
	if (!(slow && running))
	{
		wmove(tv, head->y, head->x);
		wrefresh(tv);
	}
	if (!running)
		alarm(1);
}

void
crash()
{
	leave(0);
}

void
setup()
{
	clear();
	refresh();
	touchwin(stw);
	wrefresh(stw);
	touchwin(tv);
	wrefresh(tv);
	alarm(1);
}
