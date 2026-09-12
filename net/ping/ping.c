/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*
 * ping.c -- ICMP echo, after Minix 2.0.4's ping(1).
 *
 *	ping [-c count] [-s size] [-w seconds] host
 *
 * Reaches the stack the way every Minix net client does: open("/dev/ip") plus
 * NWIO* ioctls, which libsocket's shim turns into a channel to the inet daemon.
 * No socket() call and nothing in the kernel.
 *
 * NWIOSIPOPT names only the access mode and the protocol.  Everything else --
 * the IP header the stack builds, the read/write mode, which packets are
 * accepted -- is inherited from the options an ip fd is opened with
 * (ip_int.h NWIO_DEFAULT: EN_LOC | EN_BROAD | REMANY | RWDATALL | HDR_O_SPEC),
 * and those defaults are exactly what a ping wants:
 *
 *	HDR_O_SPEC  the stack fills in the version, header length, tos, ttl and
 *		    fragment fields, so this program writes an IP header with
 *		    only ih_dst set.  Without it ip_write() takes the raw branch
 *		    and rejects a header whose ih_vers_ihl or ih_ttl is zero.
 *	RWDATALL    a read returns the whole packet, IP header included, which
 *		    is what says where a reply came from.
 *	REMANY      a reply is accepted from any address -- the peer answers
 *		    from whichever of its addresses the route chose.
 *
 * The echo request goes to every ip fd bound to protocol 1, and this program's
 * fd is one of them.  When the destination is this machine's own address the
 * stack loops the packet back internally, so the REQUEST arrives here as well
 * as the reply; and a late reply to an earlier sequence number can arrive after
 * a timeout has moved on.  Neither is an error, so the receive loop reads until
 * it sees an Echo Reply carrying THIS process's identifier and the sequence
 * number outstanding, and discards the rest.
 */
#include <sys/types.h>
#include <sys/timeb.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <net/gen/in.h>
#include <net/gen/ip_hdr.h>
#include <net/gen/icmp_hdr.h>
#include <net/gen/ip_io.h>
#include <net/ioctl.h>
#include <netdb.h>

#define ICMP_ECHOREPLY	0
#define ICMP_ECHO	8

/*
 * The largest packet that can be received in one piece.  A reply is collected
 * through the channel's hold buffer (inet_chan.h ICHAN_HOLD), and a packet
 * longer than that is truncated on the way in rather than returned in pieces.
 */
#define PKTMAX		512
#define HDRLEN		(IP_MIN_HDR_SIZE + 8)
#define DEFDATA		56
#define DEFCOUNT	4
#define DEFWAIT		5

/*
 * Word-aligned packet storage.  ip_hdr_t and icmp_hdr_t are read and written
 * through pointers into these buffers, and a Z8001 word or long access to an
 * odd address traps -- a plain char array is only byte-aligned.
 */
static u32_t obuf[PKTMAX / 4];
static u32_t ibuf[PKTMAX / 4];

static char *progname = "ping";

/*
 * The two calls whose result is wider than an int.  Without these the K&R
 * default return type applies and an address comes back through a 16-bit
 * register: gethostbyname()'s far pointer loses its segment, and inet_addr()'s
 * 32-bit address keeps only its low half.
 */
extern struct hostent *gethostbyname();
extern ipaddr_t inet_addr();

/* ARGSUSED */
static void alarmed(sig)
int sig;
{
}

/*
 * The one's-complement sum IP and ICMP both use, over `n' bytes.
 *
 * The running sum is a long: on this machine an int holds 16 bits, which is the
 * width of the sum itself, so an int accumulator has nowhere to keep the carries
 * that have to be folded back in.
 */
static u16_t cksum(p, n)
unsigned char *p;
int n;
{
	long sum;
	int i;

	sum = 0;
	for (i = 0; i + 1 < n; i += 2)
		sum += ((long)p[i] << 8) | p[i + 1];
	if (i < n)
		sum += (long)p[i] << 8;
	while ((sum >> 16) != 0)
		sum = (sum & 0xFFFFL) + (sum >> 16);
	return (u16_t)(~sum & 0xFFFFL);
}

