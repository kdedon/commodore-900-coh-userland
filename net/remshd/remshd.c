/*
 * remshd -- the remote shell daemon (BSD rshd, service "shell", port 514).
 *
 *	remshd [-d] [-m maxsessions] [-p service]
 *
 * Named remshd, not rshd, because /usr/bin/rsh on this system is COHERENT's
 * RESTRICTED SHELL and has been since V7.  The client is remsh for the same
 * reason; HP-UX renamed the pair the same way and for the same collision.
 *
 * From Minix 2.0.4's in.rshd.c (Philip Homburg), with two structural changes.
 *
 * TWO MODES, ONE BINARY.
 *
 * STANDALONE: THE LISTENER IS OURS.  in.rshd expects a super-server to hand it
 * the connection on fd 0; nothing here can do that, because a connection is a
 * pair of FIFOs plus the framing state in `struct ichan' and none of it survives
 * exec(2).  So this does its own passive open, like telnetd and fingerd.
 *
 * INETD MODE, which is how rc.net runs it now: /etc/inetd relays the connection
 * over a PIPE, so the protocol arrives on standard input and the answers go out
 * on standard output, one session, then this exits.  Selected by the presence of
 * the switchboard's INETD_* variables.
 *
 * AND THIS DAEMON NEEDS THEM FOR MORE THAN DETECTION.  rshd authenticates on
 * the PEER's address and on its port being a reserved one, and on a pipe it
 * cannot ask: getpeername(0) and ioctl(0, NWIOGTCPCONF) both fail.  So both
 * numbers come out of INETD_REMADDR and INETD_REMPORT, which inetd sets from the
 * accepted socket -- and if either is absent or unparseable the session is
 * refused rather than served with an address of zero, because an address of zero
 * is one iruserok() would happily match a wildcard .rhosts against.
 *
 * THE COMMAND RUNS ON PIPES, and one process pumps both directions.  The donor
 * gave the shell the connection itself as fd 0/1 -- again impossible here -- and
 * used a process per direction, which is worse than impossible: two processes
 * sharing one channel each read the single reply FIFO, so a reply is taken by
 * whichever the kernel wakes and the other waits for ever.  One poll() loop over
 * the connection and the pipe is the shape this stack requires, and it is what
 * telnetd's term_inout() already does.
 *
 * NO SEPARATE STDERR CHANNEL.  The protocol's optional second connection, and
 * with it the signal-forwarding path, is not implemented: the client sends "0"
 * for the stderr port and the command's standard error is merged into its
 * standard output.  remsh always asks for that form.  A stock BSD rsh asking
 * for a back channel is refused, and told so.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/hton.h>
#include <net/netlib.h>
#include <net/gen/in.h>
#include <net/gen/tcp.h>
#include <net/gen/tcp_io.h>
#include <net/gen/socket.h>
#include <net/gen/netdb.h>
#include <net/gen/inet.h>
#include <net/ioctl.h>

#define CMDMAX		512
#define USERMAX		16

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

extern int optind;
extern char *optarg;
extern int opterr;
extern char **environ;

/* inet_addr returns a 32-bit address and getenv a far pointer; K&R assumes int
 * for an undeclared function, which would truncate both.  net/gen/inet.h has
 * the first under _ANSI only. */
extern ipaddr_t inet_addr();

static char *prog_name;
static int opt_d= 0;
static int opt_m= 2;

static usage();
static serve();
static session();
static inetd_session();
static shuttle();
static int getstr();
static deny();
static netput();

