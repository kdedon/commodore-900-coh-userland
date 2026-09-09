/*
 * kshim.c -- a host stand-in for the parts of COHERENT that test measures,
 * with injectable defects.
 *
 * The programs in test are compiled here VERBATIM by the host cc and linked
 * against this file, which sits between them and the host kernel.  With $BREAK
 * unset it stays out of the way (or models the target behaviour where the host
 * has none), and every test must PASS.  With $BREAK set to one of the modes
 * below it misbehaves in one specific, named way, and the test whose stated
 * purpose is to catch that must FAIL.
 *
 * That pairing is the whole point: a check nobody has ever seen fail is
 * decoration, and the answer is cheap to have here -- two seconds a case,
 * no image, no emulator.
 *
 * Interposition is by strong definition: a function defined in the executable
 * wins over the shared libc, and the real one is reached through
 * dlsym(RTLD_NEXT).  So the sources need no edits, no -D and no wrappers.
 *
 * THE MODES.  Each names a defect in the kernel, driver or libc that some check
 * in test claims to catch:
 *
 *   none			behave.  Every test must pass.
 *
 *   sbrk-dirty			sbrk() hands back memory full of 0xA5 rather
 *				than zeros -- the whole reason tests/sbrkzero
 *				exists: the inet stack's `#if ZERO' blocks skip
 *				table initialisation on the promise it is zero.
 *   sbrk-refuse		sbrk() refuses (returns NULL, as libc/sys/sbrk.c
 *				really does -- not (char *)-1).
 *   sbrk-nocarry		sbrk() refuses once the first 64K segment is
 *				full instead of carrying into the next.
 *   sbrk-wrap			sbrk() wraps back to offset 0 of the SAME
 *				segment, aliasing memory already handed out.
 *				The silent corruption sbrkzero exists to
 *				prevent.
 *   sbrk-liar			sbrk(0) reports a break that is not where the
 *				last block ended.
 *
 *   halt-unimpl		halt(2) is not implemented: -1/ENOSYS.  The
 *				negative control for tests/ddtentry.
 *   halt-eperm			halt(2) refused because the caller is not root.
 *
 *   clock-stuck		time(2) never advances.
 *   clock-back			time(2) runs backwards.
 *
 *   lp-enxio			/dev/rlp has no driver on its major.
 *   lp-eio			/dev/rlp fails with something that is neither
 *				"no driver" nor "no printer attached".
 *
 *   strn-signed		strncmp/strncpy/strncat clamp their measured
 *				length against n with a SIGNED compare, so a
 *				string of 32768 bytes or more reads as shorter
 *				than any n.  The bug tests/strnlong exists for.
 *   strn-count0		a count of 0 runs the block instruction to
 *				completion -- 65536 bytes -- because LDIRB
 *				decrements before it tests.  The other one.
 *
 *   poll-deaf			poll(2) never reports an event: it sleeps out
 *				its timeout and answers 0.
 *   poll-ready			poll(2) always answers "readable".
 *   poll-nval			poll(2) sets POLLNVAL on every descriptor.
 *   poll-noout			poll(2) never reports POLLOUT.
 *   poll-nohup			poll(2) never reports POLLHUP or POLLERR: the
 *				level check on a writerless pipe is missing.
 *   poll-alwayshup		poll(2) reports POLLHUP for everything.  The
 *				control: a `return POLLHUP' that passes the
 *				hangup cases must fail the live-pipe one.
 *   poll-nosig			an INFTIM poll(2) ignores signals and never
 *				returns.  Nothing about it is visible except a
 *				process that never comes back.
 *   sel-ready			select(2) answers "readable" for every
 *				descriptor asked about, whether or not anything
 *				is there -- an empty FIFO included.
 *   sel-deaf			select(2) never reports anything ready.
 *   sel-sticky			a descriptor stays readable after its bytes
 *				have been taken off it -- the queue's count
 *				never returns to zero.  Any caller that trusts
 *				select(2) and then reads blocks for ever.
 *   sel-clamp32		a wait longer than 32 s is served as 32 s and
 *				then reported as the timeout that was asked
 *				for.  What reducing a timeval to poll(2)'s int
 *				of milliseconds without checking produces, and
 *				a silent one: nothing fails, the caller is
 *				simply told its deadline passed early.
 *   sel-roundup		the other rounding: every wait is served in
 *				whole 32 s units, so a 3 s timeout costs 32.
 *   sel-chunkwait		a long wait is split into pieces with the
 *				descriptors not watched during a piece, so a
 *				readiness arriving inside one is not seen
 *				until the piece ends.
 *   sel-negzero		a negative timeout is served as a length --
 *				zero -- rather than refused.
 *
 *   excl-never-set		open(O_CREAT|O_EXCL) never marks the inode.
 *   excl-never-clear		the mark is never taken off, on close or on
 *				process death.  gzip's "file exists" for an
 *				archive it had just written.
 *   excl-leak-on-death		close() clears the mark but process death does
 *				not.
 *
 *   pty-few			only two pty masters will open.
 *   pty-noexcl			a second master open of a channel that already
 *				has one succeeds: the channel is no longer
 *				exclusive, so two programs drive one line.
 *   pty-nocarrier		a slave open does not wait for a master: it
 *				succeeds on a channel with no carrier, which is
 *				the master-before-slave rule gone.
 *   pty-nohup			the master's last close reports to the slave as
 *				end of file rather than as a hangup: the slave's
 *				read answers 0 instead of EIO, and a login shell
 *				reading its own terminal sees a clean EOF where
 *				the line has actually gone.
 *   pty-hupdeaf		the master cannot tell a dead channel from an
 *				idle one: p_mopen == 3 is never reported, so
 *				poll(2) says "nothing ready" and a non-blocking
 *				read says EAGAIN, for ever.  The defect
 *				sys/drv/pty.c ptyread's "hangup outranks
 *				IONDLY" comment describes.
 *   pty-nxio			NUPTY is smaller than the node set, so nodes
 *				exist whose channel the driver refuses ENXIO.
 *   pty-noreclaim		a closed channel is not given back to the
 *				kalloc arena.
 *   fork-cap			fork(2) refuses after five children.
 *   child-mute			a child's one-byte status write is dropped, so
 *				the parent's rendezvous read never completes.
 *				This one has no wrong answer to give: it is the
 *				HANG, and what it proves is that the deadline
 *				turns a hang into a reported failure.
 *
 *   tc-setnop			TCSETA is `return 0': it stores nothing.
 *   tc-noveol			TCSETA stores everything except c_cc[VEOL],
 *				which is VTIME's slot -- the pair an
 *				sgttyb-backed driver loses.
 *   vtime-instant		a VMIN=0/VTIME read returns 0 at once: the
 *				timer is stored and never armed.
 *   vtime-forever		the same read never returns at all.
 *   not-a-tty			isatty() says no.
 *
 *   font-nomatch		the font bank reads back different bytes.
 *   font-lowwrite		the read-only bank accepts a write.
 *   font-past			a font write runs past the last slot.
 *   font-none			there is no font ioctl (an ordinary console).
 *
 *   stack-nogrow		the stack is not grown past its initial size.
 *   stack-wrongsig		the ceiling kills the process with SIGBUS
 *				rather than faulting it cleanly.
 *   stack-wedge		the process that hits the ceiling is never
 *				killed at all: it wedges in the growth path,
 *				which is the third of the three failures and
 *				the one with no wrong answer to give.
 *
 *   swap-scribble		a segment comes back from the swap device with
 *				its contents changed.
 *   swap-mute			a sleeper never announces itself.  The hang
 *				again, and the same thing to prove about it.
 *
 * Host-only scaffolding.  Nothing here is compiled for the C900 and nothing
 * here ships.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <sgtty.h>
#include "termio.h"

/* The COHERENT errno numbers the tests name.  See hostcompat.h. */
#define	C_EDATTN	38
#define	C_EDBUSY	39

