/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * A debugger.
 * Instruction decoder for the Z8000.
 */
#include <stdio.h>
#include <types.h>
#include <machine.h>
#include "trace.h"
#include "z8001.h"

/*
 * Disassemble a Z8001 instruction.
 */
char *
coninst(sp, s)
register char *sp;
{
	register INS *ip;
	register char *cp;
	int w;

	if (getb(s, &w, 2) == 0)
		return (NULL);
	for (ip=instab; ip->i_name!=NULL; ip++) {
		if ((ip->i_mask&0xC000)==0 && (w&0xC000)==0xC000)
			continue;
		if ((w&ip->i_mask) == ip->i_code)
			if ((cp=putifmt(sp, s, ip->i_name, w)) != NULL)
				return (cp);
	}
	sprintf(sp, ".word\t0x%04x", w);
	return (&sp[10]);
}

/*
 * Print out the operands to a Z8001 instruction according to a
 * format string.
 */
char *
putifmt(sp, s, fp, ins0)
register char *sp;
register char *fp;
unsigned ins0;
{
	register char *cp;
	register int n;
	register int c;
	register int f;
	register unsigned u;
	unsigned c1;
	unsigned c2;
	unsigned rtype;
	unsigned ctype;
	unsigned insn;
	unsigned mode;
	unsigned form;
	long insl;

	mode = ins0 >> 14;
	form = ins0 & 0xF;
	insn = ins0;
	rtype = 2;
	while ((c=*fp++) != '\0') {
		if (c != '%') {
			*sp++ = c;
			continue;
		}
		ctype = rtype;
	next:
		switch (c=*fp++) {
		case '0':
		case '1':
		case '2':
		case '3':
			form = (ins0>>((c-'0')*4)) & 0xF;
			goto next;
		case '4':
		case '5':
		case '6':
		case '7':
			form = (insn>>((c-'4')*4)) & 0xF;
			goto next;
		case 'a':
			sp = concons( sp, (long)insn);
			continue;
		case 'b':
			ctype = 1;
			goto next;
		case 'c':
			cp = ccdtab[form];
			goto copy;
		case 'd':
			switch (*fp++) {
			case '+':
				insl = add + 2*insn;
				break;
			case '-':
				insl = add - 2*insn;
				break;
			case '.':
				insl = add + insn;
				break;
			}
			insl &= 0x7F00FFFFL;
			sp = conaddr( sp, -1, insl, I);
			continue;
		case 'e':
			c1 = *fp++;
			n = (c1<='9') ? c1-'0' : c1-'a'+10;
			insn &= (unsigned)0xFFFF >> (16-n);
			goto next;
		case 'f':
			for (n=0; n<4; n++) {
				if (form & (1<<(3-n))) {
					if (f++ == 0)
						*sp++ = ',';
					*sp++ = "czsv"[n];
				}
			}
			continue;
		case 'i':
			switch (*fp++) {
			case '+':
				insn++;
				break;
			case '|':
				if ((int)insn < 0)
					insn = -insn;
				break;
			}
			goto next;
		case 'k':
			cp = ctltab[ins0&0x7];
			goto copy;
		case 'l':
			ctype = 4;
			goto next;
		case 'm':
			rtype = ctype;
			continue;
		case 'n':
			if (getb(s, &insn, sizeof(insn)) == 0)
				return (NULL);
			goto next;
		case 'o':
		litl:
			switch (ctype) {
			case 1:
				if (getb(s, (char *)&insn, sizeof(insn)) == 0)
					return (NULL);
				sp = concons(sp, (long)(insn>>8));
				break;
			case 2:
				if (getb(s, (char *)&insn, sizeof(insn)) == 0)
					return (NULL);
				sp = concons(sp, (long)insn);
				break;
			case 4:
				if (getb(s, (char *)&insl, sizeof(insl)) == 0)
					return (NULL);
				if( ( insl & 0x80FF0000L) == 0
				   && ( insl & 0x7F000000L) != 0)
					sp = conaddr( sp, -1, insl, I);
				else
					sp = concons(sp, (long)insl);
				break;
			}
			continue;
		case 'p':
			sp = conaddr(sp, -1, (long)insn, I);
			continue;
		case 'q':
			ctype = 8;
			goto next;
		case 'r':
		mreg:
			*sp++ = 'r';
			switch (ctype) {
			case 8:
				n = 0xC;
				*sp++ = 'q';
				break;
			case 4:
				n = 0xE;
				*sp++ = 'r';
				break;
			case 2:
				n = 0xF;
				break;
			case 1:
				n = 0x7;
				*sp++ = (form>7) ? 'l' : 'h';
				break;
			}
			sprintf(sp, "%d", form&n);
			goto skip;
		case 's':
			if ((n=ins0&0xFF)<=NMICALL && (cp=sysitab[n])!=NULL)
				goto copy;
			if ((u=n-SMDCALL)<=NMDCALL && (cp=sysdtab[u])!=NULL)
				goto copy;
			sprintf(sp, "%02x", n);
			goto skip;
		case 't':
			if ((ins0&0x100) != 0)
				ctype = 2;
			else {
				*sp++ = 'b';
				ctype = 1;
			}
			rtype = ctype;
			continue;
		case 'u':
			c1 = *fp++;
			n = (c1<='9') ? c1-'0' : c1-'a'+10;
			n = 1 << (n-1);
			if (insn & n)
				insn -= (n<<1);
			goto next;
		case 'v':
			cp = inttab[ins0&0x3];
			goto copy;
		case 'w':
			ctype = 2;
			goto next;
		case 'x':
			ctype = 4;		/* was 2 for non-seg */
			goto next;
		case 'y':
			n = *fp++ - '0';
			c1 = *fp++;
			c2 = *fp++;
			c = (form&(1<<n)) ? c1 : c2;
			if (c != '*')
				*sp++ = c;
			continue;
		case 'z':
			n = *fp++ - '0';
			if (mode==n && form==0)
				return (NULL);
			goto next;
		case 'A':
			if (form==0 && mode==0) {
				*sp++ = '$';
				goto litl;
			}
		case 'B':
			switch (mode) {
			case 0:
				cp = "(%xr)";
				break;
			case 1:
				cp = form==0 ? "%S" : "%S(%r)";
				break;
			case 2:
				goto mreg;
			case 3:
				goto uerr;
			}
			sp = putifmt(sp, s, cp, form);
			continue;
		case 'S':
			if (getb(s, &insn, sizeof(insn)) == 0)
				return (NULL);
			if ((insn & 0x8000) == 0) {	/* short addr */
				insl = ( ( (long)insn & 0x7F00) << 16)
					+ (insn & 255);
			} else {
				insl = ( (long)insn & 0x7F00) << 16;
				if (getb(s, &insn, sizeof(insn)) == 0)
					return (NULL);
				insl += insn;
			}
			sp = conaddr( sp, -1, insl, I);
			continue;
		default:
			*sp++ = c;
			continue;
		}
	copy:
		while (*cp)
			*sp++ = *cp++;
		continue;
	skip:
		while (*sp)
			sp++;
		continue;
	uerr:
		*sp++ = '?';
	}
	return (sp);
}
/*
 * Given a size character, `t1', and a type, `t2', return the appropriate
 * format string.
 */
char *
getform(t1, t2)
register int t1;
register int t2;
{
	register char *cp;
	register char *sp;

	if (t1=='f' || t1=='F')
		return ("%g");
	if (t1 == 'h')
		t1 = 'w';
	if ((cp=index(sp="bwlv", t1)) == NULL)
		return ("?");
	t1 = cp-sp;
	if ((cp=index(sp="duox", t2)) == NULL)
		return ("?");
	t2 = cp-sp;
	return (formtab[t1][t2]);
}

/* convert long for display as segmented addr */

char *
conaddnum( sp, a)
register char *sp;
long	a;
{
	sp = concons( sp, ( a >> 24) & 127);
	*sp++ = '|';
	return( concons( sp, a & 65535L));
}
