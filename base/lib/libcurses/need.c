/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/* $Header: /newbits/usr/lib/libcurses/RCS/need.c,v 1.2 91/09/30 13:06:39 bin Exp Locker: bin $
 *
 *	The  information  contained herein  is a trade secret  of INETCO
 *	Systems, and is confidential information.   It is provided under
 *	a license agreement,  and may be copied or disclosed  only under
 *	the terms of that agreement.   Any reproduction or disclosure of
 *	this  material  without  the express  written  authorization  of
 *	INETCO Systems or persuant to the license agreement is unlawful.
 *
 *	Copyright (c) 1989
 *	An unpublished work by INETCO Systems, Ltd.
 *	All rights reserved.
 */

# include	"curses.ext"

#ifdef	COHERENT
/**
 *
 * _need( s )
 * char * s;
 *
 *	Input:	s = string which will eventually be passed to _putchar().
 *
 *	Action:	If output buffering is enabled, and there is insufficient
 *		room in the output buffer for the specified string,
 *		flush the output buffer.
 *
 *	Return:	 0 = atomic output is guaranteed.
 *		-1 = buffering disabled.
 *
 *	Notes:	This attempts to ensure that escape sequences are sent to
 *		the terminal in one atomic operation, without being split
 *		by other terminal output.
 */
int
_need( s )
uchar * s;
{
	reg FILE * fp = stdout;

	/*
	 * A NULL capability string is not an error, and must not reach
	 * strlen().  tputs(3) has always accepted one -- its first statement
	 * is `if (cp == 0) return' -- because a termcap entry legitimately
	 * omits what the terminal does not have, and curses hands those
	 * straight through.  vt100 has no `ti' or `vs', so _puts(TI) with a
	 * null argument is the ordinary case rather than a fault.
	 *
	 * This wrapper is COHERENT's own, added around tputs to keep escape
	 * sequences atomic, and it was less defensive than the function it
	 * wraps: Tputs(s,n,f) evaluates _need(s) FIRST, so the strlen below
	 * ran on the null before tputs could reject it.  Every program that
	 * emitted an absent capability died here -- hunt(6) does it twice, on
	 * the second line of its terminal setup, and reported only
	 * "Segmentation violation".
	 */
	if ( s == NULL )
		return (-1);

	/*
	 * Stdout exists, and has a buffer allocated.
	 * _bp = pointer to beginning of buffer.
	 * _cp = pointer to current char in buffer.
	 * Flush stdout if insufficient room for string.
	 */
	if ( (fp != NULL) && (fp->_bp != NULL) && (fp->_cp != NULL) ) {
		if ( ((fp->_cp - fp->_bp) + strlen(s)) >= BUFSIZ )
			fflush( stdout );
		return (0);
	}

	/*
	 * Atomic output by stdio cannot be guaranteed.
	 */
	return (-1);
}
#endif
