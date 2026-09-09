/*
 * bit_blit -- move a rectangle between bitmaps under one of the sixteen
 * boolean functions of source and destination.
 *
 * The frame word is 16 bits, MSB leftmost (screen.h).  Everything here works
 * a whole word at a time; nothing is done pixel by pixel.
 *
 * Three engines cover the sixteen functions:
 *
 *	fill	the four that do not read the source (0 clear, 5 complement,
 *		10 leave alone, 15 set), and every blit whose source bitmap
 *		is null.  One word operation per whole word.
 *	copy	function 12, source into destination -- what text, window
 *		motion and scrolling use.  Per word, one load, two shifts,
 *		one or and one store when the source and destination bit
 *		phases differ; one load and one store when they agree.
 *	rop	the other eleven, from the algebraic normal form
 *		    r = K ^ (d & A) ^ (s & B) ^ (d & s & C)
 *		whose four coefficient masks come from the function number
 *		once per call.  Seven extra word operations, no per-word
 *		branch and no indirect call.
 *
 * A destination word that is only partly covered is read, merged under the
 * edge mask and written back; the words between the two edges are written
 * whole.  A row is walked right to left when the source and destination
 * overlap in the same bitmap with the destination to the right, and the rows
 * are walked bottom to top when the destination is below the source.
 *
 * Rows of a screen bitmap come from hraddr(): rows 0..511 live in segment
 * 0x3A and rows 512..799 in 0x3B, and an address may not be carried from one
 * to the other.  The row range is therefore cut into bands that lie wholly
 * inside one segment before an engine runs, so an engine steps from row to
 * row by plain addition and the split costs nothing per row.
 */
#include "screen.h"

/*
 * A function whose source is all ones is a function of the destination
 * alone: nsrc[] carries the four such functions, indexed by bits 2 and 3 of
 * the original.  zsrc[] flags the four that never read the source at all.
 */
static char nsrc[16] = {
	 0,  0,  0,  0,
	 5,  5,  5,  5,
	10, 10, 10, 10,
	15, 15, 15, 15
};

static char zsrc[16] = {
	1, 0, 0, 0, 0,
	1, 0, 0, 0, 0,
	1, 0, 0, 0, 0,
	1
};

/*
 * What one engine call has to know.  Held here rather than passed so that
 * the engines take no arguments and the setup is written once.
 */
static DATA *b_dp;			/* first word of the destination row */
static DATA *b_sp;			/* first word of the source row */
static int b_dpit;			/* words to the next destination row */
static int b_spit;			/* words to the next source row */
static int b_rows;			/* rows in this band */
static int b_nw;			/* words the destination row spans */
static DATA b_lm;			/* write mask of the first word */
static DATA b_rm;			/* write mask of the last word */
static int b_lsh;			/* merge shifts; b_lsh 0 when aligned */
static int b_rsh;
static int b_rev;			/* walk each row right to left */
static DATA b_K, b_A, b_B, b_C;		/* rop coefficient masks */

#define ROP(d,s) (b_K ^ ((d) & b_A) ^ ((s) & b_B) ^ ((d) & (s) & b_C))

/*
 * The destination alone: fill with a constant, or complement.  A masked
 * complement is an exclusive-or with the mask.
 */
static
eng_fill()
{
	register DATA *p;
	register int n;
	register DATA v;
	DATA *d;
	DATA lm, nlm, rm, nrm, vl, vr;
	int r;

	lm = b_lm;
	nlm = ~lm;
	rm = b_rm;
	nrm = ~rm;
	d = b_dp;
	r = b_rows;

	if (b_A == 0) {				/* clear or set */
		v = b_K;
		vl = v & lm;
		vr = v & rm;
		while (--r >= 0) {
			p = d;
			*p = (*p & nlm) | vl;
			if (b_nw > 1) {
				n = b_nw - 1;
				while (--n > 0)
					*++p = v;
				++p;
				*p = (*p & nrm) | vr;
			}
			d += b_dpit;
		}
	} else {				/* complement */
		while (--r >= 0) {
			p = d;
			*p ^= lm;
			if (b_nw > 1) {
				n = b_nw - 1;
				while (--n > 0) {
					++p;
					*p = ~*p;
				}
				++p;
				*p ^= rm;
			}
			d += b_dpit;
		}
	}
}

/*
 * Function 12: the source, moved into the destination.
 */
