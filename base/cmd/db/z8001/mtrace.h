/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * A debugger.
 * Machine dependent header for the Zilog 8001.
 */
#define	FORZ8001 1
#define	NOUT	1
#define	I	077777			/* Integer infinity */
#define LI	017777777777L		/* Long infinity */
#define MLI	020000000000L		/* Minus long infinity */

/*
 * Formats.
 */
#define	INLEN	2			/* Size of smallest instruction */
#define	VAWID	8			/* Size of virtual address */
#define	DDCHR	'x'			/* Default debugger format */
#define OAFMT	"0x%x"			/* Offset address format */
#define DAFMT	"%08lx"			/* Display address format */
#define VAFMT	"%08lx"			/* Virtual address */

/*
 * Breakpoint instruction size definition.
 */
typedef char	BIN[2];
#undef u				/* a real kludge for u area conflict */
