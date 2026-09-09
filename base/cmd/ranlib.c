/*
 * Copyright (c) 1977-1995 Robert Swartz.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Make libraries more accessible to the loader
 * by copying all the globally defined symbols
 * into a special module at the front of the archive.
 * Speed up by simply overwriting the header in the archive
 * if it fits, which it will more often than not.
 */
#include <stdio.h>
#include "n.out.h"
#include <ar.h>
#include <canon.h>

static	struct	ldheader	ldh;

main(argc, argv)
int	argc;
char	*argv[];
{
	register int	i, errors = 0;

	if (argc==1) {
		fprintf(stderr, "Usage: %s [ archive]+\n", argv[0]);
		exit(1);
	}
	for (i=1; i<argc; i++) {
		errors += ranlib(argv[i]);
	}
	unlink(HDRNAME);
	exit(errors);
}

static
void
complain(plaint)
char	*plaint;
{
	fprintf(stderr, "Ranlib: %r", &plaint);
	putc('\n', stderr);
}

static
void
xwrite(sym, fp)
ar_sym	*sym;
FILE	*fp;
{
	if (fwrite(sym, sizeof(ar_sym), 1, fp)!=1) {
		complain("write error on header file");
		exit(1);
	}
}

static
void
addhdr(base, arhp, hdrfp, arfp, archive)
fsize_t	base;
struct	ar_hdr	*arhp;
FILE	*hdrfp, *arfp;
char	*archive;
{
	fsize_t	off;

	cansize(arhp->ar_size);
	arhp->ar_size += off = ftell(arfp);
	off -= base;
	ldh.l_magic = 0;	/* in case fread fails */
	if (fread(&ldh, sizeof ldh, 1, arfp)!=1) {
		complain("%.*s in %s is not a load module",
			DIRSIZ, arhp->ar_name, archive);
	} else {
		canshort(ldh.l_magic);
		canshort(ldh.l_flag);
		if (ldh.l_magic!=L_MAGIC) {
			complain("%.*s in %s is not a load module",
				DIRSIZ, arhp->ar_name, archive);
		} else {
			int	seg, nsym;
			fsize_t	offset;

			if ((ldh.l_flag & LF_32) == 0)
				ldh.l_tbase = sizeof(ldh) - 2*sizeof(short);
			else
				canshort(ldh.l_tbase);
			offset = ldh.l_tbase - (fsize_t)sizeof(ldh);
			for (seg=0; seg<L_SYM; seg++) {
				if (seg==L_BSSI || seg==L_BSSD)
					continue;
				cansize(ldh.l_ssize[seg]);
				offset += ldh.l_ssize[seg];
			}
			fseek(arfp, offset, 1);
			cansize(ldh.l_ssize[L_SYM]);
			if ((ldh.l_flag & LF_32) == 0)
				nsym = ldh.l_ssize[L_SYM]
				    / (sizeof(struct ldsym) - sizeof(short));
			else
				nsym = ldh.l_ssize[L_SYM]/sizeof(struct ldsym);
			while (nsym--) {
				/*
				 * One record read as a load symbol and
				 * rewritten in place as a ranlib symbol:
				 * the name heads both, and the offset
				 * lands where the type and value were.
				 */
				union	{
					struct ldsym	sy_lds;
					ar_sym		sy_ars;
				}	sym;

				if (readsym((struct ldsym *)&sym, arfp) == 0) {
					complain("truncated module %.*s in %s",
					    DIRSIZ, arhp->ar_name, archive);
					exit(1);
				}
				if ((sym.sy_lds.ls_type&L_GLOBAL) == 0)
					continue;
				if ((sym.sy_lds.ls_type&LR_SEG) != L_REF) {
					sym.sy_ars.ar_off = off;
					cansize(sym.sy_ars.ar_off);
					xwrite(&sym, hdrfp);
				}
			}
		}
	}
	fseek(arfp, arhp->ar_size, 0);	/* to end of module */
	return;
}

static
readsym(sp, fp)
register struct ldsym *sp;
FILE *fp;
{
	register int r;
	union { long l; unsigned u[2]; } u;

	if ((ldh.l_flag & LF_32) == 0) {
		r = fread(sp, sizeof(struct ldsym)-sizeof(short), 1, fp);
		u.l = sp->ls_addr;
		canshort(u.u[0]);
		sp->ls_addr = u.u[0];
	} else {
		r = fread(sp, sizeof(struct ldsym), 1, fp);
		canlong(sp->ls_addr);
	}
	canshort(sp->ls_type);
	return (r);
}

