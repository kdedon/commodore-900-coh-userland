/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * nlistfar -- nlist(3) over the two spans that do not fit 16 bits.
 *
 * A symbol's value on this machine is a virtual address: 32 bits, with the
 * segment above the offset.  A kernel symbol looks like 0x32002c2c, so
 * anything that carries one through an `int', an `unsigned' or an
 * `unsigned short' loses the segment and keeps a plausible-looking offset.
 * Nothing reports that: the caller gets a number, seeks to it in /dev/kmem
 * and reads whatever is there.
 *
 * The symbol table's POSITION in the file is the other span.  It sits after
 * the text and data, so on a kernel it starts past 64K -- 92890 bytes into
 * the 3.2 kernel this was written against -- and the offset nlist seeks to
 * has to be computed and passed in a type that holds it.
 *
 * Each case therefore asserts a whole 32-bit value, from a table placed
 * beyond 32767 and beyond 65535 as well as within both.  The l.out files are
 * built here, byte by byte, in the canonical on-disk encoding (16-bit fields
 * little-endian, 32-bit fields high word first and each word little-endian)
 * rather than by writing out this machine's structures: what nlist has to
 * agree with is the format, and a test that writes its input through the same
 * struct declaration nlist reads it through agrees with itself whatever the
 * declaration says.  The two sizes that the hand-built layout depends on are
 * asserted for the same reason.
 *
 *	nlistfar [dir]		dir defaults to `.'; three l.out files are
 *				made there and removed
 */
#include <stdio.h>
#include <l.out.h>

#define	HDRSIZE	48		/* struct ldheader on disk */
#define	SYMSIZE	22		/* struct ldsym on disk */
#define	NSYM	4		/* symbols in each file's table */

#define	T_BSSD	025		/* L_GLOBAL|L_BSSD, as ld writes it */

char	dir[128];
char	path[160];
int	fails;

struct nlist	nl_value_probe;	/* for the width of n_value itself */

/*
 * Report one case whose subject is a 32-bit value.  Printed as %08lx: a
 * truncated address differs from the right one only in the half a %x of an
 * int would not show.
 */
checkl(what, got, want)
char *what;
long got, want;
{
	if (got == want)
		printf("  ok   %s (%08lx)\n", what, got);
	else {
		printf("  FAIL %s: got %08lx, want %08lx\n", what, got, want);
		fails++;
	}
}

check(what, got, want)
char *what;
unsigned got, want;
{
	if (got == want)
		printf("  ok   %s (%u)\n", what, got);
	else {
		printf("  FAIL %s: got %u, want %u\n", what, got, want);
		fails++;
	}
}

/*
 * The canonical encodings, into a byte buffer.
 */
put16(p, v)
char *p;
unsigned v;
{
	p[0] = v;
	p[1] = v >> 8;
}

put32(p, v)
char *p;
long v;
{
	put16(p, (unsigned)(v >> 16));		/* high word first */
	put16(p + 2, (unsigned)v);
}

/*
 * One symbol table entry: name, type, value.
 */
putsym(p, name, type, value)
char *p;
char *name;
unsigned type;
long value;
{
	int i;

	for (i = 0; i < NCPLN; i++)
		p[i] = i < strlen(name) ? name[i] : '\0';
	put16(p + NCPLN, type);
	put32(p + NCPLN + 2, value);
}

/*
 * Write an l.out whose symbol table starts `at' bytes into the file: a header,
 * `at' - HDRSIZE bytes of text and data for it to sit behind, and the table.
 * The text size is what moves the table, so it is the only thing that differs
 * between the three files.
 */
