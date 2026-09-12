/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*
 * gettimeofday -- from times(2), the only clock with sub-second resolution
 * here (HZ = 100, so the answer is 10 ms granular).  Derived from Harry
 * Pulley's COHERENT 4.0 emulation in src/libcoh, with the tbuffer made real:
 * the kernel's utimes() copies 16 bytes to whatever pointer it is given,
 * unconditionally (sys/coh/sys1.c), so times(0) is a wild write.
 */
#include <sys/times.h>
#include <sys/time.h>

int
gettimeofday(buf, tz)
struct timeval *buf;
char *tz;
{
	struct tbuffer tb;
	long t;

	t = times(&tb);
	buf->tv_sec = t / 100;
	buf->tv_usec = (t % 100) * 10000L;
	return 0;
}