static
int
ranlib(archive)
char	*archive;
{
	FILE	*arfile, *hdrfile;
	char	arcmd[256];
	struct	ar_hdr	arhdr;
	struct	ar_sym	arsym;
	fsize_t	ohdrsize, nhdrsize;
	int	armagic = 0;
	fsize_t	arbase = sizeof armagic+sizeof arhdr;
	extern	time_t	time();

	if ((arfile=fopen(archive, "rb"))==NULL) {
		complain("can't open archive %s", archive);
		return (1);
	}
	fread(&armagic, sizeof armagic, 1, arfile);
	canint(armagic);
	if (armagic!=ARMAG) {
		complain("%s is not an archive", archive);
		fclose(arfile);
		return (1);
	}
	if (fread(&arhdr, sizeof arhdr, 1, arfile)!=1) {
		fclose(arfile);	/* void archive */
		return (0);
	}
	if ((hdrfile=fopen(HDRNAME, "wb"))==NULL) {
		complain("can't create header file");
		fclose(arfile);
		exit(1);
	}
	if (strncmp(arhdr.ar_name, HDRNAME, DIRSIZ)==0) {
		sprintf(arcmd, "ar ru %s %s", archive, HDRNAME);
		cansize(arhdr.ar_size);
		ohdrsize = arhdr.ar_size;
		fseek(arfile, arhdr.ar_size, 1);
		arbase += arhdr.ar_size+sizeof arhdr;
	} else {
		sprintf(arcmd, "ar rub %.*s %s %s", DIRSIZ, arhdr.ar_name,
			archive, HDRNAME);
		ohdrsize = 0;
		addhdr(arbase, &arhdr, hdrfile, arfile, archive);
	}
	while(fread(&arhdr, sizeof arhdr, 1, arfile)==1)
		addhdr(arbase, &arhdr, hdrfile, arfile, archive);
	nhdrsize = ftell(hdrfile);
	if (fclose(hdrfile)==EOF) {
		complain("close error on header file");
		exit(1);
	}
	if (ferror(arfile)) {
		complain("read error on archive %s", archive);
		fclose(arfile);
		return (1);
	}
	fclose(arfile);
	if (ohdrsize < nhdrsize)
		return (system(arcmd));
	if (ohdrsize == 0) {
		complain("zero length header file");
		return (0);
	}
	/*
	 * The table fits where the old one lies, so write it there and leave
	 * the rest of the archive where it is.
	 */
	if ((arfile=fopen(archive, "rwb"))==NULL) {
		complain("can't reopen archive %s", archive);
		return (1);
	}
	fread(&armagic, sizeof armagic, 1, arfile);
	canint(armagic);
	if (armagic!=ARMAG) {
		complain("%s is no longer an archive", archive);
		fclose(arfile);
		return (1);
	}
	if ((hdrfile=fopen(HDRNAME, "rb"))==NULL) {
		complain("can't reopen header file");
		fclose(arfile);
		exit(1);
	}
	if (fread(&arhdr, sizeof arhdr, 1, arfile)!=1) {
		complain("can't read archive: %s", archive);
		fclose(arfile);
		fclose(hdrfile);
		return (1);
	}
	arhdr.ar_date = time((time_t *)0);
	cantime(arhdr.ar_date);
	fseek(arfile, -(fsize_t)sizeof arhdr, 1);
	fwrite(&arhdr, sizeof arhdr, 1, arfile);
	while (fread(&arsym, sizeof arsym, 1, hdrfile) == 1) {
		fwrite(&arsym, sizeof arsym, 1, arfile);
		ohdrsize -= sizeof arsym;
	}
	/*
	 * Fill what is left of the old table with a nameless entry at offset
	 * zero, which the loader looks up and never matches.
	 */
	{
		register char	*cp = (char *)&arsym;
		register int	n = sizeof arsym;

		while (--n >= 0)
			*cp++ = '\0';
	}
	while (ohdrsize > 0) {
		fwrite(&arsym, sizeof arsym, 1, arfile);
		ohdrsize -= sizeof arsym;
	}
	if (fclose(arfile) == EOF)
		complain("close error on overwritten archive: %s", archive);
	if (fclose(hdrfile) == EOF)
		complain("close error on reread header file");
	return (0);
}
