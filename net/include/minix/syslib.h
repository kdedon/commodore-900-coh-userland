/*
 * minix/syslib.h -- COHERENT osdep shim.
 *
 * Minix's real syslib.h prototypes the kernel-syscall wrappers (sys_*,
 * _taskcall, and the send/receive/sendrec IPC primitives).  In the
 * COHERENT inet port these are confined to the osdep files (inet.c,
 * clock.c, sr.c, mnx_eth.c), which are being rewritten against the
 * COHERENT kernel; the portable protocol core (generic/*) needs none of
 * them.  This shim is deliberately empty so the shared header chain
 * parses.  The osdep rewrite provides real definitions where required.
 */
#ifndef _SYSLIB_H
#define _SYSLIB_H

#endif /* _SYSLIB_H */
