/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * pollfifo.c -- does select() on a named FIFO actually block and then wake?
 *
 * This is the exact mechanism the inet daemon's rendezvous depends on: the
 * daemon opens /dev/inet O_RDWR and select()s on it, a client opens it O_WRONLY
 * and writes, and the daemon must wake.  Isolating it from the daemon means one
 * boot answers the question instead of instrumenting 143KB of stack.
 *
 * Prints one line per step so a partial run still says where it stopped.  The
 * child writes after a delay, so a select() that returns IMMEDIATELY (before
 * "child wrote") is as much a failure as one that never returns.
 *
 * The verdict is the exit status; every probe is a ck(), not a printout.
 *
 * Step 4 is a select() with a NULL timeout on exactly the wakeup path this
 * program exists to doubt: a kernel that never wakes the poller leaves it
 * there for ever.  DEADLINE turns that hang into a reported failure.
 */
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <string.h>

extern int errno;

#define FIFO	"/tmp/pt"
#define DEADLINE 60			/* seconds for the whole run */

int	fails;

/*
 * The deadline expired.  Reported, not silent: an unexplained exit and a hang
 * are equally useless to whoever reads the log.
 */
static
hung()
{
	printf("pollfifo: FAIL -- no verdict within %d s.  The wakeup this"
		" program waits\n", DEADLINE);
	printf("pollfifo: for (step 4, select with no timeout) never came;"
		" a test that hangs\n");
	printf("pollfifo: has no verdict, which is worse than one that"
		" fails.\n");
	fflush(stdout);
	exit(1);
}

/*
 * Report one check and count a failure.
 */
static void
ck(what, got, want)
char *what;
int got, want;
{
	printf("pollfifo: %-40s got %3d want %3d  %s\n", what, got, want,
		got == want ? "ok" : "FAIL");
	if (got != want)
		fails++;
	fflush(stdout);
}

main()
{
	int fd, wfd, r, n, e;
	fd_set rd;
	char buf[16];

	(void)signal(SIGALRM, hung);
	(void)alarm(DEADLINE);

	(void)unlink(FIFO);
	if (mknod(FIFO, 010000|0666, 0) < 0) {
		printf("FAIL mknod\n");
		return 1;
	}
	printf("1 mknod ok\n");

	if ((fd = open(FIFO, O_RDWR)) < 0) {
		printf("FAIL open rdwr\n");
		return 1;
	}
	printf("2 open rdwr fd=%d\n", fd);

	if (fork() == 0) {
		sleep(3);
		if ((wfd = open(FIFO, O_WRONLY)) < 0)
			_exit(1);
		write(wfd, "hi", 2);
		close(wfd);
		_exit(0);
	}

	/*
	 * Three probes, because "select returned -1" alone does not say which
	 * check inside upoll() rejected it.  errno names it: 14 EFAULT (the
	 * user-pointer validation), 22 EINVAL (npoll range), 4 EINTR (woken by
	 * a signal after the sleep).  The 0ms poll cannot block, so if IT also
	 * fails the fault is in validation, not in the wait.
	 */
	{
		struct pollfd pfd;
		int e;

		pfd.fd = fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		errno = 0;
		r = poll(&pfd, 1L, 0);	/* LONG: see libc/gen/select.c */
		e = errno;		/* before the printf: fflush zeroes it */
		printf("3a poll(0ms) r=%d errno=%d revents=%d\n",
			r, e, pfd.revents);
		/* The FIFO is empty and the child has not written yet, so the
		 * only correct answer is 0.  1 means the descriptor was called
		 * readable with nothing in it; -1 means validation refused it. */
		ck("3a empty fifo, 0ms poll, is not ready", r, 0);
		ck("3a   no POLLNVAL", pfd.revents & POLLNVAL, 0);

		pfd.revents = 0;
		errno = 0;
		r = poll(&pfd, 1L, 2000);
		e = errno;
		printf("3b poll(2s) r=%d errno=%d revents=%d\n",
			r, e, pfd.revents);
		/* The child sleeps 3 s, so a 2 s wait must expire empty-handed:
		 * a 1 here is a wakeup for something that has not happened. */
		ck("3b empty fifo, 2s poll, times out", r, 0);
	}

	FD_ZERO(&rd);
	FD_SET(fd, &rd);
	printf("3c selecting\n");
	fflush(stdout);
	errno = 0;
	r = select(fd + 1, &rd, (fd_set *)0, (fd_set *)0, (struct timeval *)0);
	e = errno;			/* before the printf: fflush zeroes it */
	printf("4 select r=%d errno=%d isset=%d\n",
		r, e, FD_ISSET(fd, &rd) ? 1 : 0);
	ck("4 select woke for the write", r, 1);
	ck("4   and it is the fifo", FD_ISSET(fd, &rd) ? 1 : 0, 1);

	if (r > 0) {
		n = read(fd, buf, sizeof(buf));
		buf[n > 0 ? n : 0] = '\0';
		printf("5 read n=%d buf=%s\n", n, buf);
		ck("5 read got both bytes", n, 2);
		ck("5   and they are the child's", strcmp(buf, "hi") == 0, 1);
	}

	printf("pollfifo: %s\n", fails ? "FAIL" : "PASS");
	fflush(stdout);
	return fails ? 1 : 0;
}