static char *dotted(a)
ipaddr_t a;
{
	static char buf[16];

	sprintf(buf, "%d.%d.%d.%d",
		(int)((a >> 24) & 0xFFL), (int)((a >> 16) & 0xFFL),
		(int)((a >> 8) & 0xFFL), (int)(a & 0xFFL));
	return buf;
}

/* Milliseconds from `a' to `b'.
 *
 * millitm is an unsigned short, which promotes to UNSIGNED int here, so a
 * difference taken directly is a huge positive number whenever the millisecond
 * field wrapped.  Each end is widened to a signed int first, and the seconds
 * are long because time_t is. */
static long msec(a, b)
struct timeb *a, *b;
{
	return (b->time - a->time) * 1000L
		+ (long)((int)b->millitm - (int)a->millitm);
}

/*
 * Is this the reply we are waiting for?  Anything else is discarded silently,
 * since the request looped back and a late reply to an earlier sequence number
 * both arrive here in the ordinary course -- except a corrupt checksum, which
 * is an answer that arrived and could not be used, and is said out loud.
 */
static int isreply(pack, len, id, seq)
unsigned char *pack;
int len, id, seq;
{
	ip_hdr_t *ip;
	icmp_hdr_t *ic;
	int hl, iclen;

	if (len < IP_MIN_HDR_SIZE)
		return 0;
	ip = (ip_hdr_t *)pack;
	hl = (ip->ih_vers_ihl & IH_IHL_MASK) * 4;
	if (hl < IP_MIN_HDR_SIZE || len < hl + 8)
		return 0;
	if (ip->ih_proto != IPPROTO_ICMP)
		return 0;
	ic = (icmp_hdr_t *)(pack + hl);
	iclen = len - hl;
	if (ic->ih_type != ICMP_ECHOREPLY)
		return 0;
	if (cksum((unsigned char *)ic, iclen) != 0)
	{
		fprintf(stderr, "%s: reply from %s has a bad ICMP checksum\n",
			progname, dotted(ip->ih_src));
		return 0;
	}
	if (ic->ih_hun.ihh_idseq.iis_id != (u16_t)id
	    || ic->ih_hun.ihh_idseq.iis_seq != (u16_t)seq)
		return 0;
	return 1;
}

static usage()
{
	fprintf(stderr, "Usage: %s [-c count] [-s size] [-w seconds] host\n",
		progname);
	exit(1);
}

