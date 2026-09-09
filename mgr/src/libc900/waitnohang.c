/*
 * waitnohang -- wait(2) bounded by the alarm clock, standing in for the
 * waitpid(...,WNOHANG) and wait3(...,WNOHANG,...) this system does not have.
 * wait(2) is the only reaper here and it blocks until some child dies, so the
 * bound is a caught SIGALRM: the kernel's wait sleeps below CVNOSIG
 * (sys/sched.h: CVWAIT 128), a caught signal unwinds the system call, and the
 * caller gets -1/EINTR.  That is reported as 0, the WNOHANG answer for `no
 * child was ready'.
 *
 * SIGALRM is 4 on this machine and its handler is whatever the caller had
 * armed -- in the server, catch(), which prints the fault and quits -- so the
 * handler is swapped for one that only returns and swapped back afterwards.
 * Cancelling the timer before restoring the handler is what keeps a timer that
 * expires alongside a successful wait from reaching the caller's handler.  The
 * alarm clock is one per process: a timeout already pending when this is
 * called is re-armed on the way out, for the whole of the count that was left.
 */
#include <errno.h>
#include <signal.h>

/* Long enough that a child which has entered exit(2) is always collected, and
   short enough to be an interruption rather than a hang: the server is single
   threaded and draws nothing while it is in here. */
#define REAPWAIT	2

extern int errno;
extern void (*signal())();
extern unsigned alarm();

static void
onalarm(sig)
int sig;
{
	signal(SIGALRM, onalarm);
}

int
waitnohang(statusp)
int *statusp;
{
	void (*oldfunc)();
	unsigned oldleft;
	int pid, err;

	oldleft = alarm(0);
	oldfunc = signal(SIGALRM, onalarm);
	alarm((unsigned)REAPWAIT);
	errno = 0;
	pid = wait(statusp);
	err = errno;
	alarm(0);
	signal(SIGALRM, oldfunc);
	if (oldleft != 0)
		alarm(oldleft);
	if (pid < 0 && err == EINTR)
		return 0;
	return pid;
}
