/*
 * remsh -- run a command on another machine (BSD rsh, service "shell").
 *
 *	remsh [-l login] host command ...
 *
 * Named remsh, not rsh: /usr/bin/rsh here is COHERENT's RESTRICTED SHELL, and
 * has been since V7.  HP-UX renamed this program the same way over the same
 * collision.  With no command there is nothing to run, so -- unlike BSD's rsh
 * -- this does not turn into rlogin; run rlogin(1) directly.
 *
 * From Minix 2.0.4's rsh.c (4.3BSD 5.24).  The connection is made by rcmd(),
 * which now lives in libsocket because two programs need it.
 *
 * TWO THINGS THE DONOR DID THAT THIS CANNOT.
 *
 * There is no separate stderr connection.  rcmd() is called with no fd2p, so
 * the protocol's stderr port is sent as "0" and the remote command's standard
 * error comes back merged into its standard output.  That also removes the
 * signal-forwarding path -- an interrupt here does not reach the remote command
 * -- which is why SIGINT is left to kill this program rather than being caught
 * and forwarded down a channel that does not exist.
 *
 * There is ONE process, not three.  The donor forked a process per direction.
 * Here the connection is a FIFO pair plus the framing state in `struct ichan',
 * held in this program: two processes sharing it each read the one reply FIFO,
 * so a reply is taken by whichever the kernel wakes and the other waits for
 * ever.  poll() over the terminal and the connection together is the shape
 * this stack requires.
 */
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
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
#include <net/gen/netdb.h>
#include <net/gen/inet.h>
#include <net/ioctl.h>

extern int optind;
extern char *optarg;

static char *prog_name;

static usage();
static char *copyargs();
static talk();

main(argc, argv)
int argc;
char *argv[];
{
	struct passwd *pw;
	struct servent *sp;
	int ch;
	int rem;
	char *args, *host, *user;

	prog_name= strrchr(argv[0], '/');
	if (prog_name)
		prog_name++;
	else
		prog_name= argv[0];

	host= (char *)0;
	user= (char *)0;
	while ((ch= getopt(argc, argv, "dnl:")) != EOF) switch (ch) {
	case 'l':
		user= optarg;
		break;
	case 'd':
	case 'n':
		/* Accepted and ignored: there is no debug socket option here,
		 * and standard input is never forked away, so -n asks for
		 * behaviour this already has. */
		break;
	default:
		usage();
	}
	argc -= optind;
	argv += optind;

	if (argc < 2)
		usage();
	host= argv[0];
	argc--;
	argv++;

	if (!(pw= getpwuid(getuid())))
	{
		fprintf(stderr, "%s: unknown user id.\n", prog_name);
		exit(1);
	}
	if (!user)
		user= pw->pw_name;

	args= copyargs(argv);

	if ((sp= getservbyname("shell", "tcp")) == (struct servent *)0)
	{
		fprintf(stderr, "%s: shell/tcp: unknown service.\n",
			prog_name);
		exit(1);
	}

	/*
	 * No fd2p: one connection, stderr merged.  rcmd() sends "0" for the
	 * stderr port when it is not asked for a second channel, which is the
	 * protocol's own way of saying so.
	 */
	rem= rcmd(&host, sp->s_port, pw->pw_name, user, args, (int *)0);
	if (rem < 0)
		exit(1);

	(void) setuid(getuid());

	talk(rem);
	exit(0);
}

/*
 * Standard input to the connection, the connection to standard output, until
 * the remote command's output ends.
 *
 * Standard input is watched only while it is open; at end of file the write
 * direction is shut down, which is what tells the remote shell its input has
 * finished.
 */
static talk(rem)
int rem;
{
	struct pollfd pfd[2];
	char buf[256];
	int n, held, nfd;
	int in_open= 1;

	for (;;)
	{
		pfd[0].fd= rem;
		pfd[0].events= POLLIN;
		pfd[0].revents= 0;
		pfd[1].fd= 0;
		pfd[1].events= POLLIN;
		pfd[1].revents= 0;
		nfd= in_open ? 2 : 1;

		/* Bytes already off the reply FIFO are invisible to poll(). */
		held= sockheld(rem);
		if (held == 0 &&
		    poll(pfd, (unsigned long)nfd, -1) < 0)
		{
			if (errno == EINTR)
				continue;
			break;
		}

		if (held != 0 || pfd[0].revents != 0)
		{
			if ((n= read(rem, buf, sizeof(buf))) <= 0)
				break;
			(void) write(1, buf, n);
			continue;
		}

		if (in_open && pfd[1].revents != 0)
		{
			if ((n= read(0, buf, sizeof(buf))) <= 0)
			{
				in_open= 0;
				(void) ioctl(rem, NWIOTCPSHUTDOWN, (char *)0);
				continue;
			}
			(void) write(rem, buf, n);
		}
	}
	return 0;
}

/* The command, as one space-separated string -- which is what the remote shell
 * is given to parse, so quoting is resolved THERE, not here. */
static char *copyargs(argv)
char **argv;
{
	int cc;
	char **ap, *p, *args;

	cc= 0;
	for (ap= argv; *ap; ap++)
		cc += strlen(*ap) + 1;
	if (!(args= malloc((unsigned)cc)))
	{
		fprintf(stderr, "%s: out of memory.\n", prog_name);
		exit(1);
	}
	p= args;
	for (ap= argv; *ap; ap++)
	{
		strcpy(p, *ap);
		p += strlen(p);
		if (ap[1])
			*p++= ' ';
	}
	*p= '\0';
	return args;
}

static usage()
{
	fprintf(stderr, "usage: %s [-l login] host command\n", prog_name);
	exit(1);
}
