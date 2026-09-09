/*
 * inetd.c -- the super-server: listen on the ports named in /etc/inetd.conf,
 * and run a program per connection.
 *
 *	inetd [-d] [-m maxchildren] [conffile]
 *
 * THE NAME.  For the whole of this port /etc/inetd was the TCP/IP STACK -- the
 * Minix inet server, which owns every connection this machine has and reads no
 * service table at all.  It is now /etc/inet, which is what it is, and this
 * program has the name that describes it.  The two share no code: the stack is
 * net/inet/, and this is an ordinary client of it, linked against libsocket
 * like telnet or ping.
 *
 * WHAT IT IS FOR: memory and process slots, not connections.  A listening port
 * on this machine IS an open channel to the stack, whether a super-server or a
 * standalone daemon holds it, so nothing here reclaims connections.  What it
 * reclaims is the resident cost of daemons that are waiting rather than
 * working -- telnetd, remshd and fingerd are about 132 KB of text between them
 * plus a proc slot and a u-area each, and none of them does anything until
 * somebody calls.
 *
 * WHY A CONNECTION CANNOT SIMPLY BE EXEC'D, AND WHAT IS DONE INSTEAD.  On BSD,
 * inetd dup2()s the accepted socket onto 0, 1 and 2 and execs the service: the
 * descriptor is the connection, and exec keeps descriptors.  Here it is not.  A
 * connection is two FIFOs plus the framing state in `struct ichan' that
 * sequences them, and that state lives in libsocket's data; exec throws it
 * away, so the new program's read(0) would fall through to the raw reply FIFO
 * and collect channel headers instead of the peer's bytes.  cmd/fingerd and
 * cmd/telnetd both say the same thing, which is why they carry their own
 * passive opens rather than expecting a super-server.
 *
 * So the service is exec'd on a PIPE, and a relay copies bytes between the pipe
 * and the socket.  A connection therefore costs two processes -- the relay,
 * which is this program after a fork and so already holds the channel, and the
 * service it exec'd -- and in exchange the service sees the ordinary
 * stdin/stdout contract every inetd-style daemon is written against.  What is
 * lost is that the service cannot getpeername() its own connection; what is
 * gained is that a program needs to know nothing about this stack to be served
 * by it.
 *
 * INTERNAL SERVICES cost neither the pipe nor the second process: the forked
 * child answers on the socket itself and exits.  echo, discard, daytime,
 * chargen and time are here for the reason BSD has them -- they are the
 * smallest thing that makes a machine answerable from outside, and they need
 * nothing else to be installed or ported first.
 *
 * AND ONE INTERNAL SERVICE IS A REAL DAEMON: ntalk (518/udp), which used to be
 * /etc/talkd.  A datagram service has no accept(), so what a child is given is
 * the LISTENING socket, and it keeps it: this process sockdrop()s its own copy
 * and binds a fresh one only once that child has exited, because a socket driven
 * by two processes leaves the other's `struct ichan' describing a channel that
 * has moved on.  net/talkserv.c holds the service and says where the invitation
 * table lives and why.  What it buys is a 41 KB program image and a standing
 * process slot that nothing occupies between calls; what it costs is this
 * program's text, which is shared with every child it forks anyway.
 *
 * WHERE A DIAGNOSTIC GOES: syslog(3), facility LOG_DAEMON, and standard error as
 * well when -d was given.  A service under a super-server has nowhere else to
 * complain -- on BSD its standard error IS the connection, and here the relay's
 * pipe would put it there too -- so the peer would collect a diagnostic as
 * protocol and an administrator would see nothing.  So the relay gives a service
 * a THIRD pipe for its standard error and files what arrives on it, a line at a
 * time, under the service's name.  NOTHING THIS PROGRAM OR A SERVICE WRITES AS A
 * DIAGNOSTIC REACHES THE CONNECTION.
 *
 * THE CEILING IS DESCRIPTORS, and it is low.  A socket costs this process TWO
 * of them (request FIFO and reply FIFO -- net/inet_chan.c), the per-process
 * table is NUFILE deep (<sys/param.h>, the same header the kernel sizes
 * u_filep[] with), and stdio holds three; accept() opens a fresh listening
 * channel before it hands the connection over, so THREE more have to be free
 * while it runs.  MAXSERV is that arithmetic written down rather than a number
 * somebody once did it to reach, and so is this process not keeping the log
 * open between messages.  The stack's own side of the same
 * limit is printed when it starts ("inet: ready, NN connections") and measured
 * by /bin/chanmax.
 *
 * SIGNAL NUMBERS ON THIS SYSTEM ARE NOT THE USUAL ONES: SIGTERM is 5, SIGPIPE
 * is 8, and there is NO SIGCLD at all (include/signal.h lists eleven
 * signals and that is all of them), nor any waitpid() or WNOHANG.  The missing
 * SIGCLD is why the reaping below is shaped the way it is: a child is collected
 * either as backpressure at the concurrency limit (reap()) or on an idle poll
 * timeout, where the only available non-blocking wait is a blocking one bounded
 * by alarm(2) (reapidle()).
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <unistd.h>

extern int errno;
extern int optind;
extern char *optarg;
extern int opterr;

/*
 * Every one of these returns something wider than the int K&R assumes for an
 * undeclared function, and on this machine a pointer is wider still.  A
 * missing declaration here does not fail to compile; it truncates the value
 * and the program misbehaves somewhere else entirely, which is this port's
 * most recurring bug.
 */
extern char *strrchr();
extern char *ctime();
extern char *inet_ntoa();
extern long time();

/*
 * Services that may be configured at once.
 *
 * Two descriptors per listening socket, out of the NUFILE a process gets,
 * three of which are already stdin/stdout/stderr.  THREE MORE HAVE TO BE FREE
 * WHILE accept() RUNS, not two: opening the replacement listening channel
 * takes a request FIFO, a reply FIFO and, for as long as the hello is being
 * written, the daemon's rendezvous (net/inet_chan.c ichan_open), and then the
 * connection is dup()ed onto a descriptor of its own while that channel is
 * held.  So 2 * MAXSERV + 3 + 3 <= NUFILE, which is the definition below read
 * backwards: at NUFILE 20 it is seven and nothing else may be held, so the log
 * is not (report()).  The last slot is ntalk's: a datagram service costs the
 * same two descriptors while it is unclaimed and gives them back for the
 * length of a call, because the child it is handed to takes them over.
 *
 * The kernel's table is what this follows.  Deriving it here rather than
 * writing the answer down is what makes a program built against a different
 * <sys/param.h> bind the number of services that header allows, instead of
 * the number that one somebody once read did.
 *
 * A conf file naming more is not a syntax error: the extra lines are reported
 * and skipped, because the alternative is a daemon that starts, binds some
 * ports and silently answers on none of the rest.
 */
#define MAXSERV		((NUFILE - 3 - 3) / 2)
/*
 * Bounds on the two failures that are not events on any descriptor.  A poll()
 * that fails for a reason other than a signal is not transient, so the daemon
 * stops after POLLFAILMAX of them; a failed accept() is (see the call site), so
 * ACCFAILMAX bounds how many are REPORTED rather than how many are retried.
 */
