/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/* config.h -- COHERENT/Z8001 configuration for patch.
 *
 * Hand-written in place of the Configure-generated file: the target is
 * cross-compiled, so Configure's probes cannot run.
 */

/* The C library spells the string-search routines strchr/strrchr. */
#define	index	strchr
#define	rindex	strrchr

/* signal.h declares signal() as returning a pointer to a void function. */
#define	VOIDSIG

/* unistd.h is present. */
#define	HAVE_UNISTD_H

/* Filenames are longer than 14 characters, so ".orig" and ".rej" fit. */
#define	FLEXFILENAMES

/* Backup files carry a fixed suffix; no directory scan for numbered names. */
#define	NODIR

/* The compiler keeps two register variables per function. */
#define Reg1 register
#define Reg2 register
#define Reg3
#define Reg4
#define Reg5
#define Reg6
#define Reg7
#define Reg8
#define Reg9
#define Reg10
#define Reg11
#define Reg12
#define Reg13
#define Reg14
#define Reg15
#define Reg16

/* void is declarable; arrays of pointers to void functions are not used. */
#ifndef VOIDUSED
#define VOIDUSED 1
#endif
#define VOIDFLAGS 1
#if (VOIDFLAGS & VOIDUSED) != VOIDUSED
#define void int
#define M_VOID
#endif
