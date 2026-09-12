/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * sntp.c -- set this machine's clock from an SNTP (RFC 4330) server.
 *
 *	sntp [-q] [-v] [-t seconds] [-r tries] server [server...]
 *
 *	-q	query only; print what the server said, do not set the clock
 *	-v	print the reply's stratum, reference id and round trip
 *	-t	seconds to wait for each reply	(default 5)
 *	-r	attempts per server		(default 3)
 *
 * One shot: ask, set, exit -- the shape of ntpdate(8), not of ntpd(8).  That is
 * not a simplification, it is the only shape available.  Disciplining a clock
 * means slewing it, and slewing needs adjtime() or settimeofday(); this kernel
 * has neither.  Its whole time-setting interface is stime(2) -- syscall 25,
 * sys/coh/sys1.c ustime(), one pointer to a time_t -- which STEPS the clock and
 * offers no way to nudge it.  So a step is what this does, and running it
 * periodically from cron is the closest thing to discipline this machine has.
 *
 * WHY IT MATTERS HERE.  read_cmos() returns 0 (sys/z8001/src/mdstub.c), and the
 * M58321 real-time clock has no driver yet, so the machine boots at the epoch --
 * 1970 -- every single time.  Until that driver exists this is the only
 * automatic clock the C900 has, and everything that compares two times depends
 * on it: every file's mtime, every make(1) decision about what is out of date,
 * find -newer, and the mail and log timestamps.
 *
 * THE ARITHMETIC IS THE HARD PART, not the protocol.  NTP counts seconds from
 * 1900 and UNIX from 1970, and the difference -- 2208988800 -- is LARGER than
 * LONG_MAX (2147483647).  So is the NTP seconds field itself for every date
 * after 1968: today it is about 3.99e9.  Both therefore have to be `unsigned
 * long' at every step, and the conversion is an unsigned subtraction that
 * crosses the 2^31 boundary.  Only the RESULT is safely signed (a 2026 time_t
 * is 1.78e9).  One signed slip anywhere along that path lands the machine in
 * 1900 or at a negative time_t, and nothing downstream would say why.
 *
 * The offset is spelled in HEX HALVES rather than as the decimal 2208988800:
 * cc0's constant type is 16 bits wide, and two 16-bit constants shifted
 * together cannot be mistyped.
 *
 * NO BYTE SWAPPING.  The Z8000 is big-endian and so is the NTP wire format,
 * so the 32-bit timestamp
 * fields are read straight out of the packet.  The 48-byte struct below maps
 * onto the wire exactly -- verified on target, offsets 0/1/2/3/4/8/12/40 and
 * sizeof 48 -- because char members pack and every long lands on a multiple of
 * four.  ntohs() is still used on the PORT, which is a socket address field and
 * not part of the packet.
 *
 * IT DOES NOT DISTURB THE STACK.  The inet daemon's get_time() is an uptime
 * with a one-day step clamp, written that way precisely so the wall clock
 * can be set underneath a running stack without
 * any armed timer moving.  Setting the clock in the middle of a session -- which
 * is what this program does, over a connection carried by that same stack -- is
 * the case that was designed for.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

extern int errno;
extern unsigned long inet_addr();
extern char *inet_ntoa();
extern char *ctime();
extern long time();
extern int stime();		/* syscall 25; no header declares it (cf. cmd/date.c) */

/*
 * Seconds between 1900-01-01 and 1970-01-01: 2208988800, which is 0x83AA7E80.
 * Written as halves -- see the header comment.
 */
#define NTP_EPOCH	(((unsigned long)0x83AAU << 16) | 0x7E80U)

/*
 * 2^32 - NTP_EPOCH = 2085978496 (0x7C558180): what to ADD to an era-1 NTP
 * timestamp, i.e. one taken after the counter wraps on 2036-02-07.  Those dates
 * still fit a signed 32-bit time_t (which does not run out until 2038-01-19), so
 * the case is worth the four lines even though nothing can reach it yet.  It is
 * also the only reason the "seconds is below the offset" test is not simply an
 * error: before 2036 it means a broken server, after 2036 it is normal.
 */
#define NTP_ERA1	(((unsigned long)0x7C55U << 16) | 0x8180U)

/* Half a second, as an NTP fraction: used to round rather than truncate. */
#define NTP_HALF	(((unsigned long)0x8000U << 16) | 0x0000U)

#define NTP_PORT	123
#define NTP_VERSION	4		/* RFC 4330 */
#define MODE_CLIENT	3
#define MODE_SERVER	4
#define LI_ALARM	3		/* server not synchronised to anybody */

/*
 * The packet, which is also the wire format.  RFC 4330 figure 1: 48 bytes, the
 * first four a byte each, then four 32-bit words, then four 64-bit timestamps
 * (each a seconds half and a fraction half).  Big-endian throughout, so on this
 * machine the longs need no conversion.
 */
struct ntpmsg {
	unsigned char	li_vn_mode;	/* leap<<6 | version<<3 | mode */
	unsigned char	stratum;
	char		poll;
	char		precision;
	unsigned long	rootdelay;
	unsigned long	rootdisp;
	unsigned long	refid;
	unsigned long	reftime_s, reftime_f;
	unsigned long	org_s, org_f;	/* originate: our transmit time */
	unsigned long	rec_s, rec_f;	/* server received it */
	unsigned long	xmt_s, xmt_f;	/* server sent the reply -- what we use */
};

static char *prog = "sntp";
static int qflag;		/* -q: query only */
static int vflag;		/* -v: describe the reply */
static int timeout = 5;		/* -t: seconds per attempt */
static int tries = 3;		/* -r: attempts per server */

/*
 * The most poll(2) can be asked to wait in one call, and the same span in
 * whole seconds: its timeout is a plain int of milliseconds, so 32.767 s is
 * all one call reaches.  -t takes seconds and has no ceiling of its own, so a
 * `-t 33' (or anything above 32) is served as more than one poll(2) rather
 * than truncated into a millisecond count that wraps negative and blocks
 * forever.
 */