#define POLLFAILMAX	5
#define ACCFAILMAX	3
/*
 * And on the third: a service whose listening socket turned out not to be one
 * is bound again, and asked for no more than this many times.  A port held by
 * something else does not clear by being asked, so an unbounded retry would
 * write a line every idle turn for as long as the machine was up and bury the
 * one that said what happened.
 */
#define REBINDMAX	5
/*
 * How long a turn is where there is nothing to poll -- the last service lost
 * its socket, so no descriptor's readiness can pace the retry.  The idle poll
 * timeout, so that a service is bound again at the same rate either way.
 */
#define REBINDWAIT	10
#define MAXARGV		8		/* argv slots per service	*/
#define LINELEN		200
#define POOL		1024		/* bytes of parsed conf text	*/
#define CONF		"/etc/inetd.conf"
#define BUFLEN		512		/* relay/copy buffer		*/

/* Children alive at once.  Each is a relay plus, for an external service, the
 * program it exec'd, so this bounds processes as much as callers. */
#define MAXKIDS		4

struct serv {
	char	*sv_name;		/* service name, for messages	*/
	int	sv_port;		/* network byte order		*/
	int	sv_internal;		/* which internal service, or 0	*/
	int	sv_dgram;		/* datagram service, owned by one child */
	int	sv_uid, sv_gid;
	char	*sv_prog;		/* path to exec, or null	*/
	char	*sv_argv[MAXARGV + 1];
	int	sv_fd;			/* listening socket, -1 if none	*/
	int	sv_pid;			/* the child that owns sv_fd, or 0 */
	int	sv_rebind;		/* binds still owed this service */
};

/* The internal services, by the number this file gives them. */
#define I_ECHO		1
#define I_DISCARD	2
#define I_DAYTIME	3
#define I_CHARGEN	4
#define I_TIME		5
#define I_NTALK		6

static struct serv servs[MAXSERV];
static int nserv;
static char pool[POOL];
static int poolused;

/* Where a datagram goes when there is no process to serve it -- see the fork
 * failure in the main loop.  recvfrom() drops what does not fit, as UDP does, so
 * this is a place to put a datagram and not a size to get right. */
static char dgdrop[16];

static char *prog_name = "inetd";
/* Not static: net/talkserv.c reports through this file's report() and asks the
 * same question about -d that everything else here does. */
int opt_d;
static int opt_m = MAXKIDS;
static char *conffile = CONF;

/* Set from a handler, acted on in the main loop: a handler that did the work
 * itself would be tearing descriptors down underneath a poll(). */
static int want_quit;
static int want_reload;

static void onterm() { want_quit = 1; }
static void onhup() { want_reload = 1; }

/* SIGALRM exists only to interrupt the wait() in reapidle(), so its handler has
 * nothing to do; it must exist all the same, because the default action for
 * SIGALRM is to terminate the process.  Re-armed on delivery: signal(2) here
 * restores the default once the handler has been entered. */
static void onalrm() { signal(SIGALRM, onalrm); }

static void loadconf();
static void openserv();
static void closeserv();
static void dropserv();
static void service();
static void internal();
static void relay();
static int drainerr();
static void errline();
static char **makeenv();
static int reap();
static int reapidle();
static int copyout();
extern void talkserv();

/*
 * ONE DIAGNOSTIC, and it goes where somebody will read it.
 *
 * syslog(3) always, because this program and every child of it is between a
 * caller and a service and has no terminal: rc.net starts it in the background,
 * and the standard error of a relayed service is a pipe back to the peer.  A
 * record is one write of under 256 bytes on /dev/log, which this kernel
 * guarantees is atomic, so the relays' lines cannot interleave with each other's
 * -- and syslogd starts ahead of rc.net for exactly this (etc/rc).  LOG_CONS: a
 * machine with no syslogd puts the record on the console rather than dropping
 * it, since a message about a service that will not start is the one that must
 * not be lost.
 *
 * Standard error AS WELL under -d, which is how the switchboard is run by hand
 * and under the test harness: the transcript is then the whole record and needs
 * no second file opened to read it.  Flushed, because an unflushed complaint
 * about a conf file is a complaint nobody ever reads.
 *
 * Not static: net/talkserv.c reports the same way, for the same reason.
 *
 * Varargs, unlike the pfx()/fprintf/nl() this replaces: with the format
 * deciding each argument's width there is no fixed parameter for an int to be
 * pushed into where a far pointer is read, which is why the earlier shape
 * avoided a helper at all.
 */
void report(pri, fmt)
int pri;
char *fmt;
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(pri, fmt, ap);
	va_end(ap);
	/*
	 * THE LOG IS NOT KEPT OPEN BETWEEN MESSAGES, and that is descriptor
	 * arithmetic rather than tidiness: syslog(3) opens /dev/log on its first
	 * message and holds it, and this process is at the descriptor ceiling
	 * with every service bound (see MAXSERV).  One descriptor held for a
	 * channel that is written to only when something has gone wrong is the
	 * one an accept() needs to open its replacement listener with.
	 */
	closelog();
	if (opt_d)
	{
		fprintf(stderr, "%s: ", prog_name);
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
		fprintf(stderr, "\n");
		fflush(stderr);
	}
}

/* Copy a token into the parse pool and return a pointer to it, or null when
 * the pool is full. */
static char *keep(s)
char *s;
{
	int n;
	char *p;

	n = strlen(s) + 1;
	if (poolused + n > POOL)
		return (char *)0;
	p = pool + poolused;
	strcpy(p, s);
	poolused += n;
	return p;
}

/*
 * Split `line' into whitespace-separated fields, in place.  Returns the count;
 * `f' holds pointers into the line, so the caller copies anything it keeps.
 */
static int split(line, f, max)
char *line;
char **f;
int max;
{
	int n;
	char *p;

	n = 0;
	p = line;
	while (*p && n < max)
	{
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '\0' || *p == '\n')
			break;
		f[n++] = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n')
			p++;
		if (*p)
			*p++ = '\0';
	}
	return n;
}

/* The internal handler this service name asks for, or 0 for none. */
static int internal_id(name)
char *name;
{
	if (strcmp(name, "echo") == 0)		return I_ECHO;
	if (strcmp(name, "discard") == 0)	return I_DISCARD;
	if (strcmp(name, "daytime") == 0)	return I_DAYTIME;
	if (strcmp(name, "chargen") == 0)	return I_CHARGEN;
	if (strcmp(name, "time") == 0)		return I_TIME;
	if (strcmp(name, "ntalk") == 0)		return I_NTALK;
	return 0;
}

/* Does this internal service want a datagram socket held for its whole life --
 * BSD's `dgram udp wait' -- rather than a connection per caller? */
static int internal_dgram(id)
int id;
{
	return id == I_NTALK;
}