static char *bmode;
static pid_t rootpid;			/* the process that started the run */
static char *work = "/tmp";		/* $KWORK: stands in for the root fs */

static char *mode(void)
{
	if (!bmode) {
		bmode = getenv("BREAK");
		if (!bmode || !*bmode)
			bmode = "none";
	}
	return bmode;
}

static int is(const char *m)
{
	return strcmp(mode(), m) == 0;
}

/* Are we a child of the process that started?  Several defects belong to one
 * side of a fork and not the other. */
static int inchild(void)
{
	return getpid() != rootpid;
}

/* ------------------------------------------------------------------------
 * The real entry points.
 * ---------------------------------------------------------------------- */
static int (*r_open)(const char *, int, ...);
static int (*r_close)(int);
static ssize_t (*r_read)(int, void *, size_t);
static ssize_t (*r_write)(int, const void *, size_t);
static int (*r_poll)(struct pollfd *, nfds_t, int);
static int (*r_select)(int, fd_set *, fd_set *, fd_set *, struct timeval *);
static int (*r_ioctl)(int, unsigned long, ...);
static int (*r_isatty)(int);
static pid_t (*r_fork)(void);
static time_t (*r_time)(time_t *);
static int (*r_mknod)(const char *, mode_t, dev_t);
static int (*r_unlink)(const char *);
static int (*r_creat)(const char *, mode_t);
static int (*r_stat)(const char *, struct stat *);
static unsigned (*r_sleep)(unsigned);

#define REAL(p, n)	if (!p) p = dlsym(RTLD_NEXT, n)

/* ------------------------------------------------------------------------
 * Paths.  Several tests write to the root of the filesystem, which they may do
 * on the target and may not here, so the handful of names they use are moved
 * into $KWORK.  Only those names: a blanket remapping would catch libc's own
 * opens and hide real behaviour.
 * ---------------------------------------------------------------------- */
static const char *rootpaths[] = {
	"/pp.fifo", "/sl.fifo", "/oxtst1", "/oxtst2", "/oxtst3",
	"/deepstack.out",
	"/tmp/pt", "/tmp/pollexit.p", 0
};

static const char *remap(const char *p)
{
	static char buf[256];
	int i;

	if (!p || *p != '/')
		return p;
	for (i = 0; rootpaths[i]; i++)
		if (strcmp(p, rootpaths[i]) == 0) {
			snprintf(buf, sizeof buf, "%s%s", work, p);
			return buf;
		}
	return p;
}

/* ------------------------------------------------------------------------
 * sbrk(2), and the segment model underneath it.
 *
 * The target's break walks a chain of 64K hardware segments: a block never
 * straddles a boundary, so a request that does not fit the remainder of one
 * segment starts at offset 0 of the next, and the far pointer's top byte
 * changes.  tests/sbrkzero checks exactly that, and checks that every byte of a
 * new region reads back as zero.  Both need real memory the program can write
 * to, so the model is a row of separately mapped 64K regions whose addresses
 * differ in bits 24 and up -- which is what makes `(cur >> 24) != (prev >> 24)'
 * mean "carried into the next segment" here as it does there.
 * ---------------------------------------------------------------------- */
#define NSEG		12
#define SEGSIZE		0x10000UL
#define SEGSTRIDE	0x01000000UL
#define SEGBASE		0x20000000UL

static char *segbase[NSEG];
static int cseg = -1;
static unsigned long coff;
static char *lastbrk;

