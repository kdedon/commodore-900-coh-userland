/*
inet/inet.h

Created:	Dec 30, 1991 by Philip Homburg

Copyright 1995 Philip Homburg
*/

#ifndef INET__INET_H
#define INET__INET_H

#define _SYSTEM	1	/* get OK and negative error codes */

#include <ansi.h>

/*
 * _EM_WSIZE is an Amsterdam Compiler Kit builtin -- the word size ACK
 * compiles for.  The MWC compiler does not define it, so it must be set
 * here or CRAMPED comes out FALSE on the most cramped target this stack
 * supports: int is 16 bits and a segment is 64K.
 */
#ifndef	_EM_WSIZE
#define	_EM_WSIZE	2	/* MWC cc: int and word are 2 bytes */
#endif

#define CRAMPED (_EM_WSIZE==2)	/* 64K code and data is quite cramped. */
#define ZERO 0	/* Used to comment out initialization code that does nothing. */

#include <sys/types.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <minix/config.h>
#include <minix/type.h>
#include <minix/const.h>
#include <minix/com.h>
#include <minix/syslib.h>
#include <net/hton.h>
#include <net/gen/ether.h>
#include <net/gen/eth_hdr.h>
#include <net/gen/eth_io.h>
#include <net/gen/in.h>
#include <net/gen/ip_hdr.h>
#include <net/gen/ip_io.h>
#include <net/gen/icmp.h>
#include <net/gen/icmp_hdr.h>
#include <net/gen/oneCsum.h>
#include <net/gen/psip_hdr.h>
#include <net/gen/psip_io.h>
#include <net/gen/route.h>
#include <net/gen/tcp.h>
#include <net/gen/tcp_hdr.h>
#include <net/gen/tcp_io.h>
#include <net/gen/udp.h>
#include <net/gen/udp_hdr.h>
#include <net/gen/udp_io.h>
#include <net/ioctl.h>

#include "const.h"
#include "inet_config.h"

#define PUBLIC
#define EXTERN	extern
#define PRIVATE	static
#define FORWARD	static

typedef int ioreq_t;

/* alloc() -- the daemon's table allocator (inet_config.c, an sbrk wrapper).
 *
 * DECLARED HERE, not only in inet_config.h, because the seven generic/*.c files
 * that build their port tables with it -- icmp, ip, tcp, udp, eth, arp, psip --
 * include this header and none of them includes inet_config.h.  Undeclared, K&R
 * makes it return int, which is 16 bits on the Z8001 while a pointer is a 32-bit
 * far pointer: every port table then lost its segment and pointed somewhere
 * wild.  The symptom was the icmp state machine reading its state as 27680
 * (0x6C20, ASCII "l " -- string data) and the daemon dying on the first packet
 * that reached that state.  On Minix the same missing declaration is harmless
 * only because int and pointer are both 32 bits there.
 */
char *alloc();

#define THIS_FILE static char *this_file= __FILE__;

#if CRAMPED

/* Minimum panic info. */
#define ip_panic(print_list)  panic(this_file, __LINE__)
_PROTOTYPE( void panic, (char *file, int line) );

#else /* !CRAMPED */

/* Maximum panic info. */
#define ip_panic(print_list)  \
	(panic0(this_file, __LINE__), printf print_list, panic())
_PROTOTYPE( void panic0, (char *file, int line) );
_PROTOTYPE( void panic, (void) );

#endif /* !CRAMPED */

#if DEBUG
#define ip_warning(print_list)  \
	( \
		printf("warning at %s, %d: ", this_file, __LINE__), \
		printf print_list, \
		printf("\ninet stacktrace: "), \
		stacktrace() \
	)

#define DBLOCK(level, code) \
	do { if ((level) & DEBUG) { where(); code; } } while(0)
#define DIFBLOCK(level, condition, code) \
	do { if (((level) & DEBUG) && (condition)) \
		{ where(); code; } } while(0)

#else /* !DEBUG */
#define ip_warning(print_list)	0
#define DBLOCK(level, code)	0
#define DIFBLOCK(level, condition, code)	0
#endif

/*
 * NWMARK: a single console character, for bring-up under the emulator only.
 * Defined away unless the build asks for it (EXTRA_NETDEFS=-DNWTRACE), so the
 * shipped daemon carries neither the calls nor the text.
 */
#ifdef NWTRACE
void nw_mark();		/* K&R: ARGS() is not defined until below */
#define NWMARK(c)	nw_mark(c)
#else
#define NWMARK(c)	0
#endif

#define ARGS(x) _ARGS(x)

extern char version[];
extern int this_proc, synal_tasknr;

void stacktrace ARGS(( void ));

#endif /* INET__INET_H */

/*
 * $PchId: inet.h,v 1.8 1996/05/07 21:05:04 philip Exp $
 */
