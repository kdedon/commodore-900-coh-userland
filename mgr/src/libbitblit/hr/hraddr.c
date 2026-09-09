/*
 * Row addressing for MGR bitmaps on the HR board.
 */
#include "screen.h"

/*
 * First word of row `y' of `map', y counted in the coordinates of the
 * primary bitmap.  Screen rows come from hrrow(), which knows that rows
 * 0..511 are in segment 0x3A and 512..799 in 0x3B; memory rows are linear.
 *
 * The word index is formed unsigned: a 512-row 64-word bitmap reaches word
 * 32704, whose byte offset of 65408 does not fit in an int.
 */
DATA *
hraddr(map, y)
register BITMAP *map;
register int y;
{
	if (IS_SCREEN(map))
		return (hrrow(y));
	return (map->data + (unsigned)y * (unsigned)BIT_LINE(map));
}
