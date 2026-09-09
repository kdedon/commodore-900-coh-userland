/*
stacktrace.c

Created:	Jan 19, 1993 by Philip Homburg

Copyright 1995 Philip Homburg
*/

#include "inet.h"

/*
 * The Z8001 frame pointer addresses the BOTTOM of the frame, so nothing a walk
 * wants sits at a fixed offset from it.  A function opens with
 *
 *	DEC R15,#size	or  SUB R15,#size		allocate the frame
 *	LD  @RR14,R13	or  LDL @RR14,RR12		save R13, and any
 *			or  LDM @RR14,Rlo,#count	callee-saved registers
 *							below it, at the bottom
 *	LD  R13,R15					FP = the frame bottom
 *
 * and that prologue fixes the two offsets the walk needs:
 *
 *	FP+saved	caller's frame pointer.  `saved' is where R13 lands in
 *			the saved run, which always ends at R13: 0 for LD, 2 for
 *			LDL (R12 first, then R13), 2*(count-1) for LDM.
 *	FP+size		the caller's stack pointer, holding the four-byte return
 *			address a segmented CALL pushed -- segment word first,
 *			then offset.  Both halves are printed: the daemon's text
 *			spans three segments, so an offset alone does not
 *			identify a caller.
 *	FP+size+4	the first argument.
 *
 * Both offsets are per-function, so every level is decoded from the prologue of
 * the function that frame belongs to.  Only global symbols survive the link and
 * most of this daemon is static, so a function's entry is found by scanning the
 * text back from the return address for the prologue that built the frame.
 *
 * A function that builds no frame presents no prologue to match, and the walk
 * ends there rather than reporting a frame that does not exist.
 *
 * Casting the 16-bit frame pointer back to a pointer is sound because the stack
 * lives in segment 0 -- the compiler addresses locals as `0x00:disp(R13)', and
 * the cast emits a matching `CLR' of the segment half.  Text is reached the long
 * way, through the segment word of the return address, because it does not.
 */

typedef unsigned int word_t;	/* one 16-bit machine word */

#define OP_SUBSP	0x030F	/* SUB  R15,#size   (size in the next word)    */
#define OP_DECSP	0xABF0	/* DEC  R15,#size   (size = (word & 15) + 1)    */
#define OP_LDFP		0x2FED	/* LD   @RR14,R13				*/
#define OP_LDLFP	0x1DEC	/* LDL  @RR14,RR12				*/
#define OP_LDMFP	0x1CE9	/* LDM  @RR14,Rlo,#count (count-1 in next word)	*/
#define OP_SETFP	0xA1FD	/* LD   R13,R15				*/
#define OP_RET		0x9E08	/* RET  T -- the single exit of a function	*/

#define SCAN_MAX	2048	/* words of text a backward scan may cover	*/
#define LEVEL_MAX	32	/* frames printed before giving up		*/

/* Build a far pointer to text from the two halves of a return address. */
PRIVATE word_t *codeptr(seg, off)
word_t seg;
word_t off;
{
	union { long l; word_t *p; } u;

	u.l= ((long)seg << 16) | (long)off;
	return u.p;
}

/*
 * Decode the prologue at a function entry.  Returns 1 with the frame size and
 * the offset of the saved frame pointer, or 0 if there is no frame here.
 */
PRIVATE int prologue(entry, sizep, savedp)
word_t *entry;
word_t *sizep;
word_t *savedp;
{
	word_t w;
	int i;

	i= 1;
	w= entry[0];
	if (w == OP_SUBSP)
	{
		*sizep= entry[1];
		i= 2;
	}
	else if ((w & 0xFFF0) == OP_DECSP)
		*sizep= (w & 15) + 1;
	else
		return 0;

	w= entry[i++];
	if (w == OP_LDFP)
		*savedp= 0;
	else if (w == OP_LDLFP)
		*savedp= 2;
	else if (w == OP_LDMFP)
		*savedp= (entry[i++] & 15) << 1;
	else
		return 0;

	return entry[i] == OP_SETFP;
}

/*
 * Find the entry of the function holding a return address.  Instructions are
 * word aligned, so the scan steps back two bytes at a time until a prologue
 * matches, the segment base is reached, or the scan runs out.
 *
 * The compiler gives a function one exit, so the RET that closes the function
 * before this one is the floor of the scan: reaching it means the function
 * holding the return address opens no frame, and there is no entry to report.
 */
PRIVATE word_t *funcentry(seg, off)
word_t seg;
word_t off;
{
	word_t *p;
	word_t size, saved;
	int n;

	for (n= SCAN_MAX; n > 0 && off >= 2; n--)
	{
		off-= 2;
		p= codeptr(seg, off);
		if (prologue(p, &size, &saved))
			return p;
		if (*p == OP_RET)
			break;
	}
	return (word_t *)0;
}

PUBLIC void stacktrace()
{
	extern word_t get_bp ARGS(( void ));
	void (*self) ARGS(( void ));
	word_t *entry;
	word_t *ret;
	word_t fp, nextfp;
	word_t size, saved;
	word_t pcseg, pcoff;
	int lev;

	/* The first frame is this function's own: get_bp() builds no frame, so
	 * R13 still holds the frame pointer set up here, and the entry that
	 * prologue belongs to is this function's address.
	 */
	self= stacktrace;
	entry= (word_t *)self;
	fp= get_bp();

	for (lev= 0; lev < LEVEL_MAX; lev++)
	{
		if (!prologue(entry, &size, &saved))
			break;
		ret= (word_t *)(fp + size);
		pcseg= ret[0];
		pcoff= ret[1];
		printf("0x%x:0x%x ", pcseg, pcoff);

		nextfp= *(word_t *)(fp + saved);
		if (nextfp <= fp)
		{
			if (nextfp != 0)
				printf("???");
			break;
		}
		entry= funcentry(pcseg, pcoff);
		if (entry == (word_t *)0)
			break;
		fp= nextfp;
	}
	printf("\n");
}

/*
 * $PchId: stacktrace.c,v 1.6 1996/05/07 21:11:34 philip Exp $
 */
