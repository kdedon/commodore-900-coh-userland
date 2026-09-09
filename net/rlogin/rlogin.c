/*
 * Copyright (c) 1983, 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
char copyright[] =
"@(#) Copyright (c) 1983, 1990 The Regents of the University of California.\n\
 All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#ifdef ID
static char sccsid[] = "@(#)rlogin.c	5.33 (Berkeley) 3/1/91";
#endif
#endif /* not lint */

/*
 * rlogin - remote login
 *
 * ONE process carries both directions, waiting on the keyboard and the
 * connection together.  The connection is not a socket the kernel knows about:
 * it is a request FIFO and a reply FIFO to the inet daemon, framed by state
 * that lives in this program (net/inet_chan.c).  Two processes sharing that
 * channel each read the one reply FIFO, so a reply is taken by whichever of
 * them the kernel happens to wake, and the one that was waiting for it waits
 * for ever.  See cmd/telnet/ttn.c and cmd/telnetd/term.c, which drive the same
 * channel the same way.
 *
 * The line is put in raw mode, so the far end does the echoing and the
 * interpreting; `~.' at the start of a line ends the session locally.
 */
#include <sys/types.h>
#include <ansi.h>

#include <net/ioctl.h>
#include <net/netlib.h>
#include <net/hton.h>
#include <net/gen/in.h>
#include <net/gen/netdb.h>
#include <net/gen/tcp.h>
#include <net/gen/tcp_io.h>

#include <termio.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <poll.h>

typedef unsigned char u_char;

extern int errno;
extern char *rindex();

int eight, litout, rem;

int noescape;
u_char escapechar = '~';

/*
 * Line speeds as a termio carries them -- the CBAUD field of c_cflag rather
 * than a POSIX speed_t, and nothing above 38400 exists here.  The name is sent
 * to the far end as the second half of TERM, which is what tells its stty what
 * to claim the line runs at.
 */
struct speed
{
	int speed;
	char *name;
} speeds[] = {
	{ B0, "0" }, { B50, "50" }, { B75, "75" }, { B110, "110" },
	{ B134, "134" }, { B150, "150" }, { B200, "200" }, { B300, "300" },
	{ B600, "600" }, { B1200, "1200" }, { B1800, "1800" },
	{ B2400, "2400" }, { B4800, "4800" }, { B9600, "9600" },
	{ B19200, "19200" }, { B38400, "38400" },
	{ -1, (char *)0 }
};

extern int main _ARGS(( int argc, char **argv ));
static void usage _ARGS(( void ));
static u_char getescape _ARGS(( char *p ));
static char *speeds2str _ARGS(( int speed ));
static void lostpeer _ARGS(( int sig ));
static void doit _ARGS(( void ));
static void setsignal _ARGS(( int sig, int (*act)() ));
static void msg _ARGS(( char *str ));
static void done _ARGS(( int status ));
static void session _ARGS(( void ));
static int fromnet _ARGS(( void ));
static int fromkeyboard _ARGS(( void ));
static void mode _ARGS(( int f ));
static void echo _ARGS(( int c ));

