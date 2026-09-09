extern int f0();
extern int fmid();
extern int ftail();
extern int fback();
extern int (*getfp())();
extern int callthru();

static int (*sfp)() = ftail;

static char big[40000];

static
dup0(a, b)
int a, b;
{
	int t;
	t = a * 1 + b;
	t = t ^ (a << 3);
	t = t + (b >> 1);
	t = t - (a & 0x5A5A);
	t = t | (b + 0);
	return t;
}

static
dupmid(a, b)
int a, b;
{
	int t;
	t = a * 51 + b * 3;
	t = t ^ (b << 2);
	return t - (a & 0x0F0F);
}

static
duptail(a, b)
int a, b;
{
	int t;
	t = a * 77 + b * 5;
	t = t ^ (a << 4);
	return t + (b & 0x3333);
}

static
ok(s, n)
char *s;
int n;
{
	write(1, s, n);
}

static int nfail;

static
bad(s, n)
char *s;
int n;
{
	nfail++;
	write(1, s, n);
}

main()
{
	register int x, y;
	register unsigned j;
	int nz;
	int (*fp)();

	write(1, "t3 start\n", 9);

	x = f0(3, 5);
	if (x == dup0(3, 5))
		ok("t3 near ok\n", 11);
	else
		bad("t3 FAIL near\n", 13);

	x = ftail(7, 9);
	if (x == duptail(7, 9))
		ok("t3 far ok\n", 10);
	else
		bad("t3 FAIL far\n", 12);

	x = fback(11, 13);
	y = dup0(11, 13) + dupmid(11, 13) + 777;
	if (x == y)
		ok("t3 back ok\n", 11);
	else
		bad("t3 FAIL back\n", 13);

	x = callthru(sfp, 6, 8);
	if (x == duptail(6, 8))
		ok("t3 fp1 ok\n", 10);
	else
		bad("t3 FAIL fp1\n", 12);

	fp = getfp();
	x = (*fp)(9, 4);
	if (x == dupmid(9, 4))
		ok("t3 fp2 ok\n", 10);
	else
		bad("t3 FAIL fp2\n", 12);

	/* bssv: the 40K BSS through runtime indexing only -- clearing,
	   then a write/read at both ends and the middle */
	nz = 0;
	for (j = 0; j < 40000; j += 997)
		if (big[j] != 0)
			nz++;
	j = 0;
	big[j] = 0x5A;
	j = 19999;
	big[j] = 0x3C;
	j = 39999;
	big[j] = 0x69;
	x = 0;
	j = 0;
	if (big[j] == 0x5A) x++;
	j = 19999;
	if (big[j] == 0x3C) x++;
	j = 39999;
	if (big[j] == 0x69) x++;
	if (nz == 0 && x == 3)
		ok("t3 bssv ok\n", 11);
	else
		bad("t3 FAIL bssv\n", 13);

	/* bssc: the same cells through CONSTANT displacements past 0x8000.
	   Kept LAST: a miscompiled displacement targets the wrong hardware
	   segment and this dies by SIGSEGV rather than by marker */
	big[39999] = 0x77;
	j = 39999;
	if (big[j] == 0x77 && big[0x9C3F] == 0x77)
		ok("t3 bssc ok\n", 11);
	else
		bad("t3 FAIL bssc\n", 13);

	if (nfail == 0) {
		write(1, "t3 PASS\n", 8);
		return 0;
	}
	return 1;
}
