/*
 * pollhup.c -- does poll(2) report EOF on a pipe nobody can write to?
 *
 * POSIX: a pipe whose every write end has been closed reports POLLHUP, and a
 * read of it returns 0.  Without that, every program that multiplexes a pipe
 * against something else hangs at the point its child exits -- select() never
 * returns, so the program never learns the child is gone and never reaps it.
 *
 * tests/pollpipe asks whether a WRITE while blocked wakes the poller;
 * tests/pollfifo asks the same of a named FIFO; tests/pollexit asks what a
 * signal does to the event buffer.  None of the three ever closes a write end,
 * so none of them can see this.  This one closes it, two different ways:
 *
 *   A  the write end is ALREADY closed when poll() is entered  -- a level test
 *      of ppoll() itself, which needs no wakeup to pass.
 *   D  the last write end is closed by a CHILD that exits WHILE the parent is
 *      already blocked in poll() -- an edge test, which needs pclose() to
 *      pollwake the reader's event queue.
 *
 * Those are DIFFERENT defects with different fixes, and from outside a program
 * both look like "select() hangs", so each is asked on its own.  Every case
 * that could block carries a timeout and prints revents in octal, so the three
 * outcomes stay apart:
 *
 *	r == 0			 never woke / never noticed	(hard fail)
 *	r == 1, revents == 020	 POLLHUP			(correct)
 *	r == 1, revents == other woke but reported the wrong event
 *
 * Case B is the control that keeps a fix honest: a pipe that still HAS a writer
 * must NOT report a hangup.  A `return POLLHUP always' passes A and D and fails
 * B.
 *
 * The second argument of poll(2) is unsigned long in this ABI and there are no
 * prototypes: `1L', never `1'.
 *
 * EVERY POLL HERE IS CAPPED, BUT THE READS ARE NOT POLLS.  The whole claim of
 * cases A, D and E is that a read of a writerless pipe returns 0 rather than
 * blocking -- so on the kernel this exists to doubt, the `read of it returns
 * EOF' lines are precisely where it would block for ever, and a wait(2) for a
 * child that never dies is another.  A test that hangs has no verdict, which is
 * worse than one that fails, so the whole run is under a deadline.
 */
#include <sys/types.h>
#include <sys/select.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>

extern int errno;

#define DEADLINE 90			/* seconds for the whole run */

int	fails;

/*
 * The deadline expired.  `step' names the wait that was outstanding, because
 * "pollhup hung" and "the read of a writerless pipe blocked" are the same
 * event seen from different distances, and only the second is a bug report.
 */
static char *step = "startup";

static
hung()
{
	printf("pollhup: FAIL -- no verdict within %d s, waiting on: %s\n",
		DEADLINE, step);
	printf("pollhup: a blocking read of a pipe with no writer is one of the"
		" defects\n");
	printf("pollhup: this program is looking for; it is reported here"
		" rather than hung.\n");
	fflush(stdout);
	exit(1);
}

static
say(s)
char *s;
{
	printf("pollhup: %s\n", s);
	fflush(stdout);
}

static
ck(what, got, want)
char *what;
int got, want;
{
	printf("pollhup: %-40s got %3d want %3d  %s\n", what, got, want,
		got == want ? "ok" : "FAIL");
	if (got != want)
		fails++;
	fflush(stdout);
}

/*
 * Report one poll outcome in the three-way form described above, and count the
 * failure.  `want' is the revents expected; 0 means "expected to time out".
 *
 * `e' is the errno the CALLER copied before anything printed.  Reading errno
 * in here would always report 0: the printf above these lines flushes on its
 * newline and fflush() opens with `errno = 0' (libc/stdio/fflush.c:16).
 */
static
ckpoll(what, r, revents, want, e)
char *what;
int r, revents, want, e;
{
	printf("pollhup: %-24s rc %2d revents 0%o want 0%o  ",
		what, r, revents & 0777, want & 0777);
	if (r < 0)
		printf("FAIL (errno %d)\n", e);
	else if (want == 0)
		printf(r == 0 ? "ok\n" : "FAIL (woke, should have timed out)\n");
	else if (r == 0)
		printf("FAIL (NEVER WOKE / no event reported)\n");
	else if ((revents & want) == want)
		printf("ok\n");
	else
		printf("FAIL (woke with the WRONG event)\n");
	fflush(stdout);
	if (r < 0 || (want == 0 ? r != 0 : (r == 0 || (revents & want) != want)))
		fails++;
}

