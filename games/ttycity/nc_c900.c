/* nc_c900.c -- the COHERENT/Z8001 half of the curses compatibility layer.
 *
 * c900.h fills the gaps that are pure spelling.  These need code: a getch()
 * with a deadline, an n-limited addstr, a directory read, the three attribute
 * calls <curses.h> declares and no file in this library defines, and two libc
 * functions that are simply absent here.
 */

#include "sim.h"
#include <curses.h>
#include <termio.h>
#include <sys/dir.h>
#include <fcntl.h>
#include <sys/timeb.h>
#include "nc.h"

/* ---- getch() with a deadline --------------------------------------------
 * This libcurses has no timeout()/halfdelay(), and its nodelay() is compiled
 * out under COHERENT (getch.c's whole fcntl block is inside #ifndef COHERENT).
 * What does work is the line discipline: termio VMIN/VTIME make read(2) itself
 * return empty after a delay, and wgetch() -> getkey() -> read() answers 0 for
 * an empty read, which is the ERR-equivalent the caller tests for.
 *
 * VTIME is in tenths of a second, so a request finer than 100 ms rounds up to
 * one tick -- with SIM_TICK_MS at 50 that makes the frame period 100 ms, which
 * on this machine is far below the cost of a simulation frame anyway.
 *
 * The saved state is restored by tc_endtimeout(), which must run before
 * endwin() so curses puts back the modes it saved on top of ours.
 */

static struct termio TcOld;
static int TcSaved = 0;

int
tc_settimeout(ms)
int ms;
{
	struct termio t;
	int tenths;

	if (ioctl(0, TCGETA, &TcOld) < 0)
		return (ERR);
	TcSaved = 1;
	t = TcOld;
	tenths = (ms + 99) / 100;
	if (tenths < 1) tenths = 1;
	if (tenths > 255) tenths = 255;
	t.c_lflag &= ~(ICANON | ECHO);
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = tenths;
	return ((ioctl(0, TCSETA, &t) < 0) ? ERR : OK);
}

void
tc_endtimeout()
{
	if (TcSaved) {
		ioctl(0, TCSETA, &TcOld);
		TcSaved = 0;
	}
}

/* ---- addnstr: at most n characters, stopping at the NUL ------------------
 * mvaddbytes()/waddbytes(), the BSD spelling, writes exactly n bytes and would
 * spill the terminator and whatever follows it onto the screen.
 */

int
tc_addnstr(s, n)
char *s;
int n;
{
	int len;

	if (s == (char *)0)
		return (ERR);
	len = strlen(s);
	if ((n >= 0) && (len > n))
		len = n;
	return (waddbytes(stdscr, s, len));
}

int
tc_mvaddnstr(y, x, s, n)
int y;
int x;
char *s;
int n;
{
	if (wmove(stdscr, y, x) == ERR)
		return (ERR);
	return (tc_addnstr(s, n));
}

/* ---- directory listing ---------------------------------------------------
 * No opendir()/readdir() in this libc, and <sys/dir.h> makes DIR a bare
 * char *.  A directory here is a plain file of fixed `struct direct' records,
 * so read it as one: skip d_ino == 0 (a free slot) and terminate the name at
 * DIRSIZ, which is not NUL-terminated when the name fills the field.
 *
 * Fills up to max entries in names[], each freshly allocated; returns the
 * count, or -1 if the directory could not be opened.  tc_dirfree() releases
 * them.  "." and ".." are skipped.
 */

int
tc_dirlist(dir, names, max)
char *dir;
char **names;
int max;
{
	struct direct d;
	int fd, n;
	char nm[DIRSIZ + 1];

	if ((fd = open(dir, O_RDONLY)) < 0)
		return (-1);
	n = 0;
	while ((n < max) && (read(fd, (char *)&d, sizeof d) == sizeof d)) {
		if (d.d_ino == 0)
			continue;
		strncpy(nm, d.d_name, DIRSIZ);
		nm[DIRSIZ] = '\0';
		if ((strcmp(nm, ".") == 0) || (strcmp(nm, "..") == 0))
			continue;
		if ((names[n] = (char *)ckalloc(strlen(nm) + 1)) == (char *)0)
			break;
		strcpy(names[n], nm);
		n++;
	}
	close(fd);
	return (n);
}

void
tc_dirfree(names, n)
char **names;
int n;
{
	while (--n >= 0)
		if (names[n] != (char *)0) {
			ckfree(names[n]);
			names[n] = (char *)0;
		}
}

/* ---- attributes ----------------------------------------------------------
 * wattron/wattroff/wattrset are declared by <curses.h> and implemented by no
 * file in this library: _attrs is a dead field, and the only attribute a cell
 * can carry is _STANDOUT, which addbytes() stamps from win->_flags and
 * refresh() renders with the SO/SE capabilities.  So the whole attribute
 * vocabulary collapses onto one bit, and the mapping has to choose.
 *
 * Reverse and standout turn it on; bold, dim and underline do not.  That is
 * the way round that keeps the map readable: nc_render.c asks for A_BOLD on
 * most populated tiles, so honouring bold as standout would put the greater
 * part of the city in reverse video, while A_REVERSE is reserved for the things
 * that genuinely need to stand out -- the cursor, selection bars, the notice
 * box.  Nothing is lost that this terminal could have shown.
 */

#define TC_STANDOUT	(A_REVERSE | A_STANDOUT)

int
wattrset(win, at)
WINDOW *win;
int at;
{
	if (at & TC_STANDOUT)
		win->_flags |= _STANDOUT;
	else
		win->_flags &= ~_STANDOUT;
	win->_attrs = at;
	return (OK);
}

int
wattron(win, at)
WINDOW *win;
int at;
{
	win->_attrs |= at;
	if (at & TC_STANDOUT)
		win->_flags |= _STANDOUT;
	return (OK);
}

int
wattroff(win, at)
WINDOW *win;
int at;
{
	win->_attrs &= ~at;
	if (at & TC_STANDOUT)
		win->_flags &= ~_STANDOUT;
	return (OK);
}

/* ---- libc gaps ----------------------------------------------------------
 * strstr(3) is not in this libc (strchr/strrchr/index/rindex are), and
 * gettimeofday(2) does not exist -- ftime(2) is the finest clock the kernel
 * offers, at millisecond resolution, which is finer than this program needs.
 * Both belong in libc.
 */

char *
strstr(hay, needle)
char *hay;
char *needle;
{
	int n;

	if ((hay == (char *)0) || (needle == (char *)0))
		return ((char *)0);
	if ((n = strlen(needle)) == 0)
		return (hay);
	for ( ; *hay; hay++)
		if ((*hay == *needle) && (strncmp(hay, needle, n) == 0))
			return (hay);
	return ((char *)0);
}

/* <sys/time.h> here declares struct timeval and nothing else -- there is no
 * struct timezone -- so the second argument is accepted and dropped.  Every
 * caller in this program passes a null pointer for it. */
int
gettimeofday(tv, tz)
struct timeval *tv;
char *tz;
{
	struct timeb tb;

	if (ftime(&tb) < 0)
		return (-1);
	if (tv != (struct timeval *)0) {
		tv->tv_sec = tb.time;
		tv->tv_usec = (long)tb.millitm * 1000L;
	}
	return (0);
}
