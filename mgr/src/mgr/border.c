/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*
 * draw the border around this window.             broman@nosc.mil, 1996/03
 */

#include <mgr/bitblit.h>
#include "defs.h"


#define ONE_BOX( bm, x, y, w, h, b, op)                                   \
/* top, right, bottom, left */                                            \
bit_blit( bm, (x),         (y),         (w), (b),         (op), (BITMAP *)0, 0, 0); \
bit_blit( bm, (x)+(w)-(b), (y)+(b),     (b), (h)-(b)-(b), (op), (BITMAP *)0, 0, 0); \
bit_blit( bm, (x),         (y)+(h)-(b), (w), (b),         (op), (BITMAP *)0, 0, 0); \
bit_blit( bm, (x),         (y)+(b),     (b), (h)-(b)-(b), (op), (BITMAP *)0, 0, 0);


void border(win, be_fat)
WINDOW *win;
int be_fat;
{
    int both = win->borderwid;
    int out = (be_fat==BORDER_FAT)? both - 1: win->outborderwid;
    int inr = both - out;

    int clr = PUTOP(BIT_CLR,W(style));
    int set = PUTOP(BIT_SET,W(style));
    /* An inactive window's border is drawn in its saved image; one without a
       saved image is still on the screen, so draw it there. */
    BITMAP *bdr = (W(flags)&W_ACTIVE || W(save)==(BITMAP *)0)?
			W(border): W(save);
    int w, h;

    if( bdr == (BITMAP *) 0)  return;
    w = BIT_WIDE(bdr);
    h = BIT_HIGH(bdr);

    if( both <= 0)  return;

    ONE_BOX( bdr, 0,   0,   w,         h,         out, set);
    ONE_BOX( bdr, out, out, w-out-out, h-out-out, inr, clr);
}
