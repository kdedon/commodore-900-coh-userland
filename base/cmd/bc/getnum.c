/*
 * Copyright (c) 1977-1995 Robert Swartz.
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <ctype.h>
#include "bc.h"

/*
 *	Anydig is set by a caller for which the letters A-F are digits with
 *	the values ten through 15 whatever the input base is; where it is
 *	clear, a digit outside the input base ends the number instead.
 */

int	anydig = FALSE;

/*
 *	Getnum reads in a number from standard input.  It allows one
 *	radix point and sets the scale field of the number accordingly.
 *	If ibase==10, it recognizes leading '0' for octal 8 and '0x' for hex.
 */

rvalue	*
getnum(ch)
register int	ch;
{
	register rvalue	*result;
	register FILE	*inf = infile;
	mint	dig;
	int	val;
	int	dot;
	int	savbase;

	result = (rvalue *)mpalc(sizeof *result);
	newscalar(result);
	minit(&dig);
	savbase = ibase;
	if (ibase <= 10 && ch == '0' && (ch = getc(inf)) != '.') {
		if (ch == 'x') {
			ch = getc(inf);
			ibase = 16;
		} else
			ibase = 8;
	}
	for (dot = FALSE;; ch = getc(inf)) {
		if ((val = digit(ch)) < ibase || (anydig && val < 16)) {
			mitom(val, &dig);
			smult(&result->mantissa, ibase, &result->mantissa);
			madd(&result->mantissa, &dig, &result->mantissa);
			if (dot)
				++result->scale;
		} else if (ch == '.' & !dot)
			dot = TRUE;
		else
			break;
	}
	ungetc(ch, inf);
	mvfree(&dig);
	/* Rescale into decimal if necessary, losing some precision */
	if (ibase != 10) for (dot = result->scale; dot != 0; dot -= 1) {
		smult(&result->mantissa, 10, &result->mantissa);
		sdiv(&result->mantissa, ibase, &result->mantissa, &val);
	}
	ibase = savbase;
	return (result);
}

/*
 *	Digit maps characters to digit values.
 */
digit(c) register int c;
{
	if (isascii(c)) {
		if (isdigit(c))
			return c - '0';
		if (islower(c))
			return c - 'a' + 10;
		if (isupper(c))
			return c - 'A' + 10;
	}
	return NOTDIGIT;
}

/*
 *	Sibase takes the rvalue pointed to by lval and set ibase
 *	to it if it is in range (ie. between 2 and 16).
 */

sibase(lval)
rvalue	*lval;
{
	register int	base;

	base = rtoint(lval);
	if (2 > base || base > 16)
		bcmerr("Invalid input base");
	ibase = base;
}
