/* c900.h -- COHERENT 3.5 / Z8001 compatibility shim for ttycity.
 *
 * Included from the bottom of sim.h's system-header block, so it is in scope
 * for every translation unit ahead of <curses.h>.  Three jobs:
 *
 *   1. Fill the gaps between this system's BSD/INETCO libcurses and the SysV
 *	/ncurses surface the front end was written against.  Nothing here
 *	reaches into a WINDOW except getmaxyx, which reads the same _maxy/_maxx
 *	that <curses.h>'s own getyx reads from _cury/_curx.
 *   2. Neutralise color.  This libcurses has no color model at all, so the
 *	pair machinery collapses to attribute 0 and has_colors() is false; the
 *	front end's NC_MONO then drives every renderer down its mono path and
 *	no COLOR_PAIR ever reaches the terminal.  The pair arithmetic in
 *	nc_render.c is left intact so a color curses can light it up later.
 *   3. Name the replacements (nc_c900.c) for the calls that need real work:
 *	a timed getch, an n-limited addstr, and directory reads.
 *
 * The color, locale and mouse macros are deliberately expressions and not
 * empty text, so that call sites used for value keep compiling.
 */

#ifndef TC_C900_H
#define TC_C900_H 1

/* ---- <curses.h> gaps ---------------------------------------------------- */

/* No color model.  COLOR_PAIRS 1 keeps nc_colors_init()'s bound test true for
 * pair 0 only, so its init_pair loop runs zero useful times. */
#define has_colors()		0
#define start_color()		ERR
#define use_default_colors()	ERR
#define init_pair(p, f, b)	ERR
#define COLOR_PAIR(n)		0
#define COLORS			0
#define COLOR_PAIRS		1
#define COLOR_BLACK		0
#define COLOR_RED		1
#define COLOR_GREEN		2
#define COLOR_YELLOW		3
#define COLOR_BLUE		4
#define COLOR_MAGENTA		5
#define COLOR_CYAN		6
#define COLOR_WHITE		7

/* No per-cell attribute plane, hence no recolor-in-place. */
#define chgat(n, a, p, o)	OK
#define mvchgat(y, x, n, a, p, o) OK

/* No alternate character set: the HR and LR consoles and a vt100 all agree on
 * ASCII and nothing else, so the line-drawing set is the 7-bit approximation.
 * A_ALTCHARSET is 0 so nc_screenshot's test for it is simply never true. */
#define A_ALTCHARSET	0
#define ACS_HLINE	'-'
#define ACS_VLINE	'|'
#define ACS_ULCORNER	'+'
#define ACS_URCORNER	'+'
#define ACS_LLCORNER	'+'
#define ACS_LRCORNER	'+'
#define ACS_LTEE	'+'
#define ACS_RTEE	'+'
#define ACS_TTEE	'+'
#define ACS_BTEE	'+'
#define ACS_PLUS	'+'
#define ACS_DIAMOND	'*'
#define ACS_CKBOARD	':'
#define ACS_DEGREE	'\''
#define ACS_BULLET	'o'
#define ACS_BLOCK	'#'

/* Window extent and cursor.  _maxy/_maxx are the window's size, which is what
 * ncurses getmaxyx yields; all four are `short' in the WINDOW here. */
#define getmaxyx(w, y, x)	((y) = (w)->_maxy, (x) = (w)->_maxx)
#define getcurx(w)		((w)->_curx)
#define getcury(w)		((w)->_cury)

/* A cell here carries no pair, only the one standout bit (see wattrset() in
 * nc_c900.c), so the pair recovered from a chtype is always 0. */
#define PAIR_NUMBER(c)		0

/* The hardware cursor cannot be hidden through this library (no `vi'/`ve' in
 * its capability table), so it parks wherever the last write left it. */
#define curs_set(n)		OK

/* getch() with a deadline: see tc_settimeout() in nc_c900.c. */
#define timeout(ms)		tc_settimeout(ms)

/* addnstr semantics -- stop at NUL, at most n chars.  waddbytes/mvaddbytes,
 * the BSD spelling, writes exactly n bytes and would spill past the NUL. */
#define mvaddnstr(y, x, s, n)	tc_mvaddnstr((y), (x), (char *)(s), (n))
#define addnstr(s, n)		tc_addnstr((char *)(s), (n))

/* ---- no <locale.h> ------------------------------------------------------ */

#define LC_ALL		0
#define LC_CTYPE	2
#define setlocale(c, s)	((char *)0)
#define MB_CUR_MAX	1		/* single-byte only: no unicode mode */

/* ---- nc_c900.c --------------------------------------------------------- */

extern int  tc_settimeout();		/* getch deadline, milliseconds */
extern void tc_endtimeout();		/* restore the tty before endwin */
extern int  tc_mvaddnstr();
extern int  tc_addnstr();
extern int  tc_dirlist();		/* names in a directory -> char *[] */
extern void tc_dirfree();

#endif /* TC_C900_H */
