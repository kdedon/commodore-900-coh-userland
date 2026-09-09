#include "sim.h"

#include <sys/types.h>
#include <stdlib.h>

#define SIM_RAND_MAX 0xffff

static unsigned QUAD next = 1;

/* Discard the low eight bits of the linear congruential state, which cycle
 * far too regularly, and keep the sixteen above them.  The modulus is formed
 * in QUAD: SIM_RAND_MAX + 1 does not fit an int here.  The result is the full
 * unsigned 16-bit range, so a caller wanting a signed value converts it
 * itself (Rand16Signed). */
int
sim_rand()
{
	next = next * 1103515245 + 12345;
	return ((int)((next % (((unsigned QUAD)SIM_RAND_MAX + 1) << 8)) >> 8));
}

void
sim_srand(seed)
unsigned int seed;
{
	next = seed;
}
