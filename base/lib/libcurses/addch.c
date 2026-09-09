/*
 * Copyright (c) 1981 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef COHERENT
#ifndef lint
static uchar sccsid[] = "@(#)addch.c	5.4 (Berkeley) 6/30/88";
#endif /* not lint */
#endif /* not COHERENT */

# include	"curses.ext"

/*
 *	This routine adds the character to the current position
 *
 */
waddch(win, c)
WINDOW	*win;
uchar		c;
{
    /*
     * The character is copied into a local before its address is taken.
     * A char argument is promoted to int by the call, so the parameter
     * occupies a 16-bit stack slot, and on this big-endian machine the
     * address of that slot names the HIGH half -- zero for every ASCII
     * character.  &c would therefore hand waddbytes() a NUL whatever the
     * caller passed, and the cursor would advance over a character that
     * was never drawn.  A local uchar is a whole object, so its address
     * names the byte itself.  (Correct as written on the little-endian
     * i8086 this backend descends from, which is why it stood.)
     */
    uchar	b;

    b = c;
    return waddbytes(win, &b, 1);
}