#define MSECMAX	32000
#define SECMAX	(MSECMAX / 1000)

/*
 * Wait up to `secs' seconds for `pfd' (a single descriptor) to become ready,
 * split into as many poll(2) calls as MSECMAX requires.  Returns poll(2)'s
 * result for the call that ended the wait: >0 ready, 0 on a full timeout, -1
 * on error with errno set.
 */
static int
waitpoll(pfd, secs)
struct pollfd *pfd;
int secs;
{
	long rsec;
	int msec, r;

	rsec = (long)secs;
	for (;;) {
		msec = rsec >= (long)SECMAX ? MSECMAX : (int)rsec * 1000;
		pfd->revents = 0;
		if ((r = poll(pfd, (unsigned long)1, msec)) != 0)
			return r;
		if (msec < MSECMAX)
			return 0;
		rsec -= (long)SECMAX;
		if (rsec == 0L)
			return 0;
	}
}

/*
 * The reference identifier is four ASCII characters for a stratum-1 server
 * (GPS, PPS, DCF) and an IPv4 address for anything below it.  Printed because
 * it is the one field that says WHAT the machine is being set from.
 */
static char *refidstr(refid, stratum, buf)
unsigned long refid;
int stratum;
char *buf;
{
	int i, c;
	struct in_addr a;

	if (stratum <= 1)
	{
		for (i = 0; i < 4; i++)
		{
			c = (int)((refid >> (24 - 8 * i)) & 0xFF);
			buf[i] = (c >= ' ' && c < 0x7F) ? c : '.';
		}
		buf[4] = '\0';
		return buf;
	}
	a.s_addr = refid;
	strcpy(buf, inet_ntoa(a));
	return buf;
}

/*
 * Ask one server.  Returns 1 and stores the UNIX time in *tp on success, 0 if
 * the server did not answer or answered unusably.  Every failure says which,
 * because "sntp failed" on a boot console is not a diagnosis.
 */
