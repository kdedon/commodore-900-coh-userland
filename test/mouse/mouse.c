/*
 * mouse.c -- exercise /dev/mouse, the native FN#7 pointing-device interface.
 *
 * The driver (sys/drv/mouse.c) samples the HR card's two free-running 10-bit
 * counters and its three button lines at 100 Hz and delivers one fixed-width
 * `struct msevent' per sample in which anything happened.  There is no framing
 * and nothing to resynchronise to: a read either returns whole events or it
 * returns nothing, so this program reads them straight into an array.
 *
 * Motion is relative and the counters run free, so the only thing that can be
 * checked against an injection is the SUM of the reported deltas.  One
 * injected detent is one reported count.  The running totals are printed after
 * every event for that comparison.
 *
 * Y is reported in screen order: positive is DOWN, the direction mouse_inject
 * calls positive dy.  An inverted total is a sign error in the driver.
 *
 * Phase 0 checks the arithmetic the driver does, compiled here and run on the
 * same 16-bit int: the 10-bit counters do not wrap where a 16-bit int would,
 * so every difference has to be taken modulo 1024 and sign-extended.  It needs
 * no device and no injection.
 *
 * Phase A checks the interface with nothing moving: the exclusive open, a
 * request too small to hold an event, the raw-state ioctl, the non-blocking
 * read of an idle device, and a poll() that has to time out rather than claim
 * the device is always readable.
 *
 * Phase B collects events while the operator injects motion.
 *
 * Every read is backed by an alarm.  A driver whose wakeup path is broken
 * hangs instead of answering, and a test that hangs reports nothing.
 *
 * PHASE B HAS NO VERDICT OF ITS OWN UNLESS IT IS GIVEN ONE.  Everything it
 * collects is relative motion produced by whoever is injecting, so the
 * program cannot know what it should have seen; a silent device must not
 * read as a pass.  Two options supply the verdict:
 *
 *	-e N		 at least N events must arrive.  Without it, a phase B
 *			 that saw nothing exits 2 (INCONCLUSIVE) rather than 0,
 *			 so no script can mistake silence for a pass.
 *	-x DX -y DY	 the totals that must be reported.  One injected detent
 *			 is one reported count, so these are the numbers handed
 *			 to mouse_inject.  This is the only check that tests
 *			 what the header calls the point of the phase.
 *
 * Usage:  mouse [-s] [-e events] [-x dx] [-y dy] [seconds]
 *	-s	 open /dev/mousems and decode the Mouse Systems packets the
 *		 compatibility minor emits for MGR
 *	seconds  how long to collect for (default 20)
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mouse.h>

#define	DEV	"/dev/mouse"
#define	DEVMS	"/dev/mousems"

#define	MMASK	(MSCOUNTS-1)
#define	MSIGN	(MSCOUNTS>>1)

/*
 * The button bits of the X port word, as sys/drv/mouse.c:64-66 names them.
 * They are the driver's own, not the header's, and they are repeated here so
 * the port word can be checked for bits that belong to neither field.
 */
#define	DSMENU	0x8000			/* left button, active high */
#define	DWMENU	0x4000			/* middle button */
#define	DACTION	0x2000			/* right button */

#define	PBLEFT	4			/* MGR mouse.h MS_BUTLEFT */
#define	PBMID	2			/* MS_BUTMIDDLE */
#define	PBRIGHT	1			/* MS_BUTRIGHT */

#define	NEV	8			/* events read at a time */

int	fails;
int	serial;				/* decode the compatibility minor */
int	tmo = 20;			/* seconds to collect for */
int	quit;				/* the collect window has expired */

/*
 * What phase B saw, and what it was told to expect.  The counts are
 * file-scope so the verdict downstream of the phase can test them.
 */
long	sawdx, sawdy;			/* totals reported by the driver */
int	sawev;				/* events delivered */
int	wantev = -1;			/* -e: events required, -1 = unasked */
long	wantdx, wantdy;			/* -x/-y: totals required */
int	havexy;				/* -x or -y was given */

static void
ck(what, got, want)
char *what;
int got, want;
{
	printf("mouse: %-40s got %4d want %4d  %s\n", what, got, want,
		got == want ? "ok" : "FAIL");
	if (got != want)
		fails++;
	fflush(stdout);
}

static void
note(what)
char *what;
{
	printf("mouse: %s\n", what);
	fflush(stdout);
}

/*
 * Alarm handler for the collect window: stop reading and report.
 */
static void
done(sig)
int sig;
{
	quit = 1;
}

/*
 * Alarm handler for the fixed checks: a hang there is a failure, and saying so
 * is more use than waiting.
 */
