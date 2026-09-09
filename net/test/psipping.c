/*
 * psipping.c -- prove real IP traffic through the stack, with no serial line.
 *
 *	psipping [<count> [echo|proto|badsum|runt|all]]
 *
 * psip is the stack's point-to-point serial IP interface, and what it carries
 * over its control channel is a RAW IP PACKET in each direction: psip_write()
 * hands its payload straight to ip_arrived(), and psip_read() collects whatever
 * the ip layer wants to transmit.  A userland process on that channel is
 * therefore the wire -- which means the whole IP path can be exercised without
 * a second serial port, a host-side bridge, or a SLIP peer.  That matters here
 * because the simulator's serial API cannot carry SLIP's framing bytes at
 * all, and the fast emulator has only the console line.
 *
 * The test is an ICMP echo: inject a well-formed Echo Request from the peer
 * address to ours, then read what the stack transmits in response.  A correct
 * Echo Reply coming back is end-to-end evidence -- inbound psip -> ip_arrived ->
 * icmp -> ip_send -> outbound psip -- and it needs no checksum arithmetic on the
 * reply, since we verify rather than generate it.
 *
 * Run AFTER ifconfig, on a daemon that already has an address: the ip layer
 * drops packets for an address it does not hold.
 *
 *	/etc/inet &
 *	/etc/ifconfig 10.0.0.2 255.255.255.0
 *	/bin/psipping 1 all
 *
 * SCORING.  Three of the four kinds are NEGATIVE: the packet is deliberately
 * spoiled and the correct behaviour is that nothing comes back.  Those modes
 * used to be scored as though a reply were wanted -- `ok == want' for every
 * kind -- so a stack that answered a bad-checksum packet exited 0 and a stack
 * that correctly ignored it exited 1.  That is worse than no check: it does not
 * merely miss a defect, it rewards it.  Each kind now names what it expects and
 * is scored against that:
 *
 *	echo	a valid Echo Reply carrying our sequence number
 *	runt	NOTHING transmitted -- ip_arrived drops it on length
 *	badsum	NOTHING transmitted -- a packet whose IP header checksum is
 *		wrong must never be answered, not even with an ICMP error,
 *		because its source address cannot be believed
 *	proto	NO ECHO REPLY.  Silence and an ICMP destination-unreachable are
 *		both correct here, so this one is scored on the absence of a
 *		reply rather than on total silence.
 */
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include "inet_ipc.h"
#include "inet_chan.h"

#define PSIP_MINOR	0	/* psip interface 0 (if2minor(0,PSIP_DEV_OFF)) */
#define PACKLEN		2048

#define LOCAL_A		10	/* 10.0.0.2 -- must match ifconfig */
#define LOCAL_B		0
#define LOCAL_C		0
#define LOCAL_D		2
#define PEER_D		1	/* 10.0.0.1, the far end of the link */

#define IPPROTO_ICMP	1
#define ICMP_ECHO	8
#define ICMP_ECHOREPLY	0

struct ichan psip;

/* One's-complement sum, as both IP and ICMP use. */
static unsigned cksum(p, n)
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
		sum = (sum & 0xFFFF) + (sum >> 16);
	return (unsigned)(~sum & 0xFFFF);
}

static put16(p, v)
unsigned char *p;
int v;
{
	p[0] = (v >> 8) & 0xFF;
	p[1] = v & 0xFF;
}

/* Build an ICMP Echo Request, peer -> us.  Returns its length.
 *
 * `kind' deliberately spoils the packet, to bisect the daemon's inbound path
 * without reading all of ip.c: a "runt" is rejected on length, a "badsum" on
 * the header checksum, and a "proto" has a valid header but a protocol nothing
 * handles.  Each one gets further in than the last, so whichever kind first
 * misbehaves names the stage. */
static int mkecho(pack, seq, kind)
unsigned char *pack;
int seq;
char *kind;
{
	unsigned char *ip, *ic;
	int iclen, len;

	ip = pack;
	ic = pack + 20;
	iclen = 16;			/* 8 header + 8 of payload */
	len = 20 + iclen;

	memset((char *)pack, 0, len);
	if (strcmp(kind, "runt") == 0)
	{
		pack[0] = 0x45;
		return 4;
	}
	ip[0] = 0x45;			/* IPv4, 20-byte header */
	ip[1] = 0;			/* tos */
	put16(&ip[2], len);
	put16(&ip[4], 0x1234);		/* id */
	put16(&ip[6], 0);		/* no fragmentation */
	ip[8] = 64;			/* ttl */
	ip[9] = (strcmp(kind, "proto") == 0) ? 253 : IPPROTO_ICMP;
	put16(&ip[10], 0);		/* checksum, filled in below */
	ip[12] = LOCAL_A; ip[13] = LOCAL_B; ip[14] = LOCAL_C; ip[15] = PEER_D;
	ip[16] = LOCAL_A; ip[17] = LOCAL_B; ip[18] = LOCAL_C; ip[19] = LOCAL_D;
	put16(&ip[10], cksum(ip, 20));
	if (strcmp(kind, "badsum") == 0)
		ip[10] ^= 0xFF;

	ic[0] = ICMP_ECHO;
	ic[1] = 0;			/* code */
	put16(&ic[2], 0);		/* checksum, filled in below */
	put16(&ic[4], 0x4321);		/* identifier */
	put16(&ic[6], seq);
	memcpy((char *)&ic[8], "c900ping", 8);
	put16(&ic[2], cksum(ic, iclen));
	return len;
}

