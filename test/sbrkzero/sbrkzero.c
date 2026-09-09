/*
 * sbrkzero.c -- is sbrk()-grown memory zero-filled?
 *
 * The ported Minix inet stack depends on it absolutely: alloc() is a thin sbrk()
 * wrapper, and every `#if ZERO' block in the stack (eight files) comments OUT
 * the initialisation of the tables it returns, because on Minix that memory
 * arrives zeroed.  If Coherent does not do the same, those tables start as
 * garbage -- which is what "icmp.c, 148: unknown state 27680" reported.
 *
 * Checks three things separately, because they can fail independently:
 *   1. the pointer sbrk() returns is sane (it is a far pointer -- a missing
 *      `char *' declaration truncates it);
 *   2. a fresh region reads back as zero;
 *   3. it is still zero after a second, larger growth (which may relocate the
 *      segment, and the kernel must clear the new part).
 * Every check is a ck(), and the exit status carries the verdict.
 *
 * The refusal sentinel is NULL, not (char *)-1: libc/sys/sbrk.c returns
 * NULL when brk(2) sets errno, so a test against (char *)-1 can never be
 * true and a refused sbrk would fall through to reading segment 0.
 */
#include <stdio.h>
#include <unistd.h>

int	fails;

/*
 * Report one check and count a failure.  `got' and `want' are longs because
 * the interesting quantities here are byte counts and far addresses.
 */
static void ck(what, got, want)
char *what;
long got, want;
{
	printf("sbrkzero: %-44s got %ld want %ld  %s\n", what, got, want,
		got == want ? "ok" : "FAIL");
	if (got != want)
		fails++;
	fflush(stdout);
}

static int nonzero(p, n)
char *p;
unsigned n;
{
	unsigned i, bad;

	bad = 0;
	for (i = 0; i < n; i++)
		if (p[i] != 0)
			bad++;
	return bad;
}

main()
{
	char *a, *b, *c, *s;
	unsigned na, nb;

	/* A SMALL first request is the interesting case.  exec sets the break at
	 * the end of bss, but the data segment is rounded up to a whole click, so
	 * a small sbrk is satisfied from the slack inside the last click and the
	 * segment never grows -- which means the kernel's grow path, and its
	 * clearing, never runs.  A first request of 4096 would force a growth
	 * and hide the case completely. */
	printf("break at start 0x%lx\n", (long)sbrk(0));
	s = sbrk(16);
	printf("sbrk(16) = 0x%lx\n", (long)s);
	ck("sbrk(16) was granted", s != NULL ? 1L : 0L, 1L);
	if (s == NULL)
		return 1;
	ck("sbrk(16) region is zero-filled", (long)nonzero(s, 16), 0L);

	na = 4096;
	nb = 20000;

	a = sbrk(na);
	printf("sbrk(%u) = 0x%lx\n", na, (long)a);
	/* NULL, not (char *)-1: libc/sys/sbrk.c:66. */
	if (a == NULL) { printf("sbrkzero: FAIL: sbrk refused\n"); return 1; }
	ck("first region is zero-filled", (long)nonzero(a, na), 0L);

	/* Dirty it, then grow again: the second growth must be clear even
	 * though the first is now full of data. */
	for (nb = 0; nb < na; nb++)
		a[nb] = 0xA5;
	nb = 20000;
	b = sbrk(nb);
	printf("sbrk(%u) = 0x%lx\n", nb, (long)b);
	if (b == NULL) { printf("sbrkzero: FAIL: second sbrk refused\n"); return 1; }
	ck("second region is zero-filled", (long)nonzero(b, nb), 0L);
	/* The control.  nonzero() reporting 0 for a region that IS zero proves
	 * nothing on its own -- a nonzero() that always answered 0, or a read
	 * that always returned 0, would satisfy every check above.  The region
	 * just written must read back as entirely non-zero. */
	ck("first region is still the dirty one", (long)nonzero(a, na), (long)na);

	c = sbrk(0);
	/* The break must be exactly where the last block ended: sbrk() reports
	 * the OLD break and moves it by the increment, so anything else means
	 * the two disagree about how much was handed out. */
	ck("break is where the last block ended", (long)c, (long)b + (long)nb);

	/* Walk the break PAST the end of the first segment.
	 *
	 * A pointer is seg:off with a 16-bit offset and arithmetic does not carry
	 * out of it, so a block may never straddle a boundary.  sbrk() answers
	 * that by abandoning the tail of a segment and starting the next block at
	 * offset 0 of the next segment; the kernel maps a data region across as
	 * many hardware segments as its size needs (uproto).
	 *
	 * Three outcomes are distinguished, because they have different causes:
	 *   - the offset goes BACKWARDS within the same segment: the address
	 *     wrapped and now aliases memory the program already owns.  This is
	 *     the silent corruption the whole exercise exists to prevent.
	 *   - the segment number advances: the carry worked.  Then WRITE to the
	 *     block and read it back, because a plausible-looking address that
	 *     is not actually mapped is the next thing that goes wrong.
	 *   - NULL: refused, which is safe but means data past 64K is still
	 *     unavailable.
	 */
	{
		unsigned long prev, cur;
		int n, carried;

		carried = 0;
		prev = (unsigned long)sbrk(0);
		for (n = 0; n < 40; n++) {
			c = sbrk(4096);
			if (c == NULL) {
				printf("sbrk refused after %d grows, last 0x%lx:"
					" %s\n", n, prev,
					carried ? "carried then refused"
						: "REFUSED-NO-CARRY");
				if (!carried)
					fails++;
				break;
			}
			cur = (unsigned long)c;
			if ((cur >> 24) == (prev >> 24) &&
			    (cur & 0xFFFFL) < (prev & 0xFFFFL)) {
				printf("sbrk WRAPPED: 0x%lx then 0x%lx"
					" (offset went backwards): FAIL\n",
					prev, cur);
				return 1;
			}
			if ((cur >> 24) != (prev >> 24)) {
				printf("carried into segment %ld at 0x%lx"
					" (was 0x%lx)\n",
					cur >> 24, cur, prev);
				carried = 1;
				/* The new segment must be real memory. */
				c[0] = 0x5A;
				c[4095] = 0x3C;
				if (c[0] != 0x5A || c[4095] != 0x3C) {
					printf("carried block is NOT usable:"
						" FAIL\n");
					return 1;
				}
				printf("carried block reads back: OK\n");
			}
			prev = cur;
		}
		printf("40 grows, last 0x%lx, carried=%d\n", prev, carried);
		ck("the break crossed a segment boundary", (long)carried, 1L);
	}
	printf("sbrkzero: %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
	fflush(stdout);
	return fails ? 1 : 0;
}