static void
hung(sig)
int sig;
{
	printf("mouse: FAIL -- hung waiting on the device\n");
	fflush(stdout);
	exit(1);
}

/*
 * The driver's counter differencing, compiled here to run on the target.
 */
static int
msdiff(now, was)
register int now;
register int was;
{
	register int d;

	d = (now - was) & MMASK;
	if (d & MSIGN)
		d -= MSCOUNTS;
	return d;
}

/*
 * Phase 0 -- the arithmetic, with no device.
 */
static void
phase0()
{
	int i, n, c, d;

	note("phase 0 -- counter arithmetic");
	ck("sizeof (struct msevent)", (int)sizeof (struct msevent), 10);
	ck("sizeof (struct msstate)", (int)sizeof (struct msstate), 8);
	ck("char is signed", (int)(char)0x80, -128);

	ck("1020 -> 4 is +8", msdiff(4, 1020), 8);
	ck("4 -> 1020 is -8", msdiff(1020, 4), -8);
	ck("1023 -> 0 is +1", msdiff(0, 1023), 1);
	ck("0 -> 1023 is -1", msdiff(1023, 0), -1);
	ck("0 -> 511 is +511", msdiff(511, 0), 511);
	ck("0 -> 512 is -512", msdiff(512, 0), -512);
	ck("a still mouse reads 0", msdiff(777, 777), 0);

	n = 0;
	c = 0;
	for (i = 0; i < 400; i++) {
		d = (c + 7) & MMASK;
		if (msdiff(d, c) != 7)
			n++;
		c = d;
	}
	ck("400 forward steps of +7", n, 0);

	n = 0;
	c = 0;
	for (i = 0; i < 400; i++) {
		d = (c - 7) & MMASK;
		if (msdiff(d, c) != -7)
			n++;
		c = d;
	}
	ck("400 reverse steps of -7", n, 0);
}

/*
 * Collect and print native events until the window closes.
 */
static void
collect(fd)
int fd;
{
	struct msevent ev[NEV];
	int i, n, ne, esz;
	long tx, ty;

	esz = sizeof (struct msevent);
	ne = 0;
	tx = 0;
	ty = 0;
	for (;;) {
		if (quit)
			break;
		n = read(fd, (char *)ev, sizeof ev);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			/* Any other errno ends the phase, and is reported:
			 * a driver that fails every read must not look like
			 * a mouse nobody moved. */
			printf("mouse: FAIL -- read: errno %d\n", errno);
			fails++;
			break;
		}
		if (n == 0)
			break;
		if (n % esz != 0) {
			printf("mouse: FAIL -- read %d is not whole events\n", n);
			fails++;
			break;
		}
		for (i = 0; i < n / esz; i++) {
			ne++;
			tx += ev[i].ms_dx;
			ty += ev[i].ms_dy;
			printf(
			  "mouse: ev %3d dx %4d dy %4d %c%c%c%s t %5u x %ld y %ld\n",
				ne, ev[i].ms_dx, ev[i].ms_dy,
				(ev[i].ms_buttons & MSBLEFT)   ? 'L' : '-',
				(ev[i].ms_buttons & MSBMIDDLE) ? 'M' : '-',
				(ev[i].ms_buttons & MSBRIGHT)  ? 'R' : '-',
				(ev[i].ms_flags & MSFCOALESCE) ? " coalesced" : "",
				ev[i].ms_time, tx, ty);
			fflush(stdout);
		}
	}
	printf("mouse: %d events, total dx %ld dy %ld\n", ne, tx, ty);
	fflush(stdout);
	sawev = ne;
	sawdx = tx;
	sawdy = ty;
}

/*
 * Read one packet byte with a deadline.  Returns the byte, or -1 once the
 * collect window has closed or the device stops answering.
 */
static int
getb(fd)
int fd;
{
	char c;
	int n;

	for (;;) {
		if (quit)
			return -1;
		n = read(fd, &c, 1);
		if (n == 1)
			return c & 0377;
		if (n == 0)
			return -1;
		if (errno != EINTR) {
			/* As in collect(): an unexpected errno is the answer,
			 * not a reason to stop quietly and report no packets. */
			printf("mouse: FAIL -- read: errno %d\n", errno);
			fails++;
			return -1;
		}
	}
}

/*
 * Collect and decode the compatibility minor's Mouse Systems packets, exactly
 * as MGR's src/mgr/mouse.c decodes them (mtype P_SUN), so that a disagreement
 * here is the driver's and not the client's.
 */
