/*
 * TNET		A server program for MINIX which implements the TCP/IP
 *		suite of networking protocols.  It is based on the
 *		TCP/IP code written by Phil Karn et al, as found in
 *		his NET package for Packet Radio communications.
 *
 *		This file contains an implementation of the "server"
 *		for the TELNET protocol.  This protocol can be used to
 *		remote-login on other systems, just like a normal TTY
 *		session.
 *
 * Usage:	telnetd [-dv] [-m maxlogins] [-p service]
 *
 * Version:	@(#)telnetd.c	1.00	07/26/92
 *
 * Author:	Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 *		Michael Temari, <temari@temari.ae.ge.com>
 */
#include <sys/types.h>
#include <sys/param.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdio.h>
#include <utmp.h>
#include <net/hton.h>
#include <net/netlib.h>
#include <net/gen/in.h>
#include <net/gen/tcp.h>
#include <net/gen/tcp_io.h>
#include <net/gen/socket.h>
#include <net/gen/netdb.h>
#include <net/gen/inet.h>
#include <net/ioctl.h>
#include "telnetd.h"

#define	GETTY		"/etc/getty"
#define	GETTY_SPEED	"P"		/* fixed-9600 table; see getty(1M) */
#define	UTMP		"/etc/utmp"	/* who(1): the sessions open now   */
#define	WTMP		"/usr/adm/wtmp"	/* ac(1): every session ever	   */

/*
 * Setup failure policy for the pre-forked children.  A child that cannot open,
 * configure or listen on its transport exits EX_SETUP; the parent retries it
 * SETUPTRY times, SETUPWAIT seconds apart, and then exits itself.  The retry is
 * there because the stack's connections are a fixed budget of fifteen and a
 * listen refused while another program holds them all succeeds a few seconds
 * later; the bound is there because a transport that is absent or misconfigured
 * never becomes available and a listener with nothing to wait on has nothing to
 * do.  A child that served a connection resets the count, so failures spread
 * over a daemon's life never accumulate to the bound.
 */
#define	EX_SETUP	2
#define	SETUPTRY	5
#define	SETUPWAIT	5

static char *Version = "@(#) telnetd 1.00 (07/26/92)";

int opt_d = 0;				/* debugging output flag	*/

static char *prog_name;
static int opt_m = 2;			/* concurrent logins served	*/
static char *session_line = (char *)0;	/* pty line this child logged in on */

_PROTOTYPE(void usage, (void));
_PROTOTYPE(static void serve, (tcpport_t port));
_PROTOTYPE(static void session, (int net_in, int net_out, int net_is_sock));
_PROTOTYPE(static void spawn_login, (char *tty_name));
_PROTOTYPE(static void logout, (char *line));
_PROTOTYPE(static int hangup, (void));

void usage()
{
   fprintf(stderr, "Usage: %s [-dv] [-m maxlogins] [-p service]\n", prog_name);

   exit(1);
}

/*
 * TWO MODES, ONE BINARY.
 *
 * STANDALONE: serve `opt_m' logins at a time, each in a child that owns its own
 * connection from the passive open onwards, and replace one as soon as it
 * finishes.  The connection is NOT inherited from a super-server: a connection
 * here is a pair of FIFOs plus the library state that frames them, so it cannot
 * be handed to an exec'd program the way a socket can -- each server child
 * therefore does its own passive open.
 *
 * INETD MODE, which is how rc.net gets a telnet service now.  Since a connection
 * cannot be exec'd, /etc/inetd relays one over a PIPE: the peer's bytes arrive on
 * standard input, this end's go out on standard output, one login, then this
 * exits.  It is selected by the presence of the switchboard's INETD_* variables
 * in the environment -- the only way a program whose fd 0 is a pipe can tell,
 * since getpeername(0) and ioctl(0, NWIOGTCPCONF) both fail there.  Nothing in
 * the telnet protocol needs the peer's address, so the values are read only for
 * the -d diagnostic.
 *
 * THE UTMP RECORDS ARE THE SAME IN BOTH MODES and they matter: whichever
 * process owns the session also owns the /etc/utmp slot and the /usr/adm/wtmp
 * pair for the pty line, and must retire them on the way out -- including when
 * it is killed rather than disconnected.  In inetd mode that
 * process is this one, so logout() and the SIGHUP/SIGTERM handler are set up
 * exactly as a standalone server child sets them up.
 */
