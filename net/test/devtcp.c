/*
 * devtcp -- the /dev/tcp shim, exercised the way the Minix net clients use it.
 *
 *	devtcp <host> <port>
 *
 * No socket() call anywhere: open the device, NWIOSTCPCONF the peer,
 * NWIOTCPCONN to connect, write a line, read the echo back, and ask
 * NWIOGTCPCONF what the stack settled on.  That is the whole API telnet, ftp and
 * the resolver use, so if this works they have nothing left to discover about
 * the transport.
 *
 * Run against an echo server on the peer (hostbuild/echo-server.py, port 7).
 *
 * EVERY "FAIL" HERE IS A FAILURE.  The NWIOGTCPCONF check printed FAIL and then
 * fell through to the unconditional "PASS devtcp" and exit(0) at the bottom, so
 * a stack that could not report the local port it had chosen -- or reported
 * zero, meaning it had chosen none -- came out of a scripted run indistinguish-
 * able from one that worked.  A single counter now decides the exit status, and
 * the last line agrees with it.
 */
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <net/gen/in.h>
#include <net/gen/tcp.h>
#include <net/gen/tcp_io.h>
#include <net/ioctl.h>

#define MSG	"devtcp-hello\n"
/* sizeof, not strlen(): no call, so no chance of a K&R width slip. */
#define MSGLEN	((int)sizeof(MSG) - 1)

/*
 * SECOND MODE: `devtcp <peer> <locport> listen', which is talk(1)'s ring.
 *
 *	NWIOSTCPCONF, then NWIOTCPLISTEN WITH AN ALARM ON IT, then -- when the
 *	alarm ends the wait -- NWIOTCPLISTEN AGAIN on the same descriptor, and
 *	then a three-byte write followed by a three-byte read.
 *
 * Every other client here connects, so nothing exercised a listen that a signal
 * interrupts -- and that is exactly what talk does: it rings, waits RING_WAIT
 * seconds for the answer with alarm(), and rings again (proto.c TalkInit).  An
 * interrupted operation that is never cancelled leaves the daemon holding it,
 * and the retry then ran with a connection already attached to the descriptor.
 * The result was a connection whose handshake completed on one connection while
 * the program read and wrote on another: a segment ACKed by the far stack and
 * retransmitted forever, and a read that never returned.
 *
 * The peer must NOT connect until after the alarm has fired, or the first listen
 * succeeds and this mode tests nothing.  net/twohost/peerfirst-test.py waits.
 */
#define LISTEN_ALARM	5	/* guest seconds; the peer waits longer	*/
#define EDCHARS		"abc"	/* three bytes, as talk sends		*/
#define EDLEN		((int)sizeof(EDCHARS) - 1)

static int alarm_fired;

static void onalarm(sig)
int sig;
{
	alarm_fired= 1;
}

/*
 * Returns 0 on success, 1 on failure, and says which on the way out.
 *
 * Both listens are reported with their errno, because the first one FAILING is
 * the point: a first listen that returns 0 means the peer connected too early
 * and the run proves nothing, which is a different thing from a pass.
 */
static int listenmode(peer, locport)
unsigned long peer;
int locport;
{
	struct nwio_tcpconf conf;
	struct nwio_tcpcl cl;
	char buf[8];
	int fd, s, n, got;

	if ((fd= open("/dev/tcp", O_RDWR)) < 0)
	{
		printf("FAIL devtcp-listen: open /dev/tcp: errno %d\n", errno);
		return 1;
	}

	memset((char *)&conf, 0, sizeof(conf));
	/* talk's own flags for the listening side (cmd/talk/net.c NetInit), with
	 * the local port SET rather than selected so the peer knows where to
	 * knock. */
	conf.nwtc_flags= NWTC_LP_SET | NWTC_SET_RA | NWTC_UNSET_RP;
	conf.nwtc_locport= locport;
	conf.nwtc_remaddr= peer;
	if (ioctl(fd, NWIOSTCPCONF, (char *)&conf) < 0)
	{
		printf("FAIL devtcp-listen: NWIOSTCPCONF: errno %d\n", errno);
		return 1;
	}
	printf("ok   NWIOSTCPCONF local port %d\n", locport);

	signal(SIGALRM, onalarm);
	alarm_fired= 0;
	alarm(LISTEN_ALARM);
	memset((char *)&cl, 0, sizeof(cl));
	s= ioctl(fd, NWIOTCPLISTEN, (char *)&cl);
	n= errno;
	alarm(0);
	printf("ok   listen #1 returned %d (errno %d, alarm %d)\n", s, n,
		alarm_fired);
	if (s == 0)
	{
		printf("FAIL devtcp-listen: the first listen SUCCEEDED -- the"
			" peer connected before the alarm, so nothing here was"
			" tested\n");
		return 1;
	}

	/* The retry, on the same descriptor.  This is the operation that used to
	 * be given a second connection. */
	memset((char *)&cl, 0, sizeof(cl));
	s= ioctl(fd, NWIOTCPLISTEN, (char *)&cl);
	if (s < 0)
	{
		printf("FAIL devtcp-listen: listen #2: errno %d\n", errno);
		return 1;
	}
	printf("ok   listen #2 connected\n");

	if (write(fd, EDCHARS, EDLEN) != EDLEN)
	{
		printf("FAIL devtcp-listen: write: errno %d\n", errno);
		return 1;
	}
	printf("ok   wrote %d bytes\n", EDLEN);

	got= 0;
	while (got < EDLEN)
	{
		if ((n= read(fd, &buf[got], EDLEN - got)) <= 0)
		{
			printf("FAIL devtcp-listen: read returned %d after %d"
				" of %d bytes, errno %d\n", n, got, EDLEN,
				errno);
			return 1;
		}
		got += n;
	}
	buf[got]= '\0';
	printf("PASS devtcp-listen: read %d bytes [%s]\n", got, buf);
	return 0;
}

