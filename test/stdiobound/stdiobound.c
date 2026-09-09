/*
 * stdiobound -- stdio's counted and positioning calls at the boundaries a
 * 16-bit int imposes.
 *
 * fread() and fwrite() count in items.  `int' is 16 bits here, so a count
 * above 32767 has the high bit set and reads negative to a caller that stores
 * the result in an int: the value is the right 16 bits and the wrong number,
 * so the count must be compared as `unsigned'.  fread's own return is
 * (bytes transferred)/size, a division of two counts that must be unsigned
 * in both operands.
 *
 * Each case is a value assertion: the file is written, read back, and the
 * returned count compared against the number of whole items that could have
 * moved.  A count is printed as `%u' throughout -- printing it as `%d' is
 * the reporting bug this file is about, not a library defect.
 *
 * Arguments above 32767 go through an `unsigned' variable and never appear as
 * a call argument: a decimal constant that does not fit a 16-bit int is a
 * long in this dialect, and one in an argument list shifts every argument
 * after it.
 *
 * The last two groups are not about widths: they are the shape of a formatted
 * field (where a zero pad goes relative to the sign) and the state a
 * successful call leaves behind it (errno, which belongs to the caller).
 *
 *	stdiobound [dir]	dir defaults to `.'; a scratch file is made
 *				there and removed
 */
#include <stdio.h>
#include <errno.h>

#define	DATA	40000U		/* the scratch file's length in bytes */

char	*buf;			/* DATA bytes; malloc'd, not bss, so the
				   program links in the small model */
char	path[128];
int	fails;

/*
 * Report one case and count a failure.  Counts are unsigned: at 33000 items
 * a signed report is itself wrong.
 */
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
 * Report one case whose subject is a string, and count a failure.
 */
checks(what, got, want)
char *what, *got, *want;
{
	if (strcmp(got, want) == 0)
		printf("  ok   %s (\"%s\")\n", what, got);
	else {
		printf("  FAIL %s: got \"%s\", want \"%s\"\n", what, got, want);
		fails++;
	}
}

/*
 * Write `n' bytes of known data to the scratch file.
 */
lay(n)
unsigned n;
{
	FILE *fp;
	unsigned i, wrote;

	for (i = 0; i < n; i++)
		buf[i] = 'A' + i % 26;
	if ((fp = fopen(path, "w")) == NULL) {
		printf("stdiobound: cannot write %s\n", path);
		exit(2);
	}
	wrote = fwrite(buf, 1, n, fp);
	check("fwrite of one buffer returns the item count", wrote, n);
	fclose(fp);
}

/*
 * fread/fwrite item counts, over and under the 32767 boundary.
 */
counts()
{
	FILE *fp;
	unsigned one, big, half, size, got;

	one = 1;
	big = 33000;
	half = 20000;

	lay(big);
	if ((fp = fopen(path, "r")) == NULL)
		exit(2);
	got = fread(buf, one, big, fp);
	check("fread(1, 33000) returns 33000", got, big);
	fclose(fp);

	/* A partial read: the file holds fewer items than were asked for, so
	   the count is the number of WHOLE items that fitted. */
	if ((fp = fopen(path, "r")) == NULL)
		exit(2);
	size = 4;
	got = fread(buf, size, 9000, fp);
	check("fread(4, 9000) over 33000 bytes returns 8250", got, 8250);
	fclose(fp);

	/* One whole object: the count is 1 when the object is there and 0
	   when it is not. */
	if ((fp = fopen(path, "r")) == NULL)
		exit(2);
	got = fread(buf, half, one, fp);
	check("fread(20000, 1) returns 1", got, one);
	fclose(fp);

	lay(half);
	if ((fp = fopen(path, "r")) == NULL)
		exit(2);
	size = 30000;
	got = fread(buf, size, one, fp);
	check("fread(30000, 1) on a 20000-byte file returns 0", got, 0);
	fclose(fp);
}

/*
 * ungetc(EOF) is refused: it returns EOF and the stream is left as it was, so
 * the next getc reads the next byte of the file rather than a pushed-back -1.
 */
ungoteof()
{
	FILE *fp;
	unsigned n;

	n = 300;
	lay(n);
	if ((fp = fopen(path, "r")) == NULL)
		exit(2);
	check("ungetc(EOF) returns EOF", (unsigned)ungetc(EOF, fp), (unsigned)EOF);
	check("getc after ungetc(EOF) is the first byte", (unsigned)getc(fp), 'A');
	check("getc after ungetc(EOF) is the second byte", (unsigned)getc(fp), 'B');
	fclose(fp);
}

/*
 * Read to the end of the file, leaving _FEOF set.
 */
