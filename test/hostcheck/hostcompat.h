/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * hostcompat.h -- force-included into every test source built here, so that
 * the source itself needs no edit and no -D.
 *
 * Two incompatibilities, and nothing else about the sources is adjusted.
 *
 * errno.  These programs say `extern int errno;', which is right on COHERENT
 * and right in K&R, but glibc's errno is thread-local and the linker rejects a
 * non-TLS reference to it.  Redefining the name turns both the declaration and
 * every use into the glibc form:
 *
 *	extern int errno;	->  extern int (*__errno_location());
 *	errno			->  (*__errno_location())
 *
 * COHERENT errno values that the host does not have.  tests/lptest tests for
 * EDATTN specifically, and tests/pty for EDBUSY and EKSPACE; the shim raises
 * exactly these numbers, so the two ends agree even though Linux has no such
 * codes.  The numbers are include/errno.h's.
 */
#ifndef HOSTCOMPAT_H
#define HOSTCOMPAT_H
#define _GNU_SOURCE 1
#include <errno.h>
extern int *__errno_location();
#undef errno
#define errno (*__errno_location())

#ifndef EDATTN
#define EDATTN	38		/* include/errno.h:54, device needs attention */
#endif
#ifndef EDBUSY
#define EDBUSY	39		/* errno.h:56, device busy */
#endif
#ifndef EKSPACE
#define EKSPACE	35		/* errno.h:48, out of kernel space */
#endif

/*
 * INFTIM is COHERENT's name for "no timeout" in poll(2); glibc has no such
 * macro, and tests/pollexit passes it by name.
 */
#ifndef INFTIM
#define INFTIM	(-1)
#endif

#endif
