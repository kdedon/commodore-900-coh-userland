/*
 * loadtest.c -- call mgrload's getload() without an MGR server in front of it.
 *
 * mgrload(1) cannot be run outside a window, so the backend that produces its
 * numbers -- src/clients/portable/mgrload/coherent.c, reached through
 * getload.c -- is exercised here instead.  It reads a fixed /coherent and
 * /dev/kmem, so the run supplies both: build a forged kernel data image with
 * mkkmem, put it and a real kernel image where those two names resolve, and
 * the value printed is the one a bar would be drawn from.
 *
 * The answer is in centiloads.  A first call is the primer and must be 0
 * whatever the forged table says; a second call against an image that has not
 * changed measures a zero interval and must also be 0.  Anything else means
 * the priming or the clamp is wrong.
 *
 * Build and run (host cross toolchain, under n2z8001 -runexec):
 *	ccz -s -i -L -Ios/include -Ios/include/sys -Imgr/.../mgrload \
 *	    -DCOHERENT -o loadtest loadtest.c mgr/.../mgrload/getload.c
 *	mkdir -p root/dev
 *	cp kernel.out root/coherent; mkkmem kernel.out root/dev/kmem
 *	N2ROOT=root n2z8001 -runexec loadtest 3
 */
#include <stdio.h>
#include "getload.h"

main(argc, argv)
int argc;
char *argv[];
{
	int n, i, v;

	n = 2;
	if (argc > 1)
		n = atoi(argv[1]);
	for (i=0; i<n; i++) {
		v = getload();
		printf("call %d: %d centiloads (%d.%02d)\n", i, v,
			v/100, v%100);
	}
	exit(0);
}
