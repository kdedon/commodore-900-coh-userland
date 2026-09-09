/* screen.c Copyright Michael Temari 08/01/1996 All Rights Reserved */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <curses.h>

#include "screen.h"

_PROTOTYPE(void gotsig, (int sig));
_PROTOTYPE(static char *delword, (WINDOW *w));
_PROTOTYPE(static void PutCh, (WINDOW *w, int c));

extern int waddbytes();

/*
 * One character into a window, through a LOCAL char.
 *
 * libcurses' waddch() was `uchar c; return waddbytes(win, &c, 1);' -- it took
 * the ADDRESS of a char parameter.  A char argument is promoted to int by the
 * K&R call, so the parameter occupies a 16-bit slot at FP+6, and on this
 * big-endian machine the byte that address names is the HIGH half: 0 for every
 * ASCII character.  So waddch() stored NUL in the window cell whatever it was
 * handed, and the cursor advanced over a character that was never drawn --
 * measured, on two guests, as a talk window whose column marched 01 -> 10 for a
 * nine-letter word with no letters in the byte stream, and as a divider row of
 * spaces where screen.c had asked for 80 dashes.
 *
 * The same call is correct on the i8086 this backend descends from, where the
 * low byte comes first, which is why the donor library never noticed.
 *
 * libcurses' addch.c now copies the argument into a local itself, so waddch()
 * is correct and this routine is no longer a workaround for it.  It stays
 * because the convention it uses is the one that is right on either byte order
 * and because libcurses is a vendored library: a re-import of the donor file
 * would put the fault back, and it would not put it back here.  The compiler's
 * own fix is in parameter binding and is not this tree's to make.
 */
static void PutCh(w, c)
WINDOW *w;
int c;
{
unsigned char b;

   b = c;
   waddbytes(w, &b, 1);
}

struct {
	WINDOW *win;
	char erase;
	char kill;
	char werase;
} window[2];

static char line[80+1];

int ScreenDone = 0;

static WINDOW *dwin;

void gotsig(sig)
int sig;
{
   ScreenDone = 1;
   signal(sig, gotsig);
}

int ScreenInit()
{
int i;

   if(initscr() == (WINDOW *)NULL) {
   	fprintf(stderr, "talk: Could not initscr\n");
   	return(-1);
   }
   signal(SIGINT, gotsig);
   signal(SIGQUIT, gotsig);
   signal(SIGPIPE, gotsig);
   signal(SIGHUP, gotsig);
   clear();
   refresh();
   noecho();
   cbreak();

   /* local window */
   window[LOCALWIN].win = newwin(LINES / 2, COLS, 0, 0);
   scrollok(window[LOCALWIN].win, TRUE);
   wclear(window[LOCALWIN].win);

   /* divider between windows */
   dwin = newwin(1, COLS, LINES / 2, 0);
   i = COLS;
   while(i-- > 0)
   	PutCh(dwin, '-');
   wrefresh(dwin);

   /* remote window */
   window[REMOTEWIN].win = newwin(LINES - (LINES / 2) - 1, COLS, LINES / 2 + 1, 0);
   scrollok(window[REMOTEWIN].win, TRUE);
   wclear(window[REMOTEWIN].win);

   return(0);
}

void ScreenMsg(msg)
char *msg;
{
WINDOW *w;

   w =window[LOCALWIN].win;

   wmove(w, 0, 0);

   if(*msg != '\0') {
	wprintw(w, "[%s]", msg);
	wclrtoeol(w);
   } else
   	werase(w);

   wrefresh(w);
}

void ScreenWho(user, host)
char *user;
char *host;
{
   if(*host != '\0') {
   	wmove(dwin, 0, (COLS - (1 + strlen(user) + 1 + strlen(host) + 1)) / 2);
   	wprintw(dwin, " %s@%s ", user, host);
   } else {
   	wmove(dwin, 0, (COLS - (1 + strlen(user) + 1)) / 2);
   	wprintw(dwin, " %s ", user);
   }
   wrefresh(dwin);
}

void ScreenEdit(lcc, rcc)
char lcc[];
char rcc[];
{
   window[LOCALWIN].erase   = lcc[0];
   window[LOCALWIN].kill    = lcc[1];
   window[LOCALWIN].werase  = lcc[2];
   window[REMOTEWIN].erase  = rcc[0];
   window[REMOTEWIN].kill   = rcc[1];
   window[REMOTEWIN].werase = rcc[2];
}

void ScreenPut(data, len, win)
char *data;
int len;
int win;
{
WINDOW *w;
unsigned char ch;
int r, c;

   w = window[win].win;

   while(len-- > 0) {
   	ch = *data++;
   	/* new line CR, NL */
   	if(ch == '\r' || ch == '\n') {
		PutCh(w, '\n');
	} else
	/* erase a character, BS, DEL  */
	if(ch == 0x08 || ch == 0x7f || ch == window[win].erase) {
		getyx(w, r, c);
		if(c > 0)
			c--;
		wmove(w, r, c);
		PutCh(w, ' ');
		wmove(w, r, c);
	} else
	/* erase line CTL-U */
	if(ch == 0x15 || ch == window[win].kill) {
		getyx(w, r, c);
		wmove(w, r, 0);
		wclrtoeol(w);
	} else
	/* refresh CTL-L */
	if(ch == 0x0c) {
		if(win == LOCALWIN) {
			touchwin(w);
			wrefresh(w);
			touchwin(window[REMOTEWIN].win);
			wrefresh(window[REMOTEWIN].win);
		}
	} else
	/* bell CTL-G */
	if(ch == 0x07) {
		putchar(ch);
	}
	else
	/* erase last word CTL-W */
	if(ch == 0x17 || ch == window[win].werase) {
		(void) delword(w);
	} else {
		getyx(w, r, c);
		if(1 || isprint(ch)) {
			if(ch != ' ' && c == (COLS - 1))
				wprintw(w, "\n%s", delword(w));
			PutCh(w, ch);
		}
	}
   }
   wrefresh(w);
}

static char *delword(w)
WINDOW *w;
{
int r, c;
int i = 0;
char ch;
char *p = &line[80];

   *p-- = '\0';
   getyx(w, r, c);
   if(c == 0) return(&line[80]);
   while(c >= 0) {
   	c--;
   	ch = mvwinch(w, r, c);
   	if(ch == ' ') break;
   	*p-- = ch;
   	i = 1;
   	PutCh(w, ' ');
   }
   c += i;
   wmove(w, r, c);
   return(++p);
}

void ScreenEnd()
{
   move(LINES - 1, 0);
   refresh();
   endwin();
}
