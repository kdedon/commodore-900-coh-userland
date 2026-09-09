/*
 * pollpipe.c -- does select() wake for a write to an ANONYMOUS pipe?
 *
 * pollfifo.c asks this of a named FIFO.  This asks it of pipe(2), in the exact
 * shape slip uses: a reader child hands bytes to the parent over a pipe while
 * the parent select()s on that pipe TOGETHER WITH another descriptor.
 *
 * That shape matters because a level check at entry can work while a wakeup
 * on a later write does not: data already in the pipe when select() is
 * entered is a different kernel path from data written while the poller is
 * asleep.  This reproduces the second, with no daemon, no stack and no
 * serial line.
 *
 * Two cases, and the difference between them IS the measurement:
 *
 *	A  data already in the pipe when select() is called   (expected: works)
 *	B  data written while select() is already blocked     (the suspect)
 *
 * Each step prints, so a run that hangs still says where.  A select() that
 * returns in case B BEFORE the child has written is as much a failure as one
 * that never returns.
 *
 * Cases A and B select() with a NULL timeout, so a wakeup that never arrives
 * would hang the run; the DEADLINE reports and exits nonzero instead.
 */
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>

extern int errno;

static int drained_still_readable();
static int split_read_still_readable();

#define OTHER	"/pp.fifo"	/* the second descriptor, as slip has	*/
#define DEADLINE 60		/* seconds for the whole run		*/

static int elapsed;

/*
 * The deadline expired: report which wait was still outstanding.  `step' is set
 * before every call that can block for ever.
 */
static char *step = "startup";

static
hung()
{
	printf("FAIL pollpipe: no verdict within %d s, waiting on: %s\n",
		DEADLINE, step);
	printf("FAIL pollpipe: a select() that never wakes is the defect this"
		" reproduces --\n");
	printf("FAIL pollpipe: reported here rather than hung, because a test"
		" that hangs has\n");
	printf("FAIL pollpipe: no verdict at all.\n");
	fflush(stdout);
	exit(1);
}

main()
{
	int p[2], other, r, n, kid, nfds;
	fd_set rd;
	struct timeval tv0;
	char buf[64];

	(void)signal(SIGALRM, hung);
	(void)alarm(DEADLINE);

	if (pipe(p) < 0) {
		printf("FAIL pipe\n");
		return 1;
	}
	printf("1 pipe ok fds %d %d\n", p[0], p[1]);

	/* A second pollable fd, so select() has more than one thing to watch --
	 * slip watches its pipe and the daemon's reply FIFO together. */
	(void)unlink(OTHER);
	if (mknod(OTHER, 010000|0666, 0) < 0 ||
	    (other = open(OTHER, O_RDWR)) < 0) {
		printf("FAIL second fd\n");
		return 1;
	}
	printf("2 second fd ok %d\n", other);
	nfds = (p[0] > other ? p[0] : other) + 1;

	/*
	 * Before anything else has polled: does a timed select() on an EMPTY
	 * named FIFO come back at all?  Asked first, and on its own, because a
	 * later failure could equally mean "FIFO poll is broken" or "poll state
	 * accumulates across calls and breaks" -- and those are different bugs.
	 */
	printf("2a empty fifo, 2s cap, nothing polled yet\n");
	FD_ZERO(&rd);
	FD_SET(other, &rd);
	tv0.tv_sec = 2;
	tv0.tv_usec = 0;
	r = select(other + 1, &rd, (fd_set *)0, (fd_set *)0, &tv0);
	if (r < 0) {
		printf("FAIL 2a select rc %d errno %d\n", r, errno);
		return 1;
	}
	printf("2b empty fifo select returned %d\n", r);
	/* Nothing has been written to this FIFO and nobody else holds it, so
	 * the only correct answer is the timeout.  A count, or the descriptor
	 * left set, means select() called an empty object readable -- which any
	 * caller answers by reading, and the read is what never returns. */
	if (r != 0 || FD_ISSET(other, &rd)) {
		printf("FAIL 2b: an empty fifo answered readable (rc %d, set %d)\n",
			r, FD_ISSET(other, &rd) != 0);
		return 1;
	}

	/* ---- case A: data is already there when select() is called ---- */
	if (write(p[1], "A", 1) != 1) {
		printf("FAIL write A\n");
		return 1;
	}
	FD_ZERO(&rd);
	FD_SET(p[0], &rd);
	FD_SET(other, &rd);
	step = "case A, select on a pipe that already holds a byte";
	r = select(nfds, &rd, (fd_set *)0, (fd_set *)0, (struct timeval *)0);
	if (r < 0) {
		printf("FAIL A select rc %d errno %d\n", r, errno);
		return 1;
	}
	if (!FD_ISSET(p[0], &rd)) {
		printf("FAIL A select woke but not for the pipe\n");
		return 1;
	}
	n = read(p[0], buf, sizeof(buf));
	printf("3 case A ok: select saw %d, read %d\n", r, n);

	/* ---- case B: the write happens while select() is blocked ---- */
	if ((kid = fork()) < 0) {
		printf("FAIL fork\n");
		return 1;
	}
	if (kid == 0) {
		sleep(3);		/* let the parent reach select() */
		(void)write(p[1], "BBBB", 4);
		_exit(0);
	}
	printf("4 child forked, parent entering select\n");

	FD_ZERO(&rd);
	FD_SET(p[0], &rd);
	FD_SET(other, &rd);
	step = "case B, select blocked before the child's write (THE SUSPECT)";
	r = select(nfds, &rd, (fd_set *)0, (fd_set *)0, (struct timeval *)0);
	if (r < 0) {
		printf("FAIL B select rc %d errno %d\n", r, errno);
		return 1;
	}
	if (!FD_ISSET(p[0], &rd)) {
		printf("FAIL B select woke but not for the pipe\n");
		return 1;
	}
	n = read(p[0], buf, sizeof(buf));
	printf("5 case B ok: select saw %d, read %d\n", r, n);

	/* ---- case C: after the data is consumed, is it STILL readable? ----
	 *
	 * This is the shape slip's parent is stuck in.  Its counters say
	 * reply_pump was entered twice and read a reply once: select() reported
	 * the channel readable a second time and the blocking read of the reply
	 * record never returned.  If a descriptor stays readable after its bytes
	 * have been taken, any caller that trusts select() and then reads will
	 * block forever -- and on a single-threaded daemon that stops the other
	 * direction too.
	 *
	 * Tested on BOTH kinds, because slip's two descriptors are one of each:
	 * an anonymous pipe from its reader child, and a named FIFO to the
	 * daemon.  A timeout is essential here -- without it a failure hangs
	 * instead of reporting.
	 */
	if (!drained_still_readable("pipe", p[0], p[1]))
		return 1;
	if (!drained_still_readable("fifo", other, other))
		return 1;

	/* ---- case D: one write, TWO reads, then is it still readable? ----
	 *
	 * This is the failing shape exactly.  The daemon writes a reply record
	 * and its payload as ONE 48-byte write; the client reads the 8-byte
	 * record, then the 40-byte payload.  slip's counters say that after
	 * doing precisely this, select() called the channel readable again and
	 * the next 8-byte read never returned -- so the queue's byte count did
	 * not come back to zero.  Case C only ever wrote and read once, which is
	 * why it passes while this happens.
	 */
	if (!split_read_still_readable("pipe", p[0], p[1]))
		return 1;
	if (!split_read_still_readable("fifo", other, other))
		return 1;

	printf("PASS pollpipe\n");
	return 0;
}

