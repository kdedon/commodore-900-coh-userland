/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * pty.c -- master/slave round trip on a pseudo-terminal.
 *
 * The first gate for the pty driver (sys/drv/pty.c), and it needs no network:
 * telnetd's "no free PTY" is the same open() failing three floors up.  Two
 * processes are needed because the two halves of a pty only move data when the
 * other side is in a system call, so a single process reading its own master
 * would sleep forever.
 *
 * The master is the pty's carrier, so it MUST be opened before the slave: a
 * slave open blocks until a master is there (pty.c ptyopen, the "ptycd" wait).
 * This program therefore opens the master, forks, and lets the child take the
 * slave -- and phase 0 checks that the rule is enforced at all, on a channel
 * with no master, since every other phase here obeys it and so none of them
 * would notice a driver that had dropped it.
 *
 * The child then CLOSES its inherited master.  fork() adds a reference to every
 * open descriptor (fd.c fdadupl), and a driver's close runs only when the last
 * one goes (fd.c fdclose -> fs3.c iclose -> bio.c dclose), so a child that keeps
 * the master holds the carrier up and the parent's close does nothing at all.
 * That is the same rule telnetd's spawn_login() follows for the network channel.
 *
 * What each open failure means -- the three are reported apart:
 *	ENXIO	the major has no driver -- drvl[] slot 9 empty in the linked
 *		config (bio.c drvmap), or the channel is >= NUPTY
 *	ENOENT	no device node
 *	EDBUSY	a master already holds the channel
 *
 * Every read here is backed by an alarm: a broken wakeup path shows up as a
 * hang, not as wrong data, and a test that hangs tells you nothing.
 *
 * Every errno is copied into a local BEFORE anything is printed.  fflush() opens
 * with `errno = 0' (libc/stdio/fflush.c:16) so that it can tell a short write
 * from an interrupted one, and a line-buffered stdout flushes on every newline
 * (libc/stdio/_fputt.c), so one printf destroys the errno you were about to
 * report.  Checks report the errno VALUE, not whether it matched, so a
 * disagreement says what it saw.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sgtty.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>		/* char *ttyname(): K&R would make it an int */

#define MASTER	"/dev/ptyp0"
#define SLAVE	"/dev/ttyp0"
#define TMO	20			/* seconds before either side gives up */

/*
 * Channels the driver and the /dev nodes are both expected to provide: NPTY in
 * sys/drv/pty.c, and dist/devices/hd.devices naming ptyp0..ptypf and
 * ttyp0..ttypf.  This is also the whole name space the hex convention has --
 * a seventeenth channel would have to be called ptyq0.
 */
#define NCHAN	16

char	*who = "pty";
int	fails;
int	kid;

static void
ck(what, got, want)
char *what;
int got, want;
{
	printf("%s: %-38s got %3d want %3d  %s\n", who, what, got, want,
		got == want ? "ok" : "FAIL");
	fflush(stdout);
	if (got != want)
		fails++;
}

/*
 * Name an open failure rather than only its number.
 */
static char *
why(e)
int e;
{
	if (e == ENXIO)
		return "ENXIO: no driver on the major, or channel >= NUPTY";
	if (e == ENOENT)
		return "ENOENT: no such device node";
	if (e == EDBUSY)
		return "EDBUSY: already held";
	if (e == EIO)
		return "EIO: carrier is down";
	if (e == EINTR)
		return "EINTR: interrupted, not hung up";
	if (e == EKSPACE)
		return "EKSPACE: the kalloc arena refused the channel";
	if (e == 0)
		return "0: nothing set it, or a printf cleared it";
	return "unexpected";
}

/*
 * An errno check.  Reports the value and its name rather than a verdict, so a
 * disagreement is an answer and not another round trip.
 */
static void
cke(what, got, want)
char *what;
int got, want;
{
	printf("%s: %-30s got %3d want %3d  %s (%s)\n", who, what, got, want,
		got == want ? "ok" : "FAIL", why(got));
	fflush(stdout);
	if (got != want)
		fails++;
}

static void
hung(sig)
int sig;
{
	printf("%s: FAIL -- no answer in %d seconds\n", who, TMO);
	fflush(stdout);
	if (kid > 0)
		(void) kill(kid, SIGKILL);
	exit(1);
}

/*
 * Does `buf' contain `pat'?  There is no strstr() in this libc.
 */
