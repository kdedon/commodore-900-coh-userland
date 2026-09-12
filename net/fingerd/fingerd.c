/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*
 * fingerd -- answer finger(1) queries from the network (RFC 742/1288).
 *
 *	fingerd [-d] [-m maxqueries] [-p service]
 *
 * The protocol body is Minix 2.0.4's in.fingerd.c (UCB 5.1): read one line,
 * turn it into an argument list for finger(1), and copy that program's output
 * back with bare newlines expanded to CRLF.  A leading "/W" asks for the long
 * form.
 *
 * TWO MODES, ONE BINARY.
 *
 * STANDALONE: the LISTENER is not the
 * donor's.  in.fingerd expects to be exec'd by a super-server with the
 * connection already on fd 0 and 1, and nothing on this system can hand over a
 * connection: it is a pair of FIFOs plus the framing state in `struct ichan'
 * that sequences them, none of which survives an exec(2) -- the new process's
 * libsocket would have an empty socket table and read(0) would fall through to
 * the raw request FIFO.  So this does its own passive open, exactly as
 * cmd/telnetd/main.c does and for the same reason.
 *
 * INETD MODE, which is what /etc/inetd starts, and the default way to run this
 * now.  Because a connection cannot be exec'd, /etc/inetd runs a service on a
 * PIPE and relays: the query arrives on standard input, the answer goes out on
 * standard output, one connection, then this exits.  There is no accept loop
 * and no listening socket, so nothing is resident between callers.
 *
 * WHICH MODE IS DECIDED BY THE ENVIRONMENT.  inetd puts the connection's four
 * addresses in it -- INETD_LOCADDR, INETD_LOCPORT, INETD_REMADDR, INETD_REMPORT
 * -- because a service on a pipe cannot ask the descriptor who is at the other
 * end: getpeername(0) and ioctl(0, NWIOGTCPCONF) both fail.  Their PRESENCE is
 * therefore also the answer to "was I spawned by the switchboard", which is the
 * question getsockname(0) answers on BSD.  fingerd needs none of the four
 * values for the protocol; it reads one only to say who called under -d.
 *
 * finger(1) itself still runs as a child on a PIPE, as in the donor.  It is a
 * separate program and it too could not be handed the connection.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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

#define FINGER		"/bin/finger"

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

static char *prog_name;
static int opt_d= 0;
static int opt_m= 2;

static usage();
static serve();
static answer();
static netline();
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
	char *peer;
	struct servent *servent;
	tcpport_t port;
	unsigned long p;
	char *end;

	prog_name= strrchr(argv[0], '/');
	if (prog_name)
		prog_name++;
	else
		prog_name= argv[0];
	service= "finger";

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

	/*
	 * INETD MODE: the connection is already on 0 and 1.  Tested before the
	 * port lookup because there is no port to look up in this mode -- inetd
	 * owns the listening socket -- and an unreadable /etc/services must not
	 * stop a service the switchboard has already accepted a caller for.
	 *
	 * -m and -p mean nothing here.  -d still writes on standard error, which
	 * the relay has dup'd onto the same pipe as standard output, so -d in this
	 * mode puts its diagnostics on the CONNECTION; that is deliberate (it is
	 * what BSD's inetd does too, where 2 is the socket) and it is why the
	 * shipped inetd.conf line does not pass it.
	 */
	if ((peer= getenv("INETD_REMADDR")) != (char *)0)
	{
		if (opt_d)
			fprintf(stderr, "%s: inetd mode, query from %s\n",
				prog_name, peer);
		answer(0, 1);
		exit(0);
	}

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

	/* A query is short and self-contained, so a child that has answered one
	 * exits and is replaced.  Same shape as telnetd: each waiting child
	 * owns its own passive open, because the open IS the accept here. */
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

/* Wait for one connection on `port' and answer the query on it. */
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

	/* SHARED: one channel per waiting child is configured on this port at
	 * once, and the stack answers EADDRINUSE for a second exclusive holder
	 * of a port. */
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

	/* The passive open.  This does not return until a peer connects. */
	memset((char *)&tcplistenopt, 0, sizeof(tcplistenopt));
	if (ioctl(net_fd, NWIOTCPLISTEN, (char *)&tcplistenopt) < 0)
	{
		fprintf(stderr, "%s: listen: %s\n", prog_name,
			strerror(errno));
		exit(EX_SETUP);
	}

	if (opt_d && ioctl(net_fd, NWIOGTCPCONF, (char *)&tcpconf) >= 0)
		fprintf(stderr, "%s: query from %s\n",
			prog_name, inet_ntoa(tcpconf.nwtc_remaddr));

	answer(net_fd, net_fd);
	(void) ioctl(net_fd, NWIOTCPSHUTDOWN, (char *)0);
	(void) close(net_fd);
	return 0;
}