/*
 * Read the configuration into servs[].
 *
 *	service socktype protocol wait/nowait user program [argv0 args...]
 *
 * BSD's format, and the two shapes this machine can honour:
 *
 *	stream tcp nowait	a connection per caller, for any program
 *	dgram  udp wait		one socket, held for its life by ONE child,
 *				and INTERNAL only
 *
 * The second is restricted to an internal service because what a `wait' service
 * is given is the listening socket itself, and a descriptor cannot be handed to
 * an exec'd program here at all -- the connection is FIFOs plus framing state
 * that exec throws away.  A `dgram udp wait' line naming a PROGRAM is therefore
 * refused rather than served badly.
 *
 * A line asking for anything else is REPORTED AND SKIPPED, not ignored -- a line
 * that vanished in silence would leave an administrator hunting a daemon bug in
 * a service that was never started.
 *
 * The port comes from getservbyname(), which reads /etc/services and falls
 * back to libsocket's built-in table.  A service field that is all digits is
 * taken as the port number itself, so a machine with neither file can still be
 * configured.
 */
static void loadconf()
{
	FILE *fp;
	char line[LINELEN];
	char *f[6 + MAXARGV];
	struct serv *sv;
	struct servent *se;
	struct passwd *pw;
	int n, i, nf, port, dgram;

	nserv = 0;
	poolused = 0;
	if ((fp = fopen(conffile, "r")) == (FILE *)0)
	{
		report(LOG_ERR, "%s: cannot open", conffile);
		return;
	}
	while (fgets(line, sizeof(line), fp) != (char *)0)
	{
		if (line[0] == '#' || line[0] == '\n')
			continue;
		nf = split(line, f, 6 + MAXARGV);
		if (nf == 0)
			continue;
		if (nf < 6)
		{
			report(LOG_ERR,
				"%s: needs six fields, has %d",
				f[0], nf);
			continue;
		}
		if (strcmp(f[1], "stream") == 0 && strcmp(f[2], "tcp") == 0)
			dgram = 0;
		else if (strcmp(f[1], "dgram") == 0 && strcmp(f[2], "udp") == 0)
			dgram = 1;
		else
		{
			report(LOG_ERR,
				"%s: only `stream tcp' and `dgram udp' are served"
				" here, not `%s %s'", f[0], f[1], f[2]);
			continue;
		}
		if (strcmp(f[3], dgram ? "wait" : "nowait") != 0)
		{
			report(LOG_ERR,
				"%s: a %s service is `%s' here, not `%s'",
				f[0], dgram ? "dgram" : "stream",
				dgram ? "wait" : "nowait", f[3]);
			continue;
		}
		if (dgram && strcmp(f[5], "internal") != 0)
		{
			report(LOG_ERR,
				"%s: a dgram service must be internal, not %s --"
				" a socket cannot be handed to an exec'd program"
				" here", f[0], f[5]);
			continue;
		}
		/*
		 * A SERVICE WHOSE PROGRAM IS NOT INSTALLED IS NOT OFFERED: the
		 * line is reported and skipped, and its port is left unbound and
		 * free for anything else that wants it -- which is how
		 * `sendmail -bd' gets port 25 on a machine whose conf file also
		 * names smtpd.  So a conf file may carry a line for every daemon
		 * this system has, and each is served exactly where its program
		 * exists: what enables a service is the dist list that installs
		 * it, with no file to edit.  Skipped rather than fatal, because
		 * one absent program is not the other services' business.
		 *
		 * Tested BEFORE the MAXSERV count below, so a line that will not
		 * be offered costs nothing against the ceiling: a conf file may
		 * name more daemons than this machine has and still fill its
		 * slots with the ones it does.
		 */
		if (strcmp(f[5], "internal") != 0 && access(f[5], X_OK) < 0)
		{
			report(LOG_NOTICE,
				"%s: %s is not installed, service not offered",
				f[0], f[5]);
			continue;
		}
		if (nserv == MAXSERV)
		{
			report(LOG_ERR,
				"%s: more than %d services configured -- this"
				" one is skipped", f[0], MAXSERV);
			continue;
		}

		/* The port. */
		port = 0;
		for (i = 0; f[0][i]; i++)
			if (f[0][i] < '0' || f[0][i] > '9')
				break;
		if (i > 0 && f[0][i] == '\0')
			port = htons(atoi(f[0]));
		else if ((se = getservbyname(f[0], f[2]))
				!= (struct servent *)0)
			port = se->s_port;
		else
		{
			report(LOG_ERR,
				"%s: no such %s service in /etc/services",
				f[0], f[2]);
			continue;
		}

		sv = &servs[nserv];
		sv->sv_fd = -1;
		sv->sv_pid = 0;
		sv->sv_rebind = 0;
		sv->sv_port = port;
		sv->sv_internal = 0;
		sv->sv_dgram = dgram;
		sv->sv_prog = (char *)0;
		sv->sv_argv[0] = (char *)0;
		if ((sv->sv_name = keep(f[0])) == (char *)0)
		{
			report(LOG_ERR,
				"%s: the conf file is larger than this"
				" program's %d-byte parse pool", f[0], POOL);
			break;
		}

		/* Who it runs as.  `root' answers without a lookup, so that a
		 * machine whose /etc/passwd is unreadable still comes up. */
		if (strcmp(f[4], "root") == 0)
		{
			sv->sv_uid = 0;
			sv->sv_gid = 0;
		}
		else if ((pw = getpwnam(f[4])) != (struct passwd *)0)
		{
			sv->sv_uid = pw->pw_uid;
			sv->sv_gid = pw->pw_gid;
		}
		else
		{
			report(LOG_ERR, "%s: no such user as `%s'", f[0], f[4]);
			continue;
		}

		if (strcmp(f[5], "internal") == 0)
		{
			if ((sv->sv_internal = internal_id(f[0])) == 0)
			{
				report(LOG_ERR,
					"%s: there is no internal service by"
					" that name", f[0]);
				continue;
			}
			/* Each internal service answers on ONE kind of socket,
			 * and which one is a property of the service and not of
			 * the line: served on the other it would wait for
			 * traffic that never comes. */
			if (internal_dgram(sv->sv_internal) != dgram)
			{
				report(LOG_ERR,
					"%s: the internal service is `%s', not"
					" `%s %s'", f[0],
					internal_dgram(sv->sv_internal) ?
						"dgram udp wait" :
						"stream tcp nowait",
					f[1], f[3]);
				continue;
			}
		}
		else
		{
			if ((sv->sv_prog = keep(f[5])) == (char *)0)
				break;
			n = 0;
			for (i = 6; i < nf && n < MAXARGV; i++)
				if ((sv->sv_argv[n++] = keep(f[i]))
						== (char *)0)
					break;
			/* BSD's convention: the field after the path is
			 * argv[0].  A line that gives none still has to hand
			 * the program a name, so the path serves as one. */
			if (n == 0)
				sv->sv_argv[n++] = sv->sv_prog;
			sv->sv_argv[n] = (char *)0;
		}
		nserv++;
	}
	fclose(fp);
}

/*
 * Bind, and for a stream service listen, for every configured service that has
 * no socket.  One that cannot be bound is reported and left with sv_fd == -1: a
 * port already in use must not cost the others their listener.
 *
 * Called at startup, on a reload, and after the child of a datagram service has
 * exited -- which is what gives that service a socket again.  A service whose
 * socket is held by a live child (sv_pid) is skipped, so a reload cannot bind a
 * second holder of a port the machine is already answering on.
 */