static int
has(buf, pat)
char *buf, *pat;
{
	int n = strlen(pat);

	while (*buf) {
		if (strncmp(buf, pat, n) == 0)
			return 1;
		buf++;
	}
	return 0;
}

/*
 * Read from the master until everything read so far contains `pat'.  Output
 * arrives in whatever pieces the line discipline hands over, so the check is
 * cumulative and not per-read.
 */
static int
expect(fd, pat, acc, size)
int fd;
char *pat, *acc;
int size;
{
	int n, used;

	used = strlen(acc);
	while (!has(acc, pat)) {
		if (used >= size - 1)
			return 0;
		n = read(fd, acc + used, size - 1 - used);
		if (n <= 0)
			return 0;
		used += n;
		acc[used] = '\0';
	}
	return 1;
}

/*
 * The slave side, in the child.  Returns the number of failures.
 */
static int
slave(mfd)
int mfd;
{
	struct sgttyb sg;
	char buf[64];
	int fd, n, e;

	who = "pty(slave)";
	fails = 0;
	kid = 0;
	(void) signal(SIGALRM, hung);
	(void) alarm(TMO);

	/* Give up the inherited master, or the parent's close is not the last
	 * one and carrier never drops.  The parent still holds it, so the slave
	 * open below still finds its carrier. */
	ck("close the inherited master", close(mfd) == 0, 1);

	if ((fd = open(SLAVE, O_RDWR)) < 0) {
		e = errno;
		printf("%s: open %s: errno %d -- %s\n", who, SLAVE, e, why(e));
		printf("%s: FAIL\n", who);
		return 1;
	}
	ck("open /dev/ttyp0", 1, 1);
	ck("  isatty(slave)", isatty(fd) != 0, 1);
	ck("  ttyname(slave) is the node", strcmp(ttyname(fd), SLAVE) == 0, 1);

	/* Echo off: with it on, the master's own writes come back as input to
	 * its next read and the handshake below cannot be told apart. */
	ck("  gtty(slave)", gtty(fd, &sg) == 0, 1);
	sg.sg_flags &= ~ECHO;
	ck("  stty(slave, -echo)", stty(fd, &sg) == 0, 1);

	/* Slave -> master. */
	ck("write ping to slave", write(fd, "ping\n", 5), 5);

	/* Master -> slave, canonical, so the line arrives whole. */
	n = read(fd, buf, sizeof(buf) - 1);
	if (n < 0)
		n = 0;
	buf[n] = '\0';
	ck("read from slave", n, 5);
	ck("  it is the master's line", strcmp(buf, "pong\n") == 0, 1);

	/* Tell the master to hang up on us. */
	ck("write ok to slave", write(fd, "ok\n", 3), 3);

	/* The master's close drops carrier, so this read must fail rather than
	 * block or return end of file.  errno first: the ck() below prints. */
	n = read(fd, buf, sizeof(buf) - 1);
	e = errno;
	ck("read after master close fails", n < 0, 1);
	if (n < 0)
		cke("  errno after hangup", e, EIO);

	(void) close(fd);
	printf("%s: %s\n", who, fails ? "FAIL" : "PASS");
	fflush(stdout);
	return fails;
}