int main(argc, argv)
int argc;
char *argv[];
{
register int c;
int nkids;
int nfail;
int status;
char *service;
struct servent *servent;
tcpport_t port;
unsigned long p;
char *end;

   (prog_name = strrchr(argv[0], '/')) ? prog_name++ : (prog_name = argv[0]);
   service = "telnet";

   opterr = 0;
   while ((c = getopt(argc, argv, "dvm:p:")) != EOF) switch(c) {
	case 'd':
	case 'v':
		opt_d = 1;
		break;
	case 'm':
		opt_m = atoi(optarg);
		if (opt_m < 1)
			usage();
		break;
	case 'p':
		service = optarg;
		break;
	default:
		usage();
   }

   /* No more arguments allowed. */
   if (optind != argc) usage();

   /*
    * INETD MODE: the connection is already on 0 and 1.  Tested BEFORE the port
    * lookup, because in this mode there is no port to look up -- inetd owns the
    * listening socket -- and an unreadable /etc/services must not turn away a
    * caller inetd has already accepted.  -m and -p mean nothing here.
    *
    * -d writes on standard error, which the relay has dup'd onto the same pipe
    * as standard output: in this mode -d puts its diagnostics ON THE PEER'S
    * TERMINAL, mixed into the telnet stream.  Harmless to the login (they are
    * printable bytes) and not something to leave enabled.
    */
   if (getenv("INETD_REMADDR") != (char *)0) {
	if (opt_d)
		fprintf(stderr, "%s: inetd mode, connection from %s\r\n",
			prog_name, getenv("INETD_REMADDR"));
	session(0, 1, 0);
	exit(0);
   }

   /* The port to listen on, by name or by number. */
   if ((servent = getservbyname(service, "tcp")) != (struct servent *)0) {
	port = servent->s_port;
   } else {
	p = strtoul(service, &end, 0);
	if (p == 0L || p > 0xFFFFL || *end != '\0') {
		fprintf(stderr, "%s: %s: unknown service\n",
			prog_name, service);
		exit(1);
	}
	port = htons((tcpport_t) p);
   }

   if (opt_d)
	fprintf(stderr, "%s: listening on port %u, %d at a time\n",
		prog_name, ntohs(port), opt_m);

   nkids = 0;
   nfail = 0;
   for (;;) {
	while (nkids < opt_m) {
		switch (fork()) {
		case -1:
			fprintf(stderr, "%s: fork: %s\n",
				prog_name, strerror(errno));
			if (nkids == 0)
				exit(1);
			goto reap;
		case 0:
			serve(port);
			exit(0);
		default:
			nkids++;
		}
	}
reap:
	/*
	 * The status distinguishes a child that answered a caller from one that
	 * never got a connection to answer on.  EINTR is not a dead child, so
	 * nothing is decremented for it.
	 */
	if (wait(&status) < 0) {
		if (errno == EINTR)
			continue;
		exit(1);
	}
	nkids--;
	if ((status & 0377) == 0 && ((status >> 8) & 0377) == EX_SETUP) {
		if (++nfail >= SETUPTRY) {
			fprintf(stderr,
				"%s: no connection could be opened"
				" in %d tries, exiting\n",
				prog_name, nfail);
			exit(1);
		}
		sleep(SETUPWAIT);
	} else
		nfail = 0;
   }
}

/*
 * Wait for one connection on `port', give it a pty with a login on the slave
 * end, and pass characters between the two until either end goes away.
 */
