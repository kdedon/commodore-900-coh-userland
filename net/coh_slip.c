/*
 * coh_slip.c -- SLIP (RFC 1055) daemon for COHERENT/Z8001.
 *
 * Bridges a raw serial line to the inet stack's psip (point-to-point serial IP)
 * interface, so the ported Minix TCP/IP stack gets on the wire over an RS-232
 * port with no NIC and no new kernel code:
 *
 *	slip /dev/tty1
 *
 * psip is served by the inet daemon, not a kernel /dev node, so slip reaches it
 * over the daemon's control channel (inet_chan.c) -- NOT by opening /dev/psip.
 * A SINGLE channel carries both directions: an outstanding async READ collects
 * the packets the stack wants to send, while inbound frames are injected with
 * async WRITEs.  Replies are demultiplexed by their op tag (ichan_reply_op), so
 * the psip minor only needs a single open.
 *
 *	inbound : serial -> SLIP-decode -> ichan_post_write (inject)
 *	outbound: READ reply -> SLIP-encode -> serial, then re-arm the READ
 */
#include <sys/types.h>
#include <sgtty.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/select.h>
#include "inet_ipc.h"
#include "inet_chan.h"

#define END	0300		/* frame delimiter			*/
#define ESC	0333		/* byte-stuffing escape			*/
#define ESC_END	0334
#define ESC_ESC	0335

#define PACKLEN	2048
#define SLIPLEN	(2*PACKLEN+2)

#define PSIP_MINOR	0	/* psip interface 0 (if2minor(0,PSIP_DEV_OFF)) */

int serial_fd;
int rxpipe[2];			/* reader child -> parent (see main)	*/
struct ichan psip;		/* one bidirectional channel to psip	*/

/*
 * Frame counters.  Silence is the hard part of watching a link daemon: "no
 * reply" can mean the bytes never arrived, the frame never decoded, the stack
 * never answered, or slip itself died -- and all four look the same from the
 * far end.  These separate them, and they emit nothing: they are read out of
 * memory (see slipstat below), so they cost a few stores and no line of output.
 */
long slip_rx;			/* raw bytes read from the line		*/
long slip_frames;		/* complete inbound frames decoded	*/
long slip_out;			/* complete outbound frames written	*/

/*
 * The same counters where the HOST can read them without the guest's help.
 *
 * The trace file needs the guest to run `sync' before anything reaches the
 * image -- so the moment the guest stops responding, the instrument stops
 * working, and "the file is not there" is indistinguishable from "the file was
 * never written".  That is exactly the case worth instrumenting.
 *
 * This block carries an eight-byte magic, so a host can find it by searching
 * the simulator's physical memory (/memory/find) and read the counters that
 * follow (/memory/read) with the machine wedged, stopped, or running.  Fields
 * are long and big-endian, which is what a Z8001 writes.
 *
 * ss_out is incremented BEFORE the write to the line and ss_writes AFTER it
 * returns, on purpose: out > writes means the process is blocked inside
 * write(), which no other measurement here can distinguish from a write that
 * completed and produced nothing.
 */
struct slipstat {
	char	ss_magic[8];
	long	ss_wakes;	/* select() returns			*/
	long	ss_rx;		/* bytes taken from the reader child	*/
	long	ss_in;		/* inbound frames decoded		*/
	long	ss_injected;	/* inbound frames handed to the stack	*/
	long	ss_pumps;	/* reply_pump() entries			*/
	long	ss_replies;	/* replies read from the channel	*/
	long	ss_out;		/* outbound frames, write STARTED	*/
	long	ss_writes;	/* outbound frames, write RETURNED	*/
	/*
	 * Written only by the READER CHILD.  fork() gives it its own copy of this
	 * block, and the host's search finds every copy in memory, so the copy
	 * with a non-zero ss_kidrx IS the child -- which is how "the child is
	 * blocked in read()" is told apart from "the child delivered bytes and the
	 * parent never woke for the pipe".  Those two are identical from outside
	 * and have nothing in common as bugs.
	 */
	long	ss_kidreads;	/* tty read() calls STARTED		*/
	long	ss_kidrx;	/* bytes the tty read() RETURNED	*/
	long	ss_kidwr;	/* bytes handed to the pipe		*/
	/*
	 * What select() actually reported, per descriptor.  The trace file
	 * records this too, but it only reaches the image after a guest `sync',
	 * and the whole point is the case where the process has stopped.  With
	 * these, "select never said the pipe was ready" and "it said so and the
	 * read went nowhere" stop looking alike.
	 */
	long	ss_wpipe;	/* wakes with the reader pipe ready	*/
	long	ss_wchan;	/* wakes with the psip channel ready	*/
	/*
	 * The outbound frame, at both ends of this function: what the stack
	 * handed over, and what went to the line.  The host saw a frame missing
	 * exactly its first byte, and "the daemon delivered it short", "slip
	 * encoded it wrong" and "the wire lost a byte" are three different bugs
	 * that look the same from the far end.
	 */
	long	ss_len;		/* bytes ichan_reply_op returned		*/
	long	ss_pack0;	/* first four bytes of the IP packet	*/
	long	ss_o;		/* bytes handed to write()		*/
	long	ss_sl0;		/* first four bytes of the SLIP frame	*/
	long	ss_wrc;		/* what write() returned		*/
};

