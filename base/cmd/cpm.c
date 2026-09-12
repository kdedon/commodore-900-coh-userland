/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * cpm.c
 * Move files between the Coherent filesystem and the CP/M-8000 drive A:
 * partition (/dev/cpma), dos(1)-style: no kernel mount, the utility walks
 * the CP/M 2.2 directory itself.
 *
 * Usage:
 *	cpm [-f image] ls
 *	cpm [-f image] read  CPMFILE [localfile]	(default: stdout)
 *	cpm [-f image] write localfile CPMFILE
 *	cpm [-f image] rm    CPMFILE
 *
 * Geometry is the C900 drive A: DPB contract (host twin: the CP/M tree's
 * mkcpmfs.py -- keep the two implementations
 * in step):  128-byte records, BLS=4096, EXM=1, DSM=2559, DRM=511 (directory
 * = first 4 allocation blocks = 16 KB), skew 0, OFF 0.
 *
 * On-disk format is CP/M 2.2 as the CP/M-8000 may83 BDOS reads it:
 *   - 32-byte entries: UU F1-8 T1-3 EX S1 S2 RC AL[8 x 16-bit LITTLE-endian].
 *   - One entry maps 8 blocks = 32 KB = 2 logical extents (EXM=1);
 *     ext_total = ((S2&0x3f)<<5)|(EX&0x1f), entry k holds 2k or 2k+1.
 *   - RC counts 128-byte records in the LAST logical extent of the entry; a
 *     16 KB-aligned final chunk is EX=2k RC=0x80, never EX=2k+1 RC=0 (that
 *     is the only EOF encoding the BDOS itself writes and accepts).
 *   - Free entries are 0xE5-filled; a slot is free only when its type byte is
 *     exactly 0xE5.
 *
 * CP/M 3 directory extensions (type byte 0x10 XFCB/password, 0x20 directory
 * label, 0x21 SFCB date stamps) are carried per the shared format
 * contract.  They are invisible to the CP/M-8000 BDOS
 * -- it allocates no blocks from them and claims only 0xE5 slots -- so a
 * stamped drive still runs unmodified CP/M.  Here: ls shows the label and
 * the per-file stamps, write and rm preserve every entry that is neither a
 * user FCB nor free (including type bytes this program does not understand),
 * and write stamps the file when the drive's label asks for stamping.
 *
 * Text files (by extension) have their final partial record padded with ^Z,
 * the CP/M text EOF; binaries are zero-padded.  Extraction does not strip
 * the padding: CP/M itself sizes files in whole records.
 */

#include <stdio.h>
#include <time.h>

#define	RECLEN		128		/* CP/M logical record */
#define	BLS		4096		/* allocation block size */
#define	DSM		2559		/* highest block number */
#define	DRM		511		/* highest directory entry number */
#define	NENT		(DRM + 1)	/* directory entries */
#define	DIRBLKS		4		/* (DRM+1)*32 / BLS */
#define	DIRBYTES	(DIRBLKS * BLS)	/* 16 KB */
#define	ENTRY_DATA	(8L * BLS)	/* bytes mapped by one entry (32 KB) */
#define	RECS_PER_LOGEXT	128		/* 16 KB logical extent / RECLEN */
#define	ENTSIZE		32

#define	DEFIMG	"/dev/cpma"

/* directory entry type byte (entry[0]) */
#define	T_FREE	0xe5			/* free slot (this value exactly) */
#define	T_XFCB	0x10			/* 0x10+user: XFCB, holds a password */
#define	T_LABEL	0x20			/* directory label */
#define	T_SFCB	0x21			/* stamps for the 3 preceding entries */

/* directory label mode bits (label entry byte 12) */
#define	DL_PASSWORD	0x80
#define	DL_ACCESS	0x40		/* stamp field 1 = access time */
#define	DL_UPDATE	0x20
#define	DL_CREATE	0x10		/* stamp field 1 = create time */
#define	DL_EXISTS	0x01

