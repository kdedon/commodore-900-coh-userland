/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*
 * Draw a line, Bresenham, one pixel at a time.
 *
 * The row step is a plain pointer addition, except for the single step that
 * crosses the segment split of a screen bitmap: `ysync' is the row at which
 * the row address has to be rebuilt, and is set to a row that cannot occur
 * for a memory bitmap.  The word index within the row is carried alongside
 * the pointer so that the rebuild can find it; that costs one increment per
 * sixteen pixels of horizontal travel.
 */
#include "screen.h"

#define	TOP	001
#define	BOTTOM	002
#define	LEFT	004
#define	RIGHT	010

#define	CROSS(x,y) \
	((x) < 0 ? LEFT : (x) >= dest->wide ? RIGHT : 0) + \
	((y) < 0 ? TOP : (y) >= dest->high ? BOTTOM : 0)

#define	XMOVE	if ((bit = bit >> 1) == 0) { bit = (DATA) MSB; dst++; xw++; }

#define	YMOVE	{ y += ystep; \
		  if (y == ysync) dst = hraddr(dest, y) + xw; \
		  else dst += d_incr; }

#define	STEP(dx,dy,xmove,ymove,rop) { \
	rincr = (dx - dy) << 1; \
	rdecr = -(dy << 1); \
	err = dx + rdecr; \
	for (count = dx; count >= 0; count--) { \
		rop; \
		xmove; \
		if (err < 0) { \
			ymove; \
			err += rincr; \
		} else \
			err += rdecr; \
		} \
	}

void
bit_line(dest, x0, y0, x1, y1, func)
register BITMAP *dest;
int x0, y0, x1, y1;
int func;
{
	register DATA *dst;
	register DATA bit;
	register int count;
	register int err;
	register int rincr, rdecr;
	int d_incr, dx, dy, temp;
	int y, ystep, ysync, xw;

	{
		register int cross0 = CROSS(x0, y0);
		register int cross1 = CROSS(x1, y1);

		while (cross0 || cross1) {
			int cross, x, y;

			if (cross0 & cross1)
				return;
			if (cross0 != 0)
				cross = cross0;
			else
				cross = cross1;
			if (cross & (LEFT | RIGHT)) {
				int edge = (cross & LEFT) ? 0 : dest->wide - 1;

				y = y0 + (y1 - y0) * (edge - x0) / (x1 - x0);
				x = edge;
			} else {
				int edge = (cross & TOP) ? 0 : dest->high - 1;

				x = x0 + (x1 - x0) * (edge - y0) / (y1 - y0);
				y = edge;
			}
			if (cross == cross0) {
				x0 = x;
				y0 = y;
				cross0 = CROSS(x, y);
			} else {
				x1 = x;
				y1 = y;
				cross1 = CROSS(x, y);
			}
		}
	}

	x0 += dest->x0;
	y0 += dest->y0;
	x1 += dest->x0;
	y1 += dest->y0;

	if (x1 < x0) {				/* always left to right */
		temp = x1, x1 = x0, x0 = temp;
		temp = y1, y1 = y0, y0 = temp;
	}
	dx = x1 - x0;
	dy = y1 - y0;

	d_incr = BIT_LINE(dest);
	ystep = 1;
	if (dy <= 0) {
		d_incr = -d_incr;
		ystep = -1;
		dy = -dy;
	}

	ysync = -2;
	if (IS_SCREEN(dest))
		ysync = ystep > 0 ? HR_YSPLIT : HR_YSPLIT - 1;

	y = y0;
	xw = x0 >> LOGBITS;
	dst = hraddr(dest, y) + xw;
	bit = ((DATA) MSB) >> (x0 & BITS);

	switch (((func >> 2) & 3) * 5) {
	case 0:
		if (dx > dy)
			STEP(dx, dy, XMOVE, YMOVE, *dst &= ~bit)
		else
			STEP(dy, dx, YMOVE, XMOVE, *dst &= ~bit)
		break;
	case 5:
		if (dx > dy)
			STEP(dx, dy, XMOVE, YMOVE, *dst ^= bit)
		else
			STEP(dy, dx, YMOVE, XMOVE, *dst ^= bit)
		break;
	case 15:
		if (dx > dy)
			STEP(dx, dy, XMOVE, YMOVE, *dst |= bit)
		else
			STEP(dy, dx, YMOVE, XMOVE, *dst |= bit)
		break;
	}
}