drain(fp)
FILE *fp;
{
	while (getc(fp) != EOF)
		;
	check("feof is set at end of file", feof(fp) ? 1 : 0, 1);
}

/*
 * _FEOF is not sticky: pushing a character back, and seeking, each make the
 * stream readable again, so feof() answers 0 and the next getc returns a
 * character rather than EOF.
 */
eofclear()
{
	FILE *fp;
	unsigned n;

	n = 300;
	lay(n);

	if ((fp = fopen(path, "r")) == NULL)
		exit(2);
	drain(fp);
	ungetc('Z', fp);
	check("feof is clear after ungetc", feof(fp) ? 1 : 0, 0);
	check("getc after ungetc at eof returns the pushed character",
		(unsigned)getc(fp), 'Z');
	fclose(fp);

	if ((fp = fopen(path, "r")) == NULL)
		exit(2);
	drain(fp);
	fseek(fp, 0L, SEEK_SET);
	check("feof is clear after fseek", feof(fp) ? 1 : 0, 0);
	check("getc after fseek at eof returns the first byte",
		(unsigned)getc(fp), 'A');
	fclose(fp);
}

/*
 * A pushed-back character is part of the logical position.  It is held in
 * fp->_uc rather than in the buffer, so ftell and a SEEK_CUR fseek each have to
 * count it: after reading three bytes and pushing the third back, the position
 * is 2, a SEEK_CUR seek of 0 stays at 2, and the next byte read is the third.
 */
ungotpos()
{
	FILE *fp;
	unsigned n;
	int c;

	n = 300;
	lay(n);

	if ((fp = fopen(path, "r")) == NULL)
		exit(2);
	getc(fp);
	getc(fp);
	c = getc(fp);
	ungetc(c, fp);
	check("ftell counts a pushed-back character", (unsigned)ftell(fp), 2);
	fclose(fp);

	if ((fp = fopen(path, "r")) == NULL)
		exit(2);
	getc(fp);
	getc(fp);
	c = getc(fp);
	ungetc(c, fp);
	fseek(fp, 0L, SEEK_CUR);
	check("fseek(0, SEEK_CUR) after ungetc stays put",
		(unsigned)ftell(fp), 2);
	check("the byte after it is the one pushed back", (unsigned)getc(fp), 'C');
	fclose(fp);
}

/*
 * rewind clears both indicators.  A stream opened for reading and written to
 * takes a write error, and after a rewind ferror() answers 0 -- so a caller
 * that recovers by rewinding is not told about an error it has already dealt
 * with.  On a stream with nothing wrong with it the rewind also positions at 0.
 */
rewindclear()
{
	FILE *fp;
	unsigned n;
	int i;

	n = 300;
	lay(n);

	if ((fp = fopen(path, "r")) == NULL)
		exit(2);
	drain(fp);
	rewind(fp);
	check("ftell is 0 after rewind", (unsigned)ftell(fp), 0);
	check("feof is clear after rewind", feof(fp) ? 1 : 0, 0);
	check("getc after rewind is the first byte", (unsigned)getc(fp), 'A');
	fclose(fp);

	if ((fp = fopen(path, "r")) == NULL)
		exit(2);
	for (i = 0; i < 2000; i++)	/* past BUFSIZ, so it reaches write(2) */
		putc('x', fp);
	check("writing a read-only stream sets ferror", ferror(fp) ? 1 : 0, 1);
	rewind(fp);
	check("ferror is clear after rewind", ferror(fp) ? 1 : 0, 0);
	fclose(fp);
}

/*
 * A stream taken over from a descriptor already positioned past 32767.  finit()
 * places the buffer pointers at the seek position modulo BUFSIZ, a remainder
 * that must be taken in unsigned or long arithmetic: a position whose low 16
 * bits are negative gives a negative remainder, and the pointers then address
 * memory BELOW the buffer, so the first getc reads whatever is there.
 */
farbuffer()
{
	FILE *fp;
	int fd;
	unsigned n, want, pos;
	long at;

	n = 40000;
	lay(n);

	/* 39004: its low 16 bits are negative as an int.  The position and the
	   byte expected there go through unsigned variables -- a decimal
	   constant this large is a long, and one in an argument list shifts
	   every argument after it. */
	at = 39004L;
	pos = 39005;
	want = buf[39004U];
	if ((fd = open(path, 0)) < 0)
		exit(2);
	if (lseek(fd, at, SEEK_SET) != at)
		exit(2);
	if ((fp = fdopen(fd, "r")) == NULL)
		exit(2);
	check("getc on a descriptor positioned at 39004",
		(unsigned)getc(fp), want);
	check("ftell agrees with the descriptor", (unsigned)ftell(fp), pos);
	fclose(fp);
}

