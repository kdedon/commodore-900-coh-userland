/*
 * waddch -- add one character to a window.
 *
 * This overrides the libcurses.a member of the same name, which puts a NUL on
 * the screen for every character on this machine.  Its definition is
 *
 *	waddch(win, c) WINDOW *win; uchar c; { return waddbytes(win, &c, 1); }
 *
 * and the fault is `&c' on a char PARAMETER.  A char argument is widened to
 * int by the caller, so the slot holds 0x0058 for 'X'; reading it as a value
 * gives 'X', but the address of the parameter is the address of the slot, and
 * on a big-endian Z8001 byte 0 of that slot is the zero half.  waddbytes()
 * therefore copies a NUL.  Verified directly:
 *
 *	f(c) char c; { printf("%d %d\n", c, *(char *)&c); }   ->   88 0
 *
 * Every single-character output goes through here -- addch, mvaddch, insch's
 * neighbours, box(), the '@' of a dungeon, the 'o' of a raindrop -- so the
 * whole curses batch draws blanks without this.  Copying the argument into a
 * local first is correct on either byte order and costs one byte of frame.
 *
 * The library and the compiler each own a candidate real fix (declare the
 * parameter int; or bind a char parameter's address to the low half of its
 * slot); this file shims it instead.  Objects on the link line are searched
 * before archives, so this definition is the one that binds and
 * libcurses.a's addch.o is never pulled in.
 */
#include <curses.h>

waddch(win, c)
	WINDOW *win;
	int c;
{
	uchar ch;

	ch = (uchar) c;
	return (waddbytes(win, &ch, 1));
}
