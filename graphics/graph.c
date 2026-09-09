/*
 * Line and cont routines.
 */


/*
 * The following definitions make C more amenable to a purist.
 */
#define	bool	int			/* boolean type */
#define	uint	unsigned int		/* short names for unsigned types */
#define	ulong	unsigned long
#define	uchar	unsigned char
#define	ushort	unsigned short int
#define	not	!			/* logical negation operator */
#define	and	&&			/* logical conjunction */
#define	or	||			/* logical disjunction */
#define	TRUE	(0 == 0)
#define	FALSE	(not TRUE)
#define	loop	while (TRUE)		/* loop until break */
#define	EOS	'\0'			/* end-of-string char */


/*
 * Screen hardware.
 */
#define	SEG0	((short *)0x3a000000L)	/* first segment of screen ram */
#define	SEG1	((short *)0x3b000000L)	/* second segment of screen ram */
#define	YSPLIT	512			/* segment crossover y coordinate */
#define	XMAX	1024			/* just past maximum x coordinate */
#define	YMAX	800			/* just past maximum y coordinate */
#define	BPW	16			/* bits per word */


/*
 * Globals functions.
 */
extern void	clear(),		/* clear screen */
		move(),			/* move current position */
		point(),		/* draw point */
		cont(),			/* continue line */
		line();			/* draw line */


/*
 * Local data.
 */
static int	curx,			/* current x coordinate */
		cury;			/* current y coordinate */


/*
 * Local functions.
 */
static void	xfast(),
		yfast();


/*
 * Clear clears the screen.
 */
void
clear()
{
	register short	*p;
	register uint	n;

	for (p=SEG0, n=(uint)YSPLIT*(XMAX/BPW); n-- != 0;)
		*p++ = 0;
	for (p=SEG1, n=(uint)(YMAX-YSPLIT)*(XMAX/BPW); n-- != 0;)
		*p++ = 0;
}

void 
clearw()
{
	register short *p;
	register uint n;
	for(p=SEG0, n=(uint)YSPLIT*(XMAX/BPW); n-- != 0;)
		*p++ = 0xffff;
	for(p=SEG1, n=(uint)(YMAX-YSPLIT)*(XMAX/BPW); n-- != 0; )
		*p++ = 0xffff;
}
/*
 * Move changes the current position.
 */
void
move(x, y)
int	x,
	y;
{
	curx = x;
	cury = y;
}


/*
 * Point simply draws a point on the screen.
 */
void
point(x, y)
register int	x,
		y;
{
	register short	*p;

	curx = x;
	cury = y;
	if (((uint)x >= XMAX)
	or  ((uint)y >= YMAX))
		return;
	if (y < YSPLIT)
		p = SEG0;
	else {
		y -= YSPLIT;
		p = SEG1;
	}
	p[(uint)y*(XMAX/BPW) + x/BPW] ^= (uint)0x8000 >> (x%BPW);
}

gp(x,y)
register int x,y;
{
	register short *p;
	if (y < YSPLIT) p = SEG0;
	else p = SEG1;
	return(p[(uint)y*(XMAX/BPW) + x/BPW] & (uint)0x8000 >> (x%BPW));
}
sp(x,y,pl)
register int x,y,pl;
{
	register short *p;
	if (y < YSPLIT) p = SEG0;
	else p = SEG1;
	if(pl)
	 p[(uint)y*(XMAX/BPW) + x/BPW] |= (uint)0x8000 >> (x%BPW);
	else
	 p[(uint)y*(XMAX/BPW) + x/BPW] &= ~((uint)0x8000 >> (x%BPW));
}
/*
 * Cont simply draws a line from the current position to its argument.
 */
void
cont(x, y)
int	x,
	y;
{
	line(curx, cury, x, y);
}


/*
 * Line is used to produce the points that are needed to fill in
 * a line between two given end-points.  It calls the routine
 * `point' to actually place the points into an object.
 */