/*
 * Phase 0: a slave open with no master must WAIT.
 *
 * Everything below opens the master first, so nothing below would notice a
 * driver that let a slave open a channel with no carrier -- and that rule is
 * what makes a pty a terminal rather than a pipe: the slave is a login shell's
 * controlling terminal, and its open is the point at which the line is known
 * to be there.  A slave that opens anyway gets a channel with no other end, so
 * its first read blocks for ever instead of failing, and telnetd's spawn_login
 * hands a session a terminal nobody is driving.
 *
 * The check is a child, because the correct behaviour is to BLOCK: it arms a
 * short alarm whose handler returns, and the open must come back EINTR rather
 * than succeed (sys/drv/pty.c ptyopen, the "ptycd" wait and its signal arm).
 * A channel this program has not touched is used, so no master of its own is
 * in the way.
 */
#define SPARE	"/dev/ttyp1"	/* a slave whose master nobody holds */

#define C_OPENED	9	/* the child's open succeeded: no carrier rule */
#define C_OTHER		8	/* it failed, but not by being interrupted */

static void
woke(sig)
int sig;
{
	/* Nothing but the return: the interrupted open is the measurement. */
}

static void
carrier()
{
	int st, fd, e;

	who = "pty(carrier)";
	if ((kid = fork()) < 0) {
		printf("%s: FAIL -- fork failed, errno %d\n", who, errno);
		fails++;
		return;
	}
	if (kid == 0) {
		(void) signal(SIGALRM, woke);
		(void) alarm(3);
		fd = open(SPARE, O_RDWR);
		e = errno;
		if (fd >= 0) {
			(void) close(fd);
			printf("%s: open %s succeeded with no master\n", who,
				SPARE);
			fflush(stdout);
			exit(C_OPENED);
		}
		printf("%s: open %s: errno %d -- %s\n", who, SPARE, e, why(e));
		fflush(stdout);
		exit(e == EINTR ? 0 : C_OTHER);
	}
	st = 0;
	(void) wait(&st);
	kid = 0;
	st = (st >> 8) & 0xFF;
	ck("slave open with no master waits", st, 0);
	fflush(stdout);
}

/*
 * Phase 2: can a master tell a dead channel from an idle one?
 *
 * This is a multiplexer's view -- screen, MGR, anything watching several
 * ptys -- and both routes must report the hangup: poll() must wake, and a
 * NON-BLOCKING read must say EIO rather than EAGAIN (ptyread() must test
 * p_mopen before IONDLY).  A blocking reader sees the EIO either way; a
 * multiplexer cannot be blocking.
 *
 * The master is opened O_NDELAY so no fcntl is needed, and the slave is opened
 * and closed by a child that is waited for, so the hangup is not a race.
 */
static int
hangup()
{
	struct pollfd set[1];
	char buf[64];
	int mfd, sfd, n, e, st;

	who = "pty(hup)";
	(void) alarm(TMO);		/* phase 2 gets its own window */

	if ((mfd = open(MASTER, O_RDWR|O_NDELAY)) < 0) {
		e = errno;
		printf("%s: open %s: errno %d -- %s\n", who, MASTER, e, why(e));
		fails++;
		return 1;
	}
	ck("open /dev/ptyp0 O_NDELAY", 1, 1);

	/* A live but idle channel is not a hangup, and must not report as one --
	 * the point of the fix is telling the two apart. */
	set[0].fd = mfd;
	set[0].events = POLLIN;
	set[0].revents = 0;
	n = poll(set, (unsigned long)1, 0);
	ck("idle master polls not-ready", n, 0);
	ck("  no POLLHUP while live", set[0].revents & POLLHUP, 0);

	n = read(mfd, buf, sizeof(buf));
	e = errno;
	ck("idle master read fails", n < 0, 1);
	if (n < 0)
		cke("  errno while live", e, EAGAIN);

	/* Give the channel a slave and take it away again. */
	if ((kid = fork()) < 0) {
		printf("%s: FAIL -- fork failed, errno %d\n", who, errno);
		fails++;
		return 1;
	}
	if (kid == 0) {
		(void) close(mfd);		/* the inherited master */
		if ((sfd = open(SLAVE, O_RDWR)) < 0)
			exit(1);
		(void) close(sfd);
		exit(0);
	}
	st = 0;
	(void) wait(&st);
	kid = 0;
	ck("a slave opened and closed", (st >> 8) & 0xFF, 0);

	/* Route one: poll.  Non-blocking first. */
	set[0].fd = mfd;
	set[0].events = POLLIN;
	set[0].revents = 0;
	n = poll(set, (unsigned long)1, 0);
	ck("hung-up master polls ready", n, 1);
	ck("  revents has POLLHUP", (set[0].revents & POLLHUP) != 0, 1);

	/* And blocking, which must return at once rather than sit out its
	 * timeout -- that is the exact shape of the defect. */
	set[0].fd = mfd;
	set[0].events = POLLIN;
	set[0].revents = 0;
	n = poll(set, (unsigned long)1, 1000);
	ck("blocking poll does not wait", n, 1);
	ck("  revents has POLLHUP", (set[0].revents & POLLHUP) != 0, 1);

	/* Route two: a non-blocking read.  EIO, not EAGAIN. */
	n = read(mfd, buf, sizeof(buf));
	e = errno;
	ck("hung-up master read fails", n < 0, 1);
	if (n < 0)
		cke("  errno after hangup", e, EIO);

	(void) close(mfd);
	return 0;
}

