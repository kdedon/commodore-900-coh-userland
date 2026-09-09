/*
 * termio -- does the 4.x termio tty (KTTY=termio, sys/drv/tty.c) honour a
 * termios round trip and VMIN/VTIME?
 *
 * Two checks on the controlling terminal, each printing one PASS/FAIL line:
 *
 *	round trip	TCGETA, alter c_cc and the flags, TCSETA, TCGETA again
 *			and compare -- the ioctl pair must preserve what it was
 *			handed, including the fields sgttyb has no room for.
 *	VMIN/VTIME	in non-canonical mode with VMIN=0 and VTIME set, a read
 *			with no input must return 0 after roughly VTIME tenths,
 *			not block and not spin.
 *
 * The settings are restored before exiting however the run ends, so the shell
 * that started it keeps a usable terminal.
 *
 * Three design points, each of which keeps a green line honest:
 *
 *   1. `bad' is a file-scope counter and it is the exit status; no failure
 *	is printed and then discarded.
 *   2. the round trip writes values no driver default and no sgttyb
 *	conversion can produce (^H/^U/^D are the driver's own defaults, and
 *	the sgttyb conversion never sets IXANY), so a TCSETA implemented as
 *	`return 0' cannot pass.
 *   3. VTIME has a LOWER bound: a driver that ignores the timer and returns
 *	0 at once fails, and a line that is not a terminal (a pipe, a
 *	redirect) is a skip, not a pass -- read(2) there returns 0 at EOF
 *	immediately.
 *
 * The VTIME read is also the one call here that can block for ever: a driver
 * that stores VTIME and never arms the timer leaves a VMIN=0 read waiting for
 * input that nobody is going to type -- an upper bound of "waited too long" is
 * no use if the read never returns to be measured.  DEADLINE reports it, and
 * puts the line back first, because a hung test that also leaves the terminal
 * in raw mode costs the session as well as the answer.
 */
#include <stdio.h>
#include <termio.h>
#include <signal.h>

#define	VT	20			/* VTIME: tenths of a second */
#define	DEADLINE 60			/* seconds for the whole run */

struct termio	saved;
int		havesaved = 0;
int		bad;			/* the verdict; the exit status */

/*
 * Put the line back the way it was found.
 */
restore()
{
	if (havesaved)
		ioctl(0, TCSETA, &saved);
}

void
bail(sig)
{
	restore();
	exit(1);
}

/*
 * The deadline expired.  The line is restored first: a hang that also leaves
 * the terminal raw takes the session down with the verdict.
 */
void
hung(sig)
{
	restore();
	printf("FAIL termio: no verdict within %d s -- the VMIN=0/VTIME=%d read"
		" never\n", DEADLINE, VT);
	printf("FAIL termio: returned, so the timer was stored and never armed."
		"  A test that\n");
	printf("FAIL termio: hangs has no verdict, which is worse than one that"
		" fails.\n");
	fflush(stdout);
	exit(1);
}

main()
{
	struct termio t, t2;
	long t0, t1;
	int n;

	/* A line that is not a terminal cannot answer either question, and a
	 * PASS from one is a lie about a subsystem that was never reached. */
	if (isatty(0) == 0) {
		printf("SKIP termio: stdin is not a terminal -- nothing tested\n");
		exit(2);
	}
	if (ioctl(0, TCGETA, &saved) < 0) {
		printf("FAIL round trip: TCGETA: no termio on this line\n");
		exit(1);
	}
	havesaved = 1;
	signal(SIGINT, bail);
	signal(SIGTERM, bail);
	signal(SIGALRM, hung);
	alarm(DEADLINE);

	/*
	 * Round trip.  ERASE/KILL/EOF and VMIN/VTIME share the c_cc array, so
	 * a driver that stores termio only as a converted sgttyb loses the
	 * last two -- set all four and read them all back.
	 *
	 * Every value below is one the driver does NOT default to and the
	 * sgttyb conversion cannot produce, so a store-nothing TCSETA reads
	 * back ^H/^U/^D and fails.  VEOL is set as well: VEOL and VTIME are the
	 * same slot (include/termio.h:150-154), and it is the slot an
	 * sgttyb-backed driver loses -- the comment above claimed all four were
	 * round-tripped while the code set three.
	 */
	t = saved;
	t.c_cc[VERASE] = 0x7F;			/* DEL, not the ^H default */
	t.c_cc[VKILL]  = 0x18;			/* ^X, not the ^U default */
	t.c_cc[VEOF]   = 0x1A;			/* ^Z, not the ^D default */
	t.c_cc[VEOL]   = 0x11;			/* the VTIME slot */
	t.c_iflag = (t.c_iflag | ICRNL | IXANY);
	t.c_oflag |= OPOST;
	t.c_lflag = (t.c_lflag | ICANON | ECHO) & ~ECHONL;
	if (ioctl(0, TCSETA, &t) < 0) {
		printf("FAIL round trip: TCSETA failed\n");
		restore();
		exit(1);
	}
	if (ioctl(0, TCGETA, &t2) < 0) {
		printf("FAIL round trip: second TCGETA failed\n");
		restore();
		exit(1);
	}
	bad = 0;
	if (t2.c_cc[VERASE] != t.c_cc[VERASE])	bad |= 0x01;
	if (t2.c_cc[VKILL]  != t.c_cc[VKILL])	bad |= 0x02;
	if (t2.c_cc[VEOF]   != t.c_cc[VEOF])	bad |= 0x04;
	if (t2.c_cc[VEOL]   != t.c_cc[VEOL])	bad |= 0x20;
	if ((t2.c_iflag & (ICRNL|IXANY)) != (t.c_iflag & (ICRNL|IXANY)))
						bad |= 0x08;
	if ((t2.c_lflag & (ICANON|ECHO|ECHONL)) != (t.c_lflag & (ICANON|ECHO|ECHONL)))
						bad |= 0x10;
	if (bad)
		printf("FAIL round trip: fields %x came back changed\n", bad);
	else
		printf("PASS round trip\n");

	/*
	 * VMIN/VTIME.  Nothing is typed, so the read has to come back empty
	 * once the timer expires.  time() is the guest's own clock, so the
	 * measurement does not depend on how fast the emulation runs.
	 */
	t = saved;
	t.c_lflag &= ~(ICANON|ECHO);
	t.c_cc[VMIN]  = 0;
	t.c_cc[VTIME] = VT;
	if (ioctl(0, TCSETA, &t) < 0) {
		printf("FAIL vmin/vtime: TCSETA failed\n");
		restore();
		exit(1);
	}
	time(&t0);
	n = read(0, (char *)&t2, 1);
	time(&t1);
	restore();
	if (n != 0) {
		printf("FAIL vmin/vtime: read returned %d, expected 0\n", n);
		bad |= 0x40;
	} else if (t1 - t0 > 4L) {
		printf("FAIL vmin/vtime: waited %ld s for a %d tenths timer\n",
			t1 - t0, VT);
		bad |= 0x80;
	} else if (t1 - t0 < 1L) {
		/* The lower bound is the whole check: a driver with no VTIME
		 * support returns 0 the instant it is asked. */
		printf("FAIL vmin/vtime: returned at once -- the %d tenths"
			" timer did not run\n", VT);
		bad |= 0x100;
	} else
		printf("PASS vmin/vtime (returned 0 after %ld s)\n", t1 - t0);

	printf("termio: %s (0x%x)\n", bad ? "FAIL" : "PASS", bad);
	exit(bad != 0);
}
