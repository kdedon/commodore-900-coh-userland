/*
 * polltty.c -- can poll(2) wait on a terminal?
 *
 * Without a c_poll entry point in the tty driver, dpoll() answers POLLNVAL
 * for every tty: a program that waits on the keyboard and a socket in the
 * same poll() -- hunt, tetris, atc, anything with an event loop -- is told
 * its terminal is not a pollable object at all, and quits.
 *
 * Three things have to hold, and only the first is about POLLNVAL:
 *
 *   1. poll() ACCEPTS the descriptor (revents has no POLLNVAL bit).
 *   2. With nothing typed, it TIMES OUT rather than returning ready.  A driver
 *      that answers "always readable" passes test 1 and still spins a game at
 *      100% cpu on an empty terminal, so the timeout is the real check.
 *   3. POLLOUT is set: the console is writable.
 *
 * The second argument of poll() is unsigned long in this ABI and there are no
 * prototypes, so a bare `1' would pass a 16-bit value where 32 are read and
 * poll() would see a garbage count -- see libc/gen/select.c.  Every call here
 * casts, which is the same trap every vendored program hits.
 *
 * Every poll here carries a timeout, but a timeout is a request to the same
 * driver that is under suspicion: a c_poll entry point that enqueues the waiter
 * and never arms the timer blocks all three of them for ever.  DEADLINE is the
 * bound that does not depend on the code being tested.
 */
#include <poll.h>
#include <signal.h>
#include <stdio.h>

#define DEADLINE 30			/* seconds for the whole run */

int	fails;

/*
 * The deadline expired.
 */
static
hung()
{
	printf("polltty: FAIL -- no verdict within %d s.  A poll(2) with a"
		" 1-second timeout\n", DEADLINE);
	printf("polltty: that has not returned means the tty's c_poll enqueued"
		" the waiter and\n");
	printf("polltty: never armed the timer.  Reported rather than hung: a"
		" test that hangs\n");
	printf("polltty: has no verdict.\n");
	fflush(stdout);
	exit(1);
}

static void ck(what, got, want)
char *what;
int got, want;
{
	printf("polltty: %-34s got %3d want %3d  %s\n", what, got, want,
		got == want ? "ok" : "FAIL");
	if (got != want)
		fails++;
	fflush(stdout);
}

int main(argc, argv)
int argc;
char **argv;
{
	struct pollfd set[1];
	int n;

	(void)signal(SIGALRM, hung);
	(void)alarm(DEADLINE);

	/* Nothing has been typed, so a 1-second wait must expire. */
	set[0].fd = 0;
	set[0].events = POLLIN;
	set[0].revents = 0;
	n = poll(set, (unsigned long)1, 1000);
	ck("poll(tty, POLLIN, 1s) returns", n, 0);
	ck("  revents has no POLLNVAL", set[0].revents & POLLNVAL, 0);

	/* The console is always writable. */
	set[0].fd = 1;
	set[0].events = POLLOUT;
	set[0].revents = 0;
	n = poll(set, (unsigned long)1, 1000);
	ck("poll(tty, POLLOUT) is ready", n, 1);
	ck("  revents has POLLOUT", (set[0].revents & POLLOUT) != 0, 1);
	ck("  revents has no POLLNVAL", set[0].revents & POLLNVAL, 0);

	/* A zero timeout must not block and must not claim input. */
	set[0].fd = 0;
	set[0].events = POLLIN;
	set[0].revents = 0;
	n = poll(set, (unsigned long)1, 0);
	ck("poll(tty, POLLIN, 0) polls", n, 0);

	printf("polltty: %s\n", fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