static void serve(port)
tcpport_t port;
{
int net_fd;
char *tcp_device;
nwio_tcpconf_t tcpconf;
nwio_tcpopt_t tcpopt;
nwio_tcpcl_t tcplistenopt;

   if ((tcp_device = getenv("TCP_DEVICE")) == (char *)0)
	tcp_device = TCP_DEVICE;

   if ((net_fd = open(tcp_device, O_RDWR)) < 0) {
	fprintf(stderr, "%s: %s: %s\n", prog_name, tcp_device,
		strerror(errno));
	/* A connection is a channel to the inet daemon and the daemon holds only
	 * so many at once, so this is ENFILE whenever the machine's connections
	 * are all spoken for -- a condition that clears when one of them closes.
	 * The parent owns both the delay and the bound (EX_SETUP above). */
	exit(EX_SETUP);
   }

   /* SHARED, not the default exclusive access: more than one channel is
    * configured on this port at once -- one per waiting server child -- and
    * the stack answers EADDRINUSE for a second exclusive holder of a port. */
   memset((char *)&tcpconf, 0, sizeof(tcpconf));
   tcpconf.nwtc_flags = NWTC_SHARED | NWTC_LP_SET |
			NWTC_UNSET_RA | NWTC_UNSET_RP;
   tcpconf.nwtc_locport = port;
   if (ioctl(net_fd, NWIOSTCPCONF, (char *)&tcpconf) < 0) {
	fprintf(stderr, "%s: can't configure TCP channel: %s\n",
		prog_name, strerror(errno));
	exit(EX_SETUP);
   }

   /* A reset for a connection nobody is waiting for is this end's business,
    * not the peer's. */
   memset((char *)&tcpopt, 0, sizeof(tcpopt));
   tcpopt.nwto_flags = NWTO_DEL_RST;
   if (ioctl(net_fd, NWIOSTCPOPT, (char *)&tcpopt) < 0 && opt_d)
	fprintf(stderr, "%s: can't set TCP options: %s\n",
		prog_name, strerror(errno));

   /* The passive open.  This does not return until a peer connects. */
   memset((char *)&tcplistenopt, 0, sizeof(tcplistenopt));
   if (ioctl(net_fd, NWIOTCPLISTEN, (char *)&tcplistenopt) < 0) {
	fprintf(stderr, "%s: listen: %s\n", prog_name, strerror(errno));
	exit(EX_SETUP);
   }

   if (opt_d && ioctl(net_fd, NWIOGTCPCONF, (char *)&tcpconf) >= 0)
	fprintf(stderr, "%s: connection from %s\n",
		prog_name, inet_ntoa(tcpconf.nwtc_remaddr));

   session(net_fd, net_fd, 1);
}

/*
 * One login on a connection somebody else opened: give it a pty with a login on
 * the slave end and pass characters between the two until either end goes away.
 *
 * `net_in' and `net_out' are the same descriptor for a standalone server child
 * and two different pipes under inetd; `net_is_sock' says which, and is what
 * decides whether the shuttle may ask sockheld() about the descriptor.
 */
static void session(net_in, net_out, net_is_sock)
int net_in;
int net_out;
int net_is_sock;
{
char buff[192];
int pty_fd;
int login_pid;
char *tty_name;

   /* Try allocating a PTY.  Say which of the three reasons it was: an absent
    * driver, an absent node and a busy channel are one message otherwise, and
    * only the last of them is the caller's problem to wait out. */
   if (get_pty(&pty_fd, &tty_name) < 0) {
	sprintf(buff, "I am sorry, but there is no free PTY: %s\r\n",
		pty_reason);
	(void) write(net_out, buff, strlen(buff));
	fprintf(stderr, "%s: no pty: %s\n", prog_name, pty_reason);
	exit(1);
   }

   /* Initialize the connection to an 8 bit clean channel. */
   term_init(net_out);

   /* From here to the end of the session this child owns the login records
    * for the line, including when it is killed rather than disconnected:
    * an operator stopping the network kills every server child at once, and
    * each of them is holding a session that who(1) would otherwise keep
    * listing until the next boot truncates /etc/utmp. */
   session_line = tty_name + (sizeof("/dev/") - 1);
   (void) signal(SIGHUP, hangup);
   (void) signal(SIGTERM, hangup);

   /* Fork off a child process and have it execute a getty(8). */
   switch (login_pid = fork()) {
   case -1:
	sprintf(buff, "I am sorry, but the fork(2) call failed!\r\n");
	(void) write(net_out, buff, strlen(buff));
	(void) close(pty_fd);
	exit(1);
   case 0:
	spawn_login(tty_name);
	/*NOTREACHED*/
   }

   term_inout(net_in, net_out, net_is_sock, pty_fd);

   /*
    * THE LOGIN CHILD IS HUNG UP BY NAME, and not left to the carrier.
    *
    * Closing the master below drops carrier and the driver hangs up whatever
    * holds the slave -- but a child that has not opened the slave YET holds
    * nothing.  It is asleep inside that open waiting for the carrier this
    * process is about to drop (sys/drv/pty.c ptyopen, "ptycd"), and no later
    * event on a line nobody holds can wake it: one connection that goes away
    * between the fork and the open leaves one process asleep for the life of
    * the machine.  That is not a rare ordering.  It is what a peer that drops
    * the connection as soon as it is accepted produces every time, since the
    * end of file is already waiting when this session starts.
    *
    * A child that did reach getty(1M) is in its own process group with the pty
    * for a controlling terminal, and SIGHUP is exactly what the carrier drop
    * would have given it -- so one signal serves both, and a child that has
    * already finished makes this an ESRCH and nothing else.
    */
   if (login_pid > 0)
	(void) kill(login_pid, SIGHUP);
   (void) close(pty_fd);
   if (net_is_sock)
	(void) close(net_in);

   logout(session_line);
   session_line = (char *)0;

   chown(tty_name, 0, 0);
   chmod(tty_name, 0666);
}

