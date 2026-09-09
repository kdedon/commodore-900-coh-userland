/*
 * hostcompat.h -- force-included into every target source built here, so that
 * the source itself needs no edit.
 *
 * The one incompatibility is errno.  These programs say `extern int errno;',
 * which is right on COHERENT and right in K&R, but glibc's errno is
 * thread-local and the linker rejects a non-TLS reference to it.  Redefining
 * the name turns both the declaration and every use into the glibc form:
 *
 *	extern int errno;	->  extern int (*__errno_location());
 *	errno			->  (*__errno_location())
 *
 * so the declaration becomes the correct prototype and the uses read the real
 * errno.  Nothing else about the sources is adjusted.
 */
#ifndef HOSTCOMPAT_H
#define HOSTCOMPAT_H
#define _GNU_SOURCE 1
#include <errno.h>
extern int *__errno_location();
#undef errno
#define errno (*__errno_location())

/*
 * The fixed-width names the stack's headers use.  On the target they come from
 * <sys/types.h> (include/sys/types.h); the host's has no such thing, and
 * net/gen/*.h are read here for the NWIO* ioctl numbers and struct layouts.
 * Same widths, so the same declarations.
 */
typedef unsigned char	u8_t;
typedef unsigned short	u16_t;
typedef unsigned int	u32_t;
typedef int		U8_t;
typedef unsigned int	U16_t;
typedef unsigned int	U32_t;
#endif