/*
 * A zero-padded numeric conversion puts the pad between the sign and the
 * digits: "%05d" of -42 is "-0042", five characters wide with the sign first.
 * The pad character is only moved past a sign for a numeric conversion, so a
 * string or a character whose first byte happens to be '-' is padded in front
 * of it like any other text.  A space pad, and a left-adjusted field, place
 * every character exactly where they did.
 */
zeropad()
{
	char b[32];
	int n;
	long l;

	n = -42;
	l = -42L;

	sprintf(b, "%05d", n);
	checks("%05d of -42", b, "-0042");
	sprintf(b, "%05ld", l);
	checks("%05ld of -42", b, "-0042");
	sprintf(b, "%08d", n);
	checks("%08d of -42", b, "-0000042");
	sprintf(b, "%05d", -n);
	checks("%05d of 42", b, "00042");
	sprintf(b, "%03d", n);
	checks("%03d of -42", b, "-42");
	sprintf(b, "%5d", n);
	checks("%5d of -42", b, "  -42");
	sprintf(b, "%-05d", n);
	checks("%-05d of -42", b, "-42  ");
	sprintf(b, "%05s", "-ab");
	checks("%05s of -ab", b, "00-ab");
	sprintf(b, "%05c", '-');
	checks("%05c of -", b, "0000-");
}

/*
 * A successful stdio call leaves errno as it found it.  errno is only
 * meaningful after a call that reported failure, so a library routine that
 * clears it on the way through destroys the caller's diagnosis: the caller
 * asks why the earlier call failed and is told there was no error.
 *
 * The marker is a genuine failure -- close(-1) sets EBADF -- rather than an
 * assignment, so what is checked is a real errno.  check() itself prints, so
 * errno is copied into `e' before any of it runs.
 *
 * Each of the three routines that reach a system call on the read and write
 * paths is covered: _fgetb (a buffered read), _fgetc (an unbuffered one) and
 * fflush (the write).  The first getc on a buffered stream is not the subject
 * -- finit() runs there and its isatty() fails on a regular file, which is an
 * error, and reports it -- so the buffered case forces a second read by
 * running past the end of the first buffer.
 */
marker()
{
	if (close(-1) != -1) {
		printf("stdiobound: close(-1) succeeded\n");
		exit(2);
	}
	return (errno);
}

errnokeep()
{
	FILE *fp;
	unsigned n;
	int i, e, want;

	n = 2000;			/* several buffers */
	lay(n);

	if ((fp = fopen(path, "r")) == NULL)
		exit(2);
	getc(fp);			/* finit: buffer, isatty, first read */
	want = marker();
	for (i = 1; i <= BUFSIZ + 1; i++)	/* into the second buffer */
		if (getc(fp) == EOF)
			exit(2);
	e = errno;
	check("a buffered read leaves errno alone", (unsigned)e, (unsigned)want);
	fclose(fp);

	if ((fp = fopen(path, "r")) == NULL)
		exit(2);
	setbuf(fp, (char *)0);		/* unbuffered: read one byte a time */
	want = marker();
	if (getc(fp) != 'A')
		exit(2);
	e = errno;
	check("an unbuffered read leaves errno alone", (unsigned)e,
		(unsigned)want);
	fclose(fp);

	if ((fp = fopen(path, "w")) == NULL)
		exit(2);
	for (i = 0; i < 40; i++)
		putc('x', fp);
	want = marker();
	if (fflush(fp) != 0)
		exit(2);
	e = errno;
	check("fflush leaves errno alone", (unsigned)e, (unsigned)want);
	fclose(fp);
}

main(argc, argv)
int argc;
char **argv;
{
	unsigned sz;
	char *malloc();

	sz = DATA;
	if ((buf = malloc(sz)) == (char *)0) {
		printf("stdiobound: cannot allocate %u bytes\n", sz);
		exit(2);
	}
	strcpy(path, argc > 1 ? argv[1] : ".");
	strcat(path, "/stdiobound.tmp");

	printf("stdiobound: item counts across 32767\n");
	counts();

	printf("stdiobound: pushback and the stream flags\n");
	ungoteof();
	eofclear();
	ungotpos();
	rewindclear();

	printf("stdiobound: a stream over a descriptor past 32767\n");
	farbuffer();

	printf("stdiobound: zero padding and the sign\n");
	zeropad();

	printf("stdiobound: errno across a successful call\n");
	errnokeep();

	unlink(path);
	printf("stdiobound: %d failed\n", fails);
	exit(fails != 0);
}