#define	SFCBLEN	10			/* bytes of one SFCB sub-record */

/*
 * K&R: every function used before its definition whose return type is not
 * int must be declared here, or the call site truncates the value.
 * time() and localtime() come from <time.h>.
 */
long	lseek();
long	fsize();
long	cpmdays();
unsigned char	*entsfcb();
unsigned char	*dirlabel();

char	*image = DEFIMG;
int	imgfd = -1;
int	imgwrite;			/* image opened read-write */

unsigned char	dir[DIRBYTES];		/* the whole directory */
unsigned char	blk[BLS];		/* one allocation block */
unsigned char	freemap[(DSM + 1 + 7) / 8];	/* block allocation bitmap */
int	alloclist[DSM + 1 - DIRBLKS];	/* blocks given to one new file */

/* ls aggregation: one slot per distinct (user, name) */
struct file {
	unsigned char	f_user;
	unsigned char	f_name[11];
	long		f_size;
	int		f_blocks;
	int		f_nent;
	unsigned char	f_stamp[8];	/* create/access + update, extent 0 */
} ftab[NENT];
int	nfiles;

/* extensions whose final partial record is ^Z-padded (CP/M text EOF) */
char	*textext[] = {
	"TXT", "C", "H", "SUB", "PD", "8KN", "S", "ASM", "DOC", "MAN", 0
};

char	*badchars = "<>.,;:=?*[] \"";

usage()
{
	fprintf(stderr, "usage:\tcpm [-f image] ls\n");
	fprintf(stderr, "\tcpm [-f image] read CPMFILE [localfile]\n");
	fprintf(stderr, "\tcpm [-f image] write localfile CPMFILE\n");
	fprintf(stderr, "\tcpm [-f image] rm CPMFILE\n");
	exit(1);
}

fatal(s, a)
char *s, *a;
{
	fprintf(stderr, "cpm: ");
	fprintf(stderr, s, a);
	fprintf(stderr, "\n");
	exit(1);
}

main(argc, argv)
int argc;
char *argv[];
{
	register char *verb;

	while (argc > 2 && argv[1][0] == '-') {
		if (argv[1][1] == 'f' && argv[1][2] == '\0') {
			image = argv[2];
			argv += 2;
			argc -= 2;
		} else
			usage();
	}
	if (argc < 2)
		usage();
	verb = argv[1];

	if (strcmp(verb, "ls") == 0) {
		if (argc != 2)
			usage();
		openimg(0);
		cmdls();
	} else if (strcmp(verb, "read") == 0) {
		if (argc != 3 && argc != 4)
			usage();
		openimg(0);
		cmdread(argv[2], argc == 4 ? argv[3] : (char *)0);
	} else if (strcmp(verb, "write") == 0) {
		if (argc != 4)
			usage();
		openimg(1);
		cmdwrite(argv[2], argv[3]);
	} else if (strcmp(verb, "rm") == 0) {
		if (argc != 3)
			usage();
		openimg(1);
		cmdrm(argv[2]);
	} else
		usage();
	exit(0);
}

/*
 * Open the image (0 = read-only, 1 = read-write) and read the directory.
 */
openimg(rw)
int rw;
{
	register int i, n;

	imgwrite = rw;
	if ((imgfd = open(image, rw ? 2 : 0)) < 0)
		fatal("cannot open %s", image);
	for (i = 0; i < DIRBYTES; i += n) {
		n = read(imgfd, (char *)dir + i, DIRBYTES - i);
		if (n <= 0)
			fatal("%s: cannot read directory", image);
	}
}

/*
 * Write the in-core directory back, then flush the buffer cache so a
 * subsequent CP/M boot sees it.
 */
putdir()
{
	if (lseek(imgfd, 0L, 0) != 0L
	 || write(imgfd, (char *)dir, DIRBYTES) != DIRBYTES)
		fatal("%s: directory write failed", image);
	sync();
}