main()
{
	struct pollfd pfd;
	fd_set rd;
	struct timeval tv;
	int p[2], r, n, kid, st;
	int e;			/* errno, copied before anything prints */
	char buf[16];

	(void)signal(SIGALRM, hung);
	(void)alarm(DEADLINE);

	/*
	 * A -- the write end is closed before poll() is ever entered.  ppoll()
	 * only has to look at the pipe's writer counts to answer this, so a
	 * failure here is purely a missing level check.
	 */
	if (pipe(p) < 0) {
		say("FAIL pipe");
		return 1;
	}
	(void)close(p[1]);
	say("A poll(read end, POLLIN, 5s) with the write end already closed");
	pfd.fd = p[0];
	pfd.events = POLLIN;
	pfd.revents = 0;
	r = poll(&pfd, 1L, 5000);
	e = errno;			/* before ckpoll prints: see ckpoll() */
	ckpoll("A writerless pipe", r, pfd.revents, POLLHUP, e);
	step = "A, the read of a writerless pipe";
	n = read(p[0], buf, sizeof(buf));
	ck("A   read of it returns EOF", n, 0);
	(void)close(p[0]);

	/*
	 * B -- the control.  Both ends still open and nothing written: poll()
	 * must time out, reporting neither data nor a hangup.
	 */
	if (pipe(p) < 0) {
		say("FAIL pipe B");
		return 1;
	}
	say("B poll(read end, POLLIN, 2s) with the write end still OPEN");
	pfd.fd = p[0];
	pfd.events = POLLIN;
	pfd.revents = 0;
	r = poll(&pfd, 1L, 2000);
	e = errno;
	ckpoll("B live pipe", r, pfd.revents, 0, e);
	(void)close(p[0]);
	(void)close(p[1]);

	/*
	 * C -- the read end is closed instead.  Nothing can ever consume what
	 * is written, so POLLOUT must not be promised: POSIX reports POLLERR.
	 * Same defect family as A -- ppoll() enqueues the waiter and sleeps --
	 * and it is what makes a writer half of a multiplexing program hang.
	 * The pipe is NOT written to here: that would raise SIGPIPE.
	 */
	if (pipe(p) < 0) {
		say("FAIL pipe C");
		return 1;
	}
	(void)close(p[0]);
	say("C poll(write end, POLLOUT, 5s) with the read end closed");
	pfd.fd = p[1];
	pfd.events = POLLOUT;
	pfd.revents = 0;
	r = poll(&pfd, 1L, 5000);
	e = errno;
	ckpoll("C readerless pipe", r, pfd.revents, POLLERR, e);
	(void)close(p[1]);

	/*
	 * D -- the shape that matters.  The parent is ALREADY blocked in poll()
	 * when the last write end goes away, and it goes away the way it does
	 * in real programs: a child exits.  Passing A and failing D means
	 * ppoll() answers correctly but nothing wakes a poller that is already
	 * asleep -- pclose() has to pollwake the reader's queue.
	 *
	 * The parent drops its own copy of the write end first, so the child's
	 * exit really is the last close.  The 10-second cap is what turns a
	 * hang into a report.
	 */
	if (pipe(p) < 0) {
		say("FAIL pipe D");
		return 1;
	}
	if ((kid = fork()) < 0) {
		say("FAIL fork");
		return 1;
	}
	if (kid == 0) {
		(void)close(p[0]);
		sleep(3);		/* let the parent reach poll() */
		_exit(0);		/* THIS is the last close of p[1] */
	}
	(void)close(p[1]);
	say("D child holds the write end and exits in 3s; parent polls, 10s cap");
	pfd.fd = p[0];
	pfd.events = POLLIN;
	pfd.revents = 0;
	r = poll(&pfd, 1L, 10000);
	e = errno;
	ckpoll("D child exit", r, pfd.revents, POLLHUP, e);
	step = "D, the read after the child's exit";
	n = read(p[0], buf, sizeof(buf));
	ck("D   read of it returns EOF", n, 0);
	st = 0;
	step = "D, wait for the child that held the write end";
	(void)wait(&st);
	(void)close(p[0]);

	/*
	 * E -- the same thing through select(), which is how every program in
	 * the tree actually asks.  libc/gen/select.c reports POLLHUP as
	 * readable, so a writerless pipe must come back READY, and the read
	 * that follows must return 0 rather than block.
	 */
	if (pipe(p) < 0) {
		say("FAIL pipe E");
		return 1;
	}
	(void)close(p[1]);
	say("E select(read end, 5s) with the write end closed");
	FD_ZERO(&rd);
	FD_SET(p[0], &rd);
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	r = select(p[0] + 1, &rd, (fd_set *)0, (fd_set *)0, &tv);
	ck("E select reports it ready", r, 1);
	ck("E   the pipe is the ready one", FD_ISSET(p[0], &rd) != 0, 1);
	if (r > 0) {
		step = "E, the read select called ready";
		ck("E   read returns EOF", read(p[0], buf, sizeof(buf)), 0);
	}
	(void)close(p[0]);

	printf("pollhup: %s\n", fails ? "FAIL" : "PASS");
	fflush(stdout);
	return fails ? 1 : 0;
}