static void openserv()
{
	struct sockaddr_in sin;
	struct serv *sv;
	int i, s;

	for (i = 0; i < nserv; i++)
	{
		sv = &servs[i];
		if (sv->sv_fd >= 0 || sv->sv_pid != 0)
			continue;
		if ((s = socket(AF_INET,
				sv->sv_dgram ? SOCK_DGRAM : SOCK_STREAM, 0)) < 0)
		{
			report(LOG_ERR,
				"%s: socket: errno %d", sv->sv_name,
				errno);
			continue;
		}
		memset((char *)&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = sv->sv_port;
		sin.sin_addr.s_addr = 0;		/* any local address */
		if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		{
			report(LOG_ERR,
				"%s: bind: errno %d", sv->sv_name,
				errno);
			close(s);
			continue;
		}
		/* A datagram socket is ready to receive as soon as it is bound;
		 * there is no passive open to start and nothing to accept. */
		if (!sv->sv_dgram && listen(s, 1) < 0)
		{
			report(LOG_ERR,
				"%s: listen: errno %d", sv->sv_name,
				errno);
			close(s);
			continue;
		}
		sv->sv_fd = s;
		if (opt_d)
		{
			report(LOG_DEBUG,
				"%s: listening on %s port %u",
				sv->sv_name, sv->sv_dgram ? "udp" : "tcp",
				(unsigned)ntohs(sv->sv_port));
		}
	}
}

/*
 * Give every listening socket back to the stack.
 *
 * A datagram service's socket is not held here but by a child, so that child is
 * ASKED TO STOP and collected.  Without it an exiting or reloading switchboard
 * would leave a process holding port 518 that nothing was going to talk to
 * again, and the next inetd could not bind it -- an orphan that outlives its
 * parent is the one way this design could lose a port for good.  SIGTERM is 5 on
 * this system, and the child restored the default disposition for it when it
 * became a service, so it dies where it stands: it holds no state anybody can
 * still ask about once the port is gone.
 */
static void closeserv()
{
	int i, tries, pid;

	for (i = 0; i < nserv; i++)
	{
		if (servs[i].sv_pid != 0)
		{
			(void)kill(servs[i].sv_pid, SIGTERM);
			/*
			 * Waited for, and bounded: the port is not free until the
			 * process holding it is gone, and the next openserv() has
			 * to be able to bind it.  wait() may hand back a relay
			 * instead, so this asks up to MAXKIDS + 1 times, each
			 * time for at most a second (the alarm is the only
			 * non-blocking wait there is here -- see reapidle).
			 */
			for (tries = 0; tries <= MAXKIDS; tries++)
			{
				alarm(1);
				pid = wait((int *)0);
				alarm(0);
				if (pid == servs[i].sv_pid || pid < 0)
					break;
			}
			servs[i].sv_pid = 0;
		}
		if (servs[i].sv_fd >= 0)
			close(servs[i].sv_fd);
		servs[i].sv_fd = -1;
	}
}

/*
 * Drop this process's hold on every listening socket, WITHOUT closing it.
 *
 * For a forked child, whose copies of the parent's listeners are descriptors it
 * must not keep and channels it must not destroy.  close() on a socket is
 * soclose(), which tells the stack to tear the channel down and unlinks both
 * FIFOs -- so a child tidying up after itself would take the parent's listeners
 * with it.  sockdrop() (libsocket.c) releases the descriptors and nothing else;
 * the channel survives because the parent still holds a writer on it.
 */
static void dropserv(except)
int except;
{
	int i;

	for (i = 0; i < nserv; i++)
		if (servs[i].sv_fd >= 0 && servs[i].sv_fd != except)
			(void)sockdrop(servs[i].sv_fd);
}

/*
 * Which service, if any, the child that has just been collected was holding a
 * socket for.  Clears the record and gives the service its socket back at the
 * next openserv(): the child owned it, so there is nothing here to close.
 *
 * Returns 1 for a service's own child, 0 for an ordinary relay -- which is what
 * decides whether the concurrency count moves, because a datagram service's
 * child is bounded by there being one socket and not by opt_m.  Counting it
 * would let a call in progress fill the limit and stop the switchboard answering
 * anything else for as long as somebody was talking.
 */
static int reaped(pid)
int pid;
{
	int i;

	if (pid <= 0)
		return 0;
	for (i = 0; i < nserv; i++)
		if (servs[i].sv_pid == pid)
		{
			servs[i].sv_pid = 0;
			if (opt_d)
				report(LOG_DEBUG,
					"%s: its child %d has finished; binding"
					" a fresh socket", servs[i].sv_name, pid);
			openserv();
			return 1;
		}
	return 0;
}

/*
 * What a collection was, since the two kinds of child are counted differently:
 *
 *	R_NONE		nothing was collected
 *	R_RELAY		an ordinary child, one connection's worth -- the
 *			caller's concurrency count comes down by one
 *	R_SERVICE	the child of a datagram service, which was never
 *			against that count; its socket has been rebound
 */
#define R_NONE		0
#define R_RELAY		1
#define R_SERVICE	2

/* How many datagram services have a child holding their socket. */
static int held()
{
	int i, n;

	for (i = n = 0; i < nserv; i++)
		if (servs[i].sv_pid != 0)
			n++;
	return n;
}

/*
 * How many services are waiting to be bound again -- ones whose listening
 * socket was given up because it had stopped being a socket (the accept arm
 * below), and which have turns left.
 *
 * A service that could not be bound in the first place is NOT one of these.
 * The two look alike from here and are not: a port somebody else holds at
 * startup is a decision about the machine and is reported once, where a
 * listener that was there and went is a fault this program can put right, and
 * leaving the port unanswered for the life of the process is the thing being
 * fixed.
 */
static int rebinding()
{
	int i, n;

	for (i = n = 0; i < nserv; i++)
		if (servs[i].sv_fd < 0 && servs[i].sv_pid == 0
				&& servs[i].sv_rebind > 0)
			n++;
	return n;
}

/*
 * Try, and spend a turn doing it.  openserv() binds exactly the services with
 * no socket, so this is one call whatever is owed; what is counted here is the
 * asking, so that a port that will never come back says so once and then stops
 * being mentioned.
 */
static void rebind()
{
	int i;

	openserv();
	for (i = 0; i < nserv; i++)
	{
		if (servs[i].sv_fd >= 0)
		{
			servs[i].sv_rebind = 0;
			continue;
		}
		if (servs[i].sv_pid != 0 || servs[i].sv_rebind == 0)
			continue;
		if (--servs[i].sv_rebind == 0)
			report(LOG_ERR,
				"%s: cannot be bound again after %d tries;"
				" the port is not answered",
				servs[i].sv_name, REBINDMAX);
	}
}

/*
 * Collect one child and say which kind it was.  Blocks until some child is there
 * to collect: this is the backpressure at the concurrency limit, where the
 * machine is at capacity and the next caller waits for a slot.  R_NONE means
 * there was nothing to wait for at all.
 */
static int reap()
{
	int pid;

	while ((pid = wait((int *)0)) < 0)
	{
		if (errno == EINTR)
			continue;
		return R_NONE;
	}
	return reaped(pid) ? R_SERVICE : R_RELAY;
}

/*
 * Collect a child if one is waiting to be collected, WITHOUT blocking when none
 * is, and say whether one was.
 *
 * This system has no SIGCLD and no waitpid(), so there is no wait flag to ask
 * for that: wait() is the only call and it blocks until some child dies.  What
 * bounds it is alarm(2) -- a signal delivered during the wait ends the system
 * call with EINTR, which is the pre-POSIX spelling of WNOHANG and the only one
 * available here.  A child that is ALREADY dead is collected before the alarm
 * can matter, so the second-granularity clock is paid only when every child is
 * still alive, which the caller keeps off the connection path.
 *
 * The alarm is cancelled on both ways out.  Leaving one armed would land a
 * SIGALRM in the middle of a later poll() or accept(), and the handler exists
 * to be harmless rather than to be expected.
 *
 * Without this, an exited child stays a zombie until the limit is reached --
 * opt_m - 1 of them permanently, which is what `ps' shows and what the proc
 * slots are spent on.  The alternative shape, fork()ing twice so the service is
 * a grandchild that init reparents and reaps, was not taken: the daemon then
 * never learns that a service has finished, and the concurrency limit that
 * bounds processes per connection burst goes with it.
 */
static int reapidle()
{
	int pid;

	alarm(1);
	pid = wait((int *)0);
	/* Cancelled BEFORE the collection is classified: reaped() binds a socket,
	 * and a SIGALRM landing in the middle of that bind would fail it for a
	 * reason that has nothing to do with the port. */
	alarm(0);
	if (pid < 0)
		return R_NONE;
	return reaped(pid) ? R_SERVICE : R_RELAY;
}

static void usage()
{
	fprintf(stderr, "Usage: %s [-d] [-m maxchildren] [conffile]\n",
		prog_name);
	exit(1);
}

int main(argc, argv)
int argc;
char **argv;
{
	struct pollfd set[MAXSERV];
	int idx[MAXSERV];
	int c, i, n, r, nfd, nkids, conn;
	int pollfail, accfail;
	struct serv *sv;
	struct sockaddr_in peer, loc;
	int peerlen, loclen;

	prog_name = strrchr(argv[0], '/');
	if (prog_name)
		prog_name++;
	else
		prog_name = argv[0];
	/* Every diagnostic from here on goes through report(): see it for what
	 * LOG_CONS is for and why standard error is not enough on its own. */
	openlog(prog_name, LOG_PID | LOG_CONS, LOG_DAEMON);

	opterr = 0;
	while ((c = getopt(argc, argv, "dvm:")) != EOF) switch (c) {
	case 'd':
	case 'v':
		opt_d = 1;
		break;
	case 'm':
		opt_m = atoi(optarg);
		if (opt_m < 1)
			opt_m = 1;
		break;
	default:
		usage();
	}
	if (optind < argc)
		conffile = argv[optind++];
	if (optind != argc)
		usage();

	/* A peer that goes away while a reply is being written must not take
	 * the switchboard with it. */
	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, onterm);
	signal(SIGHUP, onhup);
	/* Armed for reapidle(), which is the only thing that sets an alarm. */
	signal(SIGALRM, onalrm);

	loadconf();
	openserv();
	for (i = n = 0; i < nserv; i++)
		if (servs[i].sv_fd >= 0)
			n++;
	if (n == 0)
	{
		report(LOG_ERR,
			"no service could be started from %s",
			conffile);
		exit(1);
	}
	/*
	 * One line, always, and it names the file: a switchboard that came up
	 * on a conf file other than the one somebody edited looks exactly like
	 * one that ignored the edit.  "inetd: ready" is the prefix a scripted
	 * bring-up waits for, as "inet: ready" is the stack's, so it stays on
	 * standard output where such a script is watching -- and goes to the log
	 * as well, because the count is what says whether a service was skipped
	 * and rc.net's own output is a file nobody keeps.
	 */
	printf("inetd: ready, %d services from %s\n", n, conffile);
	fflush(stdout);
	report(LOG_NOTICE, "ready, %d services from %s", n, conffile);

	nkids = 0;
	pollfail = 0;
	accfail = 0;
	while (!want_quit)
	{
		if (want_reload)
		{
			want_reload = 0;
			report(LOG_NOTICE, "re-reading %s", conffile);
			closeserv();
			loadconf();
			openserv();
		}
		if (nkids >= opt_m)
		{
			switch (reap()) {
			case R_RELAY:
				nkids--;
				break;
			case R_NONE:
				nkids = 0;	/* nothing left to wait for */
				break;
			}
			continue;
		}

		nfd = 0;
		for (i = 0; i < nserv; i++)
		{
			if (servs[i].sv_fd < 0)
				continue;
			set[nfd].fd = servs[i].sv_fd;
			set[nfd].events = POLLIN;
			set[nfd].revents = 0;
			idx[nfd] = i;
			nfd++;
		}
		if (nfd == 0)
		{
			/*
			 * Nothing to poll.  That is the end of the switchboard
			 * only when there is nothing left that could give it a
			 * socket back, and there are two things that can.
			 *
			 * A datagram service's child holds the only socket
			 * there is: then there is something to wait for and a
			 * socket to bind again when it finishes, so this waits
			 * for the child instead of exiting.
			 */
			if (held() > 0)
			{
				(void)reap();
				continue;
			}
			/*
			 * OR THE ONE SOCKET THERE WAS HAS GONE, and it is owed
			 * a bind: the same fault as any other lost listener,
			 * and the only reason it reaches here is that it was
			 * the last service.  A switchboard that exited would
			 * turn one accept() that could not open a replacement
			 * channel into a machine with no network services at
			 * all until somebody noticed.
			 *
			 * THE PACE IS THIS ARM'S OWN, and it has to be: with
			 * nothing to poll there is no descriptor whose
			 * readiness could time the retry, so a bare rebind()
			 * here would spin as fast as the machine can bind and
			 * write a line for every turn of it.  REBINDWAIT is
			 * the idle poll turn spelled out, so a service that
			 * comes back does so no slower than one whose loss
			 * left other ports to poll.
			 *
			 * And it ends: rebind() spends a turn whether or not
			 * the bind succeeded, so a port somebody else holds is
			 * asked REBINDMAX times, says so once, and stops being
			 * one of these -- after which this arm falls through
			 * to the line below and the daemon stops.  A port that
			 * cannot be taken back does not become a daemon that
			 * asks for ever.
			 */
			if (rebinding() > 0)
			{
				sleep(REBINDWAIT);
				rebind();
				continue;
			}
			report(LOG_ERR, "no service is listening; nothing left to do");
			break;
		}
		/*
		 * poll(2), not select(2).  libc's select() converts the timeout
		 * to milliseconds and clamps it, returning early with a timeout
		 * the caller never asked for.  With INFTIM there is no timeout to
		 * get wrong, and a signal is what ends the wait.
		 *
		 * The (unsigned long) cast on the count is REQUIRED: the
		 * kernel's upoll() declares that argument unsigned long, and
		 * K&R has no prototype to widen a plain int for it.  An int
		 * pushes two bytes where four are read, so the call sees a
		 * huge descriptor count and answers EINVAL every time.
		 */
		/*
		 * INFTIM while there is nothing to collect, so an idle
		 * switchboard costs nothing at all; a timeout while children are
		 * outstanding, because the end of a service is not an event on
		 * any of these descriptors and a wait for one would never be
		 * woken by it.  Ten seconds, not one: what the timeout buys is a
		 * chance to collect a child that has already exited, and a
		 * daemon that woke every second to find its sessions still
		 * running would be paying for nothing.
		 *
		 * held(), not just nkids: the child of a datagram service is
		 * also outstanding, and until it is collected its service has no
		 * socket -- so a switchboard that waited for ever here would
		 * answer that port again only when some OTHER port had a caller.
		 * rebinding() is the same argument for a service whose socket was
		 * given up: nothing it is waiting on is a descriptor in this
		 * set, and an idle machine is exactly where it would wait for
		 * ever.
		 */
		n = poll(set, (unsigned long)nfd,
			(nkids > 0 || held() > 0 || rebinding() > 0)
				? 10000 : INFTIM);
		if (n < 0)
		{
			/*
			 * A SIGNAL is what normally ends this wait, and it is
			 * the only failure worth looping on at once: poll() does
			 * not fail transiently on a descriptor set this program
			 * built itself.  Anything else is reported, retried a
			 * second apart in case a reload is about to replace the
			 * descriptor, and after POLLFAILMAX the daemon stops.
			 */
			if (errno == EINTR)
				continue;
			report(LOG_ERR, "poll: errno %d", errno);
			if (++pollfail >= POLLFAILMAX)
			{
				report(LOG_ERR,
					"poll failed %d times running; exiting",
					pollfail);
				break;
			}
			sleep(1);
			continue;
		}
		pollfail = 0;
		if (n == 0)
		{
			/* Idle, with children outstanding: collect every one
			 * that has finished.  Off the connection path by
			 * construction -- poll() said no caller is waiting. */
			while ((nkids > 0 || held() > 0) && (r = reapidle()))
				if (r == R_RELAY)
					nkids--;
			/* And bind again for a service whose socket was given
			 * up, which is where a lost listener comes back.  Here
			 * rather than at the failure, because what a failure
			 * says is that the machine had nothing to give a moment
			 * ago: this arm is the machine with nothing to do. */
			if (rebinding() > 0)
				rebind();
			continue;
		}
		for (i = 0; i < nfd; i++)
		{
			if (!(set[i].revents & POLLIN))
				continue;
			/*
			 * A DATAGRAM SERVICE IS HANDED THE SOCKET ITSELF, once
			 * and for good.  There is nothing to accept: the first
			 * datagram is what says the service is wanted, and it is
			 * still on the channel for the child to read -- the poll
			 * above only observed the reply FIFO, it took nothing off
			 * it.  This process then sockdrop()s its copy and forgets
			 * the descriptor, because two processes cannot drive one
			 * channel: whichever reads next leaves the other's armed
			 * READ, hold and reply sequencing describing a channel
			 * that has moved on.  reaped() binds a fresh socket when
			 * the child finishes.
			 */
			if (servs[idx[i]].sv_dgram)
			{
				sv = &servs[idx[i]];
				switch (n = fork()) {
				case -1:
					/*
					 * The datagram is DISCARDED, which is
					 * what stops this arm spinning: it is
					 * still readable, and poll() would hand
					 * it back on the next turn for another
					 * failed fork and another line in the
					 * log.  UDP loses a datagram when a
					 * machine is out of processes, and a
					 * talk client repeats its request.
					 * Read here rather than in a child
					 * because this process still owns the
					 * socket -- no fork happened.
					 */
					report(LOG_ERR, "%s: fork: errno %d --"
						" the datagram is dropped",
						sv->sv_name, errno);
					(void)recvfrom(sv->sv_fd, dgdrop,
						(int)sizeof(dgdrop), 0,
						(struct sockaddr *)0, (int *)0);
					sleep(1);
					break;
				case 0:
					service(sv, sv->sv_fd,
						(struct sockaddr_in *)0,
						(struct sockaddr_in *)0);
					_exit(0);
				default:
					if (opt_d)
						report(LOG_DEBUG,
							"%s: a datagram arrived;"
							" child %d has the socket",
							sv->sv_name, n);
					(void)sockdrop(sv->sv_fd);
					sv->sv_fd = -1;
					sv->sv_pid = n;
				}
				break;
			}
			memset((char *)&peer, 0, sizeof(peer));
			memset((char *)&loc, 0, sizeof(loc));
			peerlen = sizeof(peer);
			conn = accept(servs[idx[i]].sv_fd,
				(struct sockaddr *)&peer, &peerlen);
			if (conn < 0)
			{
				/* Kept, because report() writes to the log and
				 * a write of its own is entitled to leave
				 * anything in errno. */
				int aerr = errno;

				/*
				 * RETRIED FOR EVER, BUT NOT AT ONCE.  accept()
				 * opens a fresh listening channel before it
				 * hands the connection over, so a failure here
				 * is descriptor or channel exhaustion while the
				 * machine is busy, and it clears when a session
				 * ends -- a switchboard must not give up its
				 * other services over one busy port.  poll()
				 * would report the same descriptor readable
				 * immediately, so what paces the retry is this
				 * arm: collect whatever has finished, wait a
				 * second, come round again.  The messages are
				 * bounded where the retries are not, since a
				 * lasting condition would otherwise fill the log
				 * it has to be diagnosed from.
				 */
				if (accfail < ACCFAILMAX)
				{
					report(LOG_ERR,
						"%s: accept: errno %d",
						servs[idx[i]].sv_name, aerr);
				}
				else if (accfail == ACCFAILMAX)
				{
					report(LOG_ERR,
						"further accept failures not reported"
						" until one succeeds");
				}
				accfail++;
				/*
				 * EBADF is not "busy": it says the descriptor
				 * is not a socket at all any more, which is
				 * what a lost listener looks like from here.
				 * Retrying it would fail the same way for ever
				 * and the port would stay unanswered, so the
				 * descriptor is given up and the service goes
				 * back to being one openserv() binds.
				 */
				if (aerr == EBADF)
				{
					report(LOG_ERR,
						"%s: its listening socket is"
						" gone; binding a fresh one",
						servs[idx[i]].sv_name);
					close(servs[idx[i]].sv_fd);
					servs[idx[i]].sv_fd = -1;
					servs[idx[i]].sv_rebind = REBINDMAX;
				}
				while ((nkids > 0 || held() > 0)
						&& (r = reapidle()))
					if (r == R_RELAY)
						nkids--;
				sleep(1);
				continue;
			}
			accfail = 0;
			/* Both ends of the connection, for the service to
			 * find in its environment -- see makeenv(). */
			loclen = sizeof(loc);
			(void)getsockname(conn, (struct sockaddr *)&loc,
				&loclen);
			if (opt_d)
			{
				report(LOG_DEBUG,
					"%s: connection from %s",
					servs[idx[i]].sv_name,
					inet_ntoa(peer.sin_addr));
			}
			switch (fork()) {
			case -1:
				report(LOG_ERR,
					"%s: fork: errno %d",
					servs[idx[i]].sv_name, errno);
				close(conn);
				break;
			case 0:
				service(&servs[idx[i]], conn, &loc, &peer);
				_exit(0);
			default:
				/* The connection is the child's now.  close()
				 * would destroy it for both of us -- see
				 * dropserv(). */
				(void)sockdrop(conn);
				nkids++;
			}
			/* One accept per poll: accept() replaced the listening
			 * channel underneath sv_fd, so the descriptor set is
			 * rebuilt from the table rather than reused. */
			break;
		}
	}
	closeserv();
	report(LOG_NOTICE, "exiting");
	return 0;
}