/*
 * Read allocation block b into blk[]; block 0 means a hole (never a valid
 * data block -- the directory owns it), read as zeros.
 */
getblock(b)
int b;
{
	register int i, n;

	if (b == 0) {
		for (i = 0; i < BLS; i++)
			blk[i] = 0;
		return;
	}
	if (b > DSM)
		fatal("%s: block number out of range", image);
	if (lseek(imgfd, (long)b * BLS, 0) != (long)b * BLS)
		fatal("%s: seek failed", image);
	for (i = 0; i < BLS; i += n) {
		n = read(imgfd, (char *)blk + i, BLS - i);
		if (n <= 0)
			fatal("%s: block read failed", image);
	}
}

putblock(b)
int b;
{
	if (lseek(imgfd, (long)b * BLS, 0) != (long)b * BLS
	 || write(imgfd, (char *)blk, BLS) != BLS)
		fatal("%s: block write failed", image);
}

/* ---- directory entry access ---------------------------------------------- */

/* entry i's ext_total = ((S2 & 0x3f) << 5) | (EX & 0x1f) */
int
enttotal(e)
register unsigned char *e;
{
	return (((e[14] & 0x3f) << 5) | (e[12] & 0x1f));
}

/* 128-byte records mapped by entry e (EXM=1: 0..256) */
int
entrecs(e)
register unsigned char *e;
{
	return ((enttotal(e) & 1) * RECS_PER_LOGEXT + e[15]);
}

/* AL slot j of entry e, 16-bit little-endian */
int
entblock(e, j)
register unsigned char *e;
int j;
{
	return (e[16 + 2 * j] | (e[17 + 2 * j] << 8));
}

/* entry live (a real file FCB, not free / XFCB / label / timestamp)? */
int
entlive(e)
register unsigned char *e;
{
	return (e[0] != T_FREE && e[0] < T_XFCB);
}

/* slot free?  Only the exact value 0xE5 -- everything else is somebody's. */
int
entfree(e)
register unsigned char *e;
{
	return (e[0] == T_FREE);
}

/* ---- CP/M 3 directory extensions ----------------------------------------- */

/*
 * Entry i's 10-byte SFCB sub-record, or 0 when the directory carries no
 * stamps for it.  The SFCB lives in the 4th entry of each group of four
 * (index i|3) and holds the sub-records of the three entries before it, so
 * an entry that IS a 4th slot is never stamped.
 */
unsigned char *
entsfcb(i)
int i;
{
	register unsigned char *s;

	if ((i & 3) == 3)
		return ((unsigned char *)0);
	s = &dir[(i | 3) * ENTSIZE];
	if (s[0] != T_SFCB)
		return ((unsigned char *)0);
	return (s + 1 + SFCBLEN * (i & 3));
}

/* Zero entry i's stamps; called when its FCB is freed. */
zapsfcb(i)
int i;
{
	register unsigned char *s;
	register int j;

	if ((s = entsfcb(i)) != 0)
		for (j = 0; j < SFCBLEN; j++)
			s[j] = 0;
}

/* The drive's type-20h directory label, or 0 when it has none. */
unsigned char *
dirlabel()
{
	register int i;

	for (i = 0; i <= DRM; i++)
		if (dir[i * ENTSIZE] == T_LABEL)
			return (&dir[i * ENTSIZE]);
	return ((unsigned char *)0);
}

/*
 * Days from 1977-12-31 to the given date, so that 1978-01-01 is day 1 --
 * the CP/M stamp epoch.  Gregorian leap rule; long because the count runs
 * past 32767 in 2067 and int is 16 bits here.
 */
long
cpmdays(year, yday)
int year;			/* full year, e.g. 2026 */
int yday;			/* 0-based day of year */
{
	register int y;
	long days;

	days = 0;
	for (y = 1978; y < year; y++)
		days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))
		    ? 366L : 365L;
	return (days + yday + 1);
}

