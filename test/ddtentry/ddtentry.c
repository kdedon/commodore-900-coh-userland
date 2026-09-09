/*
 * ddtentry -- can the in-kernel debugger be entered and left?
 *
 * halt(2) is the deliberate way in (trap.c uhalt, root only).  A kernel built
 * KDDT=1 answers with ddt's banner and a `*' prompt; one built without it falls
 * through md.s's `jr .' and the machine stops, so this is only worth running on
 * a KDDT kernel -- the marker below says which side of the call the run reached.
 *
 * Drive it with `r' (dump registers) then `c' (continue); the second marker
 * proves ddt returned rather than left the machine wedged.
 *
 * halt(2) RETURNS A VALUE and it has to be tested: reaching the next
 * statement cannot tell a run that entered the debugger and continued from
 * one where the call was refused and nothing happened.  uhalt() is root-only,
 * so a non-root run answers -1/EPERM; an instrument that does not implement
 * the call at all answers -1 too.
 *
 * errno is read as an argument to printf rather than after one: fflush() and
 * _fputc() open with `errno = 0' (libc/stdio/fflush.c:16, _fputc.c:17), so a
 * print between the call and the report destroys the value.
 */
#include <errno.h>
#include <stdio.h>

extern int errno;

main()
{
	int r, e;

	printf("ddtentry: entering ddt\n");
	fflush(stdout);
	r = halt();
	e = errno;
	if (r < 0) {
		printf("ddtentry: FAIL -- halt(2) refused, errno %d%s\n", e,
			e == EPERM ? " (EPERM: not root)" : "");
		exit(1);
	}
	printf("ddtentry: back from ddt\n");
	exit(0);
}
