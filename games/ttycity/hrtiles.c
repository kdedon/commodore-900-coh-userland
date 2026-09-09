/* hrtiles -- draw a SimCity tile set on the hi-res console using a loaded
 * character set.  Standalone: it needs neither ttycity nor curses, only the
 * HR driver's font-load ioctl.
 *
 * This is the answer to "what does a reprogrammable character set look like on
 * the HR console", separated from the 190 KB game so it can be run the day the
 * ioctl exists.  It builds the glyphs by rule rather than by hand: an HR cell
 * is 12 x 25 pixels, and every road, rail, power line, shading level and fence
 * piece in Micropolis's vocabulary is a geometric figure in that cell, so 60 of
 * the 96 loadable glyphs are computed here from four primitives.  The buildings
 * are the part that wants an artist; they are left as recognisable blocks.
 *
 * usage: hrtiles [-p]        -p = print the codes, do not load or draw
 *
 * Glyph bit convention, taken from hrtty/src/hrterm2.c plotc(): each scan line
 * is one `unsigned short' whose TOP 12 bits are the pixels, most significant
 * bit leftmost, 1 = ink.  25 scan lines per glyph, top to bottom.
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/hrio.h>

/* The loadable bank is the high one, so slot HRLOW is the byte 0xa0.  The low
 * bank is the console's own alphabet and the driver refuses to write it, which
 * is what makes a loaded tile set safe: whatever this program leaves behind,
 * ordinary text still reads. */
#define SLOT0		HRLOW
#define CODE(slot)	HRCODE(slot)

#define GW		HRGW		/* glyph width in pixels  */
#define GH		HRGH		/* glyph height in scan lines */
#define NGLYPH		HRHIGH		/* slots in the loadable bank */

static unsigned short Font[NGLYPH][GH];
static int Next = SLOT0;		/* next free slot */

/* ---- primitives ---------------------------------------------------------- */

#define PIX(col)	(1 << (15 - (col)))

/* Set pixels [c0,c1] on rows [r0,r1] of glyph g. */
static
box(g, r0, r1, c0, c1)
int g, r0, r1, c0, c1;
{
	int r, c;
	unsigned short m;

	if (g < 0 || g >= NGLYPH) return;
	m = 0;
	for (c = c0; c <= c1; c++)
		if (c >= 0 && c < GW) m |= PIX(c);
	for (r = r0; r <= r1; r++)
		if (r >= 0 && r < GH) Font[g][r] |= m;
}

/* The cell centre.  A 12 x 25 cell is nearly two squares tall, so the vertical
 * centre is row 12 and the horizontal centre is between columns 5 and 6; every
 * connector is drawn from an edge to that point, which is what makes adjacent
 * tiles join. */
#define CR		12		/* centre row    */
#define CC		5		/* centre column */

/* One arm of a connector.  dir: 0 up, 1 down, 2 left, 3 right.  w is the
 * stroke width in pixels: 1 = power line, 3 = road, and rail is drawn as two
 * 1-pixel strokes (see arm2). */
static
arm(g, dir, w)
int g, dir, w;
{
	int lo, hi;

	lo = CC - (w - 1) / 2;
	hi = lo + w - 1;
	switch (dir) {
	case 0: box(g, 0, CR, lo, hi); break;
	case 1: box(g, CR, GH - 1, lo, hi); break;
	case 2: box(g, CR - (w - 1) / 2, CR - (w - 1) / 2 + w - 1, 0, CC); break;
	case 3: box(g, CR - (w - 1) / 2, CR - (w - 1) / 2 + w - 1, CC, GW - 1);
		break;
	}
}

/* A double (rail) arm: two 1-pixel rails with a 2-pixel gap. */
static
arm2(g, dir)
int g, dir;
{
	switch (dir) {
	case 0: box(g, 0, CR, CC - 2, CC - 2); box(g, 0, CR, CC + 2, CC + 2);
		break;
	case 1: box(g, CR, GH - 1, CC - 2, CC - 2);
		box(g, CR, GH - 1, CC + 2, CC + 2); break;
	case 2: box(g, CR - 2, CR - 2, 0, CC); box(g, CR + 2, CR + 2, 0, CC);
		break;
	case 3: box(g, CR - 2, CR - 2, CC, GW - 1);
		box(g, CR + 2, CR + 2, CC, GW - 1); break;
	}
}