/* one byte packed BCD */
int
tobcd(v)
int v;
{
	return (((v / 10) << 4) | (v % 10));
}

int
unbcd(v)
int v;
{
	return (((v >> 4) & 0xf) * 10 + (v & 0xf));
}

/*
 * Fill a 4-byte stamp from the current local time: date word (days since
 * 1977-12-31) LITTLE-endian on disk, then BCD hour and BCD minute.  The
 * little-endian date word is the 8080 order the format was born in -- the
 * same convention as the AL block numbers -- not the Z8001's.
 */
mkstamp(s)
register unsigned char *s;
{
	register struct tm *tp;
	long now, days;

	now = time((long *)0);
	tp = localtime(&now);
	days = cpmdays(tp->tm_year + 1900, tp->tm_yday);
	s[0] = (int)(days & 0xff);
	s[1] = (int)((days >> 8) & 0xff);
	s[2] = tobcd(tp->tm_hour);
	s[3] = tobcd(tp->tm_min);
}

/*
 * Render a 4-byte stamp into buf as "YYYY-MM-DD HH:MM"; an all-zero date
 * word means unstamped and yields an empty string.  Returns buf.
 */
char *
prstamp(s, buf)
register unsigned char *s;
char *buf;
{
	static int mlen[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
	register int y, m;
	int d, len;
	long days;

	days = ((long)s[1] << 8) | (long)s[0];
	if (days == 0) {
		buf[0] = '\0';
		return (buf);
	}
	y = 1978;
	for (;;) {
		len = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
		if (days <= (long)len)
			break;
		days -= (long)len;
		y++;
	}
	d = (int)days;
	for (m = 0; m < 12; m++) {
		len = mlen[m];
		if (m == 1 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))
			len = 29;
		if (d <= len)
			break;
		d -= len;
	}
	sprintf(buf, "%04d-%02d-%02d %02d:%02d", y, m + 1, d,
	    unbcd(s[2]), unbcd(s[3]));
	return (buf);
}

/* entry e names name11 (attribute bits stripped) in user area 0? */
int
entmatch(e, name11)
register unsigned char *e;
unsigned char *name11;
{
	register int i;

	if (e[0] != 0)
		return (0);
	for (i = 0; i < 11; i++)
		if ((e[1 + i] & 0x7f) != name11[i])
			return (0);
	return (1);
}

/* ---- 8.3 name handling --------------------------------------------------- */

/*
 * Host name -> 11-byte upper-case space-padded F1-8/T1-3; dies on a name
 * that does not fit or holds a character CP/M forbids.
 */
mkname(host, name11)
char *host;
register unsigned char *name11;
{
	register char *s;
	register int i, c;
	int n;

	for (i = 0; i < 11; i++)
		name11[i] = ' ';
	s = host;
	n = 0;
	for (; *s != '\0' && *s != '.'; s++) {
		if (n >= 8)
			fatal("%s: name does not fit 8.3", host);
		name11[n++] = upput(*s, host);
	}
	if (*s == '.') {
		s++;
		n = 0;
		for (; *s != '\0'; s++) {
			if (n >= 3)
				fatal("%s: extension does not fit 8.3", host);
			name11[8 + n++] = upput(*s, host);
		}
	}
	if (name11[0] == ' ')
		fatal("%s: empty CP/M filename", host);
	return (0);
}

/* upper-case one filename character, rejecting what CP/M forbids */
int
upput(c, host)
int c;
char *host;
{
	register char *p;

	if (c >= 'a' && c <= 'z')
		c += 'A' - 'a';
	if (c < 0x21 || c > 0x7e)
		fatal("%s: character invalid in CP/M", host);
	for (p = badchars; *p != '\0'; p++)
		if (c == *p)
			fatal("%s: character invalid in CP/M", host);
	return (c);
}

