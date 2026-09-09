/*
 * openexcl.c -- O_EXCL must exclude while a file is open, and only then.
 *
 * open(2) with O_CREAT|O_EXCL marks the IN-CORE inode IFEXCL, and every later
 * open of that inode -- by any process -- is refused EEXIST while the mark is
 * there.  The mark is owned by the file descriptor that set it, so it has to
 * come off when the last reference to that descriptor goes away, on close(2)
 * and on process death alike.  A mark that is never removed poisons the file
 * for the rest of the boot: stat(2) keeps working and the file is on the disk,
 * so it looks like a file that exists and cannot be opened, and it comes back
 * clean after a reboot because nothing was ever wrong on disk.
 *
 * The four checks are two pairs, and both halves of each pair matter:
 *
 *   1 create with O_CREAT|O_EXCL, close, reopen        -- must SUCCEED
 *   2 create with O_CREAT|O_EXCL, reopen while it is still open
 *                                                      -- must FAIL EEXIST
 *   3 create with O_CREAT|O_EXCL over an existing file  -- must FAIL EEXIST
 *   4 a creator KILLED without closing, then reopen     -- must SUCCEED
 *
 * Checks 1 and 4 fail on a kernel that never clears the mark; checks 2 and 3
 * fail on one that never sets it.  Removing the mark by never setting it is
 * not a fix, which is why the failing directions are tested too.
 *
 * Check 4 synchronises on a pipe rather than a delay: the child writes one
 * byte once it holds the exclusive descriptor, so the parent kills it at a
 * known point instead of at a guessed time.
 *
 * A RENDEZVOUS IS A PLACE TO HANG.  That pipe read has no writer if the child
 * dies before its open returns, and the wait(2) after the SIGKILL has nothing
 * to reap if the kill did not land -- a process wedged inside an open(2) that
 * the exclusive mark refuses to release is one of the defects being looked for
 * here, and it would have stopped this program at the rendezvous rather than
 * being reported by it.  DEADLINE turns that into a failure with a name.
 */
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#define DEADLINE 60			/* seconds for the whole run */

#define P1	"/oxtst1"
#define P2	"/oxtst2"
#define P3	"/oxtst3"

int fails = 0;
int child = 0;			/* the c4 child, so the deadline can clear it */

/*
 * The deadline expired.  Reported rather than silent, and the c4 child is
 * killed on the way out so a failing run leaves nothing behind.
 */
hung()
{
	printf("openexcl: FAIL -- no verdict within %d s.  The rendezvous in"
		" check 4 (or the\n", DEADLINE);
	printf("openexcl: wait for the child killed there) never completed;"
		" a test that hangs\n");
	printf("openexcl: has no verdict, which is worse than one that"
		" fails.\n");
	fflush(stdout);
	if (child > 0)
		kill(child, SIGKILL);
	exit(1);
}

report(tag, ok, detail)
char *tag;
char *detail;
{
	printf("openexcl: %s %s (%s)\n", ok ? "ok  " : "FAIL", tag, detail);
	if (!ok)
		fails++;
}

char buf[32];

/* Reopen `path' read-only; return the fd, or -1, with errno left alone. */
reopen(path)
char *path;
{
	errno = 0;
	return open(path, O_RDONLY);
}

/* Check 1: the mark must be gone once the creating descriptor is closed. */
c1()
{
	int fd;

	unlink(P1);
	errno = 0;
	if ((fd = open(P1, O_WRONLY|O_CREAT|O_EXCL, 0600)) < 0) {
		sprintf(buf, "create errno %d", errno);
		report("closed creator reopens", 0, buf);
		return;
	}
	write(fd, "0123456789", 10);
	close(fd);

	if ((fd = reopen(P1)) < 0) {
		sprintf(buf, "errno %d", errno);
		report("closed creator reopens", 0, buf);
		return;
	}
	close(fd);
	report("closed creator reopens", 1, "open succeeded");
}

/* Check 2: while the exclusive descriptor is held, others must be refused. */
c2()
{
	int fd, fd2;

	unlink(P2);
	errno = 0;
	if ((fd = open(P2, O_WRONLY|O_CREAT|O_EXCL, 0600)) < 0) {
		sprintf(buf, "create errno %d", errno);
		report("held open excludes", 0, buf);
		return;
	}
	fd2 = reopen(P2);
	if (fd2 >= 0) {
		close(fd2);
		report("held open excludes", 0, "second open succeeded");
	} else if (errno != EEXIST) {
		sprintf(buf, "errno %d not EEXIST", errno);
		report("held open excludes", 0, buf);
	} else
		report("held open excludes", 1, "EEXIST while held");
	close(fd);

	if ((fd2 = reopen(P2)) < 0) {
		sprintf(buf, "errno %d", errno);
		report("released open admits", 0, buf);
	} else {
		close(fd2);
		report("released open admits", 1, "open after close");
	}
}

/* Check 3: O_CREAT|O_EXCL over a file that already exists must be refused. */
c3()
{
	int fd;

	errno = 0;
	if ((fd = open(P1, O_WRONLY|O_CREAT|O_EXCL, 0600)) >= 0) {
		close(fd);
		report("excl over existing", 0, "create succeeded");
	} else if (errno != EEXIST) {
		sprintf(buf, "errno %d not EEXIST", errno);
		report("excl over existing", 0, buf);
	} else
		report("excl over existing", 1, "EEXIST");
}

/* Check 4: a creator killed without closing must not leave the file poisoned. */
c4()
{
	int pd[2];
	int pid, st, fd;
	char c;

	unlink(P3);
	if (pipe(pd) < 0) {
		report("killed creator releases", 0, "no pipe");
		return;
	}
	if ((pid = fork()) < 0) {
		report("killed creator releases", 0, "no fork");
		return;
	}
	child = pid;
	if (pid == 0) {
		close(pd[0]);
		fd = open(P3, O_WRONLY|O_CREAT|O_EXCL, 0600);
		c = (fd < 0) ? 'n' : 'y';
		write(pd[1], &c, 1);
		for (;;)
			pause();
	}
	close(pd[1]);
	c = 'n';
	if (read(pd[0], &c, 1) != 1 || c != 'y') {
		close(pd[0]);
		kill(pid, SIGKILL);
		wait(&st);
		report("killed creator releases", 0, "child could not create");
		return;
	}
	close(pd[0]);
	kill(pid, SIGKILL);
	wait(&st);

	if ((fd = reopen(P3)) < 0) {
		sprintf(buf, "errno %d", errno);
		report("killed creator releases", 0, buf);
		return;
	}
	close(fd);
	report("killed creator releases", 1, "open after SIGKILL");
}

main()
{
	signal(SIGALRM, hung);
	alarm(DEADLINE);
	c1();
	c2();
	c3();
	c4();
	unlink(P1);
	unlink(P2);
	unlink(P3);
	printf("openexcl: %d failed\n", fails);
	exit(fails != 0);
}
