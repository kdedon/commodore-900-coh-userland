/*
 * Read one pixel.
 */
#include "screen.h"

/*
 * The value of the pixel at (x,y) of `bp' -- 0 or 1 on this board -- or 0
 * if (x,y) is outside the bitmap.
 */
int
bit_on(bp, x, y)
register BITMAP *bp;
register int x, y;
{
	register DATA *p;

	if (x < 0 || x >= BIT_WIDE(bp) || y < 0 || y >= BIT_HIGH(bp))
		return (0);

	x += bp->x0;
	y += bp->y0;
	p = hraddr(bp, y) + (x >> LOGBITS);
	return ((*p >> (BITS - (x & BITS))) & 1);
}
