/*
 * sntpsrv.c -- a fixed-answer SNTP server, so /etc/sntp can be tested with no
 * wire, no slip and no peer.
 *
 *	sntpsrv [count]			default 1 request, then exit
 *
 * It binds 123/udp on this machine's own address and answers each request with
 * a KNOWN transmit timestamp, so the answer sntp is supposed to reach is a
 * number this file states rather than whatever the world happens to say.  That
 * is the difference between "the clock got set" and "the clock got set to the
 * right thing" -- against a real server the only available check is that the
 * year looks plausible, which would pass just as well with the seconds half
 * misread by a fortnight.
 *
 * ip_write loops a packet addressed to its own interface back internally (the
 * same reason udpecho.c and echoserver.c need no peer), so both ends run here
 * and the test costs an emulator run instead of a simulator run with a serial
 * line and a host daemon on the other end of it.
 *
 * THE ANSWER, chosen so a mistake cannot look like a success:
 *
 *	xmt = 0xEE0F7A00 = 3993991680  ->  time_t 1785002880
 *	                                =  Sat Jul 25 18:08:00 2026 UTC
 *
 * Every byte of that is distinctive.  A signed reading of the NTP seconds gives
 * a negative number, not a nearby date; dropping the 1900->1970 offset gives
 * a date in 2096; a byte-swapped seconds field lands nowhere near 2026.  The
 * fraction is set to 0x40000000 (a quarter second) so the ROUNDING is exercised
 * as a no-op -- run with `-f' and it becomes 0xC0000000, which must add one.
 *
 * Every line starts with "sntpsrv:" so a scripted run can pick it out.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>

extern int errno;
extern unsigned long inet_addr();
extern char *strchr();		/* K&R: undeclared, it defaults to int */

#define NTP_PORT	123
#define NTP_VERSION	4
#define MODE_SERVER	4

/* The wire format; see net/sntp.c, which carries the full description. */
struct ntpmsg {
	unsigned char	li_vn_mode;
	unsigned char	stratum;
	char		poll;
	char		precision;
	unsigned long	rootdelay;
	unsigned long	rootdisp;
	unsigned long	refid;
	unsigned long	reftime_s, reftime_f;
	unsigned long	org_s, org_f;
	unsigned long	rec_s, rec_f;
	unsigned long	xmt_s, xmt_f;
};

/* 3993991680 -- 2026-07-25 18:08:00 UTC.  Hex halves, as sntp.c explains. */
#define ANSWER_S	(((unsigned long)0xEE0FU << 16) | 0x7A00U)
#define FRAC_LOW	(((unsigned long)0x4000U << 16) | 0x0000U)	/* .25 */
#define FRAC_HIGH	(((unsigned long)0xC000U << 16) | 0x0000U)	/* .75 */

int main(argc, argv)
int argc;
char **argv;
{
	int s, i, n, alen, count, hifrac;
	unsigned long me, frac;
	struct sockaddr_in sin, from;
	struct ntpmsg in, out;
	char *addr;

	count = 1;
	hifrac = 0;
	addr = "10.0.0.2";
	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-f") == 0)
			hifrac = 1;
		else if (argv[i][0] >= '0' && argv[i][0] <= '9' &&
			 strchr(argv[i], '.') == (char *)0)
			count = atoi(argv[i]);
		else
			addr = argv[i];
	}
	frac = hifrac ? FRAC_HIGH : FRAC_LOW;
	me = inet_addr(addr);

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		printf("sntpsrv: socket errno %d\n", errno);
		return 1;
	}
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(NTP_PORT);
	sin.sin_addr.s_addr = me;
	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		printf("sntpsrv: bind(%d) errno %d\n", NTP_PORT, errno);
		return 1;
	}
	printf("sntpsrv: listening on %s port %d, %d request(s)\n", addr,
		NTP_PORT, count);
	fflush(stdout);

	for (i = 0; i < count; i++)
	{
		alen = sizeof(from);
		n = recvfrom(s, (char *)&in, sizeof(in), 0,
			(struct sockaddr *)&from, &alen);
		if (n < 0)
		{
			printf("sntpsrv: recvfrom errno %d\n", errno);
			return 1;
		}
		/* The port is printed UNSIGNED: int is 16 bits here and the
		 * stack hands out ephemeral ports above 32767, so %d turns
		 * 49153 into -16383 and reads like a failure (cf. discotime.c). */
		printf("sntpsrv: request %d: %d bytes, mode %d, from port %u\n",
			i + 1, n, in.li_vn_mode & 7,
			(unsigned)ntohs(from.sin_port));
		fflush(stdout);

		memset((char *)&out, 0, sizeof(out));
		out.li_vn_mode = (NTP_VERSION << 3) | MODE_SERVER;
		out.stratum = 1;
		out.poll = 6;
		out.precision = -6;
		out.refid = ((unsigned long)(('L'<<8)|'O') << 16) |
			    (unsigned long)(('C'<<8)|'L');	/* "LOCL" */
		out.reftime_s = ANSWER_S;
		/* The originate timestamp is the client's transmit timestamp
		 * echoed back untouched: it is what lets the client tell this
		 * reply from a stale one, and a server that does not copy it
		 * makes every reply look stale. */
		out.org_s = in.xmt_s;
		out.org_f = in.xmt_f;
		out.rec_s = ANSWER_S;
		out.xmt_s = ANSWER_S;
		out.xmt_f = frac;

		if (sendto(s, (char *)&out, sizeof(out), 0,
		    (struct sockaddr *)&from, sizeof(from)) < 0)
		{
			printf("sntpsrv: sendto errno %d\n", errno);
			return 1;
		}
		printf("sntpsrv: answered %d\n", i + 1);
		fflush(stdout);
	}
	close(s);
	printf("sntpsrv: done\n");
	return 0;
}
