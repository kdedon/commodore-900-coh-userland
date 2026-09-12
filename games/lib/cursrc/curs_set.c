/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * curs_set -- cursor visibility, for the BSD-games ports.
 *
 * The in-tree libcurses is 4.x BSD curses plus INETCO's System V additions;
 * it carries the `vi'/`ve'/`vs' termcap strings as cursor_invisible,
 * cursor_normal and cursor_visible but no curs_set() to drive them.  Games
 * that hide the cursor while they animate (rain, worms, tetris) call this.
 *
 * visibility: 0 invisible, 1 normal, 2 very visible.  Returns the previous
 * setting, or ERR when the terminal has no such capability -- which is the
 * ordinary case on the LR console, and harmless: the cursor simply shows.
 */
#include <curses.h>

extern int _putchar();		/* the libcurses output routine _puts() drives */

static int _cursvis = 1;

int
curs_set(visibility)
	int visibility;
{
	int old;
	uchar *cap;

	switch (visibility) {
	case 0:		cap = cursor_invisible;	break;
	case 1:		cap = cursor_normal;	break;
	default:	cap = cursor_visible;
			if (cap == (uchar *) 0)
				cap = cursor_normal;
			break;
	}
	if (cap == (uchar *) 0)
		return (ERR);
	old = _cursvis;
	_cursvis = visibility;
	_puts(cap);
	(void) fflush(stdout);
	return (old);
}