struct slipstat slipstat = { { 'S', 'L', 'I', 'P', 'S', 'T', 'A', 'T' } };

/*
 * Bounded per-frame trace of the link, OFF unless -DSLIPTRACE, the same way
 * coh_sr.c gates its channel trace on -DSRTRACE.  A working link has nothing to
 * say per frame, and left on this one writes a file into the ROOT filesystem on
 * every boot and slows a transfer down with its own logging.  Turn it on when
 * the framing or the psip handoff is in question; the counters above stay
 * readable either way.
 *
 * To a FILE, not the console.  The console is a serial line this machine also
 * drives, and a trace written there both floods the line under test and gets
 * truncated by whatever is reading the console back.  Flushed per line so a hang
 * leaves a complete record up to the point it stopped.
 *
 * On the ROOT filesystem, not /tmp: a single-user boot has not run rc, so /tmp
 * is not mounted and a path there would land somewhere the host cannot find.
 */
#ifndef	SLIPTRACE
#define	slip_trace(what, a, b)	/* compiled out; see above */
#else
#define TRACE_MAX	40
#define TRACEFILE	"/slip.trace"

FILE *tracef;

void slip_trace(what, a, b)
char *what;
long a;
long b;
{
	static int shown;

	if (shown >= TRACE_MAX)
		return;
	if (tracef == NULL && (tracef= fopen(TRACEFILE, "w")) == NULL)
	{
		shown= TRACE_MAX;	/* stop trying */
		return;
	}
	shown++;
	fprintf(tracef, "%s a=%ld b=%ld (rx %ld in %ld out %ld)\n",
		what, a, b, slip_rx, slip_frames, slip_out);
	fflush(tracef);
}
#endif	/* SLIPTRACE */

/* Put the serial line into 8-bit raw mode at 9600 bps. */
void rawtty(fd)
int fd;
{
	struct sgttyb sg;

	if (gtty(fd, &sg) < 0) { perror("slip: gtty"); exit(1); }
	sg.sg_ispeed = B9600;
	sg.sg_ospeed = B9600;
	sg.sg_flags = RAW;
	if (stty(fd, &sg) < 0) { perror("slip: stty"); exit(1); }
}

/* One serial read: SLIP-decode and inject each recovered IP packet. */
void serial_pump()
{
	static unsigned char pack[PACKLEN];
	static int len, esc;
	unsigned char in[512];
	int n, i;

	/* From the reader child's pipe, not the tty -- see main(). */
	n = read(rxpipe[0], (char *)in, sizeof(in));
	if (n <= 0)
	{
		/* Say why: a reader that died must not look identical to a
		 * peer that never sent anything. */
		fprintf(stderr, "slip: rx pipe returned %d, exiting\n", n);
		exit(n < 0 ? 1 : 0);
	}
	slip_rx += n;
	slipstat.ss_rx = slip_rx;
	for (i = 0; i < n; i++)
	{
		int c = in[i];

		if (esc)
		{
			esc = 0;
			if (c == ESC_END) c = END;
			else if (c == ESC_ESC) c = ESC;
		}
		else if (c == ESC) { esc = 1; continue; }
		else if (c == END)
		{
			if (len > 0)
			{
				slip_frames++;
				slipstat.ss_in = slip_frames;
				slip_trace("in-frame", (long)len, 0L);
				/*
				 * Arm the outbound direction BEFORE injecting,
				 * so the stack cannot want to send while no READ
				 * is outstanding.  net/test/psipping.c does this
				 * per packet and gets 3/3 replies; slip armed
				 * once at startup and got none, with the frame
				 * demonstrably injected (in 1, out 0).
				 */
				ichan_post_read(&psip, PACKLEN);
				slipstat.ss_injected++;
				if (ichan_post_write(&psip, (char *)pack, len)
				    < 0)
					fprintf(stderr, "slip: psip write"
						" failed\n");
			}
			len = 0;
			continue;
		}
		if (len < PACKLEN)
			pack[len++] = c;
		else
			len = 0;	/* overrun: drop frame */
	}
}