/*
 * How many channels the driver answers on, which is the window ceiling of any
 * window system built on it: one window is one channel.
 *
 * There are two independent limits and they fail differently, which is why
 * this reports each one by name.  NUPTY (sys/drv/pty.c) is how many channels
 * the driver will open at all, and a channel past it is ENXIO.  The nodes
 * (dist/devices/hd.devices) are how many can be NAMED, and a missing one is
 * ENOENT.  A node whose channel is past NUPTY is the mismatch worth catching:
 * a server finds the name, opens it, and is told the device does not exist.
 *
 * Every master is held open until the end rather than opened and closed one at
 * a time, because that is the shape of the thing being sized: a window system
 * holds one master per window, and the pool they come from is the arena.
 */
static void
channels()
{
	char name[32];
	int fd[NCHAN];
	int i, e, nodes, live, past;

	nodes = 0;
	live = 0;
	past = 0;
	for (i = 0; i < NCHAN; i++) {
		sprintf(name, "/dev/ptyp%c", "0123456789abcdef"[i]);
		fd[i] = open(name, O_RDWR);
		e = errno;
		if (fd[i] >= 0) {
			nodes++;
			live++;
			continue;
		}
		if (e == ENOENT)
			continue;	/* no such node: nothing to say */
		nodes++;
		if (e == ENXIO)
			past++;		/* node exists, channel past NUPTY */
		else
			printf("%s: /dev/ptyp%x: errno %d -- %s\n", who, i, e,
				why(e));
	}
	for (i = 0; i < NCHAN; i++) {
		if (fd[i] >= 0)
			(void) close(fd[i]);
	}

	printf("%s: %d master nodes, %d open at once\n", who, nodes, live);
	ck("no node names a channel past NUPTY", past, 0);
	ck("sixteen masters open together", live >= NCHAN, 1);
	if (live < NCHAN)
		printf("%s: NOTE %s\n", who,
			nodes < NCHAN ? "add the missing /dev/ptyp* nodes"
				      : "raise NUPTY and relink");
	fflush(stdout);
}

/*
 * Phase 4: is a closed channel given back?
 *
 * A channel is allocated by the open that first needs it and returned to the
 * kalloc arena by the close of its last user (sys/drv/pty.c ptyhold/ptyrele),
 * so nothing here can read the arena's free space -- there is no interface
 * that reports it.  What it can do is spend more of the arena than exists.
 *
 * CYCLES rounds of "open all sixteen masters, close all sixteen" is
 * NCHAN*CYCLES channels at sizeof(PTY) = 462 bytes each.  At CYCLES 30 that is
 * 221,760 bytes drawn from an arena of 24,576, with never more than sixteen
 * (7,392 bytes) live at once: a driver that allocated and did not free would
 * run the arena out partway through and every later open would answer
 * EKSPACE.  Reaching the end is the reclaim.
 *
 * The failing open is reported by number and errno, so a run that does stop
 * says how far it got and whether it was the arena or something else.
 */
