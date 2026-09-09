/*
 * strnlong -- the counted string routines against an operand longer than
 * 32767 bytes.
 *
 * strncmp, strncpy and strncat each measure their string argument with a
 * cpirb pre-pass and then clamp that length to n.  The clamp is a comparison
 * against a caller-supplied count, and both values are lengths, so it must be
 * unsigned: at 32768 bytes and above a signed compare reads the measured
 * length as negative, takes the "shorter than n" branch, and the routine then
 * works in units of the whole string instead of n.
 *
 * That makes strncmp answer from bytes past n, and makes strncpy and strncat
 * write past the end of a destination sized for n.  Both are silent.  The
 * boundary is exact, so each case here runs twice: once with a string under
 * the limit and once over it, and the two must agree.
 */
#include <stdio.h>

#define	LONGLEN	33000L		/* over 32767; L, or the constant itself overflows int */
#define	SHORTLEN 20000L		/* under it, same code path otherwise */
#define	DSTSIZE	34000L

char	s2[33001];
char	pad[16];

int	fails;

/*
 * Report one case and count a failure.
 */
check(what, got, want)
char *what;
long got, want;
{
	if (got == want)
		printf("  ok   %s\n", what);
	else {
		printf("  FAIL %s: got %ld, want %ld\n", what, got, want);
		fails++;
	}
}

/*
 * Fill s2 with a known prefix and `len' total bytes.
 */
fill(len)
long len;
{
	long i;

	strcpy(s2, "HELLO-WORLD-");
	for (i = 12; i < len; i++)
		s2[i] = 'a';
	s2[len] = '\0';
}

/*
 * strncmp must look at no more than n bytes, so a difference past n is
 * invisible and the two strings compare equal.
 */
docmp(len)
long len;
{
	fill(len);
	check("strncmp(long, \"HELLO-XXXX\", 5) == 0",
		(long)strncmp(s2, "HELLO-XXXX", 5), 0L);
	check("strncmp(long, \"HELLO-WORLD\", 11) == 0",
		(long)strncmp(s2, "HELLO-WORLD", 11), 0L);
	check("strncmp(long, \"HELLO-ZZZZZ\", 7) != 0",
		strncmp(s2, "HELLO-ZZZZZ", 7) != 0 ? 1L : 0L, 1L);
}

/*
 * strncpy writes exactly n bytes.  The first byte past n must be untouched.
 */
docpy(len, dst)
long len;
char *dst;
{
	long i;

	fill(len);
	for (i = 0; i < DSTSIZE; i++)
		dst[i] = '#';
	strncpy(dst, s2, 10);
	for (i = 10; i < DSTSIZE; i++)
		if (dst[i] != '#')
			break;
	check("strncpy(dst, long, 10) writes no byte past n", i, DSTSIZE);
	check("strncpy(dst, long, 10) copies the prefix",
		(long)strncmp(dst, "HELLO-WORL", 10), 0L);
}

/*
 * strncat appends at most n bytes plus the terminator, so the result is n+1
 * bytes longer than the string it started from.
 */
docat(len, dst)
long len;
char *dst;
{
	long i;

	fill(len);
	for (i = 0; i < DSTSIZE; i++)
		dst[i] = '#';
	strcpy(dst, "PFX");
	strncat(dst, s2, 10);
	check("strncat(dst, long, 10) length is 3+10", (long)strlen(dst), 13L);
	check("strncat(dst, long, 10) leaves byte 14 alone",
		dst[14] == '#' ? 1L : 0L, 1L);
}

/*
 * A count of zero.  LDIRB and CPSIRB decrement the count before they test it,
 * so zero is one short of the whole 64K range the Z8000 manual gives them
 * (1..65536) and the block instruction runs to completion instead of not at
 * all -- 65536 bytes moved or scanned from a call that must touch nothing.
 * A blank line through strncpy(d, s, 0) was enough to write 64K over the heap.
 */
dozero(dst)
char *dst;
{
	long i;

	fill(SHORTLEN);
	for (i = 0; i < DSTSIZE; i++)
		dst[i] = '#';
	strncpy(dst, s2, 0);
	for (i = 0; i < DSTSIZE; i++)
		if (dst[i] != '#')
			break;
	check("strncpy(dst, long, 0) writes nothing", i, DSTSIZE);

	strcpy(dst, "PFX");
	for (i = 4; i < DSTSIZE; i++)
		dst[i] = '#';
	strncat(dst, s2, 0);
	check("strncat(dst, long, 0) length stays 3", (long)strlen(dst), 3L);
	check("strncat(dst, long, 0) leaves byte 4 alone",
		dst[4] == '#' ? 1L : 0L, 1L);

	check("strncmp(long, \"ZZZ\", 0) == 0",
		(long)strncmp(s2, "ZZZ", 0), 0L);
	check("strncmp(\"\", \"ZZZ\", 0) == 0",
		(long)strncmp("", "ZZZ", 0), 0L);
}

main()
{
	char *dst;
	char *malloc();
	unsigned sz;

	/* DSTSIZE will not fit in bss beside s2, and 34000 is not a valid
	   16-bit int constant, so the size goes through an unsigned. */
	sz = DSTSIZE;
	if ((dst = malloc(sz)) == (char *)0) {
		printf("strnlong: cannot allocate %u bytes\n", sz);
		exit(1);
	}

	printf("strnlong: string of %ld bytes (under the limit)\n", SHORTLEN);
	docmp(SHORTLEN);
	docpy(SHORTLEN, dst);
	docat(SHORTLEN, dst);

	printf("strnlong: string of %ld bytes (over the limit)\n", LONGLEN);
	docmp(LONGLEN);
	docpy(LONGLEN, dst);
	docat(LONGLEN, dst);

	printf("strnlong: a count of zero\n");
	dozero(dst);

	printf("strnlong: %d failed\n", fails);
	exit(fails != 0);
}
