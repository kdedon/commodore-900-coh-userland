/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

extern int f0();
extern int fmid();
extern int t4h3();

static int (*t4tab[3])() = { f0, fmid, t4h3 };

t4h2(a, b)
int a, b;
{
	return t4h3(a, b) + 17;
}

t4fpsum(a, b)
int a, b;
{
	register int i;
	register int s;

	s = 0;
	for (i = 0; i < 3; i++)
		s += (*t4tab[i])(a, b);
	return s;
}