static char *mapseg(int i)
{
	void *want, *got;

	if (i >= NSEG)
		return 0;
	if (segbase[i])
		return segbase[i];
	want = (void *)(SEGBASE + (unsigned long)i * SEGSTRIDE);
	got = mmap(want, SEGSIZE, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (got == MAP_FAILED)
		return 0;
	return segbase[i] = (char *)got;
}

void *sbrk(intptr_t inc)
{
	char *p;

	if (cseg < 0) {
		if (!mapseg(0))
			return 0;
		cseg = 0;
		coff = 0;
		lastbrk = segbase[0];
	}
	if (inc == 0)
		return is("sbrk-liar") ? lastbrk + 7 : lastbrk;
	if (inc < 0)
		return lastbrk;
	if (is("sbrk-refuse")) {
		errno = ENOMEM;
		return 0;
	}
	if (coff + (unsigned long)inc > SEGSIZE) {
		/* The block does not fit the rest of this segment. */
		if (is("sbrk-nocarry")) {
			errno = ENOMEM;
			return 0;
		}
		if (is("sbrk-wrap")) {
			coff = 0;	/* back over memory already handed out */
		} else {
			if (!mapseg(cseg + 1)) {
				errno = ENOMEM;
				return 0;
			}
			cseg++;
			coff = 0;
		}
	}
	p = segbase[cseg] + coff;
	coff += (unsigned long)inc;
	lastbrk = segbase[cseg] + coff;
	/* mmap hands back zeroed pages, which is the correct behaviour.  The
	 * defect is a kernel that does not clear what it grants. */
	if (is("sbrk-dirty"))
		memset(p, 0xA5, (size_t)inc);
	return p;
}

/* ------------------------------------------------------------------------
 * halt(2) -- the deliberate way into the in-kernel debugger, and a call the
 * host has no equivalent of at all.  Both of its failure shapes are here.
 * ---------------------------------------------------------------------- */
int halt(void)
{
	if (is("halt-unimpl")) {
		errno = ENOSYS;
		return -1;
	}
	if (is("halt-eperm")) {
		errno = EPERM;
		return -1;
	}
	return 0;		/* entered ddt and came back */
}

/* ------------------------------------------------------------------------
 * time(2).
 * ---------------------------------------------------------------------- */
time_t time(time_t *tp)
{
	static time_t frozen;
	time_t t;

	REAL(r_time, "time");
	t = r_time(0);
	if (is("clock-stuck")) {
		if (!frozen)
			frozen = t;
		t = frozen;
	} else if (is("clock-back")) {
		if (!frozen)
			frozen = t;
		t = frozen - (t - frozen);
	}
	if (tp)
		*tp = t;
	return t;
}

/* ------------------------------------------------------------------------
 * The counted string routines, in the two broken forms tests/strnlong names.
 * ---------------------------------------------------------------------- */
/* The measured length, clamped against n the way the routine really does it.
 * On the target `int' is 16 bits, so the signed compare that is the bug reads a
 * length of 32768 or more as negative; `short' reproduces that here. */
static size_t clamp(const char *s, size_t n)
{
	size_t l = strlen(s);

	if (is("strn-count0") && n == 0)
		return 65536;		/* LDIRB decrements before it tests */
	if (is("strn-signed")) {
		if ((short)l < (short)n)
			return l;
		return n;
	}
	return l < n ? l : n;
}

char *strncpy(char *d, const char *s, size_t n)
{
	size_t i, lim = clamp(s, n);

	for (i = 0; i < lim && s[i]; i++)
		d[i] = s[i];
	for (; i < lim; i++)
		d[i] = '\0';
	return d;
}

int strncmp(const char *a, const char *b, size_t n)
{
	size_t i, lim = clamp(a, n);

	for (i = 0; i < lim; i++) {
		if (a[i] != b[i])
			return (unsigned char)a[i] - (unsigned char)b[i];
		if (!a[i])
			return 0;
	}
	return 0;
}

char *strncat(char *d, const char *s, size_t n)
{
	size_t i, lim = clamp(s, n), e = strlen(d);

	for (i = 0; i < lim && s[i]; i++)
		d[e + i] = s[i];
	d[e + i] = '\0';
	return d;
}

/* ------------------------------------------------------------------------
 * The O_EXCL model.
 *
 * On COHERENT the mark lives on the IN-CORE inode and every later open of it is
 * refused EEXIST while it is there, whoever asks -- and it comes off when the
 * descriptor that set it goes away, on close(2) and on process death alike.
 * The host has nothing of the kind, so the model is here: a small table in
 * shared memory (children must see their parent's marks and vice versa), keyed
 * by path, holding the owning pid.  "Process death releases it" is the owner no
 * longer answering kill(pid, 0).
 * ---------------------------------------------------------------------- */
#define NEXCL	8
struct excl {
	char	path[256];
	pid_t	owner;
	int	used;
};
static struct excl *excls;
static int exclfd[NEXCL];		/* which fd holds which entry, per proc */

static void exclinit(void)
{
	int i;

	if (excls)
		return;
	excls = mmap(0, sizeof(struct excl) * NEXCL, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (excls == MAP_FAILED)
		excls = 0;
	for (i = 0; i < NEXCL; i++)
		exclfd[i] = -1;
}

/* The entry marking `path', or -1.  An entry whose owner has died is cleared
 * first, unless the mode says the kernel forgets to. */
static int exclfind(const char *path)
{
	int i;

	if (!excls)
		return -1;
	for (i = 0; i < NEXCL; i++) {
		if (!excls[i].used || strcmp(excls[i].path, path))
			continue;
		if (!is("excl-never-clear") && !is("excl-leak-on-death") &&
		    kill(excls[i].owner, 0) < 0 && errno == ESRCH) {
			excls[i].used = 0;
			return -1;
		}
		return i;
	}
	return -1;
}

static void exclset(const char *path, int fd)
{
	int i;

	if (!excls || is("excl-never-set"))
		return;
	for (i = 0; i < NEXCL; i++)
		if (!excls[i].used) {
			snprintf(excls[i].path, sizeof excls[i].path, "%s",
				path);
			excls[i].owner = getpid();
			excls[i].used = 1;
			if (fd >= 0 && fd < NEXCL)
				exclfd[fd] = i;
			return;
		}
}

static void exclclear(int fd)
{
	if (!excls || fd < 0 || fd >= NEXCL || exclfd[fd] < 0)
		return;
	if (!is("excl-never-clear"))
		excls[exclfd[fd]].used = 0;
	exclfd[fd] = -1;
}

/* ------------------------------------------------------------------------
 * The termio model.
 * ---------------------------------------------------------------------- */
static struct termio tio;
static int tioinit;

static void tiodefault(void)
{
	if (tioinit)
		return;
	tioinit = 1;
	tio.c_iflag = ICRNL | IXON;
	tio.c_oflag = OPOST | ONLCR;
	tio.c_lflag = ISIG | ICANON | ECHO;
	tio.c_cc[VINTR] = 0x7F;
	tio.c_cc[VQUIT] = 0x1C;
	tio.c_cc[VERASE] = 0x08;	/* ^H, the driver's own default */
	tio.c_cc[VKILL] = 0x15;		/* ^U */
	tio.c_cc[VEOF] = 0x04;		/* ^D */
	tio.c_cc[VEOL] = 0x00;
}

/* ------------------------------------------------------------------------
 * The hi-res console's loadable font bank.
 * ---------------------------------------------------------------------- */
#define HRGH		25
#define HRLOW		96
#define HRNGLYPH	192
#define HRIOCSFONT	('h' << 8 | 1)
#define HRIOCGFONT	('h' << 8 | 2)
#define HRIOCRFONT	('h' << 8 | 3)

struct hrfont {
	unsigned short	hf_first;
	unsigned short	hf_count;
	unsigned short	*hf_bits;
};

static unsigned short bank[HRNGLYPH][HRGH];

/* ------------------------------------------------------------------------
 * The pty channel model.
 *
 * The host has pseudo-terminals, and they carry data and run a line discipline,
 * so those are used as the plumbing; what the host has NOTHING of is the part
 * tests/pty measures, which is sys/drv/pty.c's channel state machine:
 *
 *	p_mopen	 0  master closed		2  master and slave both open
 *		 1  master open, no slave yet	3  master open, slave has closed
 *
 * and the rules hung off it -- a master that opens exclusively (EDBUSY),
 * a slave that SLEEPS in "ptycd" until a master asserts carrier, a master close
 * that hangs the slave up (EIO, not end of file), a hangup that outranks
 * IONDLY so a non-blocking master learns of it (EIO and POLLHUP rather than
 * EAGAIN and "nothing ready"), a channel count NUPTY the node set must not run
 * past (ENXIO), and a channel that costs the kalloc arena sizeof(PTY)+4 while
 * it is held and gives it back on the last close (EKSPACE when it cannot).
 * None of that is a property of a host pty, so all of it is decided here,
 * BEFORE the host call, and the host pty is asked only to move bytes.
 *
 * The state is shared memory because the two halves of a pty are two
 * processes: the slave is a child of the master's opener, and both sides move
 * the same channel between the four values above.  Descriptor references are
 * counted the same way the target counts them (fd.c fdadupl): fork() adds one
 * per open descriptor, so a child that inherits the master and closes it does
 * NOT drop carrier, which is the rule the probe's own header calls out.
 * ---------------------------------------------------------------------- */
static int nptyopen;			/* masters opened, for pty-few */

#define NUPTY		16		/* channels the driver answers on */
#define NPTYNODE	16		/* /dev/ptyp0../dev/ptypf exist */
#define PTYSZ		462		/* sizeof(PTY), per held channel */
#define PTYARENA	24576		/* what the kalloc arena has for them */

#define C_EKSPACE	35

/* The two sgttyb flags tests/pty moves, by their include/sgtty.h values. */
#define SG_ECHO		0x0008
#define SG_CRMOD	0x0010

struct ptych {
	int	mopen;			/* p_mopen */
	int	muse;			/* master descriptors open */
	int	suse;			/* slave descriptors open */
	int	held;			/* channel charged to the arena */
	char	pts[64];		/* the host pty carrying this channel */
};

static struct ptystate {
	struct ptych	ch[NUPTY];
	long		arena;
} *ptyst;

/* Which of this process's descriptors are pty ends, and which end. */
#define NPFD	64
static struct pfd {
	int	chan;			/* -1: not a pty */
	int	ismaster;
	int	ndelay;
	int	shadow;			/* a second master, under pty-noexcl */
} pfdt[NPFD];

static int nupty(void)
{
	return is("pty-nxio") ? 4 : NUPTY;
}

/*
 * Is `path' a pty node, and which?  Returns 1 for a name the node set has, 0
 * for anything else; a name past the node set is not a pty at all here, which
 * is what makes the open of it ENOENT.
 */
static int ptynode(const char *path, int *chan, int *ismaster)
{
	const char *p;
	int c;

	if (strncmp(path, "/dev/ptyp", 9) == 0)
		*ismaster = 1;
	else if (strncmp(path, "/dev/ttyp", 9) == 0)
		*ismaster = 0;
	else
		return 0;
	p = path + 9;
	if (!p[0] || p[1])
		return 0;
	if (p[0] >= '0' && p[0] <= '9')
		c = p[0] - '0';
	else if (p[0] >= 'a' && p[0] <= 'f')
		c = p[0] - 'a' + 10;
	else
		return 0;
	if (c >= NPTYNODE)
		return 0;		/* no such node */
	*chan = c;
	return 1;
}

static struct pfd *pfdof(int fd)
{
	if (fd < 0 || fd >= NPFD || pfdt[fd].chan < 0)
		return 0;
	return &pfdt[fd];
}

/* The arena charge for a channel: taken by the open that first needs it. */
static int ptyhold(struct ptych *c)
{
	if (c->held)
		return 1;
	if (ptyst->arena < PTYSZ)
		return 0;
	ptyst->arena -= PTYSZ;
	c->held = 1;
	return 1;
}

static void ptyrele(struct ptych *c)
{
	if (!c->held || c->muse + c->suse > 0)
		return;
	c->held = 0;
	if (!is("pty-noreclaim"))
		ptyst->arena += PTYSZ;
}

/*
 * Wait `ms' milliseconds, interruptibly.  poll(2) is the primitive rather than
 * nanosleep(2) because a handler installed with signal(3) carries SA_RESTART,
 * which resumes a nanosleep and would swallow the very signal the driver's
 * sleeps answer with EINTR.
 */
static int ptynap(int ms)
{
	REAL(r_poll, "poll");
	return r_poll((struct pollfd *)0, (nfds_t)0, ms);
}

/* A host pty pair, master end returned, its slave name left in `name'. */
static int hostpty(char *name, int size, int nonblock)
{
	int fd;
	char *s;

	if ((fd = posix_openpt(O_RDWR | O_NOCTTY | (nonblock ? O_NONBLOCK : 0)))
	    < 0)
		return -1;
	if (grantpt(fd) < 0 || unlockpt(fd) < 0 || !(s = ptsname(fd))) {
		(void)r_close(fd);
		return -1;
	}
	snprintf(name, (size_t)size, "%s", s);
	return fd;
}

static int ptyopen(const char *path, int flags)
{
	struct ptych *c;
	char junk[64];
	int chan, ismaster, fd, ndelay;

	if (!ptynode(path, &chan, &ismaster)) {
		errno = ENOENT;
		return -1;
	}
	c = &ptyst->ch[chan];
	ndelay = (flags & (O_NONBLOCK | O_NDELAY)) != 0;

	if (chan >= nupty()) {
		errno = ENXIO;
		return -1;
	}
	if (ismaster && is("pty-few") && nptyopen >= 2) {
		errno = C_EDBUSY;
		return -1;
	}
	if (!ptyhold(c)) {
		errno = C_EKSPACE;
		return -1;
	}

	if (ismaster) {
		if (c->mopen && !is("pty-noexcl")) {
			ptyrele(c);
			errno = C_EDBUSY;
			return -1;
		}
		if (c->mopen) {
			/* A second master on a channel that already has one.
			 * It gets its own host pty so the first one's plumbing
			 * is untouched, and its close is not the carrier's. */
			if ((fd = hostpty(junk, sizeof junk, ndelay)) < 0) {
				ptyrele(c);
				return -1;
			}
			c->muse++;
			nptyopen++;
			pfdt[fd].chan = chan;
			pfdt[fd].ismaster = 1;
			pfdt[fd].ndelay = ndelay;
			pfdt[fd].shadow = 1;
			return fd;
		}
		if ((fd = hostpty(c->pts, sizeof c->pts, ndelay)) < 0) {
			ptyrele(c);
			return -1;
		}
		c->muse++;
		nptyopen++;
		c->mopen = c->suse ? 2 : 1;
	} else {
		/* Wait for carrier, exactly as ptyopen's "ptycd" loop does:
		 * until a master is there, or until a caught signal ends it. */
		while (!c->mopen) {
			if (is("pty-nocarrier"))
				break;
			if (ptynap(20) < 0 && errno == EINTR) {
				ptyrele(c);
				errno = EINTR;
				return -1;
			}
		}
		if (!c->pts[0]) {
			/* No master has ever plumbed this channel, so there is
			 * nothing to join.  Only reachable with the carrier
			 * rule taken away. */
			static int orphan = -1;

			if (orphan < 0 &&
			    (orphan = hostpty(c->pts, sizeof c->pts, 0)) < 0) {
				ptyrele(c);
				return -1;
			}
		}
		if ((fd = r_open(c->pts, O_RDWR | O_NOCTTY)) < 0) {
			ptyrele(c);
			return -1;
		}
		c->suse++;
		if (c->mopen == 1 || c->mopen == 3)
			c->mopen = 2;
	}
	if (fd >= NPFD) {
		(void)r_close(fd);
		errno = EMFILE;
		return -1;
	}
	pfdt[fd].chan = chan;
	pfdt[fd].ismaster = ismaster;
	pfdt[fd].ndelay = ndelay;
	pfdt[fd].shadow = 0;
	return fd;
}

static void ptyclose(struct pfd *p)
{
	struct ptych *c = &ptyst->ch[p->chan];

	if (p->ismaster) {
		if (--c->muse == 0)
			c->mopen = 0;		/* carrier drops */
	} else {
		if (--c->suse == 0 && c->mopen == 2)
			c->mopen = 3;		/* the slave has gone */
	}
	ptyrele(c);
	p->chan = -1;
}

/* Is there anything to read on this end right now? */
static int ptydata(int fd)
{
	struct pollfd s;

	s.fd = fd;
	s.events = POLLIN;
	s.revents = 0;
	REAL(r_poll, "poll");
	return r_poll(&s, (nfds_t)1, 0) > 0 && (s.revents & POLLIN);
}

/*
 * A read of either end.  The channel's state is consulted first and the host
 * pty only afterwards: a hangup is the model's answer, not the host's.
 */
static ssize_t ptyread(struct pfd *p, int fd, void *buf, size_t n)
{
	struct ptych *c = &ptyst->ch[p->chan];

	for (;;) {
		/* A slave whose carrier has gone is hung up, and that outranks
		 * anything still queued: the host would hand over the queue and
		 * then an end of file, which is the one answer a hangup must
		 * not look like. */
		if (!p->ismaster && c->mopen == 0) {
			if (is("pty-nohup"))
				return 0;	/* end of file, not a hangup */
			errno = EIO;
			return -1;
		}
		if (ptydata(fd))
			return r_read(fd, buf, n);
		if (p->ismaster && c->mopen == 3 && !is("pty-hupdeaf")) {
			errno = EIO;
			return -1;
		}
		if (p->ndelay) {
			errno = EAGAIN;
			return -1;
		}
		if (ptynap(20) < 0 && errno == EINTR) {
			errno = EINTR;
			return -1;
		}
	}
}

/*
 * The master's poll.  A hung-up channel reports POLLHUP whether or not it was
 * asked for and without waiting; a live one is whatever the host pty says,
 * minus the host's own hangup indication -- the host reports a master with no
 * slave as hung up, and on the target that is state 1, a live idle channel.
 */
static int ptyrevents(struct pfd *p, int fd, int events)
{
	struct ptych *c = &ptyst->ch[p->chan];
	int r = 0;

	if (p->ismaster && c->mopen == 3 && !is("pty-hupdeaf")) {
		r = POLLHUP;
		if (ptydata(fd))
			r |= POLLIN;
		return r;
	}
	if ((events & POLLIN) && ptydata(fd))
		r |= POLLIN;
	if (events & POLLOUT)
		r |= POLLOUT;
	return r;
}

/* ------------------------------------------------------------------------
 * open(2) and friends.
 * ---------------------------------------------------------------------- */

int open(const char *path, int flags, ...)
{
	va_list ap;
	mode_t m = 0;
	int fd;

	REAL(r_open, "open");
	if (flags & O_CREAT) {
		va_start(ap, flags);
		m = (mode_t)va_arg(ap, int);
		va_end(ap);
	}

	/* The Centronics printer.  Three answers, three meanings. */
	if (!strcmp(path, "/dev/rlp") || !strcmp(path, "/dev/lp")) {
		if (is("lp-enxio")) {
			errno = ENXIO;
			return -1;
		}
		if (is("lp-eio")) {
			errno = EIO;
			return -1;
		}
		errno = C_EDATTN;	/* the right answer on a bare machine */
		return -1;
	}

	/* The two ends of a pty, and the channel state machine behind them. */
	if (!strncmp(path, "/dev/ptyp", 9) || !strncmp(path, "/dev/ttyp", 9))
		return ptyopen(path, flags);

	path = remap(path);

	/* The exclusive-open model. */
	if ((flags & O_CREAT) && (flags & O_EXCL)) {
		exclinit();
		if (exclfind(path) >= 0) {
			errno = EEXIST;
			return -1;
		}
		if (is("excl-never-set"))
			flags &= ~O_EXCL;	/* and so it does not exclude */
		fd = r_open(path, flags, m);
		if (fd >= 0)
			exclset(path, fd);
		return fd;
	}
	if (*path == '/' && strncmp(path, "/dev/", 5)) {
		exclinit();
		if (exclfind(path) >= 0) {
			errno = EEXIST;
			return -1;
		}
	}
	return (flags & O_CREAT) ? r_open(path, flags, m) : r_open(path, flags);
}

int creat(const char *path, mode_t m)
{
	REAL(r_creat, "creat");
	return r_creat(remap(path), m);
}

int mknod(const char *path, mode_t m, dev_t d)
{
	REAL(r_mknod, "mknod");
	/* mknod(2) for a FIFO is what the target uses; the host reserves the
	 * call for root, so a FIFO becomes mkfifo(3). */
	if ((m & 010000) || S_ISFIFO(m)) {
		(void)r_unlink;
		return mkfifo(remap(path), m & 0777);
	}
	return r_mknod(remap(path), m, d);
}

int unlink(const char *path)
{
	REAL(r_unlink, "unlink");
	return r_unlink(remap(path));
}

int stat(const char *path, struct stat *sb)
{
	REAL(r_stat, "stat");
	return r_stat(remap(path), sb);
}

int close(int fd)
{
	struct pfd *p;

	REAL(r_close, "close");
	if ((p = pfdof(fd)) != 0)
		ptyclose(p);
	exclclear(fd);
	return r_close(fd);
}

/* ------------------------------------------------------------------------
 * fork(2).
 * ---------------------------------------------------------------------- */
static int nforked;

pid_t fork(void)
{
	pid_t pid;

	REAL(r_fork, "fork");
	if (is("fork-cap") && nforked >= 5) {
		errno = EAGAIN;
		return -1;
	}
	nforked++;
	pid = r_fork();
	if (pid == 0 && ptyst) {
		/* Every open descriptor gains a reference, so a child that
		 * closes an inherited master does not drop carrier. */
		int i;

		for (i = 0; i < NPFD; i++) {
			if (pfdt[i].chan < 0)
				continue;
			if (pfdt[i].ismaster)
				ptyst->ch[pfdt[i].chan].muse++;
			else
				ptyst->ch[pfdt[i].chan].suse++;
		}
	}
	return pid;
}

/* ------------------------------------------------------------------------
 * read(2) and write(2).
 * ---------------------------------------------------------------------- */
static long nrec;			/* records the deepstack child wrote */

ssize_t read(int fd, void *buf, size_t n)
{
	struct pfd *p;

	REAL(r_read, "read");

	if ((p = pfdof(fd)) != 0)
		return ptyread(p, fd, buf, n);

	/* A read of a terminal with nothing typed, in the three shapes the
	 * driver has to tell apart.  VMIN=0 with VTIME set waits out the timer
	 * and answers empty; VMIN=0 with VTIME 0 is a poll and answers at once;
	 * anything canonical waits for a line that is never going to be typed,
	 * which is a real driver's behaviour and not a defect of this shim. */
	if (fd == 0 && tioinit && !(tio.c_lflag & ICANON) &&
	    tio.c_cc[VMIN] == 0) {
		if (tio.c_cc[VTIME] == 0 || is("vtime-instant"))
			return 0;
		if (is("vtime-forever")) {
			for (;;)
				pause();
		}
		usleep((useconds_t)tio.c_cc[VTIME] * 100000);
		return 0;
	}
	return r_read(fd, buf, n);
}

ssize_t write(int fd, const void *buf, size_t n)
{
	REAL(r_write, "write");

	/* A child's one-byte rendezvous answer, dropped. */
	if (is("child-mute") && n == 1 && inchild())
		return 1;
	/* A sleeper that never announces itself. */
	if (is("swap-mute") && n == 1 && inchild() && getpid() % 2 == 0)
		return 1;
	/* The two stack-growth defects.  The deepstack child is the one writing
	 * fixed-width records, so its own progress down the stack is where to
	 * intervene: a kernel that never grows the segment faults it inside the
	 * first ISTSIZE, and one whose ceiling is met some other way than the
	 * stack-limit path kills it with the wrong signal.
	 *
	 * Not an rlimit: lowering RLIMIT_STACK after exec does not shrink the
	 * stack the process already has, so the ceiling itself is set with
	 * `ulimit -s' in run.sh and only these two shapes need injecting. */
	if (is("stack-wedge") && inchild() && n == 20 && ++nrec > 50) {
		for (;;)		/* the faulting process never dies */
			pause();
	}
	if (is("stack-nogrow") && inchild() && n == 20 && ++nrec > 30)
		raise(SIGSEGV);
	if (is("stack-wrongsig") && inchild() && n == 20 && ++nrec > 100)
		raise(SIGBUS);
	return r_write(fd, buf, n);
}

unsigned sleep(unsigned s)
{
	char *blk;

	REAL(r_sleep, "sleep");
	if (is("swap-scribble") && inchild()) {
		/* A segment that came back from the swap device changed.  `blk'
		 * is tests/swap's own signed data; it is found by name so that
		 * this file links against every other test too. */
		blk = dlsym(RTLD_DEFAULT, "blk");
		if (blk)
			memset(blk + 1000, 0x5A, 64);
	}
	return r_sleep(s);
}

/* ------------------------------------------------------------------------
 * poll(2) and select(2).
 * ---------------------------------------------------------------------- */
int poll(struct pollfd *p, nfds_t n, int tmo)
{
	int r, i;

	REAL(r_poll, "poll");

	if (is("poll-nosig") && tmo < 0) {
		for (;;)
			sleep(3600);	/* and no signal will end it */
	}
	if (is("poll-deaf")) {
		if (tmo < 0)
			for (;;)
				pause();
		if (tmo > 0)
			usleep((useconds_t)tmo * 1000);
		for (i = 0; i < (int)n; i++)
			p[i].revents = 0;
		return 0;
	}
	if (is("poll-ready")) {
		for (i = 0; i < (int)n; i++)
			p[i].revents = POLLIN;
		return (int)n;
	}
	if (is("poll-alwayshup")) {
		for (i = 0; i < (int)n; i++)
			p[i].revents = POLLHUP;
		return (int)n;
	}

	/*
	 * A set holding pty ends is answered from the channel model: the host
	 * reports a master with no slave as hung up, which on the target is a
	 * live idle channel, and reports nothing at all for a slave that has
	 * gone, which on the target is the one event that must not wait.
	 */
	for (i = 0; i < (int)n; i++)
		if (pfdof(p[i].fd)) {
			int waited = 0, hit;

			for (;;) {
				hit = 0;
				for (i = 0; i < (int)n; i++) {
					struct pfd *q = pfdof(p[i].fd);

					p[i].revents = q ? ptyrevents(q,
						p[i].fd, p[i].events) : 0;
					if (p[i].revents)
						hit++;
				}
				if (hit || tmo == 0)
					return hit;
				if (tmo > 0 && waited >= tmo)
					return 0;
				if (ptynap(20) < 0 && errno == EINTR)
					return -1;
				waited += 20;
			}
		}

	r = r_poll(p, n, tmo);

	if (is("poll-nval"))
		for (i = 0; i < (int)n; i++) {
			p[i].revents |= POLLNVAL;
			if (r == 0)
				r = (int)n;
		}
	if (is("poll-noout"))
		for (i = 0; i < (int)n; i++) {
			p[i].revents &= ~POLLOUT;
			if (!p[i].revents)
				r = 0;
		}
	if (is("poll-nohup")) {
		r = 0;
		for (i = 0; i < (int)n; i++) {
			p[i].revents &= ~(POLLHUP | POLLERR);
			if (p[i].revents)
				r++;
		}
		/* And nothing wakes a poller for a hangup either, so a blocking
		 * poll waiting for one sleeps out its timeout. */
		if (r == 0 && tmo > 0)
			usleep((useconds_t)tmo * 1000);
	}
	return r;
}

int select(int nfds, fd_set *rd, fd_set *wr, fd_set *ex, struct timeval *tv)
{
	REAL(r_select, "select");
	if (is("sel-sticky") && rd) {
		/* Every descriptor asked about stays readable whatever has been
		 * taken off it: the queue's byte count never comes back to
		 * zero.  This is the shape slip's parent was stuck in -- it
		 * reads on the strength of the answer and never returns. */
		int i, n = 0;

		for (i = 0; i < nfds; i++)
			if (FD_ISSET(i, rd))
				n++;
		if (n > 0)
			return n;	/* rd left exactly as it was handed in */
	}
	if (is("sel-ready") && rd) {
		/* Everything asked about is readable, including a FIFO nothing
		 * has ever been written to.  The sets come back exactly as they
		 * were handed in, which is what a level check that answers
		 * before it looks produces. */
		int i, n = 0;

		for (i = 0; i < nfds; i++)
			if (FD_ISSET(i, rd))
				n++;
		if (n > 0)
			return n;
	}
	/*
	 * The four ways a timed select(2) can serve a wait other than the one
	 * it was given.  SELCHUNK is the span a single poll(2) reaches: 32 s,
	 * because its timeout is an int of milliseconds.
	 */
#define	SELCHUNK	32
	if (tv && (tv->tv_sec >= 0 && tv->tv_usec >= 0)) {
		struct timeval c = *tv;

		if (is("sel-clamp32")) {
			if (c.tv_sec > SELCHUNK) {
				c.tv_sec = SELCHUNK;
				c.tv_usec = 0;
			}
			return r_select(nfds, rd, wr, ex, &c);
		}
		if (is("sel-roundup")) {
			if (c.tv_sec < SELCHUNK) {
				c.tv_sec = SELCHUNK;
				c.tv_usec = 0;
			}
			return r_select(nfds, rd, wr, ex, &c);
		}
		if (is("sel-chunkwait") && c.tv_sec > SELCHUNK) {
			/* Nothing is watched for the length of the first
			 * piece, so a descriptor that became ready inside it
			 * is only noticed once the piece is over. */
			(void)sleep((unsigned)SELCHUNK);
			c.tv_sec -= SELCHUNK;
			return r_select(nfds, rd, wr, ex, &c);
		}
	}
	if (is("sel-negzero") && tv && (tv->tv_sec < 0 || tv->tv_usec < 0)) {
		struct timeval z;

		z.tv_sec = 0;
		z.tv_usec = 0;
		return r_select(nfds, rd, wr, ex, &z);
	}
	if (is("sel-deaf")) {
		if (!tv) {
			for (;;)
				pause();
		}
		usleep((useconds_t)tv->tv_sec * 1000000 + tv->tv_usec);
		if (rd)
			FD_ZERO(rd);
		if (wr)
			FD_ZERO(wr);
		return 0;
	}
	return r_select(nfds, rd, wr, ex, tv);
}

/* ------------------------------------------------------------------------
 * ioctl(2): the termio line and the hi-res font bank.
 * ---------------------------------------------------------------------- */
int ioctl(int fd, unsigned long req, ...)
{
	va_list ap;
	void *arg;
	struct termio *t;
	struct hrfont *hf;
	int i, j;

	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);

	tiodefault();
	switch (req) {
	case TCGETA:
		*(struct termio *)arg = tio;
		return 0;
	case TCSETA:
	case TCSETAW:
	case TCSETAF:
		if (is("tc-setnop"))
			return 0;		/* stores nothing */
		t = (struct termio *)arg;
		if (is("tc-noveol")) {
			unsigned char keep = tio.c_cc[VEOL];

			tio = *t;
			tio.c_cc[VEOL] = keep;	/* VTIME's slot, lost */
		} else
			tio = *t;
		return 0;

	case HRIOCSFONT:
		if (is("font-none")) {
			errno = EINVAL;
			return -1;
		}
		hf = (struct hrfont *)arg;
		if (!is("font-lowwrite") && hf->hf_first < HRLOW) {
			errno = EINVAL;
			return -1;
		}
		if (!is("font-past") &&
		    hf->hf_first + hf->hf_count > HRNGLYPH) {
			errno = EINVAL;
			return -1;
		}
		for (i = 0; i < hf->hf_count; i++)
			for (j = 0; j < HRGH; j++) {
				int slot = (hf->hf_first + i) % HRNGLYPH;

				bank[slot][j] = hf->hf_bits[i * HRGH + j];
			}
		return 0;
	case HRIOCGFONT:
		if (is("font-none")) {
			errno = EINVAL;
			return -1;
		}
		hf = (struct hrfont *)arg;
		for (i = 0; i < hf->hf_count; i++)
			for (j = 0; j < HRGH; j++) {
				int slot = (hf->hf_first + i) % HRNGLYPH;

				hf->hf_bits[i * HRGH + j] = bank[slot][j] ^
					(is("font-nomatch") ? 1 : 0);
			}
		return 0;
	case HRIOCRFONT:
		if (is("font-none")) {
			errno = EINVAL;
			return -1;
		}
		return 0;
	}

	REAL(r_ioctl, "ioctl");
	return r_ioctl(fd, req, arg);
}

int isatty(int fd)
{
	if (pfdof(fd))
		return 1;
	if (is("not-a-tty"))
		return 0;
	if (fd == 0 || fd == 1)
		return 1;		/* the shim's line IS the terminal */
	REAL(r_isatty, "isatty");
	return r_isatty(fd);
}

char *ttyname(int fd)
{
	static char name[32];
	struct pfd *p;

	if ((p = pfdof(fd)) != 0) {
		snprintf(name, sizeof name, "/dev/%syp%c",
			p->ismaster ? "pt" : "tt",
			"0123456789abcdef"[p->chan]);
		return name;
	}
	return "/dev/ttyp0";
}

/* ------------------------------------------------------------------------
 * gtty(2)/stty(2).  The slave's line settings, which on this host are termios;
 * only the flags tests/pty moves are carried across.
 * ---------------------------------------------------------------------- */
int gtty(int fd, struct sgttyb *sg)
{
	struct termios t;

	if (tcgetattr(fd, &t) < 0)
		return -1;
	sg->sg_ispeed = 0;
	sg->sg_ospeed = 0;
	sg->sg_erase = (char)t.c_cc[VERASE];
	sg->sg_kill = (char)t.c_cc[VKILL];
	sg->sg_flags = 0;
	if (t.c_lflag & ECHO)
		sg->sg_flags |= SG_ECHO;
	if (t.c_oflag & ONLCR)
		sg->sg_flags |= SG_CRMOD;
	return 0;
}

int stty(int fd, const struct sgttyb *sg)
{
	struct termios t;

	if (tcgetattr(fd, &t) < 0)
		return -1;
	if (sg->sg_flags & SG_ECHO)
		t.c_lflag |= ECHO;
	else
		t.c_lflag &= ~ECHO;
	if (sg->sg_flags & SG_CRMOD)
		t.c_oflag |= ONLCR;
	else
		t.c_oflag &= ~ONLCR;
	return tcsetattr(fd, TCSANOW, &t);
}

/* ------------------------------------------------------------------------
 * Start-up.
 *
 * $KWORK is the directory that stands in for the target's root filesystem.
 * $KSTDIN=pipe replaces the standard input with the read end of a pipe this
 * process holds the other end of, which is what a terminal with nothing typed
 * on it looks like to poll(2) -- an empty descriptor with a live writer.  With
 * </dev/null instead, every poll would report an immediate EOF and tests/polltty
 * would fail for a reason that has nothing to do with the driver.
 *
 * The stack ceiling tests/deepstack needs is set with `ulimit -s 64' by run.sh,
 * because it has to be in force when the program is EXECed: lowering
 * RLIMIT_STACK afterwards leaves the stack the process already has.
 * ---------------------------------------------------------------------- */
static void __attribute__((constructor)) kshim_init(void)
{
	char *s;
	int p[2];

	int i;

	rootpid = getpid();
	for (i = 0; i < NPFD; i++)
		pfdt[i].chan = -1;
	ptyst = mmap(0, sizeof *ptyst, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ptyst == MAP_FAILED)
		ptyst = 0;
	else
		ptyst->arena = PTYARENA;
	if ((s = getenv("KWORK")) && *s)
		work = s;
	if ((s = getenv("KSTDIN")) && !strcmp(s, "pipe")) {
		if (pipe(p) == 0) {
			dup2(p[0], 0);
			/* p[1] stays open, on purpose: a writer that never
			 * writes is what an idle terminal is. */
		}
	}
}
