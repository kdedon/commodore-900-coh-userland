/*
 * Callee-saved registers across the double runtime.  Driven through the
 * normal, zero and overflow paths so the retz and retinf epilogues are
 * exercised as well as the main one.
 */
#include <stdio.h>

static	double	a = 3.5;
static	double	b = 2.0;
static	double	z = 0.0;
static	double	h = 1.0e300;

main()
{
	printf("dlmul %d %d %d\n", cldmul(&a, &b), cldmul(&a, &z), cldmul(&h, &h));
	printf("dladd %d %d\n", cldadd(&a, &b), cldadd(&h, &h));
	printf("dldiv %d %d %d\n", clddiv(&a, &b), clddiv(&a, &z), clddiv(&z, &a));
	return (0);
}
