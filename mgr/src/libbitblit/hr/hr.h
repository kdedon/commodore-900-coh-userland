/*
 * The C900 hi-res bitmap board, as an MGR output device.
 *
 * 1024 x 800 x 1, MSB of each byte leftmost, big-endian words.  The board
 * decodes physical 0x3E0000 (64 KB) and 0x3F0000 (64 KB); the kernel keeps
 * those two pages mapped at virtual segments 0x3A and 0x3B for the whole
 * time /drv/hrtty is loaded, with an MMU attribute byte of 0 -- no System
 * Only bit -- so an ordinary user process reads and writes them with plain
 * far pointers.
 *
 * A scan line is 128 bytes, so segment 0x3A holds exactly rows 0..511 and
 * segment 0x3B holds rows 512..799.  Bits 16..23 of a Z8001 far pointer are
 * a hole, so adding 0x10000 to a SEG0 pointer does NOT arrive at SEG1:
 * every row address comes from hrrow(), never from arithmetic that crosses
 * the split.
 *
 * HRWORD and the two segment bases are overridable so that the blitter can
 * be run against ordinary host memory by a regression test.
 */

#ifndef HRWORD
#define	HRWORD		unsigned	/* frame word: 16 bits on the Z8001 */
#endif

#ifndef HR_SEG0
#define	HR_SEG0		((HRWORD *)0x3a000000L)   /* rows 0..511 */
#define	HR_SEG1		((HRWORD *)0x3b000000L)   /* rows 512..799 */
#endif

#define	HR_XMAX		1024		/* pixels per scan line */
#define	HR_YMAX		800		/* scan lines */
#define	HR_YSPLIT	512		/* first row living in HR_SEG1 */
#define	HR_WPERSL	(HR_XMAX/16)	/* words per scan line */
#define	HR_BPERSL	(HR_XMAX/8)	/* bytes per scan line */

extern HRWORD *hrrow();			/* hrrow(y) -> first word of row y */
