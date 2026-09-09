/*
 * Set, clear or complement one pixel.
 */
#include "screen.h"

/*
 * A one-pixel operation has an all-ones source, so the sixteen functions
 * collapse onto the four that read only the destination: 0 clear, 5
 * complement, 10 leave alone, 15 set.  Which one is bits 2 and 3 of the
 * function number, times five.
 *
 * Returns the resulting pixel, or -1 if (x,y) is outside the bitmap.
 */
int
bit_point(map, x, y, op)
register BITMAP *map;
register int x, y;
int op;
{
	register DATA *p;
	register DATA bit;

	if (x < 0 || x >= BIT_WIDE(map) || y < 0 || y >= BIT_HIGH(map))
		return (-1);

	x += map->x0;
	y += map->y0;
	p = hraddr(map, y) + (x >> LOGBITS);
	bit = ((DATA) MSB) >> (x & BITS);

	switch (((op >> 2) & 3) * 5) {
	case 0:
		*p &= ~bit;
		break;
	case 5:
		*p ^= bit;
		break;
	case 15:
		*p |= bit;
		break;
	}
	return (*p & bit);
}
