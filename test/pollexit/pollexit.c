/*
 * pollexit.c -- what happens to a poll(2) event buffer when a signal arrives?
 *
 * Every blocking poll(2) links a kernel event buffer onto TWO lists: the
 * process's p_polls, which dies with the process, and the device's circular
 * event queue, which does not -- and which a driver walks from its interrupt
 * handler (poll.c pollwake).  A buffer left on a device queue after its owner
 * is gone is read through a freed PROC by that interrupt handler, so the
 * unlink has to happen on every way out of poll(2), including the ways a
 * signal creates.  Two cases, and they are different:
 *
 *   A  a CAUGHT signal: poll(2) must return -1/EINTR to the program and unlink
 *      before it does, because the program is free to poll the same device
 *      again immediately.
 *   B  a FATAL signal: the process dies on its way out of the system call, so
 *      the unlink must survive process death (sys/coh/proc.c pexit).
 *
 * Both are checked on a FIFO, whose queue heads are fields of an in-core
 * inode, and then on the controlling terminal, whose heads on this port can
 * be data of the LOADABLE console driver.
 *
 * WHAT A FAILURE LOOKS LIKE.  A leaked buffer is not visible as a wrong answer
 * from the poll that leaked it; it shows on the NEXT one, because pollopen
 * appends through the queue's stale e_dlast and the ring closes on itself:
 *
 *   FIFO:  the writer that should wake this program never returns from write
 *          -- pollwake walks a ring that never reaches its head -- so step A3
 *          or B2 sits until its 10-second timeout and reports got 0 want 1,
 *          leaving a child spinning.
 *   tty:   the same ring is walked by the KEYBOARD interrupt handler, so the
 *          first key pressed after the damage freezes the video console with
 *          no panic while a getty on tty50 keeps running.  Step C5 is the key.
 *
 * Every step prints before it can block, so a hang names its own step.
 *
 * THE DEADLINE IS A CHILD, NOT AN ALARM.  Everything above is reached through
 * poll() calls that block for ever (INFTIM) and are meant to be ended by a
 * signal or by a child's write, so alarm(2) is this program's INSTRUMENT and
 * cannot also be its bound -- and the failure the header describes, a writer
 * that never returns from write(2) because pollwake is walking a ring that
 * never reaches its head, leaves BOTH the writer and the parent's wait(2) for
 * it stuck.  A separate watchdog process therefore reports and kills, so that a
 * damaged event ring produces a named failure instead of two wedged processes
 * and no verdict.
 *
 * Run it twice, and with -k on a video console:
 *	pollexit ; pollexit -k
 *
 * The second argument of poll(2) is unsigned long in this ABI and there are no
 * prototypes: `1L', never `1'.
 */
#include <sys/types.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>

extern int errno;

#define FIFO	"/tmp/pollexit.p"

/*
 * Long enough for every step's own cap (10 s twice, 20 s for the keystroke
 * prompt) plus the sleeps between them.
 */
#define DEADLINE 90

int	fails;
int	alrms;
int	wdog;			/* the watchdog child */

/*
 * Start the watchdog.  It waits DEADLINE seconds and then reports and kills the
 * whole test -- parent included, which is the point: the parent may be the
 * process that is stuck.
 */
/*
 * The watchdog's knock, caught in the TEST process so that the run ends with an
 * ordinary failing exit status rather than a signal.  A caller that reads
 * `killed by SIGKILL' has to know about this program to know what happened; 1
 * means the same thing everywhere.
 */
static
knocked()
{
	printf("pollexit: FAIL -- ended by its own watchdog (above)\n");
	fflush(stdout);
	exit(1);
}

static
watchdog()
{
	int parent;

	parent = getpid();
	(void)signal(SIGTERM, knocked);
	if ((wdog = fork()) != 0)
		return;
	sleep(DEADLINE);
	printf("pollexit: FAIL -- no verdict within %d s.  A poll(2) that never"
		" returned,\n", DEADLINE);
	printf("pollexit: or a write(2) that never returned because pollwake"
		" walked a ring\n");
	printf("pollexit: that does not reach its head -- which is the leak"
		" this program\n");
	printf("pollexit: exists to find, reported rather than hung.\n");
	fflush(stdout);
	/* SIGTERM first, so the test process reports the failure itself and
	 * exits 1; SIGKILL after a grace period in case it is too far gone to
	 * run a handler, which is itself worth seeing. */
	kill(parent, SIGTERM);
	sleep(3);
	kill(parent, SIGKILL);
	_exit(1);
}

/*
 * Stop the watchdog.  Called on every path that produces a verdict.
 */
static
unwatch()
{
	int st;

	if (wdog > 0) {
		kill(wdog, SIGKILL);
		st = 0;
		wait(&st);
		wdog = 0;
	}
}

/*
 * SIGALRM catcher.  A delivered signal reverts to SIG_DFL, so it is re-armed
 * here; without that the next alarm kills the process instead of waking it.
 */
int
onalrm()
{
	alrms++;
	signal(SIGALRM, onalrm);
}

static
ck(what, got, want)
char *what;
int got, want;
{
	printf("pollexit: %-38s got %3d want %3d  %s\n", what, got, want,
		got == want ? "ok" : "FAIL");
	if (got != want)
		fails++;
	fflush(stdout);
}

static
say(s)
char *s;
{
	printf("pollexit: %s\n", s);
	fflush(stdout);
}

/*
 * Fork a child that blocks in poll() on `fd' until SIGINT kills it, and reap
 * it.  Returns the wait status, whose low seven bits are the signal that
 * killed it: a child that reached poll() and died there is the only child that
 * can exercise the exit path with a buffer on a device queue.
 */