main(argc, argv)
int argc;
char *argv[];
{
	struct nwio_tcpconf conf;
	struct nwio_tcpcl cl;
	char buf[64];
	unsigned long a;
	int fd, n, got, p1, p2, p3, p4, port, fails;

	fails= 0;

	if (argc < 3 || argc > 4)
	{
		fprintf(stderr,
			"usage: devtcp <dotted-quad> <port> [listen]\n");
		exit(1);
	}
	if (sscanf(argv[1], "%d.%d.%d.%d", &p1, &p2, &p3, &p4) != 4)
	{
		fprintf(stderr, "devtcp: %s is not a dotted quad\n", argv[1]);
		exit(1);
	}
	a= ((unsigned long)p1 << 24) | ((unsigned long)p2 << 16)
	 | ((unsigned long)p3 << 8) | (unsigned long)p4;
	port= atoi(argv[2]);

	if (argc == 4 && argv[3][0] == 'l')
		exit(listenmode(a, port));

	if ((fd= open("/dev/tcp", O_RDWR)) < 0)
	{
		printf("FAIL devtcp: open /dev/tcp: errno %d\n", errno);
		exit(1);
	}
	printf("ok   open /dev/tcp -> fd %d\n", fd);

	memset((char *)&conf, 0, sizeof(conf));
	conf.nwtc_flags= NWTC_LP_SEL | NWTC_SET_RA | NWTC_SET_RP;
	conf.nwtc_remaddr= a;
	conf.nwtc_remport= port;
	if (ioctl(fd, NWIOSTCPCONF, (char *)&conf) < 0)
	{
		printf("FAIL devtcp: NWIOSTCPCONF: errno %d\n", errno);
		exit(1);
	}
	printf("ok   NWIOSTCPCONF %s:%d\n", argv[1], port);

	memset((char *)&cl, 0, sizeof(cl));
	if (ioctl(fd, NWIOTCPCONN, (char *)&cl) < 0)
	{
		printf("FAIL devtcp: NWIOTCPCONN: errno %d\n", errno);
		exit(1);
	}
	printf("ok   NWIOTCPCONN connected\n");

	/*
	 * The local port was left to the stack (LP_SEL), so only the stack knows
	 * it -- reading it back is the direction of the protocol that a writing
	 * ioctl cannot show.
	 */
	memset((char *)&conf, 0, sizeof(conf));
	if (ioctl(fd, NWIOGTCPCONF, (char *)&conf) < 0)
	{
		printf("FAIL devtcp: NWIOGTCPCONF: errno %d\n", errno);
		fails++;
	}
	else if (conf.nwtc_locport == 0)
	{
		printf("FAIL devtcp: NWIOGTCPCONF reports local port 0\n");
		fails++;
	}
	else
		printf("ok   NWIOGTCPCONF local port %u\n",
			(unsigned)conf.nwtc_locport);

	if (write(fd, MSG, MSGLEN) != MSGLEN)
	{
		printf("FAIL devtcp: write: errno %d\n", errno);
		exit(1);
	}

	/*
	 * Read until the whole message is back, not once.  TCP is a stream and
	 * may deliver it in pieces; a single read that compares only the part
	 * that arrived cannot tell a short delivery from a truncating stack.
	 */
	got= 0;
	while (got < MSGLEN)
	{
		if ((n= read(fd, &buf[got], MSGLEN - got)) <= 0)
		{
			printf("FAIL devtcp: read returned %d after %d of %d"
				" bytes, errno %d\n", n, got, MSGLEN, errno);
			exit(1);
		}
		got += n;
	}
	buf[got]= '\0';
	if (strcmp(buf, MSG) != 0)
	{
		printf("FAIL devtcp: echo came back as \"%s\"\n", buf);
		fails++;
	}
	close(fd);
	if (fails)
	{
		printf("FAIL devtcp: %d check(s) failed\n", fails);
		exit(1);
	}
	printf("PASS devtcp: %d bytes echoed through /dev/tcp\n", got);
	exit(0);
}