/*
 * Read the query line from `in_fd', run finger(1) on it, and copy the reply back
 * to `out_fd'.
 *
 * TWO DESCRIPTORS AND NOT ONE, because in inetd mode they are two different
 * pipes -- standard input carries the peer's bytes down and standard output
 * carries the answer up.  Standalone they are both the connection.
 *
 * The argument vector is built in place inside `line', so the words it points
 * at are the caller's buffer -- which is why the buffer outlives the exec that
 * uses it.  av[0] is "finger" and not the path, so finger's own usage message
 * names the program the user asked for.
 */
static answer(in_fd, out_fd)
int in_fd;
int out_fd;
{
	char line[512];
	char *av[8];
	char *sp;
	int i, p[2], pid, status;
	int n;
	char buf[128];
	char out[256];
	int nout;

	if (netline(in_fd, line, sizeof(line)) <= 0)
		return 0;

	sp= line;
	av[0]= "finger";
	i= 1;
	/* Room for av[0], the flag, one name and the terminating null. */
	while (i < 3)
	{
		while (isspace(*sp))
			sp++;
		if (!*sp)
			break;
		if (*sp == '/' && (sp[1] == 'W' || sp[1] == 'w'))
		{
			sp += 2;
			av[i++]= "-l";
		}
		if (*sp && !isspace(*sp))
		{
			av[i++]= sp;
			while (*sp && !isspace(*sp))
				sp++;
			if (*sp)
				*sp++= '\0';
		}
	}
	av[i]= (char *)0;

	if (pipe(p) < 0)
	{
		netput(out_fd, "finger: pipe failed\r\n", 21);
		return 0;
	}
	if ((pid= fork()) == 0)
	{
		close(p[0]);
		if (p[1] != 1)
		{
			close(1);
			dup(p[1]);
			close(p[1]);
		}
		/* The connection must not survive into finger(1): its channel
		 * fds are inherited as plain fds, and a child that holds them
		 * open keeps the FIFOs alive after this process is gone. */
		for (i= 3; i < NUFILE; i++)
			if (i != 1)
				(void) close(i);
		execl(FINGER, "finger", av[1], av[2], (char *)0);
		write(1, "No finger program found\n", 24);
		_exit(1);
	}
	close(p[1]);
	if (pid < 0)
	{
		netput(out_fd, "finger: fork failed\r\n", 21);
		close(p[0]);
		return 0;
	}

	/*
	 * Bare newline to CRLF, buffered.  The donor did this a character at a
	 * time through stdio; here every write is an ioctl-sized round trip to
	 * the daemon over the channel, so the expansion is done into `out' and
	 * flushed in blocks.
	 */
	nout= 0;
	while ((n= read(p[0], buf, sizeof(buf))) > 0)
	{
		for (i= 0; i < n; i++)
		{
			if (buf[i] == '\n')
				out[nout++]= '\r';
			out[nout++]= buf[i];
			if (nout >= (int)sizeof(out) - 2)
			{
				netput(out_fd, out, nout);
				nout= 0;
			}
		}
	}
	if (nout > 0)
		netput(out_fd, out, nout);
	close(p[0]);
	while ((i= wait(&status)) != pid && i != -1)
		;
	return 0;
}

/*
 * One CRLF- or LF-terminated line from the connection, without its terminator.
 * Returns the length, 0 for an empty line and -1 if the peer went away first.
 *
 * A byte at a time: the query is one short line and there is nowhere to put a
 * read-ahead that overshoots it.
 */
static netline(fd, buf, max)
int fd;
char *buf;
int max;
{
	int n= 0;
	char c;

	for (;;)
	{
		if (read(fd, &c, 1) != 1)
			return (n > 0) ? n : -1;
		if (c == '\n')
			break;
		if (c == '\r')
			continue;
		if (n < max-1)
			buf[n++]= c;
	}
	buf[n]= '\0';
	return n;
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
	fprintf(stderr, "Usage: %s [-d] [-m maxqueries] [-p service]\n",
		prog_name);
	exit(1);
}
