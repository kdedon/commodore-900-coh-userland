#include <sys/param.h>

/* getdtablesize -- NUFILE descriptors per process (param.h NOFILE). */
int
getdtablesize()
{
	return NOFILE;
}