/*
 * The child, holding one connection -- or, for a datagram service, the whole
 * socket.  It owns nothing else, so the first thing it does is let go of the
 * listening sockets its fork() copied.
 *
 * close() at the end either way, and for a datagram service that is right and
 * not the dropserv() its own listener would want: the parent has already given
 * the socket up, so this process is the last holder and the stack has to be told
 * the channel is finished.  The parent binds a fresh one.
 */
static void service(sv, conn, loc, rem)
struct serv *sv;
int conn;
struct sockaddr_in *loc;
struct sockaddr_in *rem;
{
	dropserv(conn);
	signal(SIGTERM, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	if (sv->sv_internal)
		internal(sv->sv_internal, conn);
	else
		relay(sv, conn, loc, rem);
	close(conn);
}

/*
 * The internal services.  Each answers on the connection itself: no pipe, no
 * exec, no second process.
 */
static void internal(which, fd)
int which;
int fd;
{
	char buf[BUFLEN];
	char line[80];
	unsigned long secs;
	long now;
	int n, i, k;
	char *t;

	switch (which) {
	case I_NTALK:
		/* The one internal service that is a daemon rather than a
		 * canned answer: it holds this socket, and the invitation table
		 * that goes with it, until neither is wanted (net/talkserv.c). */
		talkserv(fd);
		break;

	case I_ECHO:
		while ((n = read(fd, buf, sizeof(buf))) > 0)
			if (write(fd, buf, n) != n)
				break;
		break;

	case I_DISCARD:
		while ((n = read(fd, buf, sizeof(buf))) > 0)
			;
		break;

	case I_DAYTIME:
		now = time((long *)0);
		t = ctime(&now);
		/* ctime's string ends in a newline of its own; RFC 867 wants
		 * CRLF, so the line ending is replaced and not appended. */
		for (n = 0; n < BUFLEN - 3 && t[n] && t[n] != '\n'; n++)
			buf[n] = t[n];
		buf[n++] = '\r';
		buf[n++] = '\n';
		(void)write(fd, buf, n);
		break;

	case I_TIME:
		/*
		 * RFC 868: seconds since 1 January 1900, four bytes, most
		 * significant first.  time(2) counts from 1970 and the
		 * difference is 2,208,988,800 -- which does NOT fit in this
		 * machine's signed long, so it is written in hex and the
		 * arithmetic done unsigned.  A signed constant would have
		 * wrapped negative and put the wrong century on the wire.
		 */
		secs = (unsigned long)time((long *)0) + 0x83AA7E80L;
		buf[0] = (char)((secs >> 24) & 0xFF);
		buf[1] = (char)((secs >> 16) & 0xFF);
		buf[2] = (char)((secs >> 8) & 0xFF);
		buf[3] = (char)(secs & 0xFF);
		(void)write(fd, buf, 4);
		break;

	case I_CHARGEN:
		/* RFC 864: 72-character lines from the printable ASCII range,
		 * each starting one character further along.  It ends when the
		 * peer stops reading, which is a failed write. */
		for (k = 0; ; k = (k + 1) % 95)
		{
			for (i = 0; i < 72; i++)
				line[i] = ' ' + ((k + i) % 95);
			line[72] = '\r';
			line[73] = '\n';
			if (write(fd, line, 74) != 74)
				break;
		}
		break;
	}
}

/*
 * THE PEER ADDRESS, HANDED OVER IN THE ENVIRONMENT.
 *
 * The relay costs the service the one thing a BSD-spawned service takes for
 * granted: its standard input is a pipe, so getpeername(0) and
 * ioctl(0, NWIOGTCPCONF) both fail and the program cannot find out who is at
 * the other end.  That is not a cosmetic loss.  ftpd needs the local address to
 * open a data connection (cmd/ftpd net.c) and rlogind needs the remote one to
 * authenticate against .rhosts (setup.c) -- a server that cannot ask is a
 * server that cannot work.
 *
 * So the four numbers are put in the environment, which survives exec and costs
 * nothing to read.  THESE FOUR NAMES ARE THE INTERFACE that services read
 * instead of asking the descriptor:
 *
 *	INETD_LOCADDR	this machine's address for this connection, dotted quad
 *	INETD_LOCPORT	the service's port, decimal
 *	INETD_REMADDR	the peer's address, dotted quad
 *	INETD_REMPORT	the peer's port, decimal
 *
 * They are set only for a connection inetd is serving, so their PRESENCE is
 * also how a program tells that it was started by the switchboard rather than
 * from a shell -- the question `getsockname(0)' answers on BSD, and the one
 * every dual-mode daemon asks first.
 *
 * execve() with a built list rather than putenv(): the environment has to be
 * this child's alone, and building the list makes that plain.  Anything that
 * does not fit is dropped rather than truncated -- a half-written address is
 * worse than an absent one, because a program that finds the variable believes
 * it.
 */
#define NENVX		4		/* variables added here		*/
#define ENVLEN		32		/* NAME=value, longest is an	*/
					/* address: 13 + 15 + NUL	*/

static char envbuf[NENVX][ENVLEN];
static char *newenv[40];

static char **makeenv(loc, rem)
struct sockaddr_in *loc;
struct sockaddr_in *rem;
{
	extern char **environ;
	int n, i;