/* ---- the tile set -------------------------------------------------------- */

/* nc_render.c's nc_transit_mask() yields up<<3|dn<<2|lf<<1|rt, so a transit
 * family is 16 glyphs indexed by that mask directly -- no lookup table, the
 * neighbour bits ARE the glyph offset.  Mask 0 (no neighbours) is drawn as the
 * four-way piece, which is what the ncurses tables already do. */
static int Road, Rail, Wire;		/* base slots of the three families */

static
transit(base, weight)
int base, weight;
{
	int m, g, d;

	for (m = 0; m < 16; m++) {
		g = base - SLOT0 + m;
		for (d = 0; d < 4; d++) {
			static int bit[4] = { 8, 4, 2, 1 };	/* up dn lf rt */
			if ((m != 0) && !(m & bit[d])) continue;
			if (weight == 2) arm2(g, d); else arm(g, d, weight);
		}
	}
}

/* Shading ramp: 4 levels of ordered dither over the whole cell, used for the
 * density overlays, rubble, and the undeveloped part of a zone lot. */
static int Shade;

static
shades()
{
	int lev, r, c, g;

	/* The 2x2 Bayer matrix {{0,2},{3,1}}: ink where the threshold is at or
	 * below the level, which gives 25/50/75/100 per cent coverage.  An
	 * ordered dither and not stripes, so two adjacent cells at the same
	 * level read as one continuous tone. */
	static int bayer[2][2] = { { 0, 2 }, { 3, 1 } };

	for (lev = 0; lev < 4; lev++) {
		g = Shade - SLOT0 + lev;
		for (r = 0; r < GH; r++)
			for (c = 0; c < GW; c++)
				if (bayer[r & 1][c & 1] <= lev)
					Font[g][r] |= PIX(c);
	}
}

/* Water: two horizontal wave rows, phase-shifted per frame so animateTiles()
 * makes the sea move. */
static int Water;

static
waves()
{
	int f, c, g;

	for (f = 0; f < 4; f++) {
		g = Water - SLOT0 + f;
		for (c = 0; c < GW; c++) {
			int r = 6 + ((((c + f) & 3) < 2) ? 0 : 1);
			Font[g][r] |= PIX(c);
			r = 17 + ((((c + f + 2) & 3) < 2) ? 0 : 1);
			Font[g][r] |= PIX(c);
		}
	}
}

/* Buildings: a filled block with a roof line and a door notch, at three
 * heights, which reads as low/medium/high density at a glance.  Distinguishing
 * residential from commercial from industrial is what colour did on the X11
 * build and what a hand-drawn glyph should do here; the block heights are the
 * placeholder. */
static int Bldg;

static
buildings()
{
	int d, g, top;

	for (d = 0; d < 3; d++) {
		g = Bldg - SLOT0 + d;
		top = 16 - d * 6;			/* taller with density */
		box(g, top, GH - 2, 1, GW - 2);		/* the mass	  */
		box(g, top, top, 0, GW - 1);		/* the eaves	  */
		box(g, GH - 1, GH - 1, 0, GW - 1);	/* the ground	  */
		Font[g][GH - 4] &= ~PIX(5);		/* a door	  */
		Font[g][GH - 3] &= ~PIX(5);
		Font[g][GH - 2] &= ~PIX(5);
	}
}

/* Trees: a crown over a trunk. */
static int Tree;

static
trees()
{
	int g = Tree - SLOT0;

	box(g, 4, 6, 4, 7);
	box(g, 7, 9, 2, 9);
	box(g, 10, 13, 1, 10);
	box(g, 14, 16, 3, 8);
	box(g, 17, GH - 1, 5, 6);
}

/* Fire: a ragged upward flame, four animation phases. */
static int Fire;

static
fires()
{
	int f, r, g, w;

	for (f = 0; f < 4; f++) {
		g = Fire - SLOT0 + f;
		for (r = GH - 1; r >= 4; r--) {	/* wide at the base, a tip on top */
			w = (r - 4) / 4;
			if (w > 5) w = 5;
			box(g, r, r, CC - w + ((r + f) & 1), CC + w);
		}
	}
}

/* ---- assembly ------------------------------------------------------------ */