static
diein(fd)
int fd;
{
	struct pollfd pfd;
	int pid, st;

	if ((pid = fork()) == 0) {
		signal(SIGINT, SIG_DFL);	/* the parent's shell may hold it */
		pfd.fd = fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		say("   child is polling");
		poll(&pfd, 1L, INFTIM);
		_exit(9);			/* only if the poll came back */
	}
	sleep(2);				/* let it reach the sleep */
	kill(pid, SIGINT);
	st = 0;
	wait(&st);
	return st;
}

/*
 * Fork a child that writes one byte to `fd' after a delay.  The write is what
 * runs pollwake() on the device queue, so a corrupted ring hangs the CHILD and
 * the parent's poll expires instead.
 */
static
writer(fd)
int fd;
{
	int pid;

	if ((pid = fork()) == 0) {
		sleep(2);
		write(fd, "b", 1);
		_exit(0);
	}
	return pid;
}

int main(argc, argv)
int argc;
char **argv;
{
	struct pollfd pfd;
	int fd, r, n, st;
	int e;			/* errno saved before anything can print */
	char buf[8];

	watchdog();

	(void)unlink(FIFO);
	if (mknod(FIFO, 010000|0666, 0) < 0) {
		say("FAIL mknod");
		unwatch();
		return 1;
	}
	if ((fd = open(FIFO, O_RDWR)) < 0) {
		say("FAIL open fifo");
		unwatch();
		return 1;
	}

	/*
	 * A -- caught signal on a FIFO.
	 */
	signal(SIGALRM, onalrm);
	alrms = 0;
	errno = 0;
	pfd.fd = fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	say("A1 poll(fifo, POLLIN, forever), alarm(1) pending");
	alarm(1);
	r = poll(&pfd, 1L, INFTIM);
	/*
	 * errno has to be taken here, before ck() prints.  fflush() and
	 * _fputc() both begin with `errno = 0' -- that is how this stdio
	 * tells an interrupted write() apart from a failed one -- so any
	 * output between the system call and the test destroys the evidence.
	 */
	e = errno;
	ck("A1 interrupted poll returns", r, -1);
	ck("A1   errno is EINTR", e, EINTR);
	ck("A1   handler ran once", alrms, 1);

	write(fd, "a", 1);
	pfd.revents = 0;
	r = poll(&pfd, 1L, 0);
	ck("A2 0ms poll sees the byte", r, 1);
	ck("A2   revents has POLLIN", (pfd.revents & POLLIN) != 0, 1);
	n = read(fd, buf, 1);
	ck("A2   read got it", n, 1);

	writer(fd);
	say("A3 blocking poll, a child writes in 2s");
	pfd.revents = 0;
	r = poll(&pfd, 1L, 10000);
	ck("A3 poll woken by the write", r, 1);
	if (r > 0)
		(void)read(fd, buf, 1);
	st = 0;
	wait(&st);

	/*
	 * B -- fatal signal on a FIFO: the poller dies inside poll().
	 */
	say("B1 child polls the fifo and is killed there");
	st = diein(fd);
	ck("B1 child died of SIGINT", st & 0177, SIGINT);

	writer(fd);
	say("B2 blocking poll after the child died in poll");
	pfd.revents = 0;
	r = poll(&pfd, 1L, 10000);
	ck("B2 poll woken by the write", r, 1);
	if (r > 0)
		(void)read(fd, buf, 1);
	st = 0;
	wait(&st);
	(void)close(fd);
	(void)unlink(FIFO);

	/*
	 * C -- the same two cases on the terminal, whose queue heads can be
	 * data of a loadable driver.  Nothing may be typed until C5 asks.
	 */
	if (isatty(0) == 0)
		say("C skipped: stdin is not a terminal");
	else {
		signal(SIGALRM, onalrm);
		alrms = 0;
		errno = 0;
		pfd.fd = 0;
		pfd.events = POLLIN;
		pfd.revents = 0;
		say("C1 poll(tty, POLLIN, forever), alarm(1) -- do not type");
		alarm(1);
		r = poll(&pfd, 1L, INFTIM);
		e = errno;		/* before ck() prints: see A1 */
		ck("C1 interrupted poll returns", r, -1);
		ck("C1   errno is EINTR", e, EINTR);
		ck("C1   handler ran once", alrms, 1);

		pfd.revents = 0;
		r = poll(&pfd, 1L, 1000);
		ck("C2 idle tty times out", r, 0);
		ck("C2   no POLLNVAL", pfd.revents & POLLNVAL, 0);

		say("C3 child polls the tty and is killed there");
		st = diein(0);
		ck("C3 child died of SIGINT", st & 0177, SIGINT);

		pfd.revents = 0;
		r = poll(&pfd, 1L, 1000);
		ck("C4 tty poll still times out", r, 0);
		ck("C4   no POLLNVAL", pfd.revents & POLLNVAL, 0);

		if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'k') {
			say("C5 press any key (the machine hangs here if the");
			say("   console event ring was corrupted)");
			pfd.revents = 0;
			r = poll(&pfd, 1L, 20000);
			ck("C5 the keystroke arrives", r, 1);
			ck("C5   revents has POLLIN", (pfd.revents&POLLIN) != 0, 1);
			if (r > 0)
				ck("C5   read got it", read(0, buf, 1), 1);
		} else
			say("C5 skipped: run with -k on a video console");
	}

	unwatch();
	printf("pollexit: %s\n", fails ? "FAIL" : "PASS");
	fflush(stdout);
	return fails ? 1 : 0;
}