	n = 0;
	for (i = 0; environ != (char **)0 && environ[i] != (char *)0; i++)
	{
		if (n >= (int)(sizeof(newenv)/sizeof(newenv[0])) - NENVX - 1)
			break;
		/* Any INETD_ variable inherited from whoever started this
		 * daemon is dropped: the connection's own answer must be the
		 * only one a service can find. */
		if (strncmp(environ[i], "INETD_", 6) == 0)
			continue;
		newenv[n++] = environ[i];
	}
	sprintf(envbuf[0], "INETD_LOCADDR=%s", inet_ntoa(loc->sin_addr));
	sprintf(envbuf[1], "INETD_LOCPORT=%u", (unsigned)ntohs(loc->sin_port));
	sprintf(envbuf[2], "INETD_REMADDR=%s", inet_ntoa(rem->sin_addr));
	sprintf(envbuf[3], "INETD_REMPORT=%u", (unsigned)ntohs(rem->sin_port));
	for (i = 0; i < NENVX; i++)
		newenv[n++] = envbuf[i];
	newenv[n] = (char *)0;
	return newenv;
}

/*
 * Copy up to one buffer from `from' to `to'.  Returns 1 while both ends are
 * good, 0 at end of file, -1 on an error or a short write.
 */
static int copyout(from, to)
int from;
int to;
{
	char buf[BUFLEN];
	int n;

	if ((n = read(from, buf, sizeof(buf))) < 0)
		return -1;
	if (n == 0)
		return 0;
	return (write(to, buf, n) == n) ? 1 : -1;
}

/*
 * Read whatever a service has written on its standard error and file it, a line
 * at a time, under the service's name.  Returns 1 while the pipe is open, 0 at
 * end of file -- which is when the program has exited.
 *
 * A LINE AT A TIME, because a record is what a log is made of: what arrives here
 * is one read of a pipe and may hold several lines or half of one, so the tail is
 * carried forward between calls.  The line buffer is static for that reason and
 * because the relay's stack is small.
 */
static char pending[LINELEN];		/* a line being assembled from the pipe */
static int npend;

/* File what has been assembled, if anything, and start the next line. */
static void errline(name)
char *name;
{
	if (npend > 0)
	{
		pending[npend] = '\0';
		report(LOG_ERR, "%s: %s", name, pending);
		npend = 0;
	}
}

static int drainerr(fd, name)
int fd;
char *name;
{
	char buf[BUFLEN];
	int n, i;

	if ((n = read(fd, buf, sizeof(buf))) <= 0)
	{
		errline(name);
		return 0;
	}
	for (i = 0; i < n; i++)
	{
		if (buf[i] == '\n' || buf[i] == '\r')
		{
			errline(name);
			continue;
		}
		/* A line longer than the buffer is filed in pieces and the
		 * character that overflowed begins the next one: a message about
		 * a service that will not start is worth having in two halves,
		 * and worth having all of. */
		if (npend >= (int)sizeof(pending) - 1)
			errline(name);
		pending[npend++] = buf[i];
	}
	return 1;
}

/*
 * Run an external service on a pipe, and shuttle bytes between it and the
 * connection.
 *
 * THREE pipes, because a pipe carries one direction: `down' takes the peer's
 * bytes to the program's standard input, `up' brings the program's output back,
 * and `err' brings its standard error to syslog and NOT to the peer.  On BSD
 * standard error is the socket, and over a relay it would be the connection just
 * the same -- so a service reporting that it cannot start would deliver its
 * complaint to the caller as protocol, and to nobody who could act on it.  The
 * third pipe is what makes a failure reportable at all (drainerr()).
 *
 * This process is the relay and stays; the fork below is the service.  Waiting
 * on the OUTPUT pipe rather than on the child is what closes the connection at
 * the right moment: the pipe reaches end of file when the last copy of its
 * write end is closed, which is when the program exits.
 */
static void relay(sv, conn, loc, rem)
struct serv *sv;
int conn;
struct sockaddr_in *loc;
struct sockaddr_in *rem;
{
	struct pollfd set[3];
	int down[2], up[2], err[2];
	int n, nfd, i, net, pipein, pipeout, pipeerr;