int main(argc, argv)
	int argc;
	char **argv;
{
	extern char *optarg;
	extern int optind;
	struct passwd *pw;
	struct servent *sp;
	struct termio ttyb;
	nwio_tcpopt_t tcpopt;
	int error;
	int argoff, ch, dflag, one, uid;
	char *host, *p, *user, term[1024];

	argoff = dflag = 0;
	one = 1;
	host = user = NULL;

	if (p = rindex(argv[0], '/'))
		++p;
	else
		p = argv[0];

	if (strcmp(p, "rlogin"))
		host = p;

	/* handle "rlogin host flags" */
	if (!host && argc > 2 && argv[1][0] != '-') {
		host = argv[1];
		argoff = 1;
	}

#define	OPTIONS	"8EKLde:l:"
	while ((ch = getopt(argc - argoff, argv + argoff, OPTIONS)) != EOF)
		switch(ch) {
		case '8':
			eight = 1;
			break;
		case 'E':
			noescape = 1;
			break;
		case 'K':
			break;
		case 'L':
			litout = 1;
			break;
		case 'd':
			dflag = 1;
			break;
		case 'e':
			escapechar = getescape(optarg);
			break;
		case 'l':
			user = optarg;
			break;
		case '?':
		default:
			usage();
		}
	optind += argoff;
	argc -= optind;
	argv += optind;

	/* if haven't gotten a host yet, do so */
	if (!host && !(host = *argv++))
		usage();

	if (*argv)
		usage();

	if (!(pw = getpwuid(uid = getuid()))) {
		(void)fprintf(stderr, "rlogin: unknown user id.\n");
		exit(1);
	}
	if (!user)
		user = pw->pw_name;

	sp = getservbyname("login", "tcp");
	if (sp == NULL) {
		(void)fprintf(stderr, "rlogin: login/tcp: unknown service.\n");
		exit(1);
	}

	(void)strncpy(term, (p = getenv("TERM")) ? p : "network", sizeof(term));
	term[sizeof(term)-1]= 0;

	if (ioctl(0, TCGETA, &ttyb) == 0) {
		(void)strcat(term, "/");
		(void)strcat(term, speeds2str(ttyb.c_cflag & CBAUD));
	}

	(void)signal(SIGPIPE, lostpeer);

	rem = rcmd(&host, sp->s_port, pw->pw_name, user, term, (int *)0);

	if (rem < 0)
		exit(1);

	/* Enable BSD compatibility for urgent data. */
	tcpopt.nwto_flags= NWTO_BSD_URG;
	error= ioctl(rem, NWIOSTCPOPT, &tcpopt);
	if (error == -1)
	{
		fprintf(stderr, "rlogin: NWIOSTCPOPT failed: %s\n",
			strerror(errno));
	}

	(void)setuid(uid);
	doit();
	/*NOTREACHED*/
}

struct termio defattr, rawattr;

/*
 * Put the line in raw mode and carry the session until either the keyboard or
 * the connection ends it.
 */
static void
doit()
{
	struct termio sb;

	(void)ioctl(0, TCGETA, &sb);
	defattr = sb;
	rawattr = sb;

	rawattr.c_iflag &= ~(ICRNL | IGNCR | INLCR | ISTRIP | IXOFF | IXON |
							PARMRK | IXANY);
	rawattr.c_oflag &= ~(OPOST);
	rawattr.c_lflag &= ~(ECHONL | ECHO | ICANON | ISIG);

	(void)signal(SIGINT, SIG_IGN);
	setsignal(SIGHUP, exit);
	setsignal(SIGQUIT, exit);

	mode(1);
	session();

	msg("connection closed.");
	done(0);
}

/*
 * Wait on the keyboard and the connection together, and serve whichever has
 * something to say, until one of them reaches end of data.
 */
static void
session()
{
	struct pollfd pfd[2];
	int held;

	for (;;) {
		pfd[0].fd = rem;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		pfd[1].fd = STDIN_FILENO;
		pfd[1].events = POLLIN;
		pfd[1].revents = 0;

		/* Bytes already off the reply FIFO are invisible to poll():
		 * a write's reply parks any data that arrived in front of
		 * it, so a wait here would be for an event that has been and
		 * gone. */
		held = sockheld(rem);
		/* (unsigned long) is required: poll(2)'s count is a long in
		 * this ABI (kernel upoll(), syscall table entry 67) and there
		 * is no prototype to widen it. */
		if (held == 0 && poll(pfd, (unsigned long)2, INFTIM) < 0) {
			if (errno == EINTR)
				continue;
			return;
		}

		if ((held != 0 || pfd[0].revents != 0) && fromnet() <= 0)
			return;
		if (pfd[1].revents != 0 && fromkeyboard() <= 0)
			return;
	}
}

/* trap a signal, unless it is being ignored. */
static void
setsignal(sig, act)
	int sig;
	int (*act)();
{
	if (signal(sig, act) == SIG_IGN)
		(void)signal(sig, SIG_IGN);
}

static void
done(status)
	int status;
{
	mode(0);
	exit(status);
}

/*
 * One keystroke on its way to the connection: 0 -> line.
 * ~.				terminate
 *
 * The escape is recognised at the start of a line only, and doubling it sends
 * one through.  There is no ~^Z: COHERENT has no job control to suspend into.
 * Returns 0 when the session is over, 1 to carry on.  bol and local carry the
 * escape state from one call to the next.
 */
static int bol = 1;			/* beginning of line */
static int local = 0;

