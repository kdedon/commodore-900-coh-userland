/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * termio.h -- the TARGET's <termio.h>, trimmed, so that tests/termio/termio.c
 * compiles here verbatim against the definitions it will really meet.
 *
 * The host has a <termio.h> of its own and it is the WRONG one for this test.
 * On COHERENT (include/termio.h:145-155) VEOL and VTIME are the same c_cc
 * slot and VEOF and VMIN are the same slot -- that aliasing is the whole point
 * of the round-trip check, because it is the pair an sgttyb-backed driver
 * loses.  glibc puts VEOL at 11 and VTIME at 5, which are different slots, so
 * building against the host header would quietly test a different claim.
 *
 * Host-only scaffolding.  Nothing here ships and nothing here is compiled for
 * the C900; the target build uses include/termio.h.
 */
#ifndef HOSTCHECK_TERMIO_H
#define HOSTCHECK_TERMIO_H

#define	NCC	8

struct termio {
	unsigned short	c_iflag;
	unsigned short	c_oflag;
	unsigned short	c_cflag;
	unsigned short	c_lflag;
	char		c_line;
	unsigned char	c_cc[NCC];
};

/* c_iflag */
#define	IGNBRK	0x0001
#define	BRKINT	0x0002
#define	IGNPAR	0x0004
#define	INPCK	0x0010
#define	ISTRIP	0x0020
#define	INLCR	0x0040
#define	IGNCR	0x0080
#define	ICRNL	0x0100
#define	IUCLC	0x0200
#define	IXON	0x0400
#define	IXANY	0x0800
#define	IXOFF	0x1000

/* c_oflag */
#define	OPOST	0x0001
#define	ONLCR	0x0004

/* c_lflag */
#define	ISIG	0x0001
#define	ICANON	0x0002
#define	XCASE	0x0004
#undef	ECHO
#define	ECHO	0x0008
#define	ECHOE	0x0010
#define	ECHOK	0x0020
#define	ECHONL	0x0040
#define	NOFLSH	0x0080

/* Offsets into c_cc.  VEOL is VTIME's slot and VEOF is VMIN's: see above. */
#define	VINTR	0
#define	VQUIT	1
#define	VERASE	2
#define	VKILL	3
#define	VEOF	4
#define	VEOL	5
#define	VEOL2	6
#define	VMIN	4
#define	VTIME	5
#define	VSWTCH	6

/* The ioctls.  The numbers need only agree with hostcheck's own shim. */
#undef	TCGETA
#undef	TCSETA
#undef	TCSETAW
#undef	TCSETAF
#define	TCGETA	(('T'<<8)|1)
#define	TCSETA	(('T'<<8)|2)
#define	TCSETAW	(('T'<<8)|3)
#define	TCSETAF	(('T'<<8)|4)

#endif
