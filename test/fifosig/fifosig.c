/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * fifosig.c -- does a CAUGHT SIGNAL during a blocking FIFO read corrupt the
 * kernel's reader/writer counts?
 *
 *	fifosig [path]			default /fifosig.p
 *
 * Two phases, because psleep() has two callers that can be interrupted and the
 * damage is different.  Phase 1 is the blocking READ described below.  Phase 2
 * is the blocking OPEN: open(2) of a fifo for reading waits in popen() for a
 * writer, and an unwind from there skips fdfinish() as well as the counts --
 * leaving u_filep[fd] holding a file table entry with f_refc == 0, which
 * nothing can close and which fdaclose() meets when the process exits:
 *	if (fdp->f_refc == 0)  panic("fdclose()");	 (fd.c:224)
 * So phase 2's verdict is not printed by phase 2 at all: it is the process
 * managing to EXIT.  It also leaks the inode reference uopen() would have
 * dropped (sys3.c:192).
 *

 * WHAT IS WRONG.  pipe.c's psleep() brackets its sleep with a hand-off of the
 * caller from the "awake" count to the "sleeping" count and back again:
 *
 *	case IFWFW:
 *		--ip->i_par;  ++ip->i_psr;
 *		v_sleep(...);
 *		++ip->i_par;  --ip->i_psr;	<- pipe.c:481
 *
 * but v_sleep() does not always return.  sleep() (proc.c:579, :619) answers a
 * pending CAUGHT signal with envrest(&u.u_sigenv), and envrest is the longjmp
 * half of a setjmp pair (z8001/src/md.s:754) -- it unwinds to the syscall entry
 * and the two restoring assignments never run.  psleep's `return(-1)' with
 * EINTR at pipe.c:491 is dead code in exactly the case it was written for.
 *
 * The FIFO is then permanently one reader short in i_par and one too many in
 * i_psr, and nothing notices until the descriptor is closed: pclose() does
 * `if ( --ip->i_par < 0 ) panic("Out of sync IPR in pclose")' at pipe.c:223.
 * So the cost of one caught signal, arriving at one unlucky moment, is the
 * whole machine -- at close time, which may be long afterwards and in another
 * program entirely.  The IFWFR arm at pipe.c:484 loses i_paw the same way and
 * panics at pipe.c:226.
 *
 * Reaching it needs a signal with a HANDLER installed: sleep() only unwinds
 * when nondsig() says the pending signal is not taking its default action,
 * so a Ctrl-C that simply kills the process does not do it.  The inet
 * daemon's whole client protocol is FIFO pairs with timeouts, so every
 * network program on this machine is exposed to it.
 *
 * HOW THIS REPORTS.  There is no way to read i_par from userland, so the probe
 * is the close, and on a kernel with the bug the answer is a PANIC rather than
 * a line of output.  That is not a harness failure: a transcript that stops at
 * "about to close" IS the result, and it is unambiguous.  On a fixed kernel the
 * program prints PASS and exits 0.
 *
 * AND A DEADLINE, WHICH IS A CHILD AND NOT AN ALARM.  Every wait here is meant
 * to be ended by the caught SIGALRM that is this program's INSTRUMENT, so
 * alarm(2) cannot also be its bound.  Two of them can outlast it anyway: phase
 * 2's open(2) is a rendezvous with a writer that never comes, so a kernel that
 * does not deliver the signal into popen()'s sleep at all sits there for ever,
 * and the close(2) that is phase 1's whole verdict can sleep in pclose().  A
 * watchdog process therefore reports and kills, because a probe that hangs has
 * no verdict -- worse than one that fails, since whatever ran it is stuck too.
 * The panic this looks for is not something the watchdog can catch: a stopped
 * machine takes the watchdog with it, and that transcript IS the result.
 *
 * Needs nothing but a writable directory: no network, no daemon, no second
 * machine.  Every line starts with "fifosig:" so a scripted run can pick it out.
 */
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

extern int errno;

static int fired;

/*
 * The bound on the whole run: long enough for both phases' two-second alarms
 * and the sleeps around them, short enough to answer while somebody is looking.
 */
#define DEADLINE 60

static int wdog;

/*
 * The watchdog's knock, caught here so the run ends with an ordinary failing
 * exit status rather than a signal: a caller that reads "killed by SIGKILL" has
 * to know about this program to know what happened.
 */
static void knocked()
{
	printf("fifosig: FAIL -- ended by its own watchdog (above)\n");
	fflush(stdout);
	exit(1);
}

/*
 * Start the watchdog.  It outlives every wait this program makes and then kills
 * the test process, which may itself be the one that is stuck.
 */
static void watchdog()
{
	int parent;

	parent = getpid();
	(void)signal(SIGTERM, knocked);
	if ((wdog = fork()) != 0)
		return;
	sleep(DEADLINE);
	printf("fifosig: FAIL -- no verdict within %d s.  A read or an open"
		" that a caught\n", DEADLINE);
	printf("fifosig: signal never ended, or a close still asleep in"
		" pclose -- reported\n");
	printf("fifosig: rather than hung, because a test that hangs has no"
		" verdict at all.\n");
	fflush(stdout);
	/* SIGTERM first, so the test process reports its own failure and exits
	 * 1; SIGKILL after a grace period in case it is too far gone to run a
	 * handler, which is itself worth seeing. */
	kill(parent, SIGTERM);
	sleep(3);
	kill(parent, SIGKILL);
	_exit(1);
}

/*
 * Stop the watchdog.  Called on every path that reaches a verdict.
 */
static void unwatch()
{
	int st;

	if (wdog > 0) {
		kill(wdog, SIGKILL);
		st = 0;
		wait(&st);
		wdog = 0;
	}
}

static void caught()
{
	fired = 1;
	/* Re-arming is deliberate: the point is a handler that RETURNS, so the
	 * interrupted read is resumed or fails normally and the process lives
	 * long enough to reach the close.  Nothing here exits. */
	signal(SIGALRM, caught);
}

int main(argc, argv)
int argc;
char **argv;
{
	char *path;
	char path2[64];
	char buf[8];
	int fd, n;

	path = argc > 1 ? argv[1] : "/fifosig.p";

	watchdog();

	(void)unlink(path);
	if (mknod(path, 010000 | 0600, 0) < 0)
	{
		printf("fifosig: cannot make the FIFO %s, errno %d\n", path,
			errno);
		unwatch();
		return 2;
	}

	/*
	 * O_RDWR so the OPEN cannot block -- popen's IPR|IPW arm increments
	 * both counts and never sleeps.  What is under test is the READ, and
	 * opening this way keeps the two apart: if this program hangs here the
	 * fault is the open path and not the one being probed.
	 */
	if ((fd = open(path, O_RDWR)) < 0)
	{
		printf("fifosig: cannot open %s, errno %d\n", path, errno);
		unwatch();
		return 2;
	}
	printf("fifosig: opened %s O_RDWR as fd %d\n", path, fd);
	fflush(stdout);

	signal(SIGALRM, caught);
	alarm(2);
	printf("fifosig: reading an empty FIFO with a 2s alarm armed\n");
	fflush(stdout);

	/* Nothing has ever been written and this process is the only opener,
	 * so the read must block -- and the alarm must be what ends it. */
	errno = 0;
	n = read(fd, buf, sizeof(buf));
	printf("fifosig: read returned %d, errno %d, handler ran %d\n", n,
		errno, fired);
	fflush(stdout);
	alarm(0);

	if (!fired)
	{
		printf("fifosig: INCONCLUSIVE -- the alarm never fired, so the"
			" read was never interrupted\n");
		close(fd);
		(void)unlink(path);
		unwatch();
		return 2;
	}

	printf("fifosig: about to close.  A PANIC HERE IS THE FAILURE:\n");
	printf("fifosig:   the interrupted read left i_par one short, and\n");
	printf("fifosig:   pclose is about to decrement it below zero.\n");
	fflush(stdout);

	close(fd);

	printf("fifosig: PASS -- the close survived, so the counts are\n");
	printf("fifosig: PASS -- balanced across an interrupted read\n");
	fflush(stdout);
	(void)unlink(path);

	/*
	 * Phase 2: the same signal, taken inside a blocking OPEN.
	 *
	 * O_RDONLY with no writer anywhere is the one open that sleeps, so this
	 * is popen()'s IPR arm rather than the read path.  Nothing here can see
	 * the file table directly; the failure shows up as a panic when this
	 * process exits, so the last thing printed before the shell prompt
	 * returns is the whole result.
	 */
	strcpy(path2, path);
	strcat(path2, "o");
	(void)unlink(path2);
	if (mknod(path2, 010000 | 0600, 0) < 0)
	{
		printf("fifosig: cannot make the FIFO %s, errno %d\n", path2,
			errno);
		unwatch();
		return 2;
	}

	fired = 0;
	signal(SIGALRM, caught);
	alarm(2);
	printf("fifosig: opening a writerless FIFO with a 2s alarm armed\n");
	fflush(stdout);
	errno = 0;
	fd = open(path2, O_RDONLY);
	printf("fifosig: open returned %d, errno %d, handler ran %d\n", fd,
		errno, fired);
	fflush(stdout);
	alarm(0);
	if (fd >= 0)
	{
		/* Somebody else opened it for writing, which this program
		 * cannot arrange -- the open never slept, so it proves
		 * nothing. */
		close(fd);
		printf("fifosig: INCONCLUSIVE -- the open did not block\n");
		(void)unlink(path2);
		unwatch();
		return 2;
	}
	if (!fired)
	{
		printf("fifosig: INCONCLUSIVE -- the alarm never fired\n");
		(void)unlink(path2);
		unwatch();
		return 2;
	}
	(void)unlink(path2);
	printf("fifosig: about to exit.  A PANIC HERE IS THE FAILURE:\n");
	printf("fifosig:   the interrupted open left a file table entry with\n");
	printf("fifosig:   no reference, and exit is about to close it.\n");
	printf("fifosig: PASS -- if the shell prompt follows, the open path\n");
	printf("fifosig: PASS -- released its descriptor too\n");
	fflush(stdout);
	unwatch();
	return 0;
}
