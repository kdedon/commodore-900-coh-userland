#include <stdio.h>
#include <types.h>
#include <machine.h>
#include <n.out.h>
#include "trace.h"
#include "z8001.h"

static	char	*slib = "/lib/slibc.a";
static	FILE	*llfp;
static	struct	ldheader  lldh;

getsl( segi, seek, bp, n)
long	seek;
char	*bp;
{
	fseek( llfp, seek, 0);
	return( fread( bp, n, 1, llfp));
}

putsl( segi, seek, bp, n)
long	seek;
char	*bp;
{
	fseek( llfp, seek, 0);
	return( fwrite( bp, n, 1, llfp) && fflush( llfp) != EOF);
}

#define SI lldh.l_ssize[L_SHRI]
#define PI lldh.l_ssize[L_PRVI]
#define SD lldh.l_ssize[L_SHRD]
#define PD lldh.l_ssize[L_PRVD]

MAP *
lshrseg()
{
	register long	dofft, raddr;
	register MAP	*mp;

	if( !slflag)
		return( NULL);
	llfp = openfil( slib, rflag);
	if( fread( &lldh, sizeof(lldh), 1, llfp) != 1)
		panic( "cannot read %s", slib);
	canlout( &lldh);
	dofft = lldh.l_tbase;
	raddr = 0x1000000L;
	mp = setsmap( NULL, raddr, SI, dofft, getsl, putsl, 3);
	dofft += SI+PI;
	raddr += SI;
	return( setsmap( mp, raddr, SD, dofft, getsl, putsl, 3));
}

MAP *
lpriseg( mp)
register MAP *mp;
{
	register long	dofft, raddr;

	if( !slflag)
		return( mp);
	dofft = lldh.l_tbase + SI;
	raddr = 0x2000000L;
	mp = setsmap( mp, raddr, PI, dofft, getsl, putsl, 3);
	dofft += PI+SD;
	raddr += PI;
	return( setsmap( mp, raddr, PD, dofft, getsl, putsl, 3));
}

#undef SI
#undef PI
#undef SD
#undef PD
