/*
 * hrlight -- first light on the HR bitmap.
 *
 * Paints a test card straight into the framebuffer from user mode and holds
 * it on screen until a key is typed.  It answers, in one run, the three
 * questions the whole MGR port rests on:
 *
 *   1. can an unprivileged process write virtual segments 0x3A/0x3B at all
 *      (MMU attribute 0 = no System Only bit), or does it take a trap;
 *   2. are those segments really pointed at the board (physical 0x3E0000 /
 *      0x3F0000), which /drv/hrtty's portst() -- not the kernel's own
 *      pfix(BMS, BMPHYS) -- is what sets up;
 *   3. is the row-512 segment split handled, i.e. does a shape drawn across
 *      it arrive in one piece.
 *
 * The pattern is chosen so that each answer is separately visible:
 *   - a one-pixel frame around the whole screen           (extent, both segs)
 *   - both full diagonals                                 (addressing)
 *   - a solid 1024x64 bar over rows 480..543               (the split)
 *   - a vertical ruler tick every 64 rows                  (row pitch)
 *   - a 32x32 solid block in each corner                   (corners reached)
 */
#include "hr.h"

#define	BAR_TOP		(HR_YSPLIT-32)		/* bar straddles the split */
#define	BAR_BOT		(HR_YSPLIT+32)

/*
 * Set the pixel at (x,y).
 */
static
plot(x, y)
register int x, y;
{
	register unsigned *p;

	if (x < 0 || x >= HR_XMAX || y < 0 || y >= HR_YMAX)
		return;
	p = hrrow(y) + (x >> 4);
	*p |= (unsigned)0x8000 >> (x & 15);
}

/*
 * Fill scan lines y0..y1 solid.
 */
static
bar(y0, y1)
int y0, y1;
{
	register unsigned *p, *e;
	register int y;

	for (y = y0; y <= y1; y++) {
		p = hrrow(y);
		e = p + HR_WPERSL;
		while (p < e)
			*p++ = 0xFFFF;
	}
}

/*
 * Blank the screen.
 */
static
clear()
{
	register unsigned *p, *e;
	register int y;

	for (y = 0; y < HR_YMAX; y++) {
		p = hrrow(y);
		e = p + HR_WPERSL;
		while (p < e)
			*p++ = 0;
	}
}

/*
 * Solid wide x high block with its top left corner at (x,y).
 */
static
block(x, y, wide, high)
int x, y, wide, high;
{
	register int i, j;

	for (j = 0; j < high; j++)
		for (i = 0; i < wide; i++)
			plot(x+i, y+j);
}

main()
{
	register int i;
	char c[1];

	clear();

	/* frame */

	for (i = 0; i < HR_XMAX; i++) {
		plot(i, 0);
		plot(i, HR_YMAX-1);
	}
	for (i = 0; i < HR_YMAX; i++) {
		plot(0, i);
		plot(HR_XMAX-1, i);
	}

	/* both diagonals -- 1024 columns over 800 rows */

	for (i = 0; i < HR_XMAX; i++) {
		plot(i, i * (HR_YMAX-1) / (HR_XMAX-1));
		plot(HR_XMAX-1-i, i * (HR_YMAX-1) / (HR_XMAX-1));
	}

	/* the bar that crosses the segment split */

	bar(BAR_TOP, BAR_BOT-1);

	/* row ruler: a 16-pixel tick on the left edge every 64 rows */

	for (i = 0; i < HR_YMAX; i += 64)
		block(2, i, 16, 2);

	/* corners */

	block(0, 0, 32, 32);
	block(HR_XMAX-32, 0, 32, 32);
	block(0, HR_YMAX-32, 32, 32);
	block(HR_XMAX-32, HR_YMAX-32, 32, 32);

	/* hold the picture until a key arrives, then let the console repaint */

	read(0, c, 1);
	return (0);
}