/* 11-byte name -> printable BASE.EXT into buf[13]; returns buf */
char *
prname(name11, buf)
register unsigned char *name11;
char *buf;
{
	register char *p;
	register int i;

	p = buf;
	for (i = 0; i < 8 && (name11[i] & 0x7f) != ' '; i++)
		*p++ = name11[i] & 0x7f;
	if ((name11[8] & 0x7f) != ' ') {
		*p++ = '.';
		for (i = 8; i < 11 && (name11[i] & 0x7f) != ' '; i++)
			*p++ = name11[i] & 0x7f;
	}
	*p = '\0';
	return (buf);
}

/* is host filename CP/M-text (^Z-padded) by extension? */
int
istext(host)
char *host;
{
	register char *s, *dot;
	register int i;
	char ext[4];
	int c, n;

	dot = 0;
	for (s = host; *s != '\0'; s++)
		if (*s == '.')
			dot = s;
	if (dot == 0)
		return (0);
	n = 0;
	for (s = dot + 1; *s != '\0' && n < 3; s++) {
		c = *s;
		if (c >= 'a' && c <= 'z')
			c += 'A' - 'a';
		ext[n++] = c;
	}
	if (*s != '\0')
		return (0);
	ext[n] = '\0';
	for (i = 0; textext[i] != 0; i++)
		if (strcmp(ext, textext[i]) == 0)
			return (1);
	return (0);
}

/* ---- ls ------------------------------------------------------------------ */

/*
 * Find (or create) the ftab slot for (user, name11).
 */
struct file *
slot(user, name11)
int user;
register unsigned char *name11;
{
	register struct file *f;
	register int i;

	for (f = ftab; f < &ftab[nfiles]; f++) {
		if (f->f_user != user)
			continue;
		for (i = 0; i < 11 && f->f_name[i] == name11[i]; i++)
			;
		if (i == 11)
			return (f);
	}
	f = &ftab[nfiles++];
	f->f_user = user;
	for (i = 0; i < 11; i++)
		f->f_name[i] = name11[i];
	f->f_size = 0;
	f->f_blocks = 0;
	f->f_nent = 0;
	for (i = 0; i < 8; i++)
		f->f_stamp[i] = 0;
	return (f);
}

/* ordering key for ls: user first, then name */
int
fcmp(a, b)
register struct file *a, *b;
{
	register int i;

	if (a->f_user != b->f_user)
		return (a->f_user - b->f_user);
	for (i = 0; i < 11; i++)
		if (a->f_name[i] != b->f_name[i])
			return (a->f_name[i] - b->f_name[i]);
	return (0);
}

/*
 * Show the drive's CP/M 3 extension entries: the directory label with its
 * stamping mode, any XFCBs (passwords -- named, never interpreted), the SFCB
 * count, and any entry type this program does not know but keeps intact.
 */
lsextra()
{
	register unsigned char *e;
	register int i;
	int nsfcb, npw;
	char nb[13], cb[24], ub[24];

	nsfcb = npw = 0;
	if ((e = dirlabel()) != 0) {
		printf("label     %-12s mode 0x%02x", prname(e + 1, nb), e[12]);
		if (e[12] & DL_PASSWORD)
			printf(" password");
		if (e[12] & DL_ACCESS)
			printf(" access");
		if (e[12] & DL_UPDATE)
			printf(" update");
		if (e[12] & DL_CREATE)
			printf(" create");
		prstamp(e + 24, cb);
		prstamp(e + 28, ub);
		printf("  created %s  updated %s\n",
		    cb[0] != '\0' ? cb : "-", ub[0] != '\0' ? ub : "-");
	}
	for (i = 0; i <= DRM; i++) {
		e = &dir[i * ENTSIZE];
		if (entlive(e) || entfree(e))
			continue;
		if (e[0] == T_SFCB)
			nsfcb++;
		else if (e[0] >= T_XFCB && e[0] < T_LABEL) {
			printf("password  %2d %-12s mode 0x%02x (preserved, "
			    "not interpreted)\n",
			    e[0] - T_XFCB, prname(e + 1, nb), e[12]);
			npw++;
		} else if (e[0] != T_LABEL)
			printf("unknown   entry %d type 0x%02x (preserved "
			    "verbatim)\n", i, e[0]);
	}
	if (nsfcb != 0)
		printf("stamps    %d SFCB entries, %d file slots usable\n",
		    nsfcb, NENT - NENT / 4);
}