#define CYCLES	30

static void
reclaim()
{
	char name[32];
	int fd[NCHAN];
	int i, j, e, bad, first, at;

	who = "pty(reclaim)";
	(void) alarm(TMO * 6);		/* 480 opens and closes, not one read */

	bad = 0;
	first = 0;
	at = 0;
	for (i = 0; i < CYCLES; i++) {
		for (j = 0; j < NCHAN; j++) {
			sprintf(name, "/dev/ptyp%c", "0123456789abcdef"[j]);
			fd[j] = open(name, O_RDWR);
			e = errno;
			if (fd[j] < 0 && bad++ == 0) {
				first = e;
				at = i * NCHAN + j;
			}
		}
		for (j = 0; j < NCHAN; j++) {
			if (fd[j] >= 0)
				(void) close(fd[j]);
		}
	}

	printf("%s: %d channels taken and given back, %d at a time\n", who,
		NCHAN * CYCLES, NCHAN);
	ck("every open in the cycle succeeded", bad, 0);
	if (bad)
		printf("%s: first failure at open %d: errno %d -- %s\n", who,
			at, first, why(first));
	fflush(stdout);
}

int
main(argc, argv)
int argc;
char **argv;
{
	char acc[128];
	int mfd, other, e, st;

	(void) signal(SIGALRM, hung);
	(void) alarm(TMO);

	/* Phase 0: that a slave open waits for a master at all. */
	carrier();
	who = "pty";
	(void) signal(SIGALRM, hung);
	(void) alarm(TMO);

	/* The master first: it is the carrier the slave open waits for. */
	if ((mfd = open(MASTER, O_RDWR)) < 0) {
		e = errno;
		printf("%s: open %s: errno %d -- %s\n", who, MASTER, e, why(e));
		printf("%s: FAIL\n", who);
		return 1;
	}
	ck("open /dev/ptyp0", 1, 1);

	/* A master is exclusive.  This is also the check that tells a busy
	 * channel apart from a missing driver, which telnetd could not. */
	other = open(MASTER, O_RDWR);
	e = errno;
	if (other >= 0)
		(void) close(other);
	ck("second master open is refused", other < 0, 1);
	if (other < 0)
		cke("  errno on the refusal", e, EDBUSY);

	if ((kid = fork()) < 0) {
		printf("%s: FAIL -- fork failed, errno %d\n", who, errno);
		return 1;
	}
	if (kid == 0)
		exit(slave(mfd));

	/* Slave -> master.  Matching on the word alone rather than on the whole
	 * line keeps a missing CR a failed check instead of a hang. */
	acc[0] = '\0';
	ck("read ping off the master", expect(mfd, "ping", acc, sizeof(acc)), 1);
	/* CRMOD is in DEF_SG_FLAGS, so the slave's NL arrives as CR NL. */
	ck("  output processing added the CR", has(acc, "ping\r"), 1);

	/* Master -> slave. */
	ck("write pong to the master", write(mfd, "pong\n", 5), 5);

	/* The slave's acknowledgement, which says it read the line. */
	ck("read ok off the master", expect(mfd, "ok", acc, sizeof(acc)), 1);

	/* The last reference to the master.  Dropping carrier must fail the
	 * slave's next read. */
	(void) close(mfd);

	st = 0;
	if (wait(&st) < 0)
		ck("wait for the slave", 0, 1);
	kid = 0;			/* reaped: hung() must not kill a stale pid */
	ck("slave side had no failures", (st >> 8) & 0xFF, 0);
	ck("slave was not signalled", st & 0x7F, 0);

	/* Phase 2: the master's view of a slave that has gone. */
	hangup();

	/* Phase 3: how many channels there are. */
	who = "pty";
	channels();

	/* Phase 4: that a closed channel comes back. */
	reclaim();

	who = "pty";
	printf("%s: %s\n", who, fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
