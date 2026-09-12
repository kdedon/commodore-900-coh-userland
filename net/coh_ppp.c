/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * coh_ppp.c -- PPP (RFC 1661/1662) daemon for COHERENT/Z8001.
 *
 * The SLIP daemon's bigger sibling: it bridges the same raw /dev/tty* to the
 * inet stack's psip (serial-IP) device, but speaks PPP instead of SLIP --
 * HDLC-like framing with a 16-bit FCS (RFC 1662) and the LCP/IPCP control
 * protocols (RFC 1661/1332) to bring the link up and negotiate addresses:
 *
 *	ppp /dev/tty1 /dev/psip0
 *
 * PPP is NOT in the Minix 2.0.4 tree and the reference pppd (ppp-2.x) is far too
 * large/ANSI/BSD-sockets-bound for this environment; this is a compact
 * purpose-built implementation, KA9Q-style, feeding the same psip seam as
 * coh_slip.c.  Once IPCP reaches Opened, IP frames (PPP protocol 0x0021) flow
 * between the tty and psip exactly as in SLIP.
 *
 * STATUS: the HDLC framing + FCS-16 + protocol demux (the mechanical, testable
 * half) is complete.  The LCP and IPCP option state machines are scaffolded
 * (passive responder: ACK the peer, send our own Configure-Request); full
 * option NAK/REJ handling, retransmit timers and PAP/CHAP auth are follow-ups.
 */
#include <sys/types.h>
#include <sgtty.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/select.h>
#include "inet_ipc.h"
#include "inet_chan.h"

#define PSIP_MINOR	0	/* psip interface 0			*/

#define FLAG	0176		/* 0x7E HDLC frame delimiter		*/
#define ESC	0175		/* 0x7D control-escape			*/
#define XOR	0040		/* 0x20 escape XOR mask			*/

#define P_IP	0x0021		/* PPP protocol: IP			*/
#define P_IPCP	0x8021		/* PPP protocol: IP Control Protocol	*/
#define P_LCP	0xC021		/* PPP protocol: Link Control Protocol	*/

/* LCP/IPCP codes (RFC 1661). */
#define CONF_REQ	1
#define CONF_ACK	2
#define CONF_NAK	3
#define CONF_REJ	4
#define TERM_REQ	5
#define TERM_ACK	6

#define PACKLEN	2048
#define FRAMELEN (2 * (PACKLEN + 8) + 2)

int serial_fd;
int rxpipe[2];			/* reader child -> parent (see main)	*/
struct ichan psip;		/* one bidirectional channel to psip	*/
int ipcp_open;			/* the IP data path is up		*/

/* --- FCS-16 (CRC-CCITT, RFC 1662 appendix C), bit-at-a-time --- */
unsigned pppfcs(fcs, cp, len)
unsigned fcs;
unsigned char *cp;
int len;
{
	int i;

	while (len--)
	{
		fcs ^= *cp++;
		for (i= 0; i < 8; i++)
			fcs = (fcs & 1) ? (fcs >> 1) ^ 0x8408 : fcs >> 1;
	}
	return fcs & 0xFFFF;
}

/* Put the serial line into 8-bit raw mode at 9600 bps. */
void rawtty(fd)
int fd;
{
	struct sgttyb sg;

	if (gtty(fd, &sg) < 0) { perror("ppp: gtty"); exit(1); }
	sg.sg_ispeed = B9600;
	sg.sg_ospeed = B9600;
	sg.sg_flags = RAW;
	if (stty(fd, &sg) < 0) { perror("ppp: stty"); exit(1); }
}

