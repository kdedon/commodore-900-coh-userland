/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

extern int f0();
extern int fmid();

ftail(a, b)
int a, b;
{
	int t;
	t = a * 77 + b * 5;
	t = t ^ (a << 4);
	return t + (b & 0x3333);
}

fback(a, b)
int a, b;
{
	return f0(a, b) + fmid(a, b) + 777;
}

int (*
getfp())()
{
	return fmid;
}