int main(argc, argv)
int argc;
char **argv;
{
	struct hostent *hp;
	struct timeb t0, t1;
	nwio_ipopt_t ipopt;
	ip_hdr_t *ip;
	icmp_hdr_t *ic;
	ipaddr_t dst;
	int fd, c, count, data, wait, id, seq, len, n, sent, got;

	count = DEFCOUNT;
	data = DEFDATA;
	wait = DEFWAIT;
	if (argc > 0 && argv[0][0] != '\0')
		progname = argv[0];
	while ((c = getopt(argc, argv, "c:s:w:")) != -1)
	{
		switch (c) {
		case 'c':	count = atoi(optarg); break;
		case 's':	data = atoi(optarg); break;
		case 'w':	wait = atoi(optarg); break;
		default:	usage();
		}
	}
	if (optind != argc - 1)
		usage();
	if (count <= 0)
		count = DEFCOUNT;
	if (wait <= 0)
		wait = DEFWAIT;
	if (data < 0)
		data = 0;
	if (data > PKTMAX - HDRLEN)
	{
		fprintf(stderr, "%s: size limited to %d bytes\n",
			progname, PKTMAX - HDRLEN);
		data = PKTMAX - HDRLEN;
	}

	/*
	 * A name first, a dotted quad only if no name answers: gethostbyname()
	 * reads /etc/hosts before it asks the network, and a host that is in
	 * neither is an error rather than an address of zero.
	 */
	if ((hp = gethostbyname(argv[optind])) != (struct hostent *)0)
	{
		/* h_addr points into the resolver's own storage, which is not
		 * promised to be word-aligned; a long fetched from an odd
		 * address traps here. */
		memcpy((char *)&dst, hp->h_addr, sizeof(dst));
	}
	else
	{
		dst = inet_addr(argv[optind]);
		if (dst == (ipaddr_t)0xFFFFFFFFL)
		{
			fprintf(stderr, "%s: unknown host (%s)\n",
				progname, argv[optind]);
			exit(1);
		}
	}

	if ((fd = open("/dev/ip", O_RDWR, 0)) < 0)
	{
		fprintf(stderr, "%s: ", progname);
		perror("/dev/ip");
		exit(1);
	}

	memset((char *)&ipopt, 0, sizeof(ipopt));
	ipopt.nwio_flags = NWIO_COPY | NWIO_PROTOSPEC;
	ipopt.nwio_proto = IPPROTO_ICMP;
	if (ioctl(fd, NWIOSIPOPT, (char *)&ipopt) < 0)
	{
		fprintf(stderr, "%s: ", progname);
		perror("ioctl(NWIOSIPOPT)");
		exit(1);
	}
	if (ioctl(fd, NWIOGIPOPT, (char *)&ipopt) < 0)
	{
		fprintf(stderr, "%s: ", progname);
		perror("ioctl(NWIOGIPOPT)");
		exit(1);
	}

	id = getpid();
	len = HDRLEN + data;
	printf("PING %s (%s): %d data bytes\n", argv[optind], dotted(dst),
		data);

	sent = got = 0;
	for (seq = 1; seq <= count; seq++)
	{
		memset((char *)obuf, 0, len);
		ip = (ip_hdr_t *)obuf;
		ip->ih_dst = dst;

		ic = (icmp_hdr_t *)((char *)obuf + IP_MIN_HDR_SIZE);
		ic->ih_type = ICMP_ECHO;
		ic->ih_code = 0;
		ic->ih_chksum = 0;
		ic->ih_hun.ihh_idseq.iis_id = (u16_t)id;
		ic->ih_hun.ihh_idseq.iis_seq = (u16_t)seq;
		for (n = 0; n < data; n++)
			((unsigned char *)ic)[8 + n] = (unsigned char)n;
		ic->ih_chksum = cksum((unsigned char *)ic, 8 + data);

		ftime(&t0);
		if ((n = write(fd, (char *)obuf, len)) != len)
		{
			fprintf(stderr, "%s: ", progname);
			if (n < 0)
				perror("write");
			else
				fprintf(stderr, "wrote %d of %d bytes\n",
					n, len);
			continue;
		}
		sent++;

		/*
		 * The read blocks with nothing to time it out, so SIGALRM is
		 * what ends a lost packet: a fifo read woken by a caught signal
		 * returns EINTR having transferred nothing (sys/coh/pipe.c
		 * psleep), so no partial reply record can be left behind.  The
		 * handler is re-armed each time round because this signal(2)
		 * resets to the default disposition when it fires.
		 */
		signal(SIGALRM, alarmed);
		alarm((unsigned)wait);
		for (;;)
		{
			n = read(fd, (char *)ibuf, PKTMAX);
			if (n < 0)
			{
				if (errno == EINTR)
					printf("no reply: icmp_seq=%d\n", seq);
				else
				{
					fprintf(stderr, "%s: ", progname);
					perror("read");
				}
				break;
			}
			if (!isreply((unsigned char *)ibuf, n, id, seq))
				continue;
			alarm(0);
			ftime(&t1);
			ip = (ip_hdr_t *)ibuf;
			printf("%d bytes from %s: icmp_seq=%d ttl=%d time=%ld ms\n",
				n - (int)((ip->ih_vers_ihl & IH_IHL_MASK) * 4),
				dotted(ip->ih_src), seq, (int)ip->ih_ttl,
				msec(&t0, &t1));
			got++;
			break;
		}
		alarm(0);
	}

	printf("--- %s ping statistics ---\n", argv[optind]);
	printf("%d packets transmitted, %d packets received, %d%% packet loss\n",
		sent, got, sent ? (sent - got) * 100 / sent : 100);
	return got ? 0 : 1;
}