/*
 * Write, drain, then poll with a 2-second cap: readable now is a false
 * positive.  Returns 1 if the descriptor behaves.
 */
static int drained_still_readable(what, rfd, wfd)
char *what;
int rfd, wfd;
{
	fd_set rd;
	struct timeval tv;
	char buf[64];
	int r, n;

	printf("   %s: writing 8\n", what);
	if (write(wfd, "CCCCCCCC", 8) != 8) {
		printf("FAIL %s: write\n", what);
		return 0;
	}
	printf("   %s: wrote; reading up to %d\n", what, (int)sizeof(buf));
	step = "case C, the read after the write";
	n = read(rfd, buf, sizeof(buf));
	printf("   %s: read returned %d\n", what, n);
	if (n != 8) {
		printf("FAIL %s: read got %d, wanted 8\n", what, n);
		return 0;
	}
	printf("   %s: selecting with a 2s cap\n", what);
	FD_ZERO(&rd);
	FD_SET(rfd, &rd);
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	r = select(rfd + 1, &rd, (fd_set *)0, (fd_set *)0, &tv);
	if (r < 0) {
		printf("FAIL %s: select after drain rc %d errno %d\n",
			what, r, errno);
		return 0;
	}
	if (r > 0 && FD_ISSET(rfd, &rd)) {
		printf("FAIL %s: STILL READABLE after draining it\n", what);
		return 0;
	}
	printf("6 %s: correctly not readable once drained\n", what);
	return 1;
}

/*
 * Write 48 bytes in one write, take them in an 8-byte read and a 40-byte read,
 * then poll with a 2-second cap.  Readable now means the queue still thinks it
 * holds something.
 */
static int split_read_still_readable(what, rfd, wfd)
char *what;
int rfd, wfd;
{
	fd_set rd;
	struct timeval tv;
	char big[48], buf[64];
	int r, n, i;

	for (i = 0; i < 48; i++)
		big[i] = 'D';
	printf("   %s: one 48-byte write\n", what);
	if (write(wfd, big, 48) != 48) {
		printf("FAIL %s: 48-byte write\n", what);
		return 0;
	}
	step = "case D, the 8-byte record read";
	n = read(rfd, buf, 8);
	printf("   %s: first read (asked 8) returned %d\n", what, n);
	if (n != 8) {
		printf("FAIL %s: record read got %d\n", what, n);
		return 0;
	}
	step = "case D, the 40-byte payload read (slip stops here)";
	n = read(rfd, buf, 40);
	printf("   %s: second read (asked 40) returned %d\n", what, n);
	if (n != 40) {
		printf("FAIL %s: payload read got %d\n", what, n);
		return 0;
	}
	FD_ZERO(&rd);
	FD_SET(rfd, &rd);
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	r = select(rfd + 1, &rd, (fd_set *)0, (fd_set *)0, &tv);
	/* A select() that FAILS outright is not "correctly not readable".  This
	 * case had no r < 0 arm -- unlike its neighbour above -- so a select
	 * that came back -1 for any reason fell through and printed step 7. */
	if (r < 0) {
		printf("FAIL %s: select after a split read rc %d errno %d\n",
			what, r, errno);
		return 0;
	}
	if (r > 0 && FD_ISSET(rfd, &rd)) {
		printf("FAIL %s: STILL READABLE after a split read\n", what);
		return 0;
	}
	printf("7 %s: correct after a split read\n", what);
	return 1;
}
