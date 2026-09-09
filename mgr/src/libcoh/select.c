/* select from Udo Munk, udo@umunk.GUN.de ; implements select() using poll() */

#include <poll.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>

#if (NOFILE < FD_SETSIZE)
#define FD_MAX	NOFILE
#else
#define FD_MAX	FD_SETSIZE
#endif

/*
 * The most poll(2) can be asked to wait, and the same span in whole seconds.
 * poll(2)'s timeout is an int of milliseconds, so 32.767 s is all one call
 * reaches; 32000 is that ceiling rounded down to a whole number of seconds,
 * so a longer wait can be split into MSECMAX chunks without carrying a
 * millisecond remainder between them.  A wait longer than one poll(2) call is
 * simply served as several: nothing here limits how long select() itself can
 * be asked to wait.
 */
#define MSECMAX	32000
#define SECMAX	(MSECMAX / 1000)

extern int errno;

/*
 * Simulate BSD select() system call with poll().
 */
int
select(nfd, rfds, wfds, xfds, to)
int nfd;
fd_set *rfds, *wfds, *xfds;
struct timeval * to;
{
	long rsec;
	int msec, rms, r;
	struct pollfd pfd[FD_MAX];
	int fd;
	struct pollfd * pfp;
	int ret;
	int i, npfd, events;

	if (nfd > FD_MAX) /* should report an error here */
		nfd = FD_MAX;

	/*
	 * "to" is NULL - blocking poll
	 * "to" is zero - return immediately
	 *
	 * The wait is carried as whole seconds plus a sub-second remainder in
	 * milliseconds, not as a single millisecond count, so every timeval a
	 * caller can build is representable and waited in full.  A negative
	 * timeval is not a length, so it is refused rather than served as some
	 * other wait.
	 */
	rms = 0;
	if (to == (struct timeval *)0)
		rsec = -1L;
	else {
		if (to->tv_sec < 0L || to->tv_usec < 0L) {
			errno = EINVAL;
			return (-1);
		}
		rsec = to->tv_sec + to->tv_usec / 1000000L;
		rms = (int)((to->tv_usec % 1000000L + 999L) / 1000L);
		if (rms >= 1000) {
			rsec++;
			rms -= 1000;
		}
	}

	/*
	 * Set fd and events fields in pfd.
	 */
	for (fd = 0, pfp = pfd, npfd = 0; fd < nfd; fd++) {
		events = 0;
		if (rfds && FD_ISSET(fd, rfds))
			events |= POLLIN;
		if (wfds && FD_ISSET(fd, wfds))
			events |= POLLOUT;
		if (events) {
			pfp->events = events;
			pfp->fd = fd;
			npfd++;
			pfp++;
		}
	}

	/*
	 * One poll(2) per MSECMAX of the wait, until something is ready or the
	 * whole term has run.  revents is cleared before every call: poll(2)
	 * writes it only for descriptors it has something to say about, and a
	 * report left over from an earlier chunk would be read back as this
	 * chunk's answer.
	 *
	 * (unsigned long) on npfd is required, not cosmetic: poll(2)'s second
	 * argument is an unsigned long in the COHERENT ABI, and K&R has no
	 * prototype here to widen it -- passing a bare int pushes two bytes
	 * where the kernel reads four.
	 */
	for (;;) {
		if (rsec < 0L)
			msec = -1;		/* block */
		else if (rsec >= (long)SECMAX)
			msec = MSECMAX;
		else
			msec = (int)rsec * 1000 + rms;

		for (pfp = &pfd[0]; pfp < &pfd[npfd]; pfp++)
			pfp->revents = 0;

		if ((r = poll(pfd, (unsigned long)npfd, msec)) < 0) {
			ret = -1;
			goto done;
		}
		if (r > 0)
			break;			/* something is ready */
		if (msec < 0)
			continue;		/* only an event ends a block */
		if (msec < MSECMAX)
			break;			/* the last chunk expired */
		rsec -= (long)SECMAX;
		if (rsec == 0L && rms == 0)
			break;
	}

	/*
	 * Set return value and return bits in rfds and wfds.
	 * Punt on xfds.
	 */
	if (rfds)
		FD_ZERO(rfds);
	if (wfds)
		FD_ZERO(wfds);
	if (xfds)
		FD_ZERO(xfds);
	ret = 0;
	for (i = 0, pfp = pfd; i < npfd; i++, pfp++) {
		fd = pfp->fd;
		if (fd >= 0 && fd < nfd) {
			if (rfds && (pfp->revents & POLLIN)) {
				FD_SET(fd, rfds);
				ret++;
			}
			if (wfds && (pfp->revents & POLLOUT)) {
				FD_SET(fd, wfds);
				ret++;
			}
		}
	}

	/*
	 * Bye.
	 */
done:
	return ret;
}
