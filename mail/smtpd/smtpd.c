/*
 * smtpd -- receive mail over TCP and deliver it into the local mailboxes.
 *
 *	smtpd [-d] [-m maxsessions] [-p service]
 *
 * It speaks the RFC 821 minimum -- HELO/EHLO, MAIL, RCPT, DATA, RSET, NOOP,
 * QUIT -- and hands each accepted message to the same mailbox that mail(1)'s
 * usend() writes (cmd/mail/send.c): /usr/spool/mail/<user>, appended, with
 * a `From ' envelope line ahead of the message and the \1\1 line after it that
 * mail(1) reads as the message separator (cmd/mail/mail.c:458,480).
 * Delivery takes mail(1)'s /tmp/maillock<uid> lock for the same reason mail(1)
 * does, so a reader rewriting the box and this daemon appending to it cannot
 * interleave.
 *
 * TWO MODES, ONE BINARY.
 *
 * STANDALONE: the LISTENER is cmd/fingerd/fingerd.c's, verbatim in shape:
 * pre-forked children, one passive open each.  A connection on this system is a
 * pair of FIFOs plus the framing state in `struct ichan', none of which survives
 * an exec(2), so no super-server can hand this program a connection on fd 0.
 * Each waiting child holds one of the daemon's fifteen connections for as long
 * as it waits, so -m is a charge against that budget and not just a
 * concurrency limit (see /etc/rc.net).
 *
 * INETD MODE: because a connection cannot be exec'd, /etc/inetd runs a service
 * on a PIPE and relays -- the conversation arrives on standard input and the
 * answers go out on standard output, one connection, then this exits.  It is
 * selected by the presence of the switchboard's INETD_* variables in the
 * environment (INETD_LOCADDR, INETD_LOCPORT, INETD_REMADDR, INETD_REMPORT),
 * which is the only way a program on a pipe can tell: 0 is not
 * the connection, so getpeername(0) and ioctl(0, NWIOGTCPCONF) both fail.
 *
 * SMTP IS LINE AT A TIME IN BOTH DIRECTIONS and nothing here buffers, which is
 * what makes the pipe relay work: netline() reads a byte at a time and every
 * answer is a write(2).  A daemon that collected its output in stdio would not
 * answer until it exited, and would look like a hung relay.
 *
 * WHAT THIS PROGRAM WRITES ON STANDARD ERROR -- every diagnostic here, including
 * -d's transcript -- is not seen by the caller: under inetd, standard error is a
 * pipe of its own that the switchboard files with syslog(3) under the service's
 * name (net/inetd.c drainerr).  Only what goes to `out' is on the connection.
 *
 * A recipient is validated with getpwnam() after the alias file is consulted;
 * the domain part of RCPT TO is discarded, so this accepts mail for any domain
 * naming a local user.  That is deliberate for a private LAN and wrong for a
 * machine facing the Internet: there is no relaying, no authentication and no
 * queue -- a message this program accepts is delivered before it answers 250,
 * and one it cannot deliver is refused rather than retried.  A message is
 * accepted at the `.' that ends DATA and at no other point: a transfer whose
 * connection ends inside the body is discarded whole, unanswered, and the
 * sender that was never told 250 sends it again.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

#define	SPOOLDIR	"/usr/spool/mail/"
#define	ALIASES		"/usr/lib/mail/aliases"
#define	MAXRCPT		8
#define	LINESZ		1024

/*
 * Mailbox lock: LOCKTRY seconds is how long delivery waits for a lock another
 * process holds before it gives the message up.  SPOOLTRY is how many names
 * the spool tries before it reports that it has no spool to write in.
 */
#define	LOCKTRY		30
#define	SPOOLTRY	32

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
extern char *strrchr(), *strchr(), *ctime();
extern struct passwd *getpwnam();
extern long time();

static char *prog_name;
static int opt_d = 0;
static int opt_m = 1;

static char myname[64];
static char sender[256];
static char rcpt[MAXRCPT][64];
static int nrcpt;
static char line[LINESZ];
static char lockname[64];
static int locked;