/* A reply is waiting on the psip channel: a READ reply is an outbound packet to
 * SLIP-encode onto the line (then re-arm the READ); a WRITE reply is just the
 * ack for a previous inject, so ignore it. */
void reply_pump()
{
	/*
	 * STATIC, not auto.  These are 2048 + 4098 bytes: a six-kilobyte stack
	 * frame in one function, which this machine's user stack does not have --
	 * MADSIZE caps a stack segment at 32K and the initial allocation is far
	 * smaller.  As autos they killed slip the first time an outbound packet
	 * arrived, before the function's first statement ran, which is why its trace
	 * showed the wake with the reply ready and then nothing at all:
	 *
	 *	wake a=0 b=1 (rx 38 in 1 out 0)
	 *	<no pump-enter>
	 *
	 * serial_pump() has used static buffers all along; this was the asymmetry.
	 * Only one instance of each is needed -- the daemon is single-threaded and
	 * neither buffer outlives the call.
	 */
	static unsigned char pack[PACKLEN];
	static unsigned char sl[SLIPLEN];
	int len, i, o, rop;

	slipstat.ss_pumps++;
	slip_trace("pump-enter", 0L, 0L);
	len = ichan_reply_op(&psip, (char *)pack, PACKLEN, &rop);
	slipstat.ss_replies++;
	slipstat.ss_len = len;
	if (len >= 4)
		slipstat.ss_pack0 = ((long)pack[0] << 24) |
			((long)pack[1] << 16) | ((long)pack[2] << 8) |
			(long)pack[3];
	slip_trace("pump-got", (long)len, (long)rop);
	if (rop != NWR_READ)
	{
		/* A WRITE's acknowledgement.  Traced because "the stack never
		 * answered" and "reply_pump never ran" are different faults that
		 * look identical from the far end. */
		slip_trace("ack", (long)len, (long)rop);
		return;
	}
	if (len > 0)
	{
		o = 0;
		sl[o++] = END;
		for (i = 0; i < len; i++)
		{
			switch (pack[i])
			{
			case END: sl[o++] = ESC; sl[o++] = ESC_END; break;
			case ESC: sl[o++] = ESC; sl[o++] = ESC_ESC; break;
			default:  sl[o++] = pack[i];
			}
		}
		sl[o++] = END;
		slip_out++;
		slipstat.ss_out = slip_out;
		slip_trace("out-frame", (long)len, 0L);
		slipstat.ss_o = o;
		slipstat.ss_sl0 = ((long)sl[0] << 24) | ((long)sl[1] << 16) |
			((long)sl[2] << 8) | (long)sl[3];
		slipstat.ss_wrc = write(serial_fd, sl, o);
		slipstat.ss_writes++;
	}
	ichan_post_read(&psip, PACKLEN);	/* re-arm outbound */
}

int main(argc, argv)
int argc;
char **argv;
{
	fd_set rd;
	int nfds, rfd, kid;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: slip serial-device\n");
		exit(1);
	}
	if ((serial_fd = open(argv[1], O_RDWR)) < 0)
	{
		perror("slip: open serial");
		exit(1);
	}
	rawtty(serial_fd);

	if (ichan_open(&psip, PSIP_MINOR) < 0)
	{
		fprintf(stderr, "slip: cannot attach to psip (is /etc/inet up?)\n");
		exit(1);
	}
	ichan_post_read(&psip, PACKLEN);	/* arm the outbound direction */

	/* Say so, once both ends are attached.  A peer that starts sending
	 * before this point is talking to a line nobody has opened: alclose()
	 * leaves the channel's receive interrupts disabled between readers, so
	 * bytes arriving then are assembled into the SCC's three-deep FIFO and
	 * announced to nobody -- and a SLIP frame that loses its leading bytes
	 * is simply gone.  Scripted peers should wait for this line rather than
	 * sleep (hostbuild/slip-test.py does). */
	printf("slip: ready on %s\n", argv[1]);
	fflush(stdout);

	/*
	 * Open the trace immediately, when there is one.  Created lazily its
	 * absence would be ambiguous: it could mean no select() wake ever
	 * happened, or that the fopen itself failed (a read-only root, a missing
	 * directory).  A "start" line separates those, and the failure is reported
	 * on the console -- one line, which the console can carry.
	 */
	slip_trace("start", (long)serial_fd, (long)psip.ic_replfd);
