/*
 * The device half of libbitblit for the C900 hi-res board: opening and
 * closing the display, the byte-granular scroller, and the colour and
 * timestamp entry points that a one-bit display answers trivially.
 */
#include <sys/time.h>

#include "screen.h"

DATA *graph_mem = HR_SEG0;

/*
 * Blank the whole board, a row at a time so that no address crosses the
 * segment split.
 */
static
hrclear()
{
	register DATA *p, *e;
	register int y;

	for (y = 0; y < HR_YMAX; y++) {
		p = hrrow(y);
		e = p + HR_WPERSL;
		while (p < e)
			*p++ = 0;
	}
}

/*
 * Hand back the board as a bitmap.  There is nothing to map and nothing to
 * open: the kernel has segments 0x3A and 0x3B pointed at the board and
 * readable and writable from user mode for as long as /drv/hrtty is loaded,
 * and video is already enabled by that driver.  `name' is therefore unused;
 * the geometry is fixed in the hardware.
 */
DATA *
bit_initscreen(name, width, height, depth, devi)
char *name;
int *width, *height;
unsigned char *depth;
char **devi;
{
	*width = HR_XMAX;
	*height = HR_YMAX;
	*depth = 1;
	*devi = (char *) 0;
	return (graph_mem);
}

/*
 * Give the board back.  Nothing was mapped, so nothing is unmapped; the
 * screen is left as it is for whoever paints next.
 */
void
display_close(bitmap)
BITMAP *bitmap;
{
}

/*
 * Enter and leave graphics mode.  The hi-res console driver owns the same
 * frame buffer and has no notion of standing off, so both directions simply
 * hand over a blank screen: MGR paints its root pattern over one, and the
 * console repaints its text over the other.
 */
void
bit_grafscreen()
{
	hrclear();
}

void
bit_textscreen()
{
	hrclear();
}

/*
 * Scroll `high' rows of a rectangle up by `delta' rows, whole bytes at a
 * time.  x and y are in the coordinates of the primary bitmap.
 */
void
bit_bytescroll(map, x, y, wide, high, delta)
BITMAP *map;
int x, y, wide, high, delta;
{
	register char *d, *s;
	register int i;
	int bx, n, rows;

	bx = x >> 3;
	n = (wide + (x & 7)) >> 3;
	rows = high - delta;
	while (--rows >= 0) {
		d = (char *) hraddr(map, y) + bx;
		s = (char *) hraddr(map, y + delta) + bx;
		i = n;
		while (--i >= 0)
			*d++ = *s++;
		y++;
	}
}

/*
 * Hundredths of a second since the first call.  times(2) is the only clock
 * here with sub-second resolution, so the answer is 10 ms granular, and an
 * int holds 327 seconds of it before wrapping.
 */
int
timestamp()
{
	static long offset = 0;
	struct timeval tv;
	long t;

	gettimeofday(&tv, (char *) 0);
	if (offset == 0) {
		offset = tv.tv_sec;
		return (0);
	}
	t = (tv.tv_sec - offset) * 100 + tv.tv_usec / 10000;
	return ((int) t);
}

/*
 * One bit deep: the foreground is colour 1, and the palette is not
 * programmable.
 */
unsigned int
fg_color_idx()
{
	return (1);
}

void
setpalette(bp, index, red, green, blue, maxi)
BITMAP *bp;
unsigned int index, red, green, blue, maxi;
{
}

void
getpalette(bp, index, red, green, blue, maxi)
BITMAP *bp;
unsigned int index;
unsigned int *red, *green, *blue, *maxi;
{
	*red = 0;
	*green = 0;
	*blue = 0;
	*maxi = 1;
}

/*
 * Shrink an eight-bit bitmap to one bit.  There are no eight-bit bitmaps on
 * this board, as on every other monochrome device MGR supports.
 */
BITMAP *
bit_shrink(src_map, bg_color)
BITMAP *src_map;
int bg_color;
{
	return (BIT_NULL);
}