static void
collectms(fd)
int fd;
{
	int b[3];
	int i, np, but, dx, dy;
	long tx, ty;

	np = 0;
	tx = 0;
	ty = 0;
	if ((b[0] = getb(fd)) == -1)
		goto out;
	for (;;) {
restart:
		while ((b[0] & 0xf8) != 0x80) {
			if ((b[0] = getb(fd)) == -1)
				goto out;
		}
		for (i = 1; i < 3; i++) {
			if ((b[i] = getb(fd)) == -1)
				goto out;
			if (b[i] == 0x80) {
				b[0] = b[i];
				goto restart;
			}
		}

		but = (~b[0]) & 0x07;
		dx = (char)b[1];
		dy = -(char)b[2];

		np++;
		tx += dx;
		ty += dy;
		printf("mouse: pkt %3d dx %4d dy %4d %c%c%c x %ld y %ld\n",
			np, dx, dy,
			(but & PBLEFT)  ? 'L' : '-',
			(but & PBMID)   ? 'M' : '-',
			(but & PBRIGHT) ? 'R' : '-',
			tx, ty);
		fflush(stdout);

		if ((b[0] = getb(fd)) == -1)
			goto out;
	}
out:
	printf("mouse: %d packets, total dx %ld dy %ld\n", np, tx, ty);
	fflush(stdout);
	sawev = np;
	sawdx = tx;
	sawdy = ty;
}

