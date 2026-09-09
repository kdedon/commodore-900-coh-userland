/*
inet/coh_osdep.c -- small COHERENT/Z8001 gap-fills the inet daemon needs that
the current libc-z8001 / machine layer does not yet provide:

  memmove  -- overlap-safe copy (belongs in libc-z8001; kept here until it
	      migrates there so every program gets it).

get_bp reads the frame-base register in get_bp.s -- it has to be assembly,
because a C function cannot see its own caller's frame pointer.
*/

#include "inet.h"

PUBLIC char *memmove(dst, src, n)
char *dst;
char *src;
unsigned n;
{
	char *d= dst;

	if (d < src)
	{
		while (n--)
			*d++ = *src++;
	}
	else if (d > src)
	{
		d += n;
		src += n;
		while (n--)
			*--d = *--src;
	}
	return dst;
}