/*
 * Retire the login records for `line', a tty name with no "/dev/" on it.
 *
 * login(1) writes both records when a session starts but has no part in
 * ending one; init(1M) ends the sessions on the lines in /etc/ttys, and a pty
 * is not one of them, so this is the only place a telnet session is ever
 * closed out.  The utmp slot goes empty, which is what who(1) skips; the wtmp
 * record carries the line with an empty user name, which is what ac(1) reads
 * as the logout closing that line's open login.
 *
 * Called for a session that never got as far as a login too, and harmless
 * there: no slot holds the line, and a logout for a line with nothing open
 * accounts for nothing.
 */
static void logout(line)
char *line;
{
   struct utmp ut;
   struct utmp slot;
   int fd;

   if (line == (char *)0)
	return;

   memset((char *)&ut, 0, sizeof(ut));
   strncpy(ut.ut_line, line, sizeof(ut.ut_line));
   ut.ut_time = time((time_t *)0);

   if ((fd = open(WTMP, O_WRONLY)) >= 0) {
	(void) lseek(fd, 0L, 2);
	(void) write(fd, (char *)&ut, sizeof(ut));
	(void) close(fd);
   }

   if ((fd = open(UTMP, O_RDWR)) >= 0) {
	while (read(fd, (char *)&slot, sizeof(slot)) == sizeof(slot)) {
		if (strncmp(slot.ut_line, line, sizeof(slot.ut_line)) != 0)
			continue;
		(void) lseek(fd, -(long)sizeof(slot), 1);
		memset((char *)&slot, 0, sizeof(slot));
		(void) write(fd, (char *)&slot, sizeof(slot));
		break;
	}
	(void) close(fd);
   }
}

/*
 * Close the session out and go, without stdio: this runs from a signal.
 */
static int hangup()
{
   logout(session_line);
   _exit(1);
}

/*
 * Become a process group of our own on the slave side of the pty and run a
 * login on it.  Opening the slave is what makes it this group's control
 * terminal, so the group has to be set first, and every descriptor of the
 * connection has to be gone before it -- a login shell must not hold the
 * network channel open.
 */
static void spawn_login(tty_name)
char *tty_name;
{
   int fd;
   int i;

   setpgrp();

   /* The parent's session handlers are not this child's: hangup() retires a
    * utmp record this process has not written, and until the exec below there
    * is no session here to end.  Defaulted BEFORE the open, because the open is
    * where this process waits and a hangup that arrives while it waits has to
    * end it. */
   (void) signal(SIGHUP, SIG_DFL);
   (void) signal(SIGTERM, SIG_DFL);

   for (i = 0; i < NUFILE; i++)
	(void) close(i);

   /* No retry on a busy line, as init(1M) does when it spawns a getty: the
    * exclusive side of a pty is the master, which this process's parent
    * already holds, and the slave has no exclusive open. */
   if ((fd = open(tty_name, O_RDWR)) < 0)
	exit(1);
   if (fd != 0) {
	(void) dup2(fd, 0);
	(void) close(fd);
   }
   (void) dup2(0, 1);
   (void) dup2(0, 2);

   for (i = 1; i <= NSIG; i++)
	(void) signal(i, SIG_DFL);

   /* getty(1M) reads its speed table from argv[1]; argv[0] tells init's
    * flavour of line, which getty itself does not look at. */
   (void) execl(GETTY, "-r", GETTY_SPEED, (char *)0);
   (void) write(1, "EXEC failed!\r\n", 14);
   exit(1);
}