static
build()
{
	Road = Next;  Next += 16;
	Rail = Next;  Next += 16;
	Wire = Next;  Next += 16;
	Shade = Next; Next += 4;
	Water = Next; Next += 4;
	Bldg = Next;  Next += 3;
	Tree = Next;  Next += 1;
	Fire = Next;  Next += 4;

	transit(Road, 3);		/* heavy stroke	  */
	transit(Rail, 2);		/* double stroke  */
	transit(Wire, 1);		/* hairline	  */
	shades();
	waves();
	buildings();
	trees();
	fires();
	return (Next - SLOT0);
}

/* ---- the picture --------------------------------------------------------- */

/* A road grid with buildings in the blocks, a river across it and a power line
 * down one side: enough of a city that the glyph joins are visible, drawn with
 * plain write(2) so nothing but the font is being tested. */
static
draw()
{
	char row[128];
	int r, c, n, m;

	printf("\033[2J\033[H");		/* hrterm2.c: ESC[2J, ESC[H */
	for (r = 0; r < 28; r++) {
		n = 0;
		for (c = 0; c < 78; c++) {
			if (r == 13) {			/* the river */
				row[n++] = CODE(Water + ((c + r) & 3));
			} else if ((r % 7) == 3) {	/* an east-west road */
				m = ((c % 11) == 5) ? 15 : 3;	/* 3 = lf|rt */
				row[n++] = CODE(Road + m);
			} else if ((c % 11) == 5) {	/* a north-south road */
				row[n++] = CODE(Road + 12);	/* 12 = up|dn */
			} else if (c == 76) {		/* the power line */
				row[n++] = CODE(Wire + 12);
			} else if ((r % 7) == 5 && (c % 11) > 6) {
				row[n++] = CODE(Bldg + ((c / 3) % 3));
			} else if ((r % 7) == 1 && (c % 11) < 4) {
				row[n++] = CODE(Tree);
			} else if ((r % 7) == 6 && (c % 11) == 8) {
				row[n++] = CODE(Fire + (c & 3));
			} else if ((r % 7) == 2) {
				row[n++] = CODE(Shade + ((c / 7) & 3));
			} else {
				row[n++] = ' ';
			}
		}
		row[n++] = '\r';
		row[n++] = '\n';
		write(1, row, n);
	}
	printf("hrtiles: %d glyphs loaded at 0x%02x..0x%02x\r\n",
	       Next - SLOT0, CODE(SLOT0), CODE(Next - 1));
}

/* ---- main ---------------------------------------------------------------- */

static
dumpcodes()
{
	printf("road  0x%02x..0x%02x  (+ up<<3|dn<<2|lf<<1|rt)\n",
	       CODE(Road), CODE(Road + 15));
	printf("rail  0x%02x..0x%02x\n", CODE(Rail), CODE(Rail + 15));
	printf("wire  0x%02x..0x%02x\n", CODE(Wire), CODE(Wire + 15));
	printf("shade 0x%02x..0x%02x  (4 levels)\n", CODE(Shade), CODE(Shade+3));
	printf("water 0x%02x..0x%02x  (4 phases)\n", CODE(Water), CODE(Water+3));
	printf("bldg  0x%02x..0x%02x  (3 densities)\n", CODE(Bldg), CODE(Bldg+2));
	printf("tree  0x%02x\n", CODE(Tree));
	printf("fire  0x%02x..0x%02x  (4 phases)\n", CODE(Fire), CODE(Fire + 3));
	printf("%d of %d slots used\n", Next - SLOT0, NGLYPH);
}

main(argc, argv)
int argc;
char **argv;
{
	struct hrfont hf;
	int i, pronly = 0;

	for (i = 1; i < argc; i++)
		if (argv[i][0] == '-' && argv[i][1] == 'p') pronly = 1;

	build();
	if (pronly) {
		dumpcodes();
		return (0);
	}

	hf.hf_first = SLOT0;
	hf.hf_count = Next - SLOT0;
	hf.hf_bits = &Font[0][0];
	if (ioctl(1, HRIOCSFONT, (char *)&hf) < 0) {
		fprintf(stderr,
		    "hrtiles: no font-load ioctl on this console\n");
		dumpcodes();
		return (1);
	}
	draw();
	return (0);
}
