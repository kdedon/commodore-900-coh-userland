/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * sgtty.h -- the TARGET's <sgtty.h>, trimmed, so that tests/pty/pty.c compiles
 * here verbatim against the definitions it will really meet.
 *
 * The host's own <sgtty.h> declares gtty()/stty() and then leaves `struct
 * sgttyb' an incomplete type on this libc, and has no ECHO of its own -- so a
 * program written for COHERENT's sgtty interface does not compile against it at
 * all.  The layout and the flag bits below are include/sgtty.h's, which is
 * what the shim's gtty()/stty() answer with; the same reasoning as termio.h
 * here, where building against the host header would quietly test a different
 * claim.
 *
 * Host-only scaffolding.  Nothing here ships and nothing here is compiled for
 * the C900; the target build uses include/sgtty.h.
 */
#ifndef HOSTCHECK_SGTTY_H
#define HOSTCHECK_SGTTY_H

struct sgttyb {
	char	sg_ispeed;		/* Input speed */
	char	sg_ospeed;		/* Output speed */
	char	sg_erase;		/* Character erase */
	char	sg_kill;		/* Line kill character */
	int	sg_flags;		/* Flags */
};

/* stty/gtty modes. */
#define	XTABS	0x0002		/* Expand tabs to spaces */
#define	LCASE	0x0004		/* Lowercase mapping on input */
#undef	ECHO
#define	ECHO	0x0008		/* Echo input characters */
#undef	CRMOD
#define	CRMOD	0x0010		/* Map '\r' to '\n' */
#undef	RAW
#define	RAW	0x0020
#define	ODDP	0x0040		/* Allow odd parity */
#define	EVENP	0x0080		/* Allow even parity */
#define	ANYP	0x00C0		/* Allow any parity */

extern int gtty();
extern int stty();

#endif
