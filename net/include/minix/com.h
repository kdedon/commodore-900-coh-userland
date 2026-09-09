/*
 * minix/com.h -- COHERENT osdep shim.
 *
 * Minix's real com.h defines the kernel task numbers and message-type
 * constants for its microkernel message-passing IPC.  The COHERENT inet
 * port does NOT use message passing in the portable protocol core
 * (generic/*): send/receive/sendrec appear only in the four osdep files
 * inet.c, clock.c, sr.c and mnx_eth.c, which are being rewritten as a
 * COHERENT daemon.  This shim supplies just the handful of names the
 * shared headers reference so the portable core parses and compiles;
 * the message-passing machinery is intentionally absent.
 */
#ifndef _COM_H
#define _COM_H

/* Special task/process numbers referenced by shared headers. */
#define ANY		0x7ace	/* receive(ANY, ...) wildcard source	*/
#define HARDWARE	(-1)	/* "sender" for hardware interrupts	*/
#define SYSTASK		(-2)	/* system task				*/

/* Interrupt notification message type used by clock/eth glue. */
#define HARD_INT	2

/* Returned by a driver/protocol when a request must block ("suspend") rather
 * than complete now; the inet core tests results against it. */
#define SUSPEND		(-998)

#endif /* _COM_H */