#ifdef	SLIPTRACE
	if (tracef == NULL)
		fprintf(stderr, "slip: cannot write %s\n", TRACEFILE);
#endif

	/*
	 * A SERIAL fd may not be given to select().  poll() reaches a character
	 * device through dpoll(), which requires the driver's CON to carry DFPOL
	 * and a c_poll entry (bio.c); al.c has neither, so a tty answers
	 * POLLNVAL -- and libc's select(), built over poll(), turns POLLNVAL into
	 * -1/EBADF for the whole call.  The obvious loop
	 *
	 *	if (select(...) < 0) continue;
	 *
	 * therefore span forever without ever reading the line: slip was alive,
	 * the peer's frames sat unread in the tty queue, and nothing was logged
	 * because nothing happened.  That is what "0/3 replies, guest sent no
	 * frames" was.
	 *
	 * So the serial line is read by a dedicated CHILD doing an ordinary
	 * blocking read -- which is what a tty supports well -- and handed to the
	 * parent through a PIPE, which IS pollable (upoll's IFPIPE case).  The
	 * parent then selects only over pipes: the child's, and the psip channel's
	 * reply FIFO.
	 *
	 * Only the parent touches the psip channel.  Splitting the two directions
	 * into two processes that both wrote to the channel would interleave two
	 * request records on one FIFO and corrupt the protocol; the child writes
	 * to nothing but the pipe.  The fd is shared for opposite directions --
	 * child reads, parent writes -- which is fine.
	 *
	 * The proper fix is a c_poll in al.c so select() works on a tty at all;
	 * it costs kernel text this build does not have (see WS2).
	 */
	if (pipe(rxpipe) < 0)
	{
		perror("slip: pipe");
		exit(1);
	}
	if ((kid = fork()) < 0)
	{
		perror("slip: fork");
		exit(1);
	}
	if (kid == 0)
	{
		unsigned char buf[512];
		int n;

		close(rxpipe[0]);
		for (;;)
		{
			slipstat.ss_kidreads++;
			n = read(serial_fd, buf, sizeof(buf));
			if (n > 0)
				slipstat.ss_kidrx += n;
			if (n <= 0)
			{
				fprintf(stderr, "slip: serial read returned"
					" %d, reader exiting\n", n);
				exit(n < 0 ? 1 : 0);
			}
			if (write(rxpipe[1], (char *)buf, n) != n)
				exit(1);	/* parent gone */
			slipstat.ss_kidwr += n;
		}
	}
	close(rxpipe[1]);

	rfd = psip.ic_replfd;
	nfds = (rxpipe[0] > rfd ? rxpipe[0] : rfd) + 1;
	for (;;)
	{
		FD_ZERO(&rd);
		FD_SET(rxpipe[0], &rd);
		FD_SET(rfd, &rd);
		if (select(nfds, &rd, (fd_set *)0, (fd_set *)0,
			(struct timeval *)0) < 0)
		{
			/* Do NOT spin: an unpollable fd here is a permanent
			 * condition, not a transient one, and a `continue'
			 * on error is a busy loop. */
			perror("slip: select");
			exit(1);
		}
		slipstat.ss_wakes++;
		if (FD_ISSET(rxpipe[0], &rd))
			slipstat.ss_wpipe++;
		if (FD_ISSET(rfd, &rd))
			slipstat.ss_wchan++;
		slip_trace("wake", (long)FD_ISSET(rxpipe[0], &rd),
			(long)FD_ISSET(rfd, &rd));
		if (FD_ISSET(rxpipe[0], &rd))
			serial_pump();
		if (FD_ISSET(rfd, &rd))
			reply_pump();
	}
}