static usage();
static serve();
static session();
static netline();
static netput();
static reply();
static deliver();
static mlock();
static munlock();
static FILE *spoolopen();
static char *aliasof();

main(argc, argv)
int argc;
char *argv[];
{
	int c, nkids, nfail, status;
	char *service;
	struct servent *servent;
	tcpport_t port;

	prog_name = strrchr(argv[0], '/');
	prog_name = prog_name ? prog_name + 1 : argv[0];
	service = "smtp";

	while ((c = getopt(argc, argv, "dm:p:")) != EOF) switch (c) {
	case 'd':	opt_d = 1; break;
	case 'm':	opt_m = atoi(optarg); break;
	case 'p':	service = optarg; break;
	default:	usage();
	}
	if (optind != argc)
		usage();
	if (opt_m < 1)
		usage();

	if (gethostname(myname, sizeof(myname)) < 0)
		strcpy(myname, "localhost");

	/*
	 * INETD MODE: the connection is already on 0 and 1, so there is no port
	 * to look up and no socket to open.  Tested before the getservbyname()
	 * below for that reason -- inetd owns the listening socket, and a machine
	 * with no /etc/services must still be able to serve a caller inetd has
	 * already accepted.  -m and -p mean nothing in this mode.
	 */
	if (getenv("INETD_REMADDR") != (char *)0) {
		session(0, 1);
		exit(0);
	}

	if ((servent = getservbyname(service, "tcp")) != (struct servent *)0)
		port = servent->s_port;
	else
		port = htons((tcpport_t) 25);

	nkids = 0;
	nfail = 0;
	for (;;) {
		while (nkids < opt_m) {
			switch (fork()) {
			case -1:
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
		 * The status distinguishes a child that answered a caller from
		 * one that never got a connection to answer on.  EINTR is not a
		 * dead child, so nothing is decremented for it.
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

static usage()
{
	fprintf(stderr, "usage: %s [-d] [-m maxsessions] [-p service]\n",
		prog_name);
	exit(1);
}

static serve(port)
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
		exit(EX_SETUP);
	}
	memset((char *)&tcpconf, 0, sizeof(tcpconf));
	tcpconf.nwtc_flags = NWTC_SHARED | NWTC_LP_SET |
			     NWTC_UNSET_RA | NWTC_UNSET_RP;
	tcpconf.nwtc_locport = port;
	if (ioctl(net_fd, NWIOSTCPCONF, (char *)&tcpconf) < 0) {
		fprintf(stderr, "%s: configure: %s\n", prog_name,
			strerror(errno));
		exit(EX_SETUP);
	}
	memset((char *)&tcpopt, 0, sizeof(tcpopt));
	tcpopt.nwto_flags = NWTO_DEL_RST;
	(void) ioctl(net_fd, NWIOSTCPOPT, (char *)&tcpopt);

	memset((char *)&tcplistenopt, 0, sizeof(tcplistenopt));
	if (ioctl(net_fd, NWIOTCPLISTEN, (char *)&tcplistenopt) < 0) {
		fprintf(stderr, "%s: listen: %s\n", prog_name,
			strerror(errno));
		exit(EX_SETUP);
	}
	if (opt_d)
		fprintf(stderr, "%s: connected\n", prog_name);
	session(net_fd, net_fd);
	(void) ioctl(net_fd, NWIOTCPSHUTDOWN, (char *)0);
	(void) close(net_fd);
	return 0;
}

/*
 * One SMTP conversation: RFC 821 minimum implementation.
 *
 * Commands are read from `in' and answers written to `out'.  Two descriptors and
 * not one because in inetd mode they are two different pipes; standalone they
 * are both the connection.
 */
static session(in, out)
int in;
int out;
{
	char *cp, *ep;
	FILE *tf;
	char tname[64];
	int inbody, badspool;

	sender[0] = '\0';
	nrcpt = 0;

	reply(out, "220 ");
	while (netline(in, line, sizeof(line)) > 0) {
		for (cp = line; *cp; cp++)
			if (*cp == '\r' || *cp == '\n') { *cp = '\0'; break; }
		if (opt_d)
			fprintf(stderr, "%s: <- %s\n", prog_name, line);

		if (strncmp(line, "HELO", 4) == 0 ||
		    strncmp(line, "helo", 4) == 0 ||
		    strncmp(line, "EHLO", 4) == 0 ||
		    strncmp(line, "ehlo", 4) == 0) {
			netput(out, "250 ok\r\n", 8);
			continue;
		}
		if (strncmp(line, "MAIL", 4) == 0 ||
		    strncmp(line, "mail", 4) == 0) {
			if ((cp = strchr(line, '<')) != (char *)0) {
				cp++;
				if ((ep = strchr(cp, '>')) != (char *)0)
					*ep = '\0';
				strncpy(sender, cp, sizeof(sender) - 1);
				sender[sizeof(sender) - 1] = '\0';
			}
			nrcpt = 0;
			netput(out, "250 ok\r\n", 8);
			continue;
		}
		if (strncmp(line, "RCPT", 4) == 0 ||
		    strncmp(line, "rcpt", 4) == 0) {
			if (nrcpt >= MAXRCPT) {
				netput(out, "452 too many recipients\r\n", 25);
				continue;
			}
			if ((cp = strchr(line, '<')) == (char *)0) {
				netput(out, "501 syntax\r\n", 12);
				continue;
			}
			cp++;
			if ((ep = strchr(cp, '>')) != (char *)0)
				*ep = '\0';
			if ((ep = strchr(cp, '@')) != (char *)0)
				*ep = '\0';		/* local part only */
			if ((ep = aliasof(cp)) != (char *)0)
				cp = ep;
			if (getpwnam(cp) == (struct passwd *)0) {
				netput(out, "550 no such user\r\n", 18);
				continue;
			}
			strncpy(rcpt[nrcpt], cp, sizeof(rcpt[0]) - 1);
			rcpt[nrcpt][sizeof(rcpt[0]) - 1] = '\0';
			nrcpt++;
			netput(out, "250 ok\r\n", 8);
			continue;
		}
		if (strncmp(line, "DATA", 4) == 0 ||
		    strncmp(line, "data", 4) == 0) {
			if (nrcpt == 0) {
				netput(out, "503 need RCPT\r\n", 15);
				continue;
			}
			if ((tf = spoolopen(tname)) == (FILE *)0) {
				netput(out, "451 no spool\r\n", 14);
				continue;
			}
			netput(out, "354 send it\r\n", 13);
			inbody = 1;
			while (netline(in, line, sizeof(line)) > 0) {
				for (cp = line; *cp; cp++)
					if (*cp=='\r'||*cp=='\n') {
						*cp = '\0';
						break;
					}
				if (strcmp(line, ".") == 0) {
					inbody = 0;
					break;
				}
				cp = line;
				if (*cp == '.')		/* undo dot-stuff */
					cp++;
				fprintf(tf, "%s\n", cp);
			}
			(void) fflush(tf);
			badspool = ferror(tf);
			if (fclose(tf) != 0)
				badspool = 1;
			/*
			 * RFC 821 accepts a message at the end-of-mail-data
			 * line and nowhere else, so a body the sender stopped
			 * sending is not a message.  `inbody' still set is the
			 * other way out of that loop: a zero-length read, which
			 * is how a peer that went away without saying so is
			 * announced -- inetd's relay closes this program's
			 * standard input when the connection is gone, and a
			 * transport read returns the same 0.
			 *
			 * The fragment is DISCARDED rather than delivered with
			 * a mark on it.  A reader of the mailbox has nothing
			 * but the text to judge by, and a fragment written
			 * under the same `From ' envelope and the same \1\1
			 * separator as a finished letter reads as a finished
			 * letter; a warning added inside the body is a line the
			 * sender could equally have written.  The sender was
			 * never told 250, so a client still alive is required
			 * to send the whole message again, and a fragment kept
			 * here would make that arrival a second copy of
			 * something nobody wrote.
			 *
			 * Nothing is answered: there is no peer left to answer,
			 * and this ends the session.  The reason goes on
			 * standard error, which inetd files with syslog(3).
			 */
			if (inbody) {
				unlink(tname);
				fprintf(stderr,
					"%s: DATA from %s ended without the"
					" terminating `.': %d recipient(s),"
					" nothing delivered\n",
					prog_name,
					sender[0] ? sender : "<>", nrcpt);
				nrcpt = 0;
				return 0;
			}
			/*
			 * 250 is a promise that the message is stored, so it
			 * is answered only after every write, flush and close
			 * has succeeded, and the spool is removed only then.
			 * A message the spool or a mailbox refused stays in
			 * /tmp under the name on standard error, and the
			 * sender is told to send it again.
			 */
			if (badspool) {
				unlink(tname);
				netput(out, "451 spool write failed\r\n", 24);
				nrcpt = 0;
				continue;
			}
			if (deliver(tname) == 0) {
				netput(out, "250 accepted\r\n", 14);
				unlink(tname);
			} else {
				netput(out, "451 delivery failed\r\n", 21);
				fprintf(stderr,
					"%s: delivery failed, message held"
					" in %s\n", prog_name, tname);
			}
			nrcpt = 0;
			continue;
		}
		if (strncmp(line, "RSET", 4) == 0 ||
		    strncmp(line, "rset", 4) == 0) {
			nrcpt = 0; sender[0] = '\0';
			netput(out, "250 ok\r\n", 8);
			continue;
		}
		if (strncmp(line, "NOOP", 4) == 0 ||
		    strncmp(line, "noop", 4) == 0) {
			netput(out, "250 ok\r\n", 8);
			continue;
		}
		if (strncmp(line, "QUIT", 4) == 0 ||
		    strncmp(line, "quit", 4) == 0) {
			netput(out, "221 bye\r\n", 9);
			return 0;
		}
		netput(out, "500 unknown\r\n", 13);
	}
	return 0;
}

/*
 * Append the spooled message to each recipient's mailbox in the format
 * mail(1) reads: a `From ' envelope line, the message, then \1\1 alone on a
 * line.  The box is chowned to the recipient because this daemon runs as root
 * and mail(1) rewrites the box as the invoking user.
 */
static deliver(tname)
char *tname;
{
	FILE *in, *out;
	struct passwd *pwp;
	char box[128];
	long now;
	int i, c, fd, bad;

	for (i = 0; i < nrcpt; i++) {
		if ((pwp = getpwnam(rcpt[i])) == (struct passwd *)0)
			return 1;
		if ((in = fopen(tname, "r")) == (FILE *)0)
			return 1;
		sprintf(box, "%s%s", SPOOLDIR, rcpt[i]);
		if (mlock(pwp->pw_uid) != 0) {
			fclose(in);
			return 1;
		}
		/*
		 * A mailbox this daemon creates is the recipient's alone: mail
		 * is opened with the private mode rather than left to the
		 * umask of whatever started the daemon.
		 */
		out = (FILE *)0;
		if ((fd = open(box, O_WRONLY | O_CREAT | O_APPEND, 0600)) >= 0
		    && (out = fdopen(fd, "a")) == (FILE *)0)
			close(fd);
		if (out == (FILE *)0) {
			munlock();
			fclose(in);
			return 1;
		}
		time(&now);
		fprintf(out, "From %s %s",
			sender[0] ? sender : "MAILER-DAEMON", ctime(&now));
		while ((c = getc(in)) != EOF)
			putc(c, out);
		fprintf(out, "\1\1\n");
		/*
		 * A buffered write reports its failure at the flush or the
		 * close, so both are examined before this message counts as
		 * stored; a mailbox that took only part of it is a failure.
		 */
		(void) fflush(out);
		bad = ferror(out) || ferror(in);
		if (fclose(out) != 0)
			bad = 1;
		fclose(in);
		if (bad) {
			munlock();
			fprintf(stderr, "%s: %s: %s\n", prog_name, box,
				strerror(errno));
			return 1;
		}
		chown(box, pwp->pw_uid, pwp->pw_gid);
		munlock();
	}
	return 0;
}

/*
 * mail(1)'s mailbox lock, same name and the same file (cmd/mail/util.c), taken
 * with an exclusive create so that the holder is whoever made the file.  A lock
 * another process holds is waited for, for LOCKTRY seconds, and then the
 * message is given up: it is not broken, and munlock() removes only a lock this
 * process created, because the process that made it is still using the box.
 * Returns 0 holding the lock, -1 holding nothing.
 */
static mlock(uid)
int uid;
{
	int fd, spun;

	sprintf(lockname, "/tmp/maillock%d", uid);
	for (spun = 0; ; spun++) {
		if ((fd = open(lockname, O_WRONLY | O_CREAT | O_EXCL, 0)) >= 0) {
			close(fd);
			locked = 1;
			return 0;
		}
		if (errno != EEXIST || spun >= LOCKTRY)
			return -1;
		sleep(1);
	}
}

static munlock()
{
	if (locked)
		unlink(lockname);
	locked = 0;
	return 0;
}

/*
 * The spool file that holds one message between the end of DATA and delivery.
 * Created with an exclusive create and a private mode under a name that the
 * caller cannot work out in advance, so a name planted in /tmp beforehand
 * cannot become this file and the body is readable only by the daemon's owner.
 * The name is left in `name' for the caller to unlink.
 */
static FILE *spoolopen(name)
char *name;
{
	FILE *fp;
	unsigned key;
	int fd, try;
	long now;

	time(&now);
	key = (unsigned) now ^ ((unsigned) getpid() << 3);
	for (try = 0; try < SPOOLTRY; try++, key += 7919) {
		sprintf(name, "/tmp/smtp%u", key);
		if ((fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0600)) < 0) {
			if (errno != EEXIST)
				break;
			continue;
		}
		if ((fp = fdopen(fd, "w")) != (FILE *)0)
			return fp;
		close(fd);
		unlink(name);
		break;
	}
	name[0] = '\0';
	return (FILE *)0;
}

/* /usr/lib/mail/aliases: "name: target" one per line. */
static char *aliasof(who)
char *who;
{
	static char buf[256];
	static char target[64];
	FILE *fp;
	char *cp;

	if ((fp = fopen(ALIASES, "r")) == (FILE *)0)
		return (char *)0;
	while (fgets(buf, sizeof(buf), fp) != (char *)0) {
		if (*buf == '#')
			continue;
		if ((cp = strchr(buf, ':')) == (char *)0)
			continue;
		*cp++ = '\0';
		if (strcmp(buf, who) != 0)
			continue;
		while (*cp == ' ' || *cp == '\t')
			cp++;
		strncpy(target, cp, sizeof(target) - 1);
		target[sizeof(target) - 1] = '\0';
		for (cp = target; *cp; cp++)
			if (*cp == '\n' || *cp == ',' || *cp == ' ') {
				*cp = '\0';
				break;
			}
		fclose(fp);
		return target;
	}
	fclose(fp);
	return (char *)0;
}

static reply(fd, code)
int fd;
char *code;
{
	char b[128];

	sprintf(b, "%s%s COHERENT SMTP ready\r\n", code, myname);
	netput(fd, b, strlen(b));
	return 0;
}

/*
 * Read one CRLF line off the connection.  A positive return is a whole line,
 * terminated by the newline it ends with or by the size of the buffer; 0 is
 * end of input and -1 is a read error, and in both of those the bytes that had
 * arrived are no line at all.  The two are distinguished because a caller that
 * cannot tell them apart reads a partial line as a complete one -- the buffer
 * still holds the terminator of the line before it -- and a body cut short
 * after a lone `.' would count as the end of DATA.  The buffer is terminated
 * on every path so that no caller ever reads what the previous line left.
 */
static netline(fd, buf, size)
int fd;
char *buf;
int size;
{
	int n, i;
	char c;

	i = 0;
	while (i < size - 1) {
		n = read(fd, &c, 1);
		if (n <= 0) {
			buf[i] = '\0';
			return n < 0 ? -1 : 0;
		}
		buf[i++] = c;
		if (c == '\n')
			break;
	}
	buf[i] = '\0';
	return i;
}

static netput(fd, buf, len)
int fd;
char *buf;
int len;
{
	int n;

	while (len > 0) {
		if ((n = write(fd, buf, len)) <= 0)
			return -1;
		buf += n;
		len -= n;
	}
	return 0;
}
