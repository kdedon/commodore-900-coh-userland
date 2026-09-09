/*
 * kalloc.c -- how many processes can the kernel allocation arena still hold?
 *
 * There is one arena (sys/coh/alloc.c, sized by ALLSIZE in z8001/src/conf.c)
 * and no process table, no file table and no poll table behind it: a PROC
 * (166 bytes), a SEG (34) per segment, an FD (12) per open file description
 * and a 32-event poll cluster (576) all come out of the same pool that the
 * buffer headers and the pty pool were carved from at boot.  When it fills,
 * nothing says so -- fork(2) reports EAGAIN "no more process table entries",
 * which is a table that does not exist, and open(2) reports EAGAIN too.
 *
 * This program provokes that state on purpose and reports where it lands.  It
 * builds the shape a window system actually has:
 *
 *	all NUPTY pty masters held open at once (the pool is already spent
 *	  at boot, but holding them is what a session does)
 *	CAP child processes alive together -- each one a PROC plus its
 *	  u-area, stack and data SEGs
 *	each child holding its own pipe (two more FDs, two inodes) and
 *	  BLOCKED IN poll(2) on it, which is the only way to hold poll event
 *	  buffers: pollexit() hands them back on every path out of poll(2),
 *	  so events are charged to the arena only while someone is inside it
 *
 * The child writes its status byte BEFORE it blocks, so the parent's count is
 * of children that really reached the poll, not merely of forks that returned.
 *
 * The number to watch is `children started'.  It is a measurement, not a
 * constant: how far it gets depends on how much of the arena the running
 * system had already spent.  With ALLSIZE at 10240 and NUPTY at 8 there is
 * about 4K left after boot, which is a dozen processes for the whole machine
 * -- init, the gettys and a shell included -- and this test stops well short
 * of CAP.  It is the FIRST FAILURE line that identifies the cause: EAGAIN
 * with `alloc: kernel arena exhausted' on the console is the arena, EAGAIN
 * with silence is physical core.
 *
 * Nothing here is left behind: every child is killed and waited for, so a
 * failing run does not leave the machine short of the thing it just measured.
 *
 * BOTH OF THOSE ARE PLACES TO HANG, and on exactly the kernel this provokes.
 * The parent reads one status byte per child from a pipe every other child also
 * holds open, so a child that forks and then never reaches its write leaves the
 * read with no writer that will ever close -- the arena being exhausted is the
 * likeliest reason for that.  cleanup() then waits for children that a SIGTERM
 * could not be delivered to.  Both are under DEADLINE, because a test that
 * hangs has no verdict, which is worse than one that fails.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

extern int errno;

#define NPTY	8			/* NUPTY: masters to hold open */
#define CAP	20			/* children to attempt */
#define WANT	16			/* children a healthy arena sustains */
#define DEADLINE 120			/* seconds for the whole run */

char	*who = "kalloc";
int	fails;
int	kid[CAP];
int	nkid;

/*
 * The deadline expired.  Every child started so far is killed, so the machine
 * is not left holding the resources this run was measuring.
 */
static void
hung()
{
	int i;

	printf("%s: FAIL -- no verdict within %d s, with %d children started."
		"  A child\n", who, DEADLINE, nkid);
	printf("%s: that forked and never answered leaves the parent's read"
		" with no writer\n", who);
	printf("%s: that will ever close -- an exhausted arena is the likeliest"
		" reason.\n", who);
	fflush(stdout);
	for (i = 0; i < nkid; i++)
		(void)kill(kid[i], SIGKILL);
	exit(1);
}

static void
ck(what, got, want)
char *what;
int got, want;
{
	printf("%s: %-40s got %3d want %3d  %s\n", who, what, got, want,
		got >= want ? "ok" : "FAIL");
	fflush(stdout);
	if (got < want)
		fails++;
}

/*
 * A child: take a pipe of its own, tell the parent it got there, then block
 * in poll(2) so that its PROC, SEGs, FDs and poll event stay charged to the
 * arena for as long as the parent needs them to.  Never returns.
 */
static void
child(wfd)
int wfd;
{
	struct pollfd set[1];
	int p[2];
	char c;

	c = 'n';
	if (pipe(p) == 0)
		c = 'y';
	(void)write(wfd, &c, 1);
	if (c == 'n')
		_exit(1);

	/* An empty pipe is never readable, so this blocks until the parent's
	 * cleanup() sends a signal -- nothing else is meant to end the wait,
	 * so the timeout is INFTIM rather than some finite ms count standing
	 * in for "long enough": a duration here would only be a value that a
	 * 16-bit poll(2) call might silently fail to honor, for no benefit,
	 * since no caller ever waits for it to elapse. */
	set[0].fd = p[0];
	set[0].events = POLLIN;
	set[0].revents = 0;
	/* nfds is unsigned long in this ABI and there are no prototypes: a
	 * bare 1 would pass 16 bits where 32 are read. */
	(void)poll(set, (unsigned long)1, -1);
	_exit(0);
}

/*
 * Kill and reap everything started.  Called on every exit path.
 */
static void
cleanup()
{
	int i, st;

	for (i = 0; i < nkid; i++)
		(void)kill(kid[i], SIGTERM);
	for (i = 0; i < nkid; i++)
		(void)wait(&st);
}

int
main(argc, argv)
int argc;
char **argv;
{
	char name[16];
	char c;
	int pty[NPTY];
	int st[2];
	int nopen, ferr, i, pid, n;

	(void)signal(SIGALRM, hung);
	(void)alarm(DEADLINE);

	printf("%s: arena stress -- %d ptys, up to %d blocked children\n",
		who, NPTY, CAP);
	fflush(stdout);

	/*
	 * Hold every pty master.  The pool itself was allocated at boot, so
	 * this costs only descriptors -- but a master that will not open at
	 * all changes what the rest of the run means, so it is reported.
	 */
	nopen = 0;
	for (i = 0; i < NPTY; i++) {
		sprintf(name, "/dev/ptyp%d", i);
		if ((pty[i] = open(name, O_RDWR)) >= 0)
			nopen++;
	}
	ck("pty masters held open", nopen, NPTY);

	if (pipe(st) < 0) {
		printf("%s: FAIL cannot make status pipe\n", who);
		return 1;
	}

	/*
	 * Fork until the kernel refuses.  Each child answers before it
	 * blocks, so a child that forked but could not allocate its pipe is
	 * not counted as living space.
	 */
	ferr = 0;
	for (i = 0; i < CAP; i++) {
		if ((pid = fork()) == 0) {
			(void)close(st[0]);
			child(st[1]);
			/* not reached */
		}
		if (pid < 0) {
			ferr = errno;
			break;
		}
		kid[nkid++] = pid;
		n = read(st[0], &c, 1);
		if (n != 1 || c != 'y') {
			ferr = EAGAIN;		/* forked, could not equip */
			break;
		}
	}

	ck("children started and blocked in poll", nkid, WANT);
	printf("%s: first failure at child %d, errno %d%s\n", who, nkid, ferr,
		ferr == EAGAIN ? " (EAGAIN -- arena or core)" : "");
	printf("%s: if the console said `alloc: kernel arena exhausted',"
		" raise ALLSIZE\n", who);
	fflush(stdout);

	cleanup();
	for (i = 0; i < NPTY; i++)
		if (pty[i] >= 0)
			(void)close(pty[i]);

	printf("%s: %s\n", who, fails ? "FAIL" : "PASS");
	fflush(stdout);
	return fails ? 1 : 0;
}