static dotted(p)
unsigned char *p;
{
	printf("%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
}

/* What came back out of the stack.  R_NONE also covers a read that failed or
 * returned nothing, since either way no packet was transmitted. */
#define R_NONE		0		/* nothing transmitted		*/
#define R_ECHOREPLY	1		/* the Echo Reply we asked for	*/
#define R_OTHER		2		/* some other packet		*/

/* Describe an outbound packet, and classify it. */
static int report(pack, len, seq)
unsigned char *pack;
int len;
int seq;
{
	unsigned char *ic;
	int hl, good;

	printf("psipping: %d bytes out, ", len);
	if (len < 20)
	{
		printf("runt\n");
		return R_OTHER;
	}
	hl = (pack[0] & 0x0F) * 4;
	dotted(&pack[12]);
	printf(" -> ");
	dotted(&pack[16]);
	printf(" proto %d", pack[9]);

	good = R_OTHER;
	if (cksum(pack, hl) != 0)
		printf(" BAD-IP-CKSUM");
	else if (pack[9] != IPPROTO_ICMP || len < hl + 8)
		printf(" (not icmp)");
	else
	{
		ic = pack + hl;
		printf(" icmp type %d seq %d", ic[0], (ic[6] << 8) | ic[7]);
		if (cksum(ic, len - hl) != 0)
			printf(" BAD-ICMP-CKSUM");
		else if (ic[0] != ICMP_ECHOREPLY)
			printf(" (not a reply)");
		else if (((ic[6] << 8) | ic[7]) != seq)
			printf(" (wrong seq)");
		else
		{
			printf(" ECHO-REPLY-OK");
			good = R_ECHOREPLY;
		}
	}
	printf("\n");
	return good;
}

/* Inject one packet of `kind' and classify what the stack transmits back. */
static int exchange(pack, seq, kind)
unsigned char *pack;
int seq;
char *kind;
{
	fd_set rd;
	struct timeval tv;
	int len, rop, tries;

	/* Arm the outbound direction BEFORE injecting, so the reply cannot be
	 * produced while we have no read outstanding. */
	ichan_post_read(&psip, PACKLEN);
	len = mkecho(pack, seq, kind);
	if (ichan_post_write(&psip, (char *)pack, len) < 0)
	{
		fprintf(stderr, "psipping: inject failed\n");
		return -1;
	}
	printf("psipping: %d bytes in, seq %d, kind %s\n", len, seq, kind);

	/* Collect replies until the READ answers or we give up.  A WRITE reply
	 * just acknowledges the injection. */
	for (tries = 0; tries < 20; tries++)
	{
		FD_ZERO(&rd);
		FD_SET(psip.ic_replfd, &rd);
		tv.tv_sec = 2; tv.tv_usec = 0;
		if (select(psip.ic_replfd + 1, &rd, (fd_set *)0,
		    (fd_set *)0, &tv) <= 0)
		{
			printf("psipping: seq %d nothing transmitted\n", seq);
			return R_NONE;
		}
		len = ichan_reply_op(&psip, (char *)pack, PACKLEN, &rop);
		if (rop != NWR_READ)
			continue;	/* the write's acknowledgement */
		if (len <= 0)
		{
			printf("psipping: read status %d\n", len);
			return R_NONE;
		}
		return report(pack, len, seq);
	}
	printf("psipping: seq %d gave up waiting\n", seq);
	return R_NONE;
}

/*
 * Is `got' what `kind' is entitled to?  This is the whole judgement of the
 * program, kept in one place so it can be read against the table in the header
 * comment and so a negative-control run can be pointed straight at it.
 */
static int accept_result(kind, got)
char *kind;
int got;
{
	if (strcmp(kind, "echo") == 0)
		return got == R_ECHOREPLY;
	if (strcmp(kind, "proto") == 0)
		return got != R_ECHOREPLY;
	/* runt, badsum: total silence. */
	return got == R_NONE;
}

static char *want_str(kind)
char *kind;
{
	if (strcmp(kind, "echo") == 0)
		return "an Echo Reply";
	if (strcmp(kind, "proto") == 0)
		return "no Echo Reply";
	return "silence";
}

/* One kind, `want' times.  Returns the number of exchanges that behaved. */
static int runkind(pack, kind, want)
unsigned char *pack;
char *kind;
int want;
{
	int seq, got, ok;

	ok = 0;
	for (seq = 1; seq <= want; seq++)
	{
		got = exchange(pack, seq, kind);
		if (got < 0)
			return -1;
		if (accept_result(kind, got))
			ok++;
		else
			printf("psipping: FAIL %s seq %d -- wanted %s\n",
				kind, seq, want_str(kind));
	}
	printf("psipping: %s %d/%d correct (wanted %s)\n", kind, ok, want,
		want_str(kind));
	return ok;
}

int main(argc, argv)
int argc;
char **argv;
{
	static char *allkinds[4] = { "echo", "runt", "badsum", "proto" };
	unsigned char pack[PACKLEN];
	int want, ok, i, n, fails;
	char *kind;

	want = (argc > 1) ? atoi(argv[1]) : 1;
	if (want <= 0)
		want = 1;
	kind = (argc > 2) ? argv[2] : "echo";

	if (ichan_open(&psip, PSIP_MINOR) < 0)
	{
		fprintf(stderr, "psipping: cannot attach to psip (is /etc/inet up?)\n");
		return 1;
	}

	fails = 0;
	if (strcmp(kind, "all") == 0)
	{
		n = 4;
		for (i = 0; i < n; i++)
		{
			ok = runkind(pack, allkinds[i], want);
			if (ok != want)
				fails++;
		}
		printf("psipping: %d of %d kinds behaved -- %s\n", n - fails, n,
			fails ? "FAIL" : "PASS");
		return fails ? 1 : 0;
	}

	ok = runkind(pack, kind, want);
	printf("psipping: %s\n", ok == want ? "PASS" : "FAIL");
	return (ok == want) ? 0 : 1;
}
