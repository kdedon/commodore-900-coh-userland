/*
 * psipstub.c -- a FAKE IP stack on the psip control channel, for proving that
 * psipping(8) can tell a correct stack from a broken one.
 *
 * This is the negative control for net/test/psipping.c.  Three of that
 * program's four modes inject a deliberately spoiled packet and expect NOTHING
 * back, and until this harness existed nothing had ever demonstrated that the
 * program noticed the difference -- it scored every mode as though a reply were
 * wanted, so a stack that answered a bad-checksum packet exited 0.
 *
 * psipping.c is compiled here VERBATIM, with no -D and no edits: it links
 * against these ichan_* entry points instead of net/inet_chan.o, and they play
 * the part of the daemon.  The channel is a pipe, so psipping's select() on
 * ic_replfd works unchanged and the timing paths are the real ones.
 *
 * The stack's behaviour is chosen by $PSIPSTACK:
 *
 *	good		validates length, IP header checksum, protocol, ICMP
 *			type and ICMP checksum, and answers only a well-formed
 *			Echo Request addressed to us.  This is the stack the
 *			target is supposed to have; psipping must pass on it.
 *	answer-badsum	as `good', but does NOT verify the IP header checksum.
 *			The defect the old scoring rewarded.
 *	answer-all	answers anything long enough, whatever its protocol or
 *			checksums.  Also answers the `proto' packet.
 *	answer-runt	as `good', but also answers the 4-byte runt.
 *	deaf		never answers anything.  The plain-echo failure.
 *	wrongseq	answers the echo, with the wrong sequence number.
 *
 * Not a target file: it is built only by hostcheck/Makefile with the host cc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "inet_ipc.h"
#include "inet_chan.h"

#define IPPROTO_ICMP	1
#define ICMP_ECHO	8
#define ICMP_ECHOREPLY	0

/* Framing on the pipe.  Both ends are here, so it need only be unambiguous. */
struct rec {
	int	r_op;
	int	r_len;
};

static int wfd = -1;			/* the stack's end of the reply pipe */
static char *mode = "good";

static unsigned sum16(p, n)
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

static void put16(p, v)
unsigned char *p;
int v;
{
	p[0] = (v >> 8) & 0xFF;
	p[1] = v & 0xFF;
}

static void push(op, buf, len)
int op;
char *buf;
int len;
{
	struct rec r;

	r.r_op = op;
	r.r_len = len;
	write(wfd, (char *)&r, sizeof(r));
	if (len > 0)
		write(wfd, buf, len);
}

/*
 * The stack proper: decide whether this inbound packet earns a reply, and if
 * so build the Echo Reply the way ip/icmp would.
 */
static void arrived(pack, len)
unsigned char *pack;
int len;
{
	unsigned char out[2048];
	int hl, iclen, chk;

	chk = (strcmp(mode, "answer-all") == 0) ? 0 : 1;

	if (strcmp(mode, "deaf") == 0)
		return;
	if (len < 20)
	{
		/* A runt has no header to work from at all; answering it means
		 * echoing back whatever bytes did arrive. */
		if (strcmp(mode, "answer-runt") != 0)
			return;
		push(NWR_READ, (char *)pack, len);
		return;
	}
	hl = (pack[0] & 0x0F) * 4;
	if (hl < 20 || hl > len)
		return;
	if (chk && strcmp(mode, "answer-badsum") != 0 && sum16(pack, hl) != 0)
		return;				/* bad IP header checksum */
	if (chk && pack[9] != IPPROTO_ICMP)
		return;				/* no handler for that protocol */
	if (len < hl + 8)
		return;
	if (chk && pack[hl] != ICMP_ECHO)
		return;
	if (chk && sum16(&pack[hl], len - hl) != 0)
		return;				/* bad ICMP checksum */

	iclen = len - hl;
	memcpy((char *)out, (char *)pack, len);
	memcpy((char *)&out[12], (char *)&pack[16], 4);	/* src <- our address */
	memcpy((char *)&out[16], (char *)&pack[12], 4);	/* dst <- theirs	  */
	out[9] = IPPROTO_ICMP;
	put16(&out[10], 0);
	put16(&out[10], sum16(out, hl));
	out[hl] = ICMP_ECHOREPLY;
	if (strcmp(mode, "wrongseq") == 0)
		put16(&out[hl + 6], ((out[hl + 6] << 8) | out[hl + 7]) + 1);
	put16(&out[hl + 2], 0);
	put16(&out[hl + 2], sum16(&out[hl], iclen));
	push(NWR_READ, (char *)out, len);
}

int ichan_open(c, minor)
struct ichan *c;
int minor;
{
	int p[2];
	char *m;

	if ((m = getenv("PSIPSTACK")) != (char *)0)
		mode = m;
	if (pipe(p) < 0)
		return -1;
	memset((char *)c, 0, sizeof(*c));
	c->ic_replfd = p[0];
	c->ic_reqfd = p[1];
	wfd = p[1];
	fprintf(stderr, "psipstub: stack mode %s\n", mode);
	return 0;
}

int ichan_post_read(c, n)
struct ichan *c;
int n;
{
	return 0;
}

int ichan_post_write(c, buf, n)
struct ichan *c;
char *buf;
int n;
{
	/* The daemon acknowledges the injection first; psipping must skip that
	 * reply and keep waiting, which is a path worth exercising. */
	push(NWR_WRITE, (char *)0, 0);
	arrived((unsigned char *)buf, n);
	return 0;
}

int ichan_reply_op(c, buf, max, rop)
struct ichan *c;
char *buf;
int max;
int *rop;
{
	struct rec r;

	if (read(c->ic_replfd, (char *)&r, sizeof(r)) != sizeof(r))
		return -1;
	*rop = r.r_op;
	if (r.r_len > max)
		r.r_len = max;
	if (r.r_len > 0 && read(c->ic_replfd, buf, r.r_len) != r.r_len)
		return -1;
	return r.r_len;
}

void ichan_close(c)
struct ichan *c;
{
}
