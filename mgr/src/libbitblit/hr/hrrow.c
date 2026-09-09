/*
 * Row addressing for the HR bitmap.
 */
#include "hr.h"

/*
 * Return a pointer to the first word of scan line `y'.
 *
 * The multiply is done in `unsigned' on purpose: row 511 is at word offset
 * 32704, and cc1 scales the index by sizeof(unsigned) to a byte offset of
 * 65408 -- which an int cannot hold.  Each branch stays inside one 64 KB
 * segment, so neither product ever needs the segment number to change.
 */
HRWORD *
hrrow(y)
register int y;
{
	if (y < HR_YSPLIT)
		return (HR_SEG0 + (unsigned)y * HR_WPERSL);
	return (HR_SEG1 + (unsigned)(y - HR_YSPLIT) * HR_WPERSL);
}
