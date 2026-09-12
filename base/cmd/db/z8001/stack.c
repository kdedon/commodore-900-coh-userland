/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
#include <types.h>
#include <machine.h>
#include <stdio.h>
#include <n.out.h>
#include "trace.h"
#include "z8001.h"

setretf( pcp, fpp)
vaddr_t	*pcp, *fpp;
{
	vaddr_t	sp;
	struct	ldsym	lsym;

	return( gretregs( pcp, fpp, &sp, reg.r_pc, getfp(), getsp(), &lsym));
}

/* print out stack traceback
   for C programs only.
   requires symbol table	*/

dispsbt( lev)
int	lev;
{
   	register vaddr_t pc;
	vaddr_t	fp, sp, npc;
	int	w, n, m;
	struct	ldsym	lsym;

	strcpy( lsym.ls_id, "main_");
	if( nameval( &lsym) == 0) {
		printf( "no symbols\n");
		return;
	}
	pc = reg.r_pc;
	fp = getfp();
	sp = getsp();
	do {
		printf( "sp = %08lx fp = %08lx  ", sp, fp);
		if( gretregs( &npc, &fp, &sp, pc, fp, sp, &lsym) == 0)
			return;
		printf( "pc = %.*s + 0x%04x   ", NCPLN, lsym.ls_id,
			(int)(pc - lsym.ls_addr));
		pc = npc;
		add = pc - 6;
		getb( 1, &w, 2);
		getb( 1, &n, 2);
		getb( 1, &m, 2);
		if( (w & 0xFF0F) == 0x5F00  &&  n & 32768L /* check for jsr */
		    || (n & 0xFF0F) == 0x5F00 && ~m & 32768L
		    || (m & 0xFF0F) == 0x1F00
		    || (m & 0xF000) == 0xD000)
		    	;
		else {
			printf( "improper entry\n");
			return;
		}
		getb( 1, &w, 2);
		getb( 1, &n, 2);
		if( (w & 0xFFF0) == 0xA9F0)		/* get # of args */
			n = (w & 15) + 1;		/* dec	sp, $n */
		else if( w == 0x10F)			/* add	sp, $n */
			;
		else
			n = 0;
		if( n == 0)
			printf( "no args\n");
		else {
			n /= 2;
			m = 0;
			add = sp + 4;
			printf( "\n  args =");
			while( n != 0) {
				m++;
				n--;
				getb( 0, &w, 2);
				printf( " %4x", w);
				if( m % 10 == 0  &&  n != 0)
					printf( "\n         ");
			}
			printf( "\n");
		}
	} while( testint() == 0  &&  --lev != 0
		&&  strcmp( lsym.ls_id, "main_") != 0);
}

gretregs( pcp, fpp, spp, pc, fp, sp, lsymp)
vaddr_t *pcp, *fpp;
register vaddr_t *spp, pc;
vaddr_t fp, sp;
register struct ldsym	*lsymp;
{
	vaddr_t	badd;
	int	w, n, m, a, b;

	if( valname( 1, pc, lsymp) == 0) {
		printf( "can't locate pc\n");
		return( 0);
	}
#if 0
	add = lsymp->ls_addr &= 0x7F00FFFFL;
#else
	*(long *)&lsymp->ls_addr &= 0x7F00FFFFL;
	add = lsymp->ls_addr;
#endif
	if( getb( 1, &w, 2) == 0) {
		printf( "can't read prolog\n");
		return( 0);
	}
	if( w == 0x30F) {			/* sub	sp, $n */
		getb( 1, &w, 2);
	} else if( (w & 0xFFF0) == 0xABF0) {	/* dec	sp, $n */
		w = (w & 15) + 1;
	} else {				/* a leaf that never touches
						   r13 has no frame at all,
						   and no caller to walk to */
		printf( "frameless leaf\n");
		return( 0);
	}
	getb( 1, &n, 2);
	if( n == 0x2FED)			/* ld (rr14), r13 */
		n = 0;
	else if( n == 0x1DEC)			/* ldl (rr14), rr12 */
		n = 2;				/* r12 at the bottom, r13 above */
	else if( n == 0x1CE9) {			/* ldm (rr14), rx, $y */
		getb( 1, &n, 2);
		n = (n & 15) << 1;		/* fp at bottom */
	} else {
		printf( "bad prolog\n");
		return( 0);
	}
	badd = add;				/* addr of last prolog inst */
	add = pc;
	if( getb( 1, &m, 2) == 0) {
		printf( "can't read at pc\n");
		return( 0);
	}
	*fpp = fp;
	if( pc == lsymp->ls_addr || m == 0x9E08) {
		*spp = sp;
		goto done;
	}
	if( pc <= badd)
		goto case2;
	getb( 1, &a, 2);
	getb( 1, &b, 2);
	if( m == 0x10F && b == 0x9E08		/* check for add(inc) sp, $x */
	   ||  (m & 0xFFF0) == 0xA9F0 &&  a == 0x9E08) {
case2:		*spp = sp + w;
		goto done;
	}
	*spp = fp + w;				/* in body of function */
	add = fp + n;
	getb( 0, (int *)fpp + 1, 2);
done:
	add = *spp;
	getb( 0, pcp, 4);
	*pcp &= 0x7F00FFFFL;
	*fpp &= 0x7F00FFFFL;
	return( 1);
}