/* HDLC-frame and transmit an info field under the given PPP protocol. */
void ppp_send(proto, info, len)
int proto;
unsigned char *info;
int len;
{
	unsigned char hdr[4];
	/* STATIC: FRAMELEN is 4114 bytes, which is a stack frame this machine
	 * cannot afford (see outbound_pump).  ppp_send is not reentrant. */
	static unsigned char frame[FRAMELEN];
	unsigned fcs;
	int i, o;

	/* header: addr 0xFF, control 0x03, protocol (big-endian) */
	hdr[0] = 0xFF; hdr[1] = 0x03;
	hdr[2] = (proto >> 8) & 0xFF; hdr[3] = proto & 0xFF;

	fcs = 0xFFFF;
	fcs = pppfcs(fcs, hdr, 4);
	fcs = pppfcs(fcs, info, len);
	fcs ^= 0xFFFF;

	o = 0;
	frame[o++] = FLAG;
#define STUFF(b) do { int _b=(b)&0xFF; \
	if (_b==FLAG||_b==ESC||_b<0x20){frame[o++]=ESC;frame[o++]=_b^XOR;} \
	else frame[o++]=_b; } while(0)
	for (i = 0; i < 4; i++) STUFF(hdr[i]);
	for (i = 0; i < len; i++) STUFF(info[i]);
	STUFF(fcs & 0xFF);
	STUFF((fcs >> 8) & 0xFF);
#undef STUFF
	frame[o++] = FLAG;
	write(serial_fd, frame, o);
}

/*
 * Minimal LCP/IPCP responder: ACK whatever the peer requests (accept its
 * options) and, on the first Configure-Request, send our own empty one.  This
 * reaches Opened with a cooperative peer.  TODO: NAK/REJ unacceptable options,
 * retransmit timers, PAP/CHAP.
 */
void ctrl_recv(proto, p, len)
int proto;
unsigned char *p;
int len;
{
	unsigned char reply[PACKLEN];
	int code, id, plen, i;
	static int we_asked;

	if (len < 4)
		return;
	code = p[0]; id = p[1];
	plen = (p[2] << 8) | p[3];
	if (plen > len)
		plen = len;

	if (code == CONF_REQ)
	{
		/* Echo the request back as an ACK (accept the peer's options). */
		for (i = 0; i < plen; i++)
			reply[i] = p[i];
		reply[0] = CONF_ACK;
		ppp_send(proto, reply, plen);

		if (!we_asked)
		{
			/* Send our own (optionless) Configure-Request. */
			we_asked = 1;
			reply[0] = CONF_REQ; reply[1] = id + 1;
			reply[2] = 0; reply[3] = 4;
			ppp_send(proto, reply, 4);
		}
	}
	else if (code == CONF_ACK && proto == P_IPCP)
	{
		ipcp_open = 1;		/* our IPCP request accepted: data up */
	}
	else if (code == TERM_REQ)
	{
		reply[0] = TERM_ACK; reply[1] = id; reply[2] = 0; reply[3] = 4;
		ppp_send(proto, reply, 4);
		ipcp_open = 0;
	}
}

/* Dispatch a received, de-framed, FCS-checked frame by PPP protocol. */
void frame_recv(f, len)
unsigned char *f;
int len;
{
	int proto, off;

	if (len < 4)
		return;
	/* Skip optional addr/control (0xFF 0x03). */
	off = (f[0] == 0xFF && f[1] == 0x03) ? 2 : 0;
	proto = (f[off] << 8) | f[off + 1];
	off += 2;

	if (proto == P_IP)
	{
		if (ipcp_open)		/* inject into the stack (drop FCS) */
			ichan_post_write(&psip, (char *)(f + off),
				len - off - 2);
	}
	else if (proto == P_LCP || proto == P_IPCP)
	{
		ctrl_recv(proto, f + off, len - off - 2);
	}
	/* other protocols (auth, etc.): silently dropped for now */
}

/* one serial read: HDLC deframe -> FCS check -> frame_recv (state persists). */
void serial_pump()
{
	static unsigned char frame[FRAMELEN];
	static int len, esc;
	unsigned char in[512];
	int n, i;

	/* From the reader child's pipe, not the tty -- see main(). */
	n = read(rxpipe[0], (char *)in, sizeof(in));
	if (n <= 0)
	{
		fprintf(stderr, "ppp: rx pipe returned %d, exiting\n", n);
		exit(n < 0 ? 1 : 0);
	}
	for (i = 0; i < n; i++)
	{
		int c = in[i];

		if (c == FLAG)
		{
			if (len >= 4 && pppfcs(0xFFFF, frame, len) == 0xF0B8)
				frame_recv(frame, len);
			len = 0; esc = 0;
			continue;
		}
		if (c == ESC) { esc = 1; continue; }
		if (esc) { c ^= XOR; esc = 0; }
		if (len < FRAMELEN)
			frame[len++] = c;
		else
			len = 0;		/* overrun */
	}
}

