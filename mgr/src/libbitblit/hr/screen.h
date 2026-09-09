/*
 * screen.h -- the libbitblit device header for the C900 hi-res bitmap board,
 * in the shape of libbitblit/sunmono/sun.h.  Every libbitblit source and
 * every tool that handles external bitmaps includes it as "screen.h".
 *
 * The frame word is 16 bits because that is the Z8001's natural word and the
 * board is big-endian MSB-left, so MGR's own external ("68020 Sun-3 mono")
 * bitmap format is byte-identical to HR video RAM: no flip on load.
 */

#include "hr.h"

#ifndef DATA
#define DATA HRWORD		/* frame buffer word: 16 bits on the Z8001 */
#endif

#include <mgr/bitblit.h>

extern DATA *graph_mem;
extern DATA *hraddr();		/* hraddr(map,y) -> first word of row y */

#define LOGBITS 4
#define BITS (~(~(DATA)0<<LOGBITS))

#define bit_linesize(wide,depth) ((((depth)*(wide)+BITS)&~BITS)>>3)

/*
 * A bitmap's byte count is long.  `wide' and `high' are int, and the product
 * of a padded row width by a row count leaves 16 bits at any useful window
 * size: an 80x24 window in cmr-9x16 is 730x394, whose 289984 bits wrap to
 * 27840 and yield 3480 bytes for a 36248-byte bitmap.  BITS is unsigned, so
 * the wrap is silent and positive -- an allocation that succeeds one tenth
 * the size asked for.  The long starts at the first multiply.
 */
#define BIT_SIZE(m) BIT_Size(BIT_WIDE(m), BIT_HIGH(m), BIT_DEPTH(m))
#define BIT_Size(wide,high,depth) \
	(((((long)(depth)*(wide)+BITS)&~(long)BITS)*(long)(high))>>3)
#define BIT_LINE(x) ((((x)->primary->depth*(x)->primary->wide+BITS)&~BITS)>>LOGBITS)

extern void display_close();
