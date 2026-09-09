/*
 * Copyright 1989 Phill Everson & Martyn Shortley
 * Copyright 1989 Roberto Biancardi
 *
 * This notice and any statement of authorship must be reproduced on all
 * copies.  The full terms, including the restrictions on sale and on charging
 * a fee, are in COPYRIGHT beside this file; COHERENT port by Matt Kimmel and
 * Udo Munk.
 */
/*
 * tetris input pacing -- the one machine-dependent file.
 *
 * The main loop asks polluser() for whatever the player has typed and for a
 * "drop" tick, so this file owns both the delay and the non-blocking read.
 *
 * Upstream offers four timings: poll(), nap(), a busy-wait on times(), and (in
 * the COHERENT port) a busy-wait on ftime().  All four are wrong here.  The
 * busy-waits spin a 6 MHz machine flat out for the whole game, and the COHERENT
 * ftime() one has a bug besides -- it subtracts 990 rather than 1000 on
 * millisecond carry, so every rollover loses 10 ms.  poll() does now work on a
 * tty in this kernel (al.c carries DFPOL and a c_poll), but usleep() is simpler
 * and is exactly this: it sleeps on alarm2() clock TICKS and never spins.  Its
 * resolution is one tick, 10 ms at HZ=100, finer than any level needs.
 *
 * The non-blocking read is O_NDELAY around read(2) rather than VMIN/VTIME,
 * because the delay is already done by the time we read: what is wanted is
 * whatever is in the queue now, not a driver-side timeout.  setuptty() clears
 * ICANON and ECHO so single keystrokes arrive unbuffered.
 */

#include <termio.h>
#include <fcntl.h>

extern char *memcpy();

static struct termio orig;

/* Centiseconds of delay per drop, by level; index 0 is the slowest. */
static char timings[] = { 30, 25, 20, 15, 10, 8 };
static int  currtim;

extern int speed;

setuptty() {
	struct termio tt;
	static int getdone = 0;

	if ( !getdone ) {
		ioctl(0,TCGETA,&orig);
		getdone = 1;
	}
	memcpy( (char*)&tt, (char*)&orig, sizeof(orig) );
	tt.c_lflag &= ~(ICANON|ECHO);
	tt.c_cc[VMIN] = 0;
	tt.c_cc[VTIME] = 1;
	ioctl(0,TCSETAW,&tt);
}

closeuptty() {
	ioctl(0,TCSETAW,&orig);
}

setlevel(level)
{
	if ( level >= sizeof(timings) )
		level = sizeof(timings) - 1;
	currtim = timings[level];
}

/*
 * Wait out this level's slice of the drop interval, then take whatever has been
 * typed.  A zero byte is appended when the interval is fully spent: that is the
 * main loop's signal to drop the shape one row, not a string terminator.
 */
polluser(buf,sizebuf)
char *buf;
int sizebuf;
{
	static int timeleft = 0;
	int nc;

	if ( timeleft <= 0 )
		timeleft = currtim;
	if ( timeleft < 10 ) {
		wait_ms(timeleft*(speed/10));
		timeleft = 0;
	} else {
		wait_ms(speed);
		timeleft -= 10;
	}
	fcntl(0,F_SETFL,O_NDELAY);
	nc = read(0,buf,sizebuf);
	fcntl(0,F_SETFL,0);
	if (nc < 0)		/* nothing queued: an O_NDELAY read, not an error */
		nc = 0;
	if ( timeleft <= 0 )
		buf[nc++] = 0;
	return nc;
}

/*
 * usleep() takes an unsigned long, so the millisecond-to-microsecond scaling
 * must happen in long arithmetic: msecs * 1000 overflows a 16-bit int at 33 ms.
 */
static wait_ms( msecs )
int msecs;
{
	usleep((unsigned long) msecs * 1000L);
}
