/*
 * A debugger.
 * Z8001.
 */
#include <stdio.h>
#include <types.h>
#include <machine.h>
#include <n.out.h>
#include <signal.h>
#include <uproc.h>
#include "trace.h"
#include "z8001.h"

long
addr( num)
register long num;
{
	register unsigned int low;

	low = num & 65535L;
#if PDP11
	num = (num << 8) & 0xFF000000L;
#else
	num <<= 8;
	num &= 0xff000000L;
#endif
	return( num | low);
}

long
size( maddr)
register long maddr;
{
	register unsigned int low;

	low = maddr & 65535L;
#if PDP11
	maddr = (maddr >> 8) & 0xFF0000L;
#else
	maddr >>= 8;
	maddr &= 0xff0000L;
#endif
	return( maddr | low);
}

MAP *
smapl( next, start, length, offs)
MAP	*next;
fsize_t	start, length, offs;
{
	return( setsmap( next, addr( start), addr( start+length) - addr( start),
			offs, getf, putf, 0));
}

/*
 * Given an l.out header, set up segmentation for an l.out.
 */
setaseg(ldhp)
register struct ldheader *ldhp;
{
	register long fbase;
	register long vbase;
	register long si;
	register long pi;
	register long bi;
	register long sd;
	register long pd;

	vbase = (ldhp->l_flag&LF_KER) ? 0x3E0000L : 0x30000L;
	if( hflag)
		vbase = 0x300000L;
	if( ~ldhp->l_flag & LF_NRB)
		vbase = 0;
	slflag = ldhp->l_flag & LF_SLREF;
	fbase = ldhp->l_tbase;
	si = ldhp->l_ssize[L_SHRI];
	pi = ldhp->l_ssize[L_PRVI];
	bi = ldhp->l_ssize[L_BSSI];
	sd = ldhp->l_ssize[L_SHRD];
	pd = ldhp->l_ssize[L_PRVD];
	switch( ldhp->l_flag & (LF_SEP|LF_SHR)) {
	case 0:
		endpure = lshrseg();
		DSPACE = smapl( lpriseg( endpure), vbase, si+pi, fbase);
		DSPACE = smapl( DSPACE, vbase+si+pi+bi, sd+pd, fbase+si+pi);
		ISPACE = DSPACE;
		break;
	case LF_SHR:
		DSPACE = smapl( lshrseg(), vbase, si, fbase);
		DSPACE = smapl( DSPACE, vbase+si, sd, fbase+si+pi);
		endpure = DSPACE;
		vbase = bound( vbase+si+sd);
		DSPACE = smapl( lpriseg( DSPACE), vbase, pi, fbase+si);
		DSPACE = smapl( DSPACE, vbase+pi, pd, fbase+si+pi+sd);
		ISPACE = DSPACE;
		break;
	case LF_SEP:
		endpure = lshrseg();
		ISPACE = smapl( endpure, vbase, si+pi, fbase);
		vbase = bound( vbase+si+pi+bi);
		DSPACE = smapl( lpriseg( NULL), vbase, sd+pd, fbase+si+pi);
		break;
	case LF_SHR|LF_SEP:
		ISPACE = smapl( lpriseg(NULL), vbase, si, fbase);
		ISPACE = smapl( ISPACE, bound( vbase+si), pi, fbase+si);
		vbase = bound( vbase+si+bi+pi);
		DSPACE = smapl( NULL, vbase, sd, fbase+si+pi);
		endpure = DSPACE;
		DSPACE = smapl( lpriseg(DSPACE), bound( vbase+sd),
				pd, fbase+si+pi+sd);
		break;
	}
}

/*
 * Set up segmentation for kernel memory.
 */
setkmem(np)
char *np;
{
	register int n;
	struct ldsym lds;

	cfp = openfil(np, rflag);
	USPACE = setsmap(NULL, (fsize_t)0, (fsize_t)LI, (fsize_t)0,
		getf, putf, 1);
	ISPACE = setsmap(NULL, (fsize_t)0, (fsize_t)LI, (fsize_t)0,
		getf, putf, 1);
	strncpy(lds.ls_id, "etext_", NCPLN);
	if (nameval(&lds) == 0)
		DSPACE = ISPACE;
	else {
		n = (lds.ls_addr+0xFF) & ~0xFF;
		DSPACE = setsmap(NULL, (fsize_t)0, (fsize_t)LI, (fsize_t)n,
			getf, putf, 1);
	}
}

/*
 * Set up segmentation for a system dump.
 */
setdump(np)
char *np;
{
	setkmem(np);
}

/*
 * Update register structure in memory.
 */
setregs()
{
	struct ureg ureg;

	regflag = 0;
	add = UREGOFF;
	if (getb(2, (char *)&ureg, sizeof(ureg)) == 0) {
		printr("Cannot read registers");
		return (0);
	}
	copyreg(&ureg);
	regflag = 1;
	return (1);
}

/*
 * Copy registers from the User area structure to the register
 * structure.
 */