static
eng_copy()
{
	register DATA *p, *q;
	register int n;
	register DATA cur, nxt;
	DATA *d, *s;
	DATA lm, nlm, rm, nrm;
	int r, off, lsh, rsh;

	lm = b_lm;
	nlm = ~lm;
	rm = b_rm;
	nrm = ~rm;
	off = b_nw - 1;
	lsh = b_lsh;
	rsh = b_rsh;
	d = b_dp;
	s = b_sp;
	r = b_rows;

	if (lsh == 0) {
		if (b_rev) {
			while (--r >= 0) {
				p = d + off;
				q = s + off;
				*p = (*p & nrm) | (*q & rm);
				if (off > 0) {
					n = off;
					while (--n > 0) {
						--p;
						--q;
						*p = *q;
					}
					--p;
					--q;
					*p = (*p & nlm) | (*q & lm);
				}
				d += b_dpit;
				s += b_spit;
			}
		} else {
			while (--r >= 0) {
				p = d;
				q = s;
				*p = (*p & nlm) | (*q & lm);
				if (off > 0) {
					n = off;
					while (--n > 0)
						*++p = *++q;
					++p;
					++q;
					*p = (*p & nrm) | (*q & rm);
				}
				d += b_dpit;
				s += b_spit;
			}
		}
	} else if (b_rev) {
		while (--r >= 0) {
			p = d + off;
			q = s + off;
			nxt = q[1];
			cur = *q;
			*p = (*p & nrm) | (((cur << lsh) | (nxt >> rsh)) & rm);
			if (off > 0) {
				nxt = cur;
				n = off;
				while (--n > 0) {
					--q;
					cur = *q;
					--p;
					*p = (cur << lsh) | (nxt >> rsh);
					nxt = cur;
				}
				--q;
				cur = *q;
				--p;
				*p = (*p & nlm) |
				     (((cur << lsh) | (nxt >> rsh)) & lm);
			}
			d += b_dpit;
			s += b_spit;
		}
	} else {
		while (--r >= 0) {
			p = d;
			q = s;
			cur = *q++;
			nxt = *q++;
			*p = (*p & nlm) | (((cur << lsh) | (nxt >> rsh)) & lm);
			if (off > 0) {
				n = off;
				while (--n > 0) {
					cur = nxt;
					nxt = *q++;
					*++p = (cur << lsh) | (nxt >> rsh);
				}
				cur = nxt;
				nxt = *q;
				++p;
				*p = (*p & nrm) |
				     (((cur << lsh) | (nxt >> rsh)) & rm);
			}
			d += b_dpit;
			s += b_spit;
		}
	}
}

/*
 * The other eleven functions.  One row body serves both walk directions:
 * the word step is signed and the two source words are re-read each time
 * instead of being carried, which costs one load per word on a path that
 * already costs seven.
 */
static
eng_rop()
{
	register DATA *p, *q;
	register int n;
	register DATA dw, sw;
	DATA *d, *s;
	DATA fm, nfm, sm, nsm;
	int r, off, lsh, rsh, ws, first;

	off = b_nw - 1;
	lsh = b_lsh;
	rsh = b_rsh;
	if (b_rev) {
		fm = b_rm;
		sm = b_lm;
		ws = -1;
		first = off;
	} else {
		fm = b_lm;
		sm = b_rm;
		ws = 1;
		first = 0;
	}
	nfm = ~fm;
	nsm = ~sm;
	d = b_dp;
	s = b_sp;
	r = b_rows;

	if (lsh == 0) {
		while (--r >= 0) {
			p = d + first;
			q = s + first;
			dw = *p;
			sw = *q;
			*p = (dw & nfm) | (ROP(dw, sw) & fm);
			if (off > 0) {
				n = off;
				while (--n > 0) {
					p += ws;
					q += ws;
					dw = *p;
					sw = *q;
					*p = ROP(dw, sw);
				}
				p += ws;
				q += ws;
				dw = *p;
				sw = *q;
				*p = (dw & nsm) | (ROP(dw, sw) & sm);
			}
			d += b_dpit;
			s += b_spit;
		}
	} else {
		while (--r >= 0) {
			p = d + first;
			q = s + first;
			dw = *p;
			sw = (q[0] << lsh) | (q[1] >> rsh);
			*p = (dw & nfm) | (ROP(dw, sw) & fm);
			if (off > 0) {
				n = off;
				while (--n > 0) {
					p += ws;
					q += ws;
					dw = *p;
					sw = (q[0] << lsh) | (q[1] >> rsh);
					*p = ROP(dw, sw);
				}
				p += ws;
				q += ws;
				dw = *p;
				sw = (q[0] << lsh) | (q[1] >> rsh);
				*p = (dw & nsm) | (ROP(dw, sw) & sm);
			}
			d += b_dpit;
			s += b_spit;
		}
	}
}

/*
 * How many of the `n' rows starting at row `y' and running in the direction
 * `up' lie on the same side of the segment split as row y itself.
 */
static
band(y, n, up)
register int y, n, up;
{
	if (up) {
		if (y >= HR_YSPLIT && y - n + 1 < HR_YSPLIT)
			return (y - HR_YSPLIT + 1);
	} else {
		if (y < HR_YSPLIT && y + n > HR_YSPLIT)
			return (HR_YSPLIT - y);
	}
	return (n);
}

