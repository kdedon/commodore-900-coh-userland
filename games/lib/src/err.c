/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * err(3) -- the BSD diagnostic family, for the games ports.
 *
 * The 3.2 libc has no <err.h>, and every BSD game calls err/errx/warn/warnx.
 * Rewriting each call site into fprintf+exit is both bulky and easy to get
 * wrong (the exit(1) is what err() adds over a bare fprintf), so the family
 * lives here once.
 *
 * Variadic without <stdarg.h>: COHERENT printf's "%r" format re-runs the
 * conversion from a format string and an argument list found in memory, so
 * `fprintf(stderr, "%r", &fmt)' prints fmt with everything the caller pushed
 * after it.  That is the same idiom cmd/cmp.c and cmd/learn.c use.
 *
 * The program name prefix comes from setprogname(argv[0]); games that never
 * call it print no prefix rather than a wrong one.
 */
#include <stdio.h>

extern int errno;
extern char *strerror();
extern char *rindex();

static char *_progname = (char *) 0;

void
setprogname(name)
	char *name;
{
	char *p;

	if (name == (char *) 0)
		return;
	if ((p = rindex(name, '/')) != (char *) 0)
		name = p + 1;
	_progname = name;
}

char *
getprogname()
{
	return (_progname == (char *) 0 ? "" : _progname);
}

static void
prefix()
{
	if (_progname != (char *) 0)
		fprintf(stderr, "%s: ", _progname);
}

/* errx(eval, fmt, ...) -- message, newline, exit. */
void
errx(eval, fmt)
	int eval;
	char *fmt;
{
	prefix();
	fprintf(stderr, "%r", &fmt);
	putc('\n', stderr);
	exit(eval);
}

/* err(eval, fmt, ...) -- as errx, plus ": " and the errno text. */
void
err(eval, fmt)
	int eval;
	char *fmt;
{
	int save = errno;

	prefix();
	fprintf(stderr, "%r", &fmt);
	fprintf(stderr, ": %s\n", strerror(save));
	exit(eval);
}

/* warnx(fmt, ...) -- message and newline, no exit. */
void
warnx(fmt)
	char *fmt;
{
	prefix();
	fprintf(stderr, "%r", &fmt);
	putc('\n', stderr);
}

/* warn(fmt, ...) -- as warnx, plus the errno text. */
void
warn(fmt)
	char *fmt;
{
	int save = errno;

	prefix();
	fprintf(stderr, "%r", &fmt);
	fprintf(stderr, ": %s\n", strerror(save));
}