main(argc, argv)
int argc;
char *argv[];
{
	int c;
	int nkids;
	int nfail;
	int status;
	char *service;
	struct servent *servent;
	tcpport_t port;
	unsigned long p;
	char *end;

	prog_name= strrchr(argv[0], '/');
	if (prog_name)
		prog_name++;
	else
		prog_name= argv[0];
	service= "shell";

	opterr= 0;
	while ((c= getopt(argc, argv, "dvm:p:")) != EOF) switch (c) {
	case 'd':
	case 'v':
		opt_d= 1;
		break;
	case 'm':
		opt_m= atoi(optarg);
		if (opt_m < 1)
			usage();
		break;
	case 'p':
		service= optarg;
		break;
	default:
		usage();
	}
	if (optind != argc)
		usage();

	signal(SIGPIPE, SIG_IGN);

	/*
	 * INETD MODE.  Before the port lookup: there is no port to look up when
	 * inetd owns the listening socket, and /etc/services need not be readable
	 * for a caller inetd has already accepted to be served.  -m and -p mean
	 * nothing here.
	 */
	if (getenv("INETD_REMADDR") != (char *)0)
		exit(inetd_session());

	if ((servent= getservbyname(service, "tcp")) != (struct servent *)0)
	{
		port= servent->s_port;
	}
	else
	{
		p= strtoul(service, &end, 0);
		if (p == 0L || p > 0xFFFFL || *end != '\0')
		{
			fprintf(stderr, "%s: %s: unknown service\n",
				prog_name, service);
			exit(1);
		}
		port= htons((tcpport_t) p);
	}

	if (opt_d)
		fprintf(stderr, "%s: listening on port %u, %d at a time\n",
			prog_name, ntohs(port), opt_m);

	nkids= 0;
	nfail= 0;
	for (;;)
	{
		while (nkids < opt_m)
		{
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
		 * The status distinguishes a child that answered a caller
		 * from one that never got a connection to answer on.  EINTR
		 * is not a dead child, so nothing is decremented for it.
		 */
		if (wait(&status) < 0)
		{
			if (errno == EINTR)
				continue;
			exit(1);
		}
		nkids--;
		if ((status & 0377) == 0 &&
		    ((status >> 8) & 0377) == EX_SETUP)
		{
			if (++nfail >= SETUPTRY)
			{
				fprintf(stderr,
					"%s: no connection could be opened"
					" in %d tries, exiting\n",
					prog_name, nfail);
				exit(1);
			}
			sleep(SETUPWAIT);
		}
		else
			nfail = 0;
	}
}

/*
 * One session on the connection inetd relayed onto 0 and 1.
 *
 * The peer's address and port come from the environment because a pipe cannot be
 * asked (see the header comment).  A missing or unparseable value is a refusal:
 * inet_addr() answers 0xFFFFFFFF for text it cannot parse and 0 for "0.0.0.0",
 * and either of those handed to iruserok() is an authentication decision made on
 * a number that describes nobody.
 */
static inetd_session()
{
	char *a, *p;
	unsigned long remaddr;
	unsigned port;

	a= getenv("INETD_REMADDR");
	p= getenv("INETD_REMPORT");
	if (a == (char *)0 || p == (char *)0 || *a == '\0' || *p == '\0')
	{
		deny(1, "inetd did not say who is calling");
		return 1;
	}
	remaddr= (unsigned long) inet_addr(a);
	if (remaddr == 0L || remaddr == 0xFFFFFFFFL)
	{
		deny(1, "inetd gave an address this daemon cannot use");
		return 1;
	}
	port= (unsigned) atoi(p);
	if (opt_d)
		fprintf(stderr, "%s: inetd mode, connection from %s port %u\n",
			prog_name, a, port);
	session(0, 1, 0, remaddr, port);
	return 0;
}

/* Wait for one connection on `port' and run one command on it. */
static serve(port)
tcpport_t port;
{
	int net_fd;
	char *tcp_device;
	nwio_tcpconf_t tcpconf;
	nwio_tcpopt_t tcpopt;
	nwio_tcpcl_t tcplistenopt;

	if ((tcp_device= getenv("TCP_DEVICE")) == (char *)0)
		tcp_device= TCP_DEVICE;

	if ((net_fd= open(tcp_device, O_RDWR)) < 0)
	{
		fprintf(stderr, "%s: %s: %s\n", prog_name, tcp_device,
			strerror(errno));
		exit(EX_SETUP);
	}

	memset((char *)&tcpconf, 0, sizeof(tcpconf));
	tcpconf.nwtc_flags= NWTC_SHARED | NWTC_LP_SET |
			    NWTC_UNSET_RA | NWTC_UNSET_RP;
	tcpconf.nwtc_locport= port;
	if (ioctl(net_fd, NWIOSTCPCONF, (char *)&tcpconf) < 0)
	{
		fprintf(stderr, "%s: can't configure TCP channel: %s\n",
			prog_name, strerror(errno));
		exit(EX_SETUP);
	}

	memset((char *)&tcpopt, 0, sizeof(tcpopt));
	tcpopt.nwto_flags= NWTO_DEL_RST;
	if (ioctl(net_fd, NWIOSTCPOPT, (char *)&tcpopt) < 0 && opt_d)
		fprintf(stderr, "%s: can't set TCP options: %s\n",
			prog_name, strerror(errno));

	memset((char *)&tcplistenopt, 0, sizeof(tcplistenopt));
	if (ioctl(net_fd, NWIOTCPLISTEN, (char *)&tcplistenopt) < 0)
	{
		fprintf(stderr, "%s: listen: %s\n", prog_name,
			strerror(errno));
		exit(EX_SETUP);
	}

	if (ioctl(net_fd, NWIOGTCPCONF, (char *)&tcpconf) < 0)
	{
		fprintf(stderr, "%s: NWIOGTCPCONF: %s\n", prog_name,
			strerror(errno));
		exit(1);
	}
	if (opt_d)
		fprintf(stderr, "%s: connection from %s port %u\n", prog_name,
			inet_ntoa(tcpconf.nwtc_remaddr),
			ntohs(tcpconf.nwtc_remport));

	session(net_fd, net_fd, 1, (unsigned long)tcpconf.nwtc_remaddr,
		(unsigned)ntohs(tcpconf.nwtc_remport));
	(void) ioctl(net_fd, NWIOTCPSHUTDOWN, (char *)0);
	(void) close(net_fd);
	return 0;
}

/*
 * One session: authenticate, then run the command.
 *
 * The peer's port has to be a reserved one, which on a machine with protected
 * ports is the only thing that makes .rhosts mean anything: it says the
 * connection was made by something running as root, and therefore that the
 * remote user name on it was not chosen by whoever is sitting at that machine.
 */
static session(net_in, net_out, net_is_sock, remaddr, port)
int net_in;
int net_out;
int net_is_sock;
unsigned long remaddr;			/* network byte order, as ioctl gives it */
unsigned port;				/* host byte order			*/
{
	char remuser[USERMAX], locuser[USERMAX], cmdbuf[CMDMAX];
	char portbuf[16];
	struct passwd *pwent;
	char home[64], shellbuf[64];
	int uid, gid;
	int pin[2], pout[2];
	int pid;
	char *shell, *base;
	char zero= '\0';

	if (port >= TCPPORT_RESERVED || port < TCPPORT_RESERVED/2)
	{
		deny(net_out, "unprotected port");
		return 0;
	}

	/* The client's stderr port, as ASCII digits.  "0" means it wants none,
	 * which is the only form served here. */
	if (getstr(net_in, portbuf, sizeof(portbuf)) < 0)
		return 0;
	if (strcmp(portbuf, "0") != 0 && portbuf[0] != '\0')
	{
		deny(net_out, "no separate stderr channel is served here");
		return 0;
	}

	if (getstr(net_in, remuser, sizeof(remuser)) < 0 ||
	    getstr(net_in, locuser, sizeof(locuser)) < 0 ||
	    getstr(net_in, cmdbuf, sizeof(cmdbuf)) < 0)
		return 0;

	if ((pwent= getpwnam(locuser)) == (struct passwd *)0)
	{
		deny(net_out, "Login incorrect.");
		return 0;
	}
	/*
	 * Copy the entry out before anything else looks up a user.  getpwnam()
	 * answers from ONE static struct, and iruserok() calls it again; the
	 * pointer returned here would then be describing whoever that lookup
	 * asked about.
	 */
	uid= pwent->pw_uid;
	gid= pwent->pw_gid;
	strncpy(home, pwent->pw_dir, sizeof(home)-1);
	home[sizeof(home)-1]= '\0';
	strncpy(shellbuf, pwent->pw_shell ? pwent->pw_shell : "",
		sizeof(shellbuf)-1);
	shellbuf[sizeof(shellbuf)-1]= '\0';

	/*
	 * Root is a console account on this system.  /etc/passwd gives uid 0 an
	 * empty password field so that a machine whose only input is the
	 * keyboard in front of it can never be locked out of itself, and
	 * login(1) refuses uid 0 on any line that is not one of this machine's
	 * own consoles (cmd/login/login.c isconsole).  This daemon asks for no
	 * password at all -- it authenticates on the caller's ADDRESS -- so a
	 * command run here as uid 0 would be a root shell for whoever the
	 * address database and a .rhosts happen to name between them, with no
	 * credential anywhere in the exchange.  Refused before iruserok() is
	 * consulted, so that what ~root/.rhosts holds cannot change the answer.
	 *
	 * The refusal is told to the caller AND written on standard error, which
	 * under /etc/inetd is a pipe the switchboard files with syslog(3) under
	 * this service's name (net/inetd.c drainerr) -- so it lands wherever
	 * /etc/syslog.conf sends daemon.err, and is not lost when the caller is
	 * a program that discards what it is told.
	 */
	if (uid == 0)
	{
		deny(net_out, "root may not run commands over the network");
		fprintf(stderr,
			"%s: refused a session as root, from port %u\n",
			prog_name, port);
		return 0;
	}

	if (iruserok(remaddr, uid == 0, remuser, locuser) < 0)
	{
		deny(net_out, "Permission denied.");
		return 0;
	}

	if (pipe(pin) < 0 || pipe(pout) < 0)
	{
		deny(net_out, "Can't make pipe.");
		return 0;
	}

	shell= shellbuf;
	if (*shell == '\0')
		shell= "/bin/sh";
	base= strrchr(shell, '/');
	if (base)
		base++;
	else
		base= shell;

	/* The success byte goes out BEFORE the command runs: the client waits
	 * for it and treats anything else as a diagnostic. */
	if (write(net_out, &zero, 1) != 1)
		return 0;

	if ((pid= fork()) == 0)
	{
		int i;

		if (chdir(home) < 0)
			(void) chdir("/");
		setgid(gid);
		setuid(uid);

		close(0);
		dup(pin[0]);
		close(1);
		dup(pout[1]);
		close(2);
		dup(pout[1]);
		/* Everything else, the connection's own FIFOs included: a
		 * child holding them open keeps them alive after this process
		 * has gone. */
		for (i= 3; i < NUFILE; i++)
			(void) close(i);
		execl(shell, base, "-c", cmdbuf, (char *)0);
		_exit(127);
	}
	close(pin[0]);
	close(pout[1]);
	if (pid < 0)
	{
		close(pin[1]);
		close(pout[0]);
		return 0;
	}

	shuttle(net_in, net_out, net_is_sock, pin[1], pout[0]);
	close(pin[1]);
	close(pout[0]);
	while (wait((int *)0) < 0 && errno == EINTR)
		;
	return 0;
}

/*
 * Move bytes between the connection and the command until the command's output
 * ends.
 *
 * ONE process for both directions.  See the header comment: the connection is
 * this program's own framing state over a FIFO pair, and a second process
 * sharing it steals replies from the first.
 *
 * Data already taken off the reply FIFO is invisible to poll(), so sockheld()
 * is asked before every wait -- otherwise the wait is for a readability that
 * has already happened.
 */
static shuttle(net_in, net_out, net_is_sock, cmd_in, cmd_out)
int net_in;
int net_out;
int net_is_sock;
int cmd_in;
int cmd_out;
{
	struct pollfd pfd[2];
	char buf[256];
	int n, held;
	int net_open= 1;

	for (;;)
	{
		pfd[0].fd= net_in;
		pfd[0].events= net_open ? POLLIN : 0;
		pfd[0].revents= 0;
		pfd[1].fd= cmd_out;
		pfd[1].events= POLLIN;
		pfd[1].revents= 0;

		/* sockheld() answers about a libsocket CHANNEL.  In inetd mode
		 * net_in is a pipe, which holds nothing back and which poll()
		 * sees correctly, so it is not asked. */
		held= (net_open && net_is_sock) ? sockheld(net_in) : 0;
		/* (unsigned long): poll(2)'s count is a long in this ABI and
		 * there is no prototype to widen the argument. */
		if (held == 0 && poll(pfd, (unsigned long)2, -1) < 0)
		{
			if (errno == EINTR)
				continue;
			break;
		}

		if (pfd[1].revents != 0)
		{
			if ((n= read(cmd_out, buf, sizeof(buf))) <= 0)
				break;		/* the command is done */
			if (netput(net_out, buf, n) < 0)
				break;
			continue;
		}

		if (net_open && (held != 0 || pfd[0].revents != 0))
		{
			if ((n= read(net_in, buf, sizeof(buf))) <= 0)
			{
				/* The client has no more input.  Close the
				 * command's standard input so a program that
				 * reads to end of file can finish, and keep
				 * copying its output. */
				net_open= 0;
				close(cmd_in);
				cmd_in= -1;
				continue;
			}
			if (cmd_in >= 0)
				(void) write(cmd_in, buf, n);
		}
	}
	if (cmd_in >= 0)
		close(cmd_in);
	return 0;
}

/*
 * One NUL-terminated string off the connection.  A byte at a time, because the
 * strings are packed end to end ahead of the command's own input and a read
 * that overshot one would swallow the next.
 */
static int getstr(fd, buf, max)
int fd;
char *buf;
int max;
{
	int n= 0;
	char c;

	for (;;)
	{
		if (read(fd, &c, 1) != 1)
			return -1;
		if (c == '\0')
			break;
		if (n >= max-1)
			return -1;
		buf[n++]= c;
	}
	buf[n]= '\0';
	return n;
}

/*
 * Refuse the session.  The leading \1 is the protocol's "this is a diagnostic,
 * not the success byte"; the client prints the rest and gives up.
 */
static deny(fd, msg)
int fd;
char *msg;
{
	char buf[128];

	sprintf(buf, "\1%s\n", msg);
	(void) netput(fd, buf, strlen(buf));
	if (opt_d)
		fprintf(stderr, "%s: refused: %s\n", prog_name, msg);
	return 0;
}

/* Write `n' bytes, looping over short writes. */
static netput(fd, buf, n)
int fd;
char *buf;
int n;
{
	int k;

	while (n > 0)
	{
		if ((k= write(fd, buf, n)) <= 0)
			return -1;
		buf += k;
		n -= k;
	}
	return 0;
}

static usage()
{
	fprintf(stderr, "Usage: %s [-d] [-m maxsessions] [-p service]\n",
		prog_name);
	exit(1);
}
