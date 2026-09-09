/*
 * smtpsend -- hand a message to a remote host's SMTP server.
 *
 *	smtpsend [-f sender] user@host [user@host ...] < message
 *
 * The outbound half of this machine's mail transport.  mail(1)'s usend()
 * invokes it on a pipe for any recipient containing an `@', in the place and
 * in the manner that rsend() already invokes uux for a recipient containing a
 * `!' (cmd/mail/send.c).  The message arrives on standard input with its
 * headers already written by mail(1)'s build_header().
 *
 * Host resolution is gethostbyname(), so on a LAN /etc/hosts answers and no
 * nameserver is needed.  There is no MX lookup: a name resolves to an address
 * and the mail goes to port 25 there.
 *
 * There is no queue.  A host that does not answer, or a server that refuses a
 * recipient, is reported to the caller by a non-zero exit status -- mail(1)
 * then saves the letter in the sender's dead.letter -- and nothing is retried.
 *
 * One connection per recipient rather than one per host: on a LAN of C900s a
 * message has one recipient far more often than not, and stdin is rewound
 * between recipients, which requires it to be a file rather than a pipe when
 * more than one is given.
 */
#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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

extern int optind;
extern char *optarg;
extern char *strchr(), *strrchr();
extern struct hostent *gethostbyname();

static char *prog_name;
static char *sender = "postmaster";
static char myname[64];
static char line[1024];

static int connectto();
static int expect();
static int sendcmd();
static int sendbody();

main(argc, argv)
int argc;
char *argv[];
{
	int c, fd, i, rc;
	char *at, *host;

	prog_name = strrchr(argv[0], '/');
	prog_name = prog_name ? prog_name + 1 : argv[0];

	while ((c = getopt(argc, argv, "f:")) != EOF) switch (c) {
	case 'f':	sender = optarg; break;
	default:
		fprintf(stderr, "usage: %s [-f sender] user@host ...\n",
			prog_name);
		exit(1);
	}
	if (optind >= argc) {
		fprintf(stderr, "%s: no recipients\n", prog_name);
		exit(1);
	}
	if (gethostname(myname, sizeof(myname)) < 0)
		strcpy(myname, "localhost");

	rc = 0;
	for (i = optind; i < argc; i++) {
		if ((at = strchr(argv[i], '@')) == (char *)0) {
			fprintf(stderr, "%s: %s: not user@host\n",
				prog_name, argv[i]);
			rc = 1;
			continue;
		}
		*at = '\0';
		host = at + 1;
		if ((fd = connectto(host)) < 0) {
			*at = '@';
			rc = 1;
			continue;
		}
		sprintf(line, "HELO %s\r\n", myname);
		if (expect(fd, "220") != 0 ||
		    sendcmd(fd, line, "250") != 0)
			{ rc = 1; goto drop; }
		sprintf(line, "MAIL FROM:<%s>\r\n", sender);
		if (sendcmd(fd, line, "250") != 0) { rc = 1; goto drop; }
		sprintf(line, "RCPT TO:<%s@%s>\r\n", argv[i], host);
		if (sendcmd(fd, line, "250") != 0) { rc = 1; goto drop; }
		if (sendcmd(fd, "DATA\r\n", "354") != 0) { rc = 1; goto drop; }
		if (sendbody(fd) != 0 ||
		    sendcmd(fd, ".\r\n", "250") != 0)
			rc = 1;
		(void) sendcmd(fd, "QUIT\r\n", "221");
drop:
		(void) ioctl(fd, NWIOTCPSHUTDOWN, (char *)0);
		(void) close(fd);
		*at = '@';
		rewind(stdin);
	}
	exit(rc);
}

/* Active open to host:25 over /dev/tcp, the shape tests/net/devtcp.c uses. */
static int connectto(host)
char *host;
{
	int fd;
	char *tcp_device;
	struct hostent *hp;
	struct servent *sp;
	nwio_tcpconf_t tcpconf;
	nwio_tcpcl_t tcpcl;
	ipaddr_t addr;
	tcpport_t port;

	if ((hp = gethostbyname(host)) == (struct hostent *)0) {
		fprintf(stderr, "%s: %s: unknown host\n", prog_name, host);
		return -1;
	}
	memcpy((char *)&addr, hp->h_addr, sizeof(addr));

	if ((sp = getservbyname("smtp", "tcp")) != (struct servent *)0)
		port = sp->s_port;
	else
		port = htons((tcpport_t) 25);

	if ((tcp_device = getenv("TCP_DEVICE")) == (char *)0)
		tcp_device = TCP_DEVICE;
	if ((fd = open(tcp_device, O_RDWR)) < 0) {
		fprintf(stderr, "%s: %s: %s\n", prog_name, tcp_device,
			strerror(errno));
		return -1;
	}
	memset((char *)&tcpconf, 0, sizeof(tcpconf));
	tcpconf.nwtc_flags = NWTC_LP_SEL | NWTC_SET_RA | NWTC_SET_RP;
	tcpconf.nwtc_remaddr = addr;
	tcpconf.nwtc_remport = port;
	if (ioctl(fd, NWIOSTCPCONF, (char *)&tcpconf) < 0) {
		fprintf(stderr, "%s: configure: %s\n", prog_name,
			strerror(errno));
		close(fd);
		return -1;
	}
	memset((char *)&tcpcl, 0, sizeof(tcpcl));
	tcpcl.nwtcl_flags = 0;
	if (ioctl(fd, NWIOTCPCONN, (char *)&tcpcl) < 0) {
		fprintf(stderr, "%s: connect %s: %s\n", prog_name, host,
			strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

/* Read one reply line and check its 3-digit code. */
static int expect(fd, code)
int fd;
char *code;
{
	char buf[512];
	int i, n;
	char c;

	for (;;) {
		i = 0;
		while (i < (int) sizeof(buf) - 1) {
			if ((n = read(fd, &c, 1)) <= 0)
				return -1;
			if (c == '\r')
				continue;
			if (c == '\n')
				break;
			buf[i++] = c;
		}
		buf[i] = '\0';
		if (i >= 4 && buf[3] == '-')
			continue;		/* continuation */
		if (strncmp(buf, code, 3) == 0)
			return 0;
		fprintf(stderr, "%s: server said: %s\n", prog_name, buf);
		return -1;
	}
}

static int sendcmd(fd, cmd, code)
int fd;
char *cmd, *code;
{
	int len, n;

	len = strlen(cmd);
	while (len > 0) {
		if ((n = write(fd, cmd, len)) <= 0)
			return -1;
		cmd += n;
		len -= n;
	}
	return expect(fd, code);
}

/* Copy stdin, dot-stuffing leading periods (RFC 821 4.5.2). */
static int sendbody(fd)
int fd;
{
	char buf[1024];
	int len, n;
	char *p;

	while (fgets(buf, sizeof(buf) - 4, stdin) != (char *)0) {
		for (p = buf; *p; p++)
			if (*p == '\n') { *p = '\0'; break; }
		if (buf[0] == '.')
			write(fd, ".", 1);
		len = strlen(buf);
		p = buf;
		while (len > 0) {
			if ((n = write(fd, p, len)) <= 0)
				return -1;
			p += n;
			len -= n;
		}
		if (write(fd, "\r\n", 2) != 2)
			return -1;
	}
	return 0;
}