void
line(x, y, xf, yf)
int	x,
	y,
	xf,
	yf;
{
	register int	dx,
			dy;
	long		e;

	curx = xf;
	cury = yf;
	dx = xf - x;
	dy = yf - y;
	e = (long)yf*x - (long)y*xf;
	if (dy >= 0) {
		if ((y >= YMAX)
		or  (yf < 0))
			return;
		if (y < 0) {
			x = e / dy;
			y = 0;
		}
		if (yf >= YMAX) {
			xf = ((long)(YMAX-1)*dx + e) / dy;
			yf = YMAX - 1;
		}
	} else {
		if ((yf >= YMAX)
		or  (y < 0))
			return;
		if (yf < 0) {
			xf = e / dy;
			yf = 0;
		}
		if (y >= YMAX) {
			x = ((long)(YMAX-1)*dx + e) / dy;
			y = YMAX - 1;
		}
	}
	e = (long)xf*y - (long)x*yf;
	if (dx > 0) {
		if ((x >= XMAX)
		or  (xf < 0))
			return;
		if (x < 0) {
			y = e / dx;
			x = 0;
		}
		if (xf >= XMAX) {
			yf = ((long)(XMAX-1)*dy + e) / dx;
			xf = XMAX - 1;
		}
		if (dy > 0)
			if (dx >= dy)
				xfast(x, y, xf, yf);
			else
				yfast(x, y, xf, yf);
		else
			if (dx >= -dy)
				xfast(x, y, xf, yf);
			else
				yfast(xf, yf, x, y);
	} else {
		if ((xf >= XMAX)
		or  (x < 0))
			return;
		if (xf < 0) {
			yf = e / dx;
			xf = 0;
		}
		if (x >= XMAX) {
			y = ((long)(XMAX-1)*dy + e) / dx;
			x = XMAX - 1;
		}
		if (dy > 0)
			if (-dx >= dy)
				xfast(xf, yf, x, y);
			else
				yfast(x, y, xf, yf);
		else
			if (-dx >= -dy)
				xfast(xf, yf, x, y);
			else
				yfast(xf, yf, x, y);
	}
}


static void
xfast(x, y, xf, yf)
register int	x,
		y;
int		xf,
		yf;
{
	register short	*vp;
	register int	rem;
	register uint	bit;
	int		dx,
			dy,
			ystep;

	dx = xf - x;
	dy = yf - y;
	ystep = 1;
	if (dy < 0) {
		dy = -dy;
		ystep = -1;
	}
	rem = dx/2;
	if (y < YSPLIT)
		vp = SEG0 + y*(XMAX/BPW) + x/BPW;
	else
		vp = SEG1 + (y-YSPLIT)*(XMAX/BPW) + x/BPW;
	bit = (uint)0x8000 >> (x%BPW);
	loop {
		*vp ^= bit;
		if (++x > xf)
			break;
		bit >>= 1;
		if (bit == 0) {
			bit = 0x8000;
			++vp;
		}
		if ((rem -= dy) < 0) {
			rem += dx;
			y += ystep;
			if (ystep > 0)
				if (y == YSPLIT)
					vp = SEG1 + y*(XMAX/BPW) + x/BPW;
				else
					vp += XMAX/BPW;
			else
				if (y == YSPLIT-1)
					vp = SEG0+(y-YSPLIT)*(XMAX/BPW)+x/BPW;
				else
					vp -= XMAX/BPW;
		}
	}
}


static void
yfast(x, y, xf, yf)
register int	x,
		y;
int		xf,
		yf;
{
	register short	*vp;
	register int	rem;
	register uint	bit;
	int		dx,
			dy,
			xstep;

	dx = xf - x;
	dy = yf - y;
	xstep = 1;
	if (dx < 0) {
		dx = -dx;
		xstep = -1;
	}
	rem = dy/2;
	if (y < YSPLIT)
		vp = SEG0 + y*(XMAX/BPW) + x/BPW;
	else
		vp = SEG1 + (y-YSPLIT)*(XMAX/BPW) + x/BPW;
	bit = (uint)0x8000 >> (x%BPW);
	loop {
		*vp ^= bit;
		if (++y > yf)
			break;
		if (y == YSPLIT)
			vp = SEG1 + y*(XMAX/BPW) + x/BPW;
		else
			vp += XMAX/BPW;
		if ((rem -= dx) < 0) {
			rem += dy;
			x += xstep;
			if (xstep > 0) {
				bit >>= 1;
				if (bit == 0) {
					bit = 0x8000;
					++vp;
				}
			} else {
				bit <<= 1;
				if (bit == 0) {
					bit = 1;
					--vp;
				}
			}
		}
	}
}