cmdls()
{
	register unsigned char *e;
	register struct file *f;
	register int i, j;
	struct file tmp;
	long total, sz;
	int k;
	unsigned char *s;
	char nb[13], cb[24], ub[24];

	lsextra();
	nfiles = 0;
	for (i = 0; i <= DRM; i++) {
		e = &dir[i * ENTSIZE];
		if (!entlive(e))
			continue;
		f = slot(e[0], e + 1);
		k = enttotal(e) >> 1;
		sz = (long)k * ENTRY_DATA + (long)entrecs(e) * RECLEN;
		if (sz > f->f_size)
			f->f_size = sz;
		for (j = 0; j < 8; j++)
			if (entblock(e, j) != 0)
				f->f_blocks++;
		f->f_nent++;
		/* stamps live on the file's extent-0 entry only */
		if (enttotal(e) == 0 && (s = entsfcb(i)) != 0)
			for (j = 0; j < 8; j++)
				f->f_stamp[j] = s[j];
	}
	/* attribute bits were left in f_name by slot(); strip for sorting */
	for (f = ftab; f < &ftab[nfiles]; f++)
		for (i = 0; i < 11; i++)
			f->f_name[i] &= 0x7f;
	for (i = 0; i < nfiles - 1; i++)
		for (j = i + 1; j < nfiles; j++)
			if (fcmp(&ftab[i], &ftab[j]) > 0) {
				tmp = ftab[i];
				ftab[i] = ftab[j];
				ftab[j] = tmp;
			}
	total = 0;
	for (f = ftab; f < &ftab[nfiles]; f++) {
		total += f->f_size;
		prstamp(f->f_stamp, cb);
		prstamp(f->f_stamp + 4, ub);
		printf("%2d %-12s %8ld bytes  %3d blocks  %d entr%-3s",
		    f->f_user, prname(f->f_name, nb), f->f_size,
		    f->f_blocks, f->f_nent, f->f_nent == 1 ? "y" : "ies");
		if (cb[0] != '\0' || ub[0] != '\0')
			printf(" %-16s  %-16s",
			    cb[0] != '\0' ? cb : "-", ub[0] != '\0' ? ub : "-");
		printf("\n");
	}
	printf("%d files, %ld bytes\n", nfiles, total);
}

/* ---- read ---------------------------------------------------------------- */

/*
 * Byte size of the user-0 file name11, or -1 if absent.
 */
long
fsize(name11)
unsigned char *name11;
{
	register unsigned char *e;
	register int i;
	long sz, s;
	int found;

	found = 0;
	sz = 0;
	for (i = 0; i <= DRM; i++) {
		e = &dir[i * ENTSIZE];
		if (!entlive(e) || !entmatch(e, name11))
			continue;
		found = 1;
		s = (long)(enttotal(e) >> 1) * ENTRY_DATA
		    + (long)entrecs(e) * RECLEN;
		if (s > sz)
			sz = s;
	}
	return (found ? sz : -1L);
}

/*
 * Directory index of the user-0 entry of name11 with entry number k,
 * or -1 (a hole: unwritten 32 KB span of a sparse file).
 */
int
findent(name11, k)
unsigned char *name11;
int k;
{
	register unsigned char *e;
	register int i;

	for (i = 0; i <= DRM; i++) {
		e = &dir[i * ENTSIZE];
		if (entlive(e) && entmatch(e, name11)
		 && (enttotal(e) >> 1) == k)
			return (i);
	}
	return (-1);
}