copyreg(up)
register struct ureg *up;
{
	reg.r_rn[R0] = up->ur_r0;
	reg.r_rn[R1] = up->ur_r1;
	reg.r_rn[R2] = up->ur_r2;
	reg.r_rn[R3] = up->ur_r3;
	reg.r_rn[R4] = up->ur_r4;
	reg.r_rn[R5] = up->ur_r5;
	reg.r_rn[R6] = up->ur_r6;
	reg.r_rn[R7] = up->ur_r7;
	reg.r_rn[R8] = up->ur_r8;
	reg.r_rn[R9] = up->ur_r9;
	reg.r_rn[R10] = up->ur_r10;
	reg.r_rn[R11] = up->ur_r11;
	reg.r_rn[R12] = up->ur_r12;
	reg.r_rn[R13] = up->ur_r13;
	reg.r_rn[R14] = up->ur_r14 & 0x7f00;
	reg.r_rn[R15] = up->ur_r15;
	reg.r_pc = up->ur_pc & 0x7f00ffffL;
	reg.r_fcw = up->ur_fcw;
}

/*
 * If the given name matches a register, set up the value `vp' with
 * the address of a registers in the user area.
 */
regaddr(ldp)
struct ldsym *ldp;
{
	register char *cp;
	register int c;
	register int n;
	register int m;
	register int o;

	if (regflag == 0)
		return (0);
	cp = ldp->ls_id;
	if (strncmp(cp, "pc", NCPLN) == 0) {
		ldp->ls_addr = UREGOFF + offset(ureg, ur_pc);
		goto ret;
	}
	if (*cp++ != 'r')
		return (0);
	o = 0;
	m = 0xF;
	switch (*cp++) {
	case 'l':
		o = 1;
	case 'h':
		m = 0x7;
		break;
	case 'r':
		m = 0xE;
		break;
	default:
		--cp;
	}
	c = *cp++;
	if (c<'0' || c>'9')
		return (0);
	if ((n=c-'0') == 1) {
		if ((c=*cp++)>='0' && c<='5')
			n = n*10 + c-'0';
		else
			--cp;
	}
	if (*cp != '\0')
		return (0);
	if ((n&~m) != 0)
		return (0);
	ldp->ls_addr = UREGOFF + rintab[n] + o;
ret:
	ldp->ls_type = L_REG;
	return (1);
}

/*
 * Return the program counter.
 */
vaddr_t
getpc()
{
	return (reg.r_pc);
}

/*
 * Set the program counter.
 */
setpc(pc)
vaddr_t pc;
{
	reg.r_pc = pc;
	add = UREGOFF + offset(ureg, ur_pc);
	putb(2, (char *)&reg.r_pc, sizeof(reg.r_pc));
}

/*
 * Return the frame pointer.
 */
vaddr_t
getfp()
{
	return( ( ( (long)reg.r_rn[14] & 0x7F00) << 16) | reg.r_rn[13]);
}

vaddr_t
getsp()
{
	return( ( ( (long)reg.r_rn[14] & 0x7F00) << 16) | reg.r_rn[15]);
}

/*
 * Display registers.
 */
dispreg()
{
	register int i;

	printn("pc =%08X fcw=%04x\n", reg.r_pc, reg.r_fcw);
	for (i=0; i<16; i++) {
		if ((i%4) == 0)
			if (testint())
				return;
		printn("r%02d=%04x%s", i, reg.r_rn[i], (i%4)==3 ? "\n" : " ");
	}
}

/*
 * Get the return pc and frame pointer to set a return breakpoint.
 */
#if 0
setretf(pcp, fpp)
register vaddr_t *pcp;
register vaddr_t *fpp;
{
	register short *spcp;

	add = reg.r_pc;
	if (getb(0, pcp, sizeof(*pcp)) == 0)
		return (0);
	/*
	 * Function entry is dec r15, $x or sub r15, $x
	 */
	if (*spcp==0x030F  ||  (*spcp & 0xFFF0) == 0xABF0) {
		*fpp = getfp();
		add = reg.r_rn[R15];
	} else {
		add = reg.r_rn[R14] + 12;
		if (getb(0, (char *)fpp, sizeof(*fpp)) == 0)
			return (0);
	}
	if (getb(0, (char *)pcp, sizeof(*pcp)) == 0)
		return (0);
}
#endif

/*
 * Initialise after getting a trap.
 */
trapint()
{
	vaddr_t pc;

	cacsegn = -1;
	if (sysflag) {
		add = UREGOFF + offset(ureg, ur_pc);
		if (getb(2, (char *)&pc, sizeof(pc)) == 0) {
			printk("Cannot get pc");
			return (0);
		}
		pc -= 2;
		add = UREGOFF + offset(ureg, ur_pc);
		if (putb(2, (char *)&pc, sizeof(pc)) == 0) {
			printk("Cannot set pc");
			return (0);
		}
		add = (long)pc & 0x7F00FFFFL;
		if (putb(1, (char *)sin, sizeof(sin)) == 0) {
			printk("Cannot set sin");
			return (0);
		}
		bitflag = 1;
	}
	return (1);
}