/* A psip reply is waiting: a READ reply is an outbound IP packet to PPP-frame
 * onto the line (then re-arm the READ); a WRITE reply is an inject ack, ignore. */
void outbound_pump()
{
	/* STATIC, for the same reason slip's reply_pump() is: a 2 KB auto array is
	 * a 2 KB stack frame, and this machine's user stack has no room to spare.
	 * slip died on entry to the equivalent function with 6 KB of autos, before
	 * its first statement ran.  ppp_send() also builds a FRAMELEN buffer, so
	 * keep both off the stack. */
	static unsigned char pack[PACKLEN];
	int len, rop;

	len = ichan_reply_op(&psip, (char *)pack, PACKLEN, &rop);
	if (rop != NWR_READ)
		return;
	if (len > 0 && ipcp_open)
		ppp_send(P_IP, pack, len);
	/* else: link not up yet -- drop (IP won't route until IPCP). */
	ichan_post_read(&psip, PACKLEN);
}

int main(argc, argv)
int argc;
char **argv;
{
	fd_set rd;
	int nfds, ofd, kid;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: ppp serial-device\n");
		exit(1);
	}
	if ((serial_fd = open(argv[1], O_RDWR)) < 0)
	{
		perror("ppp: open serial"); exit(1);
	}
	rawtty(serial_fd);

	/* Reach psip through the inet daemon over one channel (both directions,
	 * replies demultiplexed by op tag). */
	if (ichan_open(&psip, PSIP_MINOR) < 0)
	{
		fprintf(stderr, "ppp: cannot attach to psip (is /etc/inet up?)\n");
		exit(1);
	}
	ichan_post_read(&psip, PACKLEN);

	/* Kick off LCP, then IPCP, by sending our first Configure-Requests. */
	ppp_send(P_LCP, (unsigned char *)"\1\1\0\4", 4);
	ppp_send(P_IPCP, (unsigned char *)"\1\1\0\4", 4);

	/*
	 * A SERIAL fd may not be given to select(): poll() reaches a character
	 * device through dpoll(), which needs the driver's CON to carry DFPOL
	 * and a c_poll entry (sys/coh/bio.c), and al.c has neither -- a tty
	 * answers POLLNVAL and libc's select() turns that into -1/EBADF for
	 * the whole call.
	 *
	 * The line is therefore read by a child doing an ordinary blocking
	 * read and handed over a pipe, which IS pollable.  All PROTOCOL state
	 * stays in the parent -- the child forwards raw bytes and knows
	 * nothing about framing or negotiation.  Only the parent touches the
	 * psip channel, so two request records can never interleave on it.
	 */
	if (pipe(rxpipe) < 0)
	{
		perror("ppp: pipe");
		exit(1);
	}
	if ((kid = fork()) < 0)
	{
		perror("ppp: fork");
		exit(1);
	}
	if (kid == 0)
	{
		unsigned char buf[512];
		int n;

		close(rxpipe[0]);
		for (;;)
		{
			n = read(serial_fd, buf, sizeof(buf));
			if (n <= 0)
			{
				fprintf(stderr, "ppp: serial read returned"
					" %d, reader exiting\n", n);
				exit(n < 0 ? 1 : 0);
			}
			if (write(rxpipe[1], (char *)buf, n) != n)
				exit(1);	/* parent gone */
		}
	}
	close(rxpipe[1]);

	ofd = psip.ic_replfd;
	nfds = (rxpipe[0] > ofd ? rxpipe[0] : ofd) + 1;
	for (;;)
	{
		FD_ZERO(&rd);
		FD_SET(rxpipe[0], &rd);
		FD_SET(ofd, &rd);
		if (select(nfds, &rd, (fd_set *)0, (fd_set *)0,
			(struct timeval *)0) < 0)
		{
			perror("ppp: select");
			exit(1);
		}
		if (FD_ISSET(rxpipe[0], &rd))
			serial_pump();
		if (FD_ISSET(ofd, &rd))
			outbound_pump();
	}
}