cmdread(cpmname, local)
char *cpmname, *local;
{
	register int j, i;
	unsigned char name11[11];
	long size, off;
	int ofd, k, n, b;

	mkname(cpmname, name11);
	size = fsize(name11);
	if (size < 0)
		fatal("%s: not found on drive A:", cpmname);
	if (local == 0)
		ofd = 1;
	else if ((ofd = creat(local, 0666)) < 0)
		fatal("cannot create %s", local);
	off = 0;
	for (k = 0; off < size; k++) {
		i = findent(name11, k);
		for (j = 0; j < 8 && off < size; j++) {
			b = i < 0 ? 0 : entblock(&dir[i * ENTSIZE], j);
			getblock(b);
			n = size - off > (long)BLS ? BLS : (int)(size - off);
			if (write(ofd, (char *)blk, n) != n)
				fatal("write error on %s",
				    local ? local : "stdout");
			off += n;
		}
	}
	if (local != 0)
		close(ofd);
}

/* ---- rm ------------------------------------------------------------------ */

/*
 * Free every user-0 entry of name11, clearing the stamps that went with it.
 * Only the matched FCBs are touched: extension entries (label, XFCB, SFCB)
 * never match a user-0 lookup and stay put.  Returns the count freed.
 */
int
zapfile(name11)
unsigned char *name11;
{
	register unsigned char *e;
	register int i, n;

	n = 0;
	for (i = 0; i <= DRM; i++) {
		e = &dir[i * ENTSIZE];
		if (entlive(e) && entmatch(e, name11)) {
			e[0] = T_FREE;
			zapsfcb(i);
			n++;
		}
	}
	return (n);
}

cmdrm(cpmname)
char *cpmname;
{
	unsigned char name11[11];

	mkname(cpmname, name11);
	if (zapfile(name11) == 0)
		fatal("%s: not found on drive A:", cpmname);
	putdir();
}

/* ---- write --------------------------------------------------------------- */

/*
 * Rebuild the free-block bitmap from the live directory.  The directory's
 * own blocks are always in use.
 */
mkfreemap()
{
	register unsigned char *e;
	register int i, j;
	int b;

	for (i = 0; i < sizeof freemap; i++)
		freemap[i] = 0;
	for (b = 0; b < DIRBLKS; b++)
		freemap[b >> 3] |= 1 << (b & 7);
	for (i = 0; i <= DRM; i++) {
		e = &dir[i * ENTSIZE];
		if (!entlive(e))
			continue;
		for (j = 0; j < 8; j++) {
			b = entblock(e, j);
			if (b != 0 && b <= DSM)
				freemap[b >> 3] |= 1 << (b & 7);
		}
	}
}

/*
 * Allocate one free block (lowest first), or -1 when the disk is full.
 */
int
allocblock()
{
	register int b;

	for (b = DIRBLKS; b <= DSM; b++)
		if ((freemap[b >> 3] & (1 << (b & 7))) == 0) {
			freemap[b >> 3] |= 1 << (b & 7);
			return (b);
		}
	return (-1);
}