/*
 * Set up before returning from a trap.  Single stepping over a system
 * call is handled here.
 */
restret()
{
	register int n;
	unsigned w;

	sysflag = 0;
	if (bitflag == 0)
		return (1);
	add = reg.r_pc;
	if (getb(1, (char *)&w, sizeof(w)) == 0) {
		printk("Cannot get sin");
		return (0);
	}
	if ((n=special(w)) == 0)
		return (1);
	add = reg.r_pc + n;
	if (getb(1, (char *)sin, sizeof(sin)) == 0) {
		printk("Cannot get breakpoint");
		return (0);
	}
	add -= sizeof(sin);
	if (putb(1, (char *)bin, sizeof(bin)) == 0) {
		printk("Cannot set breakpoint");
		return (0);
	}
	bitflag = 0;
	sysflag = 1;
	return (1);
}

/*
 * Set up breakpoint for single step continue.
 */
setcont()
{
	unsigned w;

	add = reg.r_pc;
	if (getb(1, (char *)&w, sizeof(w)) != 0) {
		if (w == ICALL)
			sinmode = SCSET;
	}
}

/*
 * Continue after initial break in single step continue.
 */
intcont()
{
	unsigned a;

	add = reg.r_rn[R15];
	if (getb(0, (char *)&a, sizeof(a)))
		setibpt(BSIN, a, getfp(), NULL);
}

/*
 * See if the given instruction is a breakpoint.
 */
testbpt(pc)
vaddr_t pc;
{
	unsigned w;

	add = pc;
	return (getb(1, (char *)&w, sizeof(w))!=0 && w==ISBPT);
}

/*
 * Read `n' characters into the buffer `bp' starting at address `a'
 * from the traced process.
 */
getp(f, a, bp, n)
register long a;
register char *bp;
register int n;
{
	while (n--) {
		if (getq(f, a++, bp++) == 0)
			return (0);
	}
	return (1);
}

/*
 * Write `n' characters from the buffer `bp' starting at address `a' in
 * the traced process.
 */
putp(f, a, bp, n)
register long a;
register char *bp;
register int n;
{
	extern int errno;
	int d;

	if (n!=0 && (a&1)!=0) {
		if (getq(f, --a, (char *)&d) == 0)
			return (0);
		((char *)&d)[1] = *bp++;
		ptrace(4+f, pid, (int *) a, d);
		if (errno)
			return (0);
		a += 2;
		--n;
	}
	while (n > 1) {
		((char *)&d)[0] = *bp++;
		((char *)&d)[1] = *bp++;
		ptrace(4+f, pid, (int *) a, d);
		if (errno)
			return (0);
		a += 2;
		n -= 2;
	}
	if (n) {
		((char *)&d)[0] = *bp++;
		if (getq(f, a+1, &((char *)&d)[1]) == 0)
			return (0);
		ptrace(4+f, pid, (int *)a, d);
		if (errno)
			return (0);
	}
	return (1);
}

/*
 * Return the byte at address `a' in segment `f' indirectly through
 * the byte pointer `bp'.
 */
getq(f, a, bp)
register long a;
register char *bp;
{
	extern int errno;
	register long w;

	if ((w=(a&~1))!=cacaddr || f!=cacsegn) {
		errno = 0;
		cacdata = ptrace(1+f, pid, (int *) w, 0);
		if (errno) {
			cacsegn = -1;
			return (0);
		}
		cacsegn = f;
		cacaddr = w;
	}
#if 1
	*bp = a&1 ? *((char *)&cacdata + 1) : *((char *)&cacdata);
#else
	*bp = ((char *)&cacdata)[a&1];
#endif
	return (1);
}
/*
 * See if the given instruction is one that we can't single step and
 * if it is, return the size in bytes of the instruction.
 */
special(i1)
register unsigned i1;
{
	register unsigned i2;

	if ((i1&0xFF00) == 0x7F00)		/* SYS */
		return (2);
	if ((i1&0xFE01) == 0xBA00)		/* CPS */
		return (4);
	if ((i1&0xFF01) == 0x6800)
		return (4);
	i2 = i1 & 0xFE07;
	if ((i2&0xFFFC) == 0x3A00)
		return (4);
	if (i2==0x3A08 || i2==0xBA01)
		return (4);
	return (0);
}

#if 0
cohfix()
{
	if( ISPACE != DSPACE)
		cohfixl( ISPACE);
	cohfixl( DSPACE);
}

cohfixl( mp)
register MAP *mp;
{
	for( ; mp != NULL; mp = mp->m_next) {
		register int zseg = *(char *)&mp->m_base;

		if( 3 <= zseg  &&  zseg < 48) {
			*(char *)&mp->m_base = zseg + 45;
			*(char *)&mp->m_bend += 45;
		}
	}
}
#endif

zptrace( req, pid, add, data)
int *add;
{
	printf( "ptrace( %d, %d, %lx, %x)\n", req, pid, add, data);
	ptrace( req, pid, add, data);
}