static int
fromkeyboard()
{
	int n, c;
	u_char ch;

	n = read(STDIN_FILENO, &ch, 1);
	if (n <= 0) {
		if (n < 0 && errno == EINTR)
			return (1);
		return (0);
	}
	c = ch;
	/*
	 * If we're at the beginning of the line and recognize a
	 * command character, then we echo locally.  Otherwise,
	 * characters are echo'd remotely.  If the command character
	 * is doubled, this acts as a force and local echo is
	 * suppressed.
	 */
	if (bol) {
		bol = 0;
		if (!noescape && c == escapechar) {
			local = 1;
			return (1);
		}
	} else if (local) {
		local = 0;
		if (c == '.' || c == defattr.c_cc[VEOF]) {
			echo(c);
			return (0);
		}
		if (c != escapechar)
			(void)write(rem, &escapechar, 1);
	}

	ch = c;
	if (write(rem, &ch, 1) == 0) {
		msg("line gone");
		return (0);
	}
	bol = c == defattr.c_cc[VKILL] ||
	    c == defattr.c_cc[VEOF] ||
	    c == defattr.c_cc[VINTR] ||
	    c == '\r' || c == '\n';
	return (1);
}

static void
echo(c)
int c;
{
	register char *p;
	char buf[8];

	p = buf;
	c &= 0177;
	*p++ = escapechar;
	if (c < ' ') {
		*p++ = '^';
		*p++ = c + '@';
	} else if (c == 0177) {
		*p++ = '^';
		*p++ = '?';
	} else
		*p++ = c;
	*p++ = '\r';
	*p++ = '\n';
	(void)write(STDOUT_FILENO, buf, p - buf);
}

/*
 * One helping of what the connection has to say, put on the screen: line -> 1.
 * Returns what was read, so 0 is end of data and a negative is the end of the
 * session.  Urgent-data mode is toggled and the read retried here, since the
 * byte that provoked it is still waiting.
 */
char rcvbuf[1024];

static int
fromnet()
{
	int rcvcnt, n, remaining;
	char *bufp;

	for (;;) {
		rcvcnt = read(rem, rcvbuf, sizeof (rcvbuf));
		if (rcvcnt >= 0)
			break;
		if (errno == EINTR)
			return (1);
		if (errno == EURG || errno == ENOURG) {
			nwio_tcpopt_t tcpopt;

			tcpopt.nwto_flags = errno == EURG ?
				NWTO_RCV_URG : NWTO_RCV_NOTURG;
			if (ioctl(rem, NWIOSTCPOPT, &tcpopt) == -1) {
				fprintf(stderr,
				    "rlogin: trouble with urgent data: %s\n",
					strerror(errno));
				return (-1);
			}
			continue;
		}
		(void)fprintf(stderr, "rlogin: read: %s\n", strerror(errno));
		return (-1);
	}
	if (rcvcnt == 0)
		return (0);

	bufp = rcvbuf;
	while ((remaining = rcvcnt - (bufp - rcvbuf)) > 0) {
		n = write(STDOUT_FILENO, bufp, remaining);
		if (n < 0) {
			if (errno != EINTR)
				return (-1);
			continue;
		}
		bufp += n;
	}
	return (rcvcnt);
}

static void
mode(f)
	int f;
{
	struct termio *sb;

	switch(f) {
	case 0:
		sb= &defattr;
		break;
	case 1:
		sb= &rawattr;
		break;
	default:
		return;
	}
	(void)ioctl(0, TCSETAF, sb);
}

static void
lostpeer(sig)
int sig;
{
	(void)signal(SIGPIPE, SIG_IGN);
	msg("\007connection closed.");
	done(1);
}

static void
msg(str)
	char *str;
{
	(void)fprintf(stderr, "rlogin: %s\r\n", str);
}

static void
usage()
{
	(void)fprintf(stderr,
	    "Usage: rlogin [-8EL] [-e char] [-l username] host\n");
	exit(1);
}

static u_char
getescape(p)
	register char *p;
{
	long val;
	int len;

	if ((len = strlen(p)) == 1)	/* use any single char, including '\' */
		return((u_char)*p);
					/* otherwise, \nnn */
	if (*p == '\\' && len >= 2 && len <= 4) {
		val = strtol(++p, (char **)0, 8);
		for (;;) {
			if (!*++p)
				return((u_char)val);
			if (*p < '0' || *p > '8')
				break;
		}
	}
	msg("illegal option value -- e");
	usage();
	/* NOTREACHED */
}

/*
 * Name the CBAUD code the line is running at, "unknown" if it is not one this
 * table knows.
 */
static char *
speeds2str(speed)
	int speed;
{
	int i;

	for (i= 0; speeds[i].name != (char *)0; i++) {
		if (speeds[i].speed == speed)
			return speeds[i].name;
	}
	return "unknown";
}