int
main(argc, argv)
int argc;
char **argv;
{
	struct pollfd set[1];
	struct msstate st, st2;
	struct msevent ev;
	char *dev;
	char junk[4];
	int fd, fd2, n, e;

	while (--argc > 0) {
		argv++;
		if ((*argv)[0] != '-') {
			tmo = atoi(*argv);
			continue;
		}
		switch ((*argv)[1]) {
		case 's':
			serial = 1;
			break;
		case 'e':
			if (--argc <= 0)
				break;
			wantev = atoi(*++argv);
			break;
		case 'x':
			if (--argc <= 0)
				break;
			wantdx = atol(*++argv);
			havexy = 1;
			break;
		case 'y':
			if (--argc <= 0)
				break;
			wantdy = atol(*++argv);
			havexy = 1;
			break;
		default:
			printf("mouse: unknown option %s\n", *argv);
			return 2;
		}
	}
	dev = serial ? DEVMS : DEV;

	phase0();

	signal(SIGALRM, hung);
	alarm(10);

	note(serial ? "phase A -- the compatibility minor"
		    : "phase A -- the interface, with nothing moving");
	if ((fd = open(dev, O_RDWR)) < 0) {
		e = errno;
		printf("mouse: FAIL -- open %s: errno %d%s\n", dev, e,
			e == ENXIO ? " (no drvl[7] driver, or no HR card)"
			  : e == ENOENT ? " (no device node)" : "");
		fflush(stdout);
		return 1;
	}
	note("open ok");

	/*
	 * Two readers would each see a fraction of the motion, so the driver
	 * takes one.
	 */
	fd2 = open(dev, O_RDWR);
	e = errno;
	ck("second open refused", fd2 < 0, 1);
	ck("second open errno EDBUSY", e, EDBUSY);
	if (fd2 >= 0)
		close(fd2);

	/*
	 * An event is indivisible, so a request that cannot hold one is
	 * refused rather than answered with a fragment.
	 */
	if (serial == 0) {
		n = read(fd, junk, sizeof junk);
		e = errno;
		ck("read smaller than an event returns", n, -1);
		ck("its errno is EINVAL", e, EINVAL);
	}

	/*
	 * The raw counters, for a caller that would rather poll than stream.
	 * Their values are whatever the mouse has done since power-on; what
	 * can be checked is that they are counts and not something else.
	 */
	st.ms_xraw = 0xFFFF;
	st.ms_yraw = 0xFFFF;
	st.ms_buttons = 0xFFFF;
	/* 0xFFFF has bits 12..10 set, which are in no field the driver
	 * documents, so the port check below fails on an ioctl that filled
	 * nothing -- the same way the three counter checks do.  Left at
	 * whatever the stack held, that check passed or failed at random on a
	 * driver that never wrote the field. */
	st.ms_port = 0xFFFF;
	if (ioctl(fd, MSIOCGETST, &st) < 0) {
		printf("mouse: FAIL -- MSIOCGETST: errno %d\n", errno);
		fails++;
	} else {
		printf("mouse: raw state x %u y %u buttons 0x%x port 0x%04x\n",
			st.ms_xraw, st.ms_yraw, st.ms_buttons, st.ms_port);
		fflush(stdout);
		ck("raw X within the counter", st.ms_xraw < MSCOUNTS, 1);
		ck("raw Y within the counter", st.ms_yraw < MSCOUNTS, 1);
		ck("no button bits outside the three",
			(int)(st.ms_buttons & ~MSBALL), 0);
		/*
		 * ms_port and ms_xraw are filled from the SAME port read one
		 * instruction apart (sys/drv/mouse.c:424-425), so comparing
		 * them to each other compares a value with itself and no change
		 * to the driver can make it disagree.  The three checks above
		 * are real only because the fields were pre-loaded with 0xFFFF,
		 * which no counter value can be -- an ioctl that filled nothing
		 * fails them.  What can be asked of the port word on its own is
		 * that it carries no bits outside the fields the driver
		 * documents.
		 */
		ck("port word has no undefined bits",
			(int)(st.ms_port & ~(MMASK|DSMENU|DWMENU|DACTION)), 0);

		/*
		 * Nothing is moving, so a second reading has to agree with the
		 * first.  This is the one property of the raw counters that
		 * does not come from a single sample: a driver reading the
		 * wrong port, or returning uninitialised memory, gives two
		 * different answers to the same question.
		 */
		st2.ms_xraw = 0xFFFF;
		st2.ms_yraw = 0xFFFF;
		if (ioctl(fd, MSIOCGETST, &st2) < 0) {
			printf("mouse: FAIL -- second MSIOCGETST: errno %d\n",
				errno);
			fails++;
		} else {
			ck("idle mouse: raw X is stable across two reads",
				(int)st2.ms_xraw, (int)st.ms_xraw);
			ck("idle mouse: raw Y is stable across two reads",
				(int)st2.ms_yraw, (int)st.ms_yraw);
		}
	}

	ck("MSIOCFLUSH accepted", ioctl(fd, MSIOCFLUSH, (char *)0) < 0, 0);

	/*
	 * Anything else, including the line settings a serial-mouse client
	 * sends, is refused rather than silently pretended.
	 */
	e = 0;
	if (ioctl(fd, ('m'<<8|99), (char *)0) < 0)
		e = errno;
	ck("an unknown ioctl errno EINVAL", e, EINVAL);

	/*
	 * Nothing has moved, so there is nothing queued.  A driver that
	 * answers a read with a stale or an empty event fails here.
	 */
	/* A driver with no O_NDELAY is a failure, not a reason to skip: the two
	 * checks below are the ones that prove no stale event is handed back,
	 * and quietly dropping them left nothing in their place. */
	ck("fcntl(F_SETFL, O_NDELAY) accepted",
		fcntl(fd, F_SETFL, O_NDELAY) < 0, 0);
	{
		n = read(fd, (char *)&ev, sizeof ev);
		e = errno;
		ck("idle non-blocking read returns", n, -1);
		ck("idle non-blocking read errno EAGAIN", e, EAGAIN);
		fcntl(fd, F_SETFL, 0);
	}

	/*
	 * And a wait has to expire.  A driver that reported POLLIN always
	 * would pass every check above and still spin a client's select loop
	 * at full tilt on a motionless mouse.
	 */
	set[0].fd = fd;
	set[0].events = POLLIN;
	set[0].revents = 0;
	n = poll(set, (unsigned long)1, 1000);
	ck("poll(POLLIN, 1s) on idle mouse returns", n, 0);
	ck("poll revents", set[0].revents, 0);

	alarm(0);
	signal(SIGALRM, done);
	printf("mouse: phase B -- collecting for %d seconds, inject motion now\n",
		tmo);
	fflush(stdout);
	alarm(tmo);
	if (serial)
		collectms(fd);
	else
		collect(fd);
	alarm(0);
	close(fd);

	/*
	 * Phase B's verdict.  Without -e/-x/-y this program has no way to know
	 * what should have arrived, and saying PASS on the strength of that is
	 * how a run in which the device delivered nothing came to look like a
	 * working mouse.  Silence is reported as INCONCLUSIVE (status 2), which
	 * is neither of the two answers a script may act on.
	 */
	if (wantev >= 0)
		ck("phase B: events delivered", sawev >= wantev ? 1 : 0, 1);
	if (havexy) {
		ck("phase B: total dx matches the injection",
			(int)sawdx, (int)wantdx);
		ck("phase B: total dy matches the injection",
			(int)sawdy, (int)wantdy);
	}
	if (fails == 0 && sawev == 0 && wantev < 0 && havexy == 0) {
		printf("mouse: INCONCLUSIVE -- phase A passed, but phase B saw"
			" no events, so nothing about motion reporting was"
			" tested.  Re-run with -e/-x/-y and an injection.\n");
		fflush(stdout);
		return 2;
	}

	printf("mouse: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
	fflush(stdout);
	return fails != 0;
}
