/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
#include "screen.h"

/* Bytes occupied by a wide x high x depth bitmap.  long: the result exceeds
   16 bits for anything larger than 8 KB, which is most of a window. */
long bit_size(wide, high, depth)
int wide;
int high;
unsigned char depth;
{
  return BIT_Size(wide,high,depth);
}
