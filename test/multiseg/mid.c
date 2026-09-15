/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

fmid(a, b)
int a, b;
{
	int t;
	t = a * 51 + b * 3;
	t = t ^ (b << 2);
	return t - (a & 0x0F0F);
}

callthru(fp, a, b)
int (*fp)();
int a, b;
{
	return (*fp)(a, b);
}