lay(name, at)
char *name;
long at;
{
	FILE *fp;
	char hdr[HDRSIZE];
	char syms[NSYM * SYMSIZE];
	long i, text;
	int n;

	strcpy(path, dir);
	strcat(path, "/");
	strcat(path, name);

	text = at - HDRSIZE;
	for (n = 0; n < HDRSIZE; n++)
		hdr[n] = '\0';
	put16(hdr, 0407);			/* l_magic */
	put16(hdr + 2, 026);			/* l_flag */
	put16(hdr + 4, M_Z8001);		/* l_machine */
	put16(hdr + 6, HDRSIZE);		/* l_tbase */
	put32(hdr + 8 + 4 * L_SHRI, text);	/* l_ssize[] */
	put32(hdr + 8 + 4 * L_SYM, (long)(NSYM * SYMSIZE));
	put32(hdr + 44, 0x3000000L);		/* l_entry */

	putsym(&syms[0 * SYMSIZE], "low_", T_BSSD, 0x00001234L);
	putsym(&syms[1 * SYMSIZE], "seg_", T_BSSD, 0x0300abcdL);
	putsym(&syms[2 * SYMSIZE], "kern_", T_BSSD, 0x32002c2cL);
	putsym(&syms[3 * SYMSIZE], "high_", T_BSSD, 0x7f00fffeL);

	if ((fp = fopen(path, "w")) == NULL) {
		printf("nlistfar: cannot write %s\n", path);
		exit(2);
	}
	fwrite(hdr, 1, sizeof hdr, fp);
	for (i = 0; i < text; i++)		/* the text and data it sits behind */
		putc('\0', fp);
	fwrite(syms, 1, sizeof syms, fp);
	if (fclose(fp) != 0) {
		printf("nlistfar: cannot finish %s\n", path);
		exit(2);
	}
}

/*
 * nlist the file just laid down and assert every value in it.  `where'
 * describes the span being exercised, so a failure names it.
 */
walk(name, at, where)
char *name;
long at;
char *where;
{
	struct nlist nl[6];
	char what[80];
	int i;

	for (i = 0; i < 6; i++) {
		nl[i].n_name[0] = '\0';
		nl[i].n_type = 0;
		nl[i].n_value = 0;
	}
	strcpy(nl[0].n_name, "low_");
	strcpy(nl[1].n_name, "seg_");
	strcpy(nl[2].n_name, "kern_");
	strcpy(nl[3].n_name, "high_");
	strcpy(nl[4].n_name, "absent_");

	lay(name, at);
	nlist(path, nl);

	strcpy(what, "low_ ");
	strcat(what, where);
	checkl(what, (long)nl[0].n_value, 0x00001234L);
	strcpy(what, "seg_ ");
	strcat(what, where);
	checkl(what, (long)nl[1].n_value, 0x0300abcdL);
	strcpy(what, "kern_ ");
	strcat(what, where);
	checkl(what, (long)nl[2].n_value, 0x32002c2cL);
	strcpy(what, "high_ ");
	strcat(what, where);
	checkl(what, (long)nl[3].n_value, 0x7f00fffeL);

	strcpy(what, "the type of kern_ ");
	strcat(what, where);
	check(what, (unsigned)nl[2].n_type, T_BSSD);
	strcpy(what, "a name not in the table stays 0 ");
	strcat(what, where);
	checkl(what, (long)nl[4].n_value, 0L);

	unlink(path);
}

/*
 * A file nlist cannot use leaves every entry as it found it, rather than
 * part-filled: the caller's only signal is the zero value.
 */
nofile()
{
	struct nlist nl[2];

	strcpy(nl[0].n_name, "low_");
	nl[0].n_type = 0;
	nl[0].n_value = 0x11111111L;
	nl[1].n_name[0] = '\0';

	strcpy(path, dir);
	strcat(path, "/nlistfar.none");
	unlink(path);
	nlist(path, nl);
	checkl("a missing file zeroes the entry", (long)nl[0].n_value, 0L);
}

main(argc, argv)
int argc;
char **argv;
{
	strcpy(dir, argc > 1 ? argv[1] : ".");

	/* The hand-built layout above is only the format if these agree. */
	printf("nlistfar: the on-disk structures\n");
	check("sizeof (struct ldheader)", (unsigned)sizeof (struct ldheader),
		HDRSIZE);
	check("sizeof (struct ldsym)", (unsigned)sizeof (struct ldsym),
		SYMSIZE);
	check("sizeof n_value", (unsigned)sizeof nl_value_probe.n_value, 4);

	printf("nlistfar: a symbol table within 32767\n");
	walk("nlistfar.near", 9348L, "(table at 9348)");

	printf("nlistfar: a symbol table past 32767\n");
	walk("nlistfar.mid", 40348L, "(table at 40348)");

	printf("nlistfar: a symbol table past 65535\n");
	walk("nlistfar.far", 80348L, "(table at 80348)");

	printf("nlistfar: a file nlist cannot read\n");
	nofile();

	printf("nlistfar: %d failed\n", fails);
	exit(fails != 0);
}
