/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * vprintf.c -- is the refactored formatter real?
 *
 * libc/stdio/printf.c was split so that printf, fprintf, sprintf, vfprintf,
 * vsprintf and vsnprintf all reach one conversion loop, _doprnt().  Two
 * things could go wrong and neither shows up as a compile error: the va_list
 * walk could disagree with the walk printf() does over its own arguments, so
 * a vsprintf'd long or pointer comes out as garbage; and the character count
 * the routines now return could be wrong, which nothing notices until a
 * caller uses it to append.
 *
 * So every case is asked TWICE -- once through sprintf, which walks the
 * argument list itself, and once through a varargs function that walks it
 * with va_start/va_arg and hands the result to vsprintf.  The two strings must
 * be identical, and the count vsprintf returns must be the string's length.
 * A case that gets the same wrong answer both ways is caught by the expected
 * text, which is written out here and not computed.
 *
 * The verdict is the exit status, and every check prints its own line: a
 * probe that returns 0 having checked nothing is worse than no probe at all.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static	int	fails;
static	int	checks;

static
ck(what, got, want)
char *what, *got, *want;
{
	checks++;
	if (strcmp(got, want) == 0)
		printf("vprintf: %-22s \"%s\"  OK\n", what, got);
	else {
		fails++;
		printf("vprintf: %-22s \"%s\" want \"%s\"  FAIL\n",
			what, got, want);
	}
}

static
cknum(what, got, want)
char *what;
int got, want;
{
	checks++;
	if (got == want)
		printf("vprintf: %-22s %d  OK\n", what, got);
	else {
		fails++;
		printf("vprintf: %-22s %d want %d  FAIL\n", what, got, want);
	}
}

/*
 * The varargs half: the same conversion, reached through a va_list.
 * Returns what vsprintf returned.
 */
static
vform(buf, fmt)
char *buf;
char *fmt;
{
	va_list	ap;
	int	n;

	va_start(ap, fmt);
	n = vsprintf(buf, fmt, ap);
	va_end(ap);
	return (n);
}

static
vformn(buf, size, fmt)
char *buf;
int size;
char *fmt;
{
	va_list	ap;
	int	n;

	va_start(ap, fmt);
	n = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return (n);
}

main()
{
	char	a[128], b[128];
	int	n;

	/* int, the ordinary case */
	sprintf(a, "n=%d x=%x", 4242, 4242);
	n = vform(b, "n=%d x=%x", 4242, 4242);
	ck("int  sprintf", a, "n=4242 x=1092");
	ck("int  vsprintf", b, a);
	cknum("int  count", n, strlen(a));

	/* long: two machine words on this target, so the argument walk is
	 * where a va_list and printf's own bump() would part company */
	sprintf(a, "l=%D u=%U", 123456789L, 4000000000L);
	n = vform(b, "l=%D u=%U", 123456789L, 4000000000L);
	ck("long sprintf", a, "l=123456789 u=4000000000");
	ck("long vsprintf", b, a);
	cknum("long count", n, strlen(a));

	/* a far pointer costs two words too, and is what syslog(3) passes */
	sprintf(a, "[%s][%s]", "one", "two");
	n = vform(b, "[%s][%s]", "one", "two");
	ck("str  sprintf", a, "[one][two]");
	ck("str  vsprintf", b, a);
	cknum("str  count", n, strlen(a));

	/* width, precision, left adjust, zero pad -- the padding arithmetic
	 * is where the new character count is computed */
	sprintf(a, "|%6d|%-6d|%06d|%.3s|", 42, 42, 42, "abcdef");
	n = vform(b, "|%6d|%-6d|%06d|%.3s|", 42, 42, 42, "abcdef");
	ck("pad  sprintf", a, "|    42|42    |000042|abc|");
	ck("pad  vsprintf", b, a);
	cknum("pad  count", n, strlen(a));

	/* mixed widths in one call, which is how a real message looks */
	sprintf(a, "%s[%d]: %s", "syslogd", 17, "start");
	n = vform(b, "%s[%d]: %s", "syslogd", 17, "start");
	ck("mix  sprintf", a, "syslogd[17]: start");
	ck("mix  vsprintf", b, a);
	cknum("mix  count", n, strlen(a));

	/* the bounded form syslog(3) uses: the string is clipped, the count
	 * says how much was stored, and nothing is written past the end */
	memset(b, '@', sizeof(b));
	n = vformn(b, 8, "%s-%s", "abcdef", "ghijkl");
	ck("clip vsnprintf", b, "abcdef-");
	cknum("clip count", n, 7);
	cknum("clip no overrun", b[8], '@');

	/* a bound larger than the text must not pad or truncate */
	n = vformn(b, 64, "%d/%d", 7, 8);
	ck("fit  vsnprintf", b, "7/8");
	cknum("fit  count", n, 3);

	/* printf itself must still count what it printed */
	n = printf("vprintf: printf returns   ");
	cknum("printf count", n, 26);

	printf("vprintf: %d checks, %d failed\n", checks, fails);
	exit(fails != 0);
}