	if (pipe(down) < 0 || pipe(up) < 0 || pipe(err) < 0)
	{
		report(LOG_ERR, "%s: pipe: errno %d", sv->sv_name, errno);
		return;
	}
	switch (fork()) {
	case -1:
		report(LOG_ERR, "%s: fork: errno %d", sv->sv_name, errno);
		return;
	case 0:
		/* The service.  sockdrop rather than close on the connection:
		 * this process letting go of it must not tear the channel down
		 * under the relay. */
		(void)sockdrop(conn);
		close(down[1]);
		close(up[0]);
		close(err[0]);
		dup2(down[0], 0);
		dup2(up[1], 1);
		dup2(err[1], 2);
		if (down[0] > 2)
			close(down[0]);
		if (up[1] > 2)
			close(up[1]);
		if (err[1] > 2)
			close(err[1]);
		if (sv->sv_gid)
			setgid(sv->sv_gid);
		if (sv->sv_uid)
			setuid(sv->sv_uid);
		execve(sv->sv_prog, sv->sv_argv, makeenv(loc, rem));
		/* execve failed, and standard error is now the pipe the relay
		 * files: the caller gets nothing and the log gets the reason. */
		fprintf(stderr, "%s: cannot execute, errno %d\n",
			sv->sv_prog, errno);
		fflush(stderr);
		_exit(1);
	}
	close(down[0]);
	close(up[1]);
	close(err[1]);
	pipein = up[0];			/* the program's output	*/
	pipeout = down[1];		/* the program's input	*/
	pipeerr = err[0];		/* its diagnostics	*/
	net = conn;