cmdwrite(local, cpmname)
char *local, *cpmname;
{
	register unsigned char *e;
	register int i, j;
	unsigned char name11[11];
	long size;
	int ifd, n, m, b, text, nblk, nent, slots, k;
	int recs, rc, ext_total, blkbase, nb, first;
	unsigned char *lab, *s;

	mkname(cpmname, name11);
	text = istext(local);
	if ((ifd = open(local, 0)) < 0)
		fatal("cannot open %s", local);

	/*
	 * Replacing an existing file: free its entries FIRST so its blocks
	 * are reusable.  The directory itself is written back only once the
	 * new data and entries are complete, so a mid-copy failure leaves
	 * the visible filesystem unchanged (any data blocks already written
	 * are still marked free).
	 */
	zapfile(name11);
	mkfreemap();

	/*
	 * Copy the data, one allocation block at a time.  The final partial
	 * record is padded (^Z for text, 0 for binary) and the rest of the
	 * final block zeroed.
	 */
	size = 0;
	nblk = 0;
	for (;;) {
		n = 0;
		while (n < BLS) {
			m = read(ifd, (char *)blk + n, BLS - n);
			if (m < 0)
				fatal("read error on %s", local);
			if (m == 0)
				break;
			n += m;
		}
		if (n == 0)
			break;
		size += n;
		if (n < BLS) {
			m = n;
			if (text)
				while (m % RECLEN != 0)
					blk[m++] = 0x1a;
			while (m < BLS)
				blk[m++] = 0;
		}
		if ((b = allocblock()) < 0)
			fatal("drive A: is full (%s not written)", cpmname);
		putblock(b);
		alloclist[nblk++] = b;
		if (n < BLS)
			break;
	}
	close(ifd);

	/*
	 * Build the directory entries: one per 32 KB mapped, blocks from
	 * alloclist[] in order.  Check the free-slot count before touching
	 * the directory.
	 */
	nent = (int)((size + ENTRY_DATA - 1) / ENTRY_DATA);
	if (nent == 0)
		nent = 1;			/* empty file: 1 entry, RC=0 */
	slots = 0;
	for (i = 0; i <= DRM; i++)
		if (entfree(&dir[i * ENTSIZE]))
			slots++;
	if (slots < nent)
		fatal("drive A: directory is full (%s not written)", cpmname);

	blkbase = 0;
	first = -1;
	i = 0;
	for (k = 0; k < nent; k++) {
		long chunk;

		chunk = size - (long)k * ENTRY_DATA;
		if (chunk > ENTRY_DATA)
			chunk = ENTRY_DATA;
		if (chunk < 0)
			chunk = 0;
		nb = (int)((chunk + BLS - 1) / BLS);
		recs = (int)((chunk + RECLEN - 1) / RECLEN);
		if (recs > RECS_PER_LOGEXT) {
			ext_total = 2 * k + 1;
			rc = recs - RECS_PER_LOGEXT;
		} else {
			ext_total = 2 * k;
			rc = recs;
		}
		/* only an exactly-0xE5 slot is free: SFCBs, the label and
		   XFCBs keep their slots */
		while (i <= DRM && !entfree(&dir[i * ENTSIZE]))
			i++;
		if (first < 0)
			first = i;
		e = &dir[i * ENTSIZE];
		for (j = 0; j < ENTSIZE; j++)
			e[j] = 0;
		e[0] = 0;			/* user 0 */
		for (j = 0; j < 11; j++)
			e[1 + j] = name11[j];
		e[12] = ext_total & 0x1f;	/* EX */
		e[14] = (ext_total >> 5) & 0x3f;	/* S2 */
		e[15] = rc;			/* RC (0x80 = full extent) */
		for (j = 0; j < nb; j++) {
			b = alloclist[blkbase + j];
			e[16 + 2 * j] = b & 0xff;	/* AL little-endian */
			e[17 + 2 * j] = (b >> 8) & 0xff;
		}
		blkbase += nb;
		i++;
	}

	/*
	 * Stamp the extent-0 entry when the drive's directory label asks for
	 * stamping and an SFCB covers that slot.  With no label, no stamping
	 * bits or no SFCB the file goes down unstamped, exactly as before --
	 * which is what keeps unstamped drives byte-for-byte unchanged.
	 */
	lab = dirlabel();
	if (lab != 0 && first >= 0 && (s = entsfcb(first)) != 0) {
		if (lab[12] & (DL_CREATE | DL_ACCESS))
			mkstamp(s);
		if (lab[12] & DL_UPDATE)
			mkstamp(s + 4);
	}
	putdir();
}