static int query(name, tp)
char *name;
long *tp;
{
	int s, i, n, alen, li, vn, mode;
	unsigned long secs, frac;
	long t;
	struct hostent *hp;
	struct servent *sp;
	struct sockaddr_in to, from;
	struct pollfd set[1];
	struct ntpmsg out, in;
	unsigned long addr;
	int port;
	char rbuf[24];

	if ((hp = gethostbyname(name)) == (struct hostent *)0)
	{
		printf("%s: %s: unknown host\n", prog, name);
		return 0;
	}
	memcpy((char *)&addr, hp->h_addr, 4);

	/*
	 * The port comes from the database so a site can move the service, and
	 * falls back to 123 -- a machine whose /etc/services was never installed
	 * must still be able to set its clock.  libsocket's built-in table
	 * carries `ntp' for exactly that case.
	 */
	port = NTP_PORT;
	if ((sp = getservbyname("ntp", "udp")) != (struct servent *)0)
		port = ntohs(sp->s_port);

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		printf("%s: socket: errno %d\n", prog, errno);
		return 0;
	}
	memset((char *)&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_port = htons(port);
	to.sin_addr.s_addr = addr;

	set[0].fd = s;
	set[0].events = POLLIN;

	for (i = 1; i <= tries; i++)
	{
		memset((char *)&out, 0, sizeof(out));
		out.li_vn_mode = (NTP_VERSION << 3) | MODE_CLIENT;
		/*
		 * The originate timestamp is ours to choose and comes back
		 * untouched in org_s, which is the only thing that ties a reply
		 * to a request.  Our clock is the thing being set, so it cannot
		 * be trusted as a value -- but it does not have to be: it only
		 * has to be UNIQUE per attempt, hence the attempt number in the
		 * fraction.
		 */
		out.xmt_s = (unsigned long)time((long *)0) + NTP_EPOCH;
		out.xmt_f = (unsigned long)i;

		if (sendto(s, (char *)&out, sizeof(out), 0,
		    (struct sockaddr *)&to, sizeof(to)) < 0)
		{
			printf("%s: %s: sendto: errno %d\n", prog, name, errno);
			continue;
		}

		errno = 0;
		n = waitpoll(set, timeout);
		if (n < 0 && errno != EINTR)
		{
			printf("%s: %s: poll: errno %d\n", prog, name, errno);
			break;
		}
		if (n <= 0)
		{
			if (vflag)
				printf("%s: %s: no reply (try %d of %d)\n",
					prog, name, i, tries);
			continue;
		}

		alen = sizeof(from);
		n = recvfrom(s, (char *)&in, sizeof(in), 0,
			(struct sockaddr *)&from, &alen);
		if (n < 0)
		{
			printf("%s: %s: recvfrom: errno %d\n", prog, name,
				errno);
			break;
		}
		/*
		 * A short packet is not a small answer, it is a different
		 * protocol: every field this reads lives past byte 40.
		 */
		if (n < (int)sizeof(in))
		{
			printf("%s: %s: runt reply, %d bytes\n", prog, name, n);
			continue;
		}
		if (in.org_s != out.xmt_s || in.org_f != out.xmt_f)
		{
			if (vflag)
				printf("%s: %s: reply to a different request\n",
					prog, name);
			continue;	/* a stale datagram from an earlier try */
		}

		li = (in.li_vn_mode >> 6) & 3;
		vn = (in.li_vn_mode >> 3) & 7;
		mode = in.li_vn_mode & 7;
		if (mode != MODE_SERVER)
		{
			printf("%s: %s: not a server reply (mode %d)\n", prog,
				name, mode);
			continue;
		}
		/*
		 * Three ways a syntactically fine reply is still worthless, and
		 * all three are the server telling the truth about itself: the
		 * alarm leap indicator means it is not synchronised to anything,
		 * stratum 0 is a kiss-o'-death packet (RFC 4330 section 8), and
		 * a zero transmit timestamp means it never had a time to send.
		 * Taking any of them would set this machine's clock to a number
		 * the server itself does not believe.
		 */
		if (li == LI_ALARM)
		{
			printf("%s: %s: server is not synchronised\n", prog,
				name);
			continue;
		}
		if (in.stratum == 0 || in.stratum > 15)
		{
			printf("%s: %s: unusable stratum %d\n", prog, name,
				(int)in.stratum);
			continue;
		}
		secs = in.xmt_s;
		frac = in.xmt_f;
		if (secs == (unsigned long)0)
		{
			printf("%s: %s: server sent a zero timestamp\n", prog,
				name);
			continue;
		}

		/*
		 * The conversion.  Everything here is unsigned long on purpose;
		 * see the header comment.  Bit 31 of the NTP seconds is the era:
		 * set means 1968..2036 (era 0), clear means the counter has
		 * wrapped and the date is 2036 or later (RFC 4330 section 3).
		 */
		if (secs >= NTP_EPOCH)
			t = (long)(secs - NTP_EPOCH);
		else if ((secs & NTP_HALF) == (unsigned long)0)
			t = (long)(secs + NTP_ERA1);	/* era 1: 2036.. */
		else
		{
			/* Era 0 but before 1970 -- 1900..1968.  No server has a
			 * legitimate reason to say this. */
			printf("%s: %s: timestamp before 1970, ignored\n", prog,
				name);
			continue;
		}
		/* Round to the nearer second rather than truncating: stime()
		 * takes whole seconds and the fraction is otherwise thrown
		 * away, which biases every set half a second slow. */
		if (frac >= NTP_HALF)
			t++;

		if (vflag)
			printf("%s: %s: stratum %d, version %d, ref %s\n",
				prog, name, (int)in.stratum, vn,
				refidstr(in.refid, (int)in.stratum, rbuf));
		close(s);
		*tp = t;
		return 1;
	}
	close(s);
	return 0;
}

static void usage()
{
	printf("usage: %s [-q] [-v] [-t seconds] [-r tries] server...\n", prog);
	exit(2);
}

int main(argc, argv)
int argc;
char **argv;
{
	int i;
	long now, t, delta;

	for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++)
	{
		switch (argv[i][1])
		{
		case 'q':	qflag = 1; break;
		case 'v':	vflag = 1; break;
		case 't':	if (++i >= argc) usage();
				timeout = atoi(argv[i]);
				if (timeout <= 0) usage();
				break;
		case 'r':	if (++i >= argc) usage();
				tries = atoi(argv[i]);
				if (tries <= 0) usage();
				break;
		default:	usage();
		}
	}
	if (i >= argc)
		usage();

	for (; i < argc; i++)
	{
		if (!query(argv[i], &t))
			continue;

		now = time((long *)0);
		delta = t - now;
		/*
		 * ctime() ends in a newline of its own, so the two dates are
		 * printed on separate lines rather than fought with.
		 */
		printf("%s: %s says %s", prog, argv[i], ctime(&t));
		if (qflag)
		{
			printf("%s: clock is %ld seconds %s (not set, -q)\n",
				prog, delta < 0 ? -delta : delta,
				delta < 0 ? "fast" : "slow");
			return 0;
		}
		if (stime(&t) < 0)
		{
			printf("%s: stime: errno %d (must be root)\n", prog,
				errno);
			return 1;
		}
		printf("%s: clock stepped %ld seconds\n", prog, delta);
		return 0;
	}
	printf("%s: no server answered\n", prog);
	return 1;
}