	while (pipein >= 0)
	{
		/*
		 * Bytes already taken off the channel by an earlier operation
		 * are not a readability event, so a loop that only ever
		 * poll()ed would wait for data it is already holding.
		 * sockheld() is what closes that gap; libsocket documents it.
		 */
		if (net >= 0 && sockheld(net) > 0)
		{
			if (copyout(net, pipeout) <= 0)
			{
				close(pipeout);
				pipeout = -1;
				net = -1;
			}
			continue;
		}
		nfd = 0;
		if (net >= 0)
		{
			set[nfd].fd = net;
			set[nfd].events = POLLIN;
			set[nfd].revents = 0;
			nfd++;
		}
		set[nfd].fd = pipein;
		set[nfd].events = POLLIN;
		set[nfd].revents = 0;
		nfd++;
		if (pipeerr >= 0)
		{
			set[nfd].fd = pipeerr;
			set[nfd].events = POLLIN;
			set[nfd].revents = 0;
			nfd++;
		}
		if ((n = poll(set, (unsigned long)nfd, INFTIM)) < 0)
		{
			/*
			 * EINTR is the ordinary end of this wait and the only
			 * one to loop on: these descriptors are this process's
			 * own, so any other failure is permanent, and one
			 * connection's failure ends one connection.
			 */
			if (errno == EINTR)
				continue;
			report(LOG_ERR,
				"%s: relay poll: errno %d",
				sv->sv_name, errno);
			break;
		}
		for (i = 0; i < nfd; i++)
		{
			if (!(set[i].revents & (POLLIN|POLLHUP|POLLERR)))
				continue;
			if (set[i].fd == pipeerr)
			{
				/* Filed, never copied to the connection. */
				if (!drainerr(pipeerr, sv->sv_name))
				{
					close(pipeerr);
					pipeerr = -1;
				}
			}
			else if (set[i].fd == pipein)
			{
				if (copyout(pipein, net) <= 0)
				{
					close(pipein);
					pipein = -1;
					break;
				}
			}
			else
			{
				/* The peer has finished talking.  The program
				 * is told by end of file on its standard
				 * input; its output is still wanted, so the
				 * loop goes on until the pipe closes. */
				if (copyout(net, pipeout) <= 0)
				{
					close(pipeout);
					pipeout = -1;
					net = -1;
				}
			}
		}
	}
	if (pipeout >= 0)
		close(pipeout);
	(void)wait((int *)0);
	/*
	 * Anything the program wrote on its standard error just before it exited
	 * is still in the third pipe, and that is exactly the message worth
	 * having -- a service that fails says so on its last line.  Drained AFTER
	 * the wait above, so the only writer is gone and end of file is certain
	 * rather than something to wait for.
	 */
	while (pipeerr >= 0 && drainerr(pipeerr, sv->sv_name))
		;
	if (pipeerr >= 0)
		close(pipeerr);
}