void
bit_blit(dst_map, x_dst, y_dst, wide, high, op, src_map, x_src, y_src)
BITMAP *dst_map;
BITMAP *src_map;
int x_dst, y_dst;
int x_src, y_src;
int wide, high;
int op;
{
	register int i;
	register int f;
	int count, n, eng, up;
	int dsc, ssc, dxw, sxw;

	f = op & 0xf;
	if (src_map == BIT_NULL)
		f = nsrc[f];
	else if (zsrc[f])
		src_map = BIT_NULL;

	count = high;

	if (wide < 0) {
		x_dst += wide;
		wide = -wide;
	}
	if (count < 0) {
		y_dst += count;
		count = -count;
	}
	if (x_dst < 0) {
		if (src_map)
			x_src -= x_dst;
		wide += x_dst;
		x_dst = 0;
	}
	if (y_dst < 0) {
		if (src_map)
			y_src -= y_dst;
		count += y_dst;
		y_dst = 0;
	}
	if (src_map) {
		if (x_src < 0) {
			x_dst -= x_src;
			wide += x_src;
			x_src = 0;
		}
		if (y_src < 0) {
			y_dst -= y_src;
			count += y_src;
			y_src = 0;
		}
		if ((i = x_src + wide - src_map->wide) > 0)
			wide -= i;
		if ((i = y_src + count - src_map->high) > 0)
			count -= i;
	}
	if ((i = x_dst + wide - dst_map->wide) > 0)
		wide -= i;
	if ((i = y_dst + count - dst_map->high) > 0)
		count -= i;
	if (wide < 1 || count < 1)
		return;

	x_dst += dst_map->x0;
	y_dst += dst_map->y0;

	dxw = x_dst >> LOGBITS;
	b_nw = ((x_dst + wide - 1) >> LOGBITS) - dxw + 1;
	b_lm = ((DATA) ~0) >> (x_dst & BITS);
	b_rm = ((DATA) ~0) << (BITS - ((x_dst + wide - 1) & BITS));
	if (b_nw < 2) {			/* one word is both edges */
		b_lm &= b_rm;
		b_rm = b_lm;
	}
	b_dpit = BIT_LINE(dst_map);
	dsc = IS_SCREEN(dst_map);

	b_rev = 0;
	up = 0;
	sxw = 0;
	ssc = 0;
	b_spit = 0;
	b_lsh = 0;
	b_rsh = 0;
	if (src_map) {
		x_src += src_map->x0;
		y_src += src_map->y0;
		sxw = x_src >> LOGBITS;
		b_spit = BIT_LINE(src_map);
		ssc = IS_SCREEN(src_map);
		i = (x_src & BITS) - (x_dst & BITS);
		if (i > 0) {
			b_lsh = i;
			b_rsh = BITS + 1 - i;
		} else if (i < 0) {
			b_rsh = -i;
			b_lsh = BITS + 1 + i;
			sxw -= 1;
		}
		if (src_map->data == dst_map->data) {
			if (y_dst > y_src)
				up = 1;
			if (x_dst > x_src)
				b_rev = 1;
		}
	}

	/* r = K ^ (d&A) ^ (s&B) ^ (d&s&C), read off the function's truth table */

	i = f;
	b_K = (i & 1) ? (DATA) ~0 : (DATA) 0;
	b_A = ((i ^ (i >> 1)) & 1) ? (DATA) ~0 : (DATA) 0;
	b_B = ((i ^ (i >> 2)) & 1) ? (DATA) ~0 : (DATA) 0;
	b_C = ((i ^ (i >> 1) ^ (i >> 2) ^ (i >> 3)) & 1) ? (DATA) ~0 : (DATA) 0;

	if (src_map == BIT_NULL) {
		if (b_A && !b_K)		/* function 10: leave alone */
			return;
		eng = 0;
	} else if (f == 0xc)
		eng = 1;
	else
		eng = 2;

	if (up) {
		y_dst += count - 1;
		y_src += count - 1;
		b_dpit = -b_dpit;
		b_spit = -b_spit;
	}

	while (count > 0) {
		n = count;
		if (dsc)
			n = band(y_dst, n, up);
		if (ssc && (i = band(y_src, n, up)) < n)
			n = i;
		b_rows = n;
		b_dp = hraddr(dst_map, y_dst) + dxw;
		if (src_map)
			b_sp = hraddr(src_map, y_src) + sxw;
		if (eng == 0)
			eng_fill();
		else if (eng == 1)
			eng_copy();
		else
			eng_rop();
		if (up) {
			y_dst -= n;
			y_src -= n;
		} else {
			y_dst += n;
			y_src += n;
		}
		count -= n;
	}
}
