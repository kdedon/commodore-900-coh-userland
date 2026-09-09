/*
inet/coh_sr.c -- COHERENT client/device multiplexer for the inet daemon.

Replaces Minix sr.c.  Minix's file server routed device opens on /dev/tcp etc.
to the inet server as DEV_* messages and moved data with sys_vcopy.  COHERENT
is monolithic and cannot make a userland daemon the backend of a /dev node, so
clients (libsocket.c) reach the daemon over FIFOs instead:

  - the daemon listens on the well-known rendezvous FIFO INET_RENDEZVOUS;
  - a client announces a new channel (one per socket) with an sr_hello record
    naming its request + reply FIFOs;
  - the daemon opens the request FIFO, allocates a channel, and thereafter
    reads requests from it and writes replies through coh_fdc.c, which owns the
    reply direction's descriptor.

A channel therefore costs the daemon ONE permanent descriptor.  Costing two --
the reply FIFO held open alongside the request FIFO -- is what limited a
twenty-descriptor daemon, and so the whole machine, to seven connections.

The per-layer callbacks (eth/psip/ip/tcp/udp open/close/read/write/ioctl/cancel)
are unchanged; each open is handed get_userdata/put_userdata/put_pkt, here inline
channel copies instead of sys_vcopy.

TWO HANDLES, ONE PER DIRECTION -- do not conflate them (that bug segfaulted the
daemon on the first ioctl).  Downwards, a layer identifies its own endpoint by
the fd its open returned: ip_open() hands back an index into ip_fd_table, and
ip_close/ip_read/ip_write/ip_ioctl/ip_cancel all expect THAT, so it is kept in
src_lfd and passed to every later callback.  Upwards, the layer calls back with
the srfd it was given at open, which here is the channel index, so
get_userdata/put_userdata/put_pkt route straight to the right channel.  Minix
sr.c keeps the same pair as srf_fd and the sr_fd_table index.  A layer open
returning a positive fd is therefore SUCCESS, not a status to relay: the client
sees OK.

Async: a read/write the layer cannot satisfy now returns SUSPEND; the channel is
left pending and completed later when the layer calls put_userdata (data ready)
or when a write's get_userdata is serviced -- routed to the same channel.  Every
reply carries an op tag (nwrepl.nwr_rop) so a client running more than one
request at once on a single channel (e.g. a link daemon with an outstanding READ
while it injects WRITEs) can demultiplex the replies; the tag is the op being
processed for a synchronous reply, or the suspended op for an async completion.

Copyright 1995 Philip Homburg (original); COHERENT port.
*/

#include "inet.h"
#include "generic/type.h"
#include "generic/assert.h"
#include "generic/buf.h"
#include "generic/sr.h"
#include "inet_ipc.h"
#include "coh_fdc.h"

#include <fcntl.h>
#include <sys/select.h>

THIS_FILE

#define N_MINORS	32		/* registered minor devices	*/
#define N_CHANS		FDC_NCHAN	/* concurrent open sockets	*/

typedef struct sr_minor {
	int		srm_inuse;
	int		srm_port;
	sr_open_t	srm_open;
	sr_close_t	srm_close;
	sr_read_t	srm_read;
	sr_write_t	srm_write;
	sr_ioctl_t	srm_ioctl;
	sr_cancel_t	srm_cancel;
} sr_minor_t;

typedef struct sr_chan {
	int	src_inuse;
	int	src_reqfd;		/* client -> daemon		*/
	/*
	 * The reply direction has no descriptor here.  It is reached through
	 * coh_fdc.c, which keeps one open while descriptors are spare and
	 * reopens it when they are not, so a channel costs the daemon ONE
	 * permanent descriptor instead of two -- see coh_fdc.h.  Held here as
	 * well, the machine's whole connection count was seven.
	 */
	int	src_minor;
	int	src_lfd;		/* the LAYER's fd (srm_open's	*/
					/* return), NOT the channel index */
	int	src_curop;		/* op currently being processed	*/
	/*
	 * Suspension is tracked PER OP CLASS, not in one slot.
	 *
	 * One slot said "an op is suspended, and it is this one", which is
	 * false as soon as a client keeps a READ outstanding while it writes --
	 * which is what slip does, and what every poll()-driven program does,
	 * because a request/reply channel is only pollable while a READ is
	 * armed.  The suspended READ then owned the slot, so the WRITE's own
	 * completion was tagged NWR_READ, the client sat waiting for a WRITE
	 * reply that would never be tagged as one, and the whole exchange
	 * stopped with both sides healthy.
	 */
	int	src_pendrd;		/* a READ is suspended		*/
	int	src_pendwr;		/* a WRITE is suspended		*/
	int	src_pendio;		/* an IOCTL is suspended	*/
	int	src_avail;		/* payload bytes of the current	*/
					/* request not yet read (nwr_dlen) */
	int	src_dataok;		/* a DATA reply was already sent */
					/* for the operation in progress */
	/*
	 * ANY reply already went out for the request being dispatched.
	 *
	 * A layer may answer a request ITSELF and then return NW_OK to say so:
	 * tcp_connect() does exactly that on every error path --
	 * tcp_reply_ioctl(EBADMODE) then `return NW_OK' -- and so do
	 * tcp_ioctl's EISCONN and ENOTCONN cases.  The dispatcher below then
	 * sent a SECOND reply carrying NW_OK, which is 0, i.e. success.
	 *
	 * The client reads one reply per request, so from there on it is one
	 * ahead of itself: connect()'s NWIOTCPCONN picked up the stray OK left
	 * by NWIOSTCPCONF and returned success while the real connect was still
	 * suspended, and the write() that followed found the descriptor not yet
	 * connected -- ENOTCONN, which ichan_fail maps to EIO.  That is the
	 * `write returned -1 (errno 5)' with a connection that had genuinely
	 * reached ESTABLISHED on the wire.
	 */
	int	src_replied;
} sr_chan_t;

/* One reply plus its payload, assembled for a single atomic write.  2 KB covers
 * the largest packet the link daemons ask for (PACKLEN). */
PRIVATE char sr_obuf[2048 + sizeof(nwrepl_t)];

PRIVATE sr_minor_t sr_minors[N_MINORS];
PRIVATE sr_chan_t  sr_chans[N_CHANS];
PRIVATE int	   sr_rv_fd = -1;	/* rendezvous FIFO		*/

FORWARD acc_t *sr_get_userdata ARGS(( int chan, size_t offset, size_t count,
							int for_ioctl ));
FORWARD int sr_put_userdata ARGS(( int chan, size_t offset, acc_t *data,
							int for_ioctl ));
FORWARD void sr_put_pkt ARGS(( int chan, acc_t *data, size_t datalen ));
FORWARD void sr_send_reply ARGS(( int chan, int status, int rop ));
FORWARD void sr_drain ARGS(( int chan ));
FORWARD int sr_readn ARGS(( int fd, char *buf, int n ));
#ifdef	SRTRACE
FORWARD int sr_num ARGS(( char *buf, int v ));
FORWARD void sr_trace ARGS(( char *what, int chan, int a, int b ));
#endif
FORWARD void sr_reply ARGS(( int chan, int status ));
FORWARD void sr_accept ARGS(( void ));
FORWARD void sr_chan_req ARGS(( int chan ));

/* Read exactly `n' bytes from a FIFO, looping over short reads. */
PRIVATE int sr_readn(fd, buf, n)
int fd;
char *buf;
int n;
{
	int got, k;

	for (got= 0; got < n; got += k)
	{
		k= read(fd, buf + got, n - got);
		if (k <= 0)
			return -1;
	}
	return n;
}

/*
 * Bounded bring-up trace of the channel protocol.  The stack answered the FIRST
 * request on a channel and then went quiet, and from a client that is
 * indistinguishable from "the reply was produced but tagged wrongly", "the reply
 * went to another channel", and "no reply was produced at all".  One line per
 * request and per reply, for the first few, tells those apart.
 *
 * write() to fd 2 rather than fprintf: this is stack code with no <stdio.h>.
 *
 * OFF unless -DSRTRACE.  It found the short-read bug and is worth keeping, but
 * left on it does harm: the trace goes to the CONSOLE, which is a serial line this
 * machine also drives, and the flood truncated the very output being read back --
 * the same way a printf in al.c's interrupt handler perturbed the transfer it was
 * measuring.  Turn it on when the channel protocol itself is in question.
 */
#ifndef	SRTRACE
#define	sr_trace(w, c, a, b)	/* compiled out; see above */
#else
#define SRTRACE_MAX	60

PRIVATE void sr_trace(what, chan, a, b)
char *what;
int chan;
int a;
int b;
{
	static int shown;
	char line[64];
	int n;

	if (shown >= SRTRACE_MAX)
		return;
	shown++;
	n= 0;
	line[n++]= 's'; line[n++]= 'r'; line[n++]= ':'; line[n++]= ' ';
	while (*what)
		line[n++]= *what++;
	line[n++]= ' '; line[n++]= 'c'; line[n++]= '0' + (chan % 10);
	line[n++]= ' '; line[n++]= 'a';
	n += sr_num(&line[n], a);
	line[n++]= ' '; line[n++]= 'b';
	n += sr_num(&line[n], b);
	line[n++]= '\n';
	(void)write(2, line, n);
}
#endif	/* SRTRACE */

#ifdef	SRTRACE
/* Decimal into buf, returning the length.  No stdio in this file. */
PRIVATE int sr_num(buf, v)
char *buf;
int v;
{
	char tmp[8];
	int n= 0, k= 0;

	if (v < 0)
	{
		buf[k++]= '-';
		v= -v;
	}
	do
		tmp[n++]= '0' + (v % 10);
	while ((v /= 10) != 0);
	while (n > 0)
		buf[k++]= tmp[--n];
	return k;
}
#endif	/* SRTRACE */

PUBLIC void sr_init()
{
	int i;

	for (i= 0; i<N_MINORS; i++)
		sr_minors[i].srm_inuse= 0;
	for (i= 0; i<N_CHANS; i++)
		sr_chans[i].src_inuse= 0;

	/* Listen for client channels.  A missing rendezvous FIFO is not fatal:
	 * a psip/eth-only daemon with no socket clients still runs. */
	sr_rv_fd= open(INET_RENDEZVOUS, O_RDWR);

	fdc_init(sr_rv_fd);
}

/* How many channels this daemon can hold, for whoever wants to say so. */
PUBLIC int sr_ceiling()
{
	return fdc_ceiling();
}

PUBLIC void sr_add_minor(minor, port, openf, closef, readf, writef, ioctlf,
	cancelf)
int minor;
int port;
sr_open_t openf;
sr_close_t closef;
sr_read_t readf;
sr_write_t writef;
sr_ioctl_t ioctlf;
sr_cancel_t cancelf;
{
	sr_minor_t *m;

	assert(minor >= 0 && minor < N_MINORS);
	m= &sr_minors[minor];
	m->srm_inuse= 1;
	m->srm_port= port;
	m->srm_open= openf;
	m->srm_close= closef;
	m->srm_read= readf;
	m->srm_write= writef;
	m->srm_ioctl= ioctlf;
	m->srm_cancel= cancelf;
}

/*
 * sr_fill_fdset / sr_handle_fds -- the main loop's window onto every client
 * channel: the rendezvous FIFO plus each open channel's request FIFO.
 */
PUBLIC void sr_fill_fdset(rd, pnfds)
fd_set *rd;
int *pnfds;
{
	int i, fd;

	if (sr_rv_fd >= 0)
	{
		FD_SET(sr_rv_fd, rd);
		if (sr_rv_fd >= *pnfds)
			*pnfds= sr_rv_fd + 1;
	}
	for (i= 0; i<N_CHANS; i++)
	{
		if (!sr_chans[i].src_inuse)
			continue;
		fd= sr_chans[i].src_reqfd;
		FD_SET(fd, rd);
		if (fd >= *pnfds)
			*pnfds= fd + 1;
	}
}

PUBLIC void sr_handle_fds(rd)
fd_set *rd;
{
	int i;

	if (sr_rv_fd >= 0 && FD_ISSET(sr_rv_fd, rd))
		sr_accept();
	for (i= 0; i<N_CHANS; i++)
	{
		if (sr_chans[i].src_inuse &&
			FD_ISSET(sr_chans[i].src_reqfd, rd))
			sr_chan_req(i);
	}
}

/*
 * Refuse a channel this daemon has no room for, so that the client fails
 * rather than waits.
 *
 * A client blocks reading the reply to the NWR_OPEN it has already written
 * (inet_chan.c ichan_open), and that request goes into a FIFO nobody will ever
 * read, so a refusal that answers nothing is indistinguishable from a daemon
 * that has stopped: every program that asked for a socket after the limit was
 * reached hung for ever, with no diagnostic anywhere.  One reply record on the
 * reply FIFO -- which needs one descriptor, and only for as long as the write
 * takes -- turns that into ENFILE at socket()/open().  The status travels as
 * the stack's own negative form (this file is _SYSTEM code, so ENFILE is
 * already -23), and 1..34 mean the same number on both systems, so
 * ichan_fail() hands the caller ENFILE and not some other errno.
 */
PRIVATE void sr_refuse(hello)
sr_hello_t *hello;
{
	nwrepl_t rep;
	int fd;

	/* The refusal needs a descriptor of its own, and the daemon is out of
	 * them by definition here -- take one back from a channel that is
	 * holding an idle reply descriptor, or the refusal cannot be sent and
	 * the client waits for ever, which is what a refusal exists to stop. */
	(void)fdc_spare();
	if ((fd= open(hello->sh_repl, O_WRONLY)) < 0)
		return;
	rep.nwr_status= ENFILE;
	rep.nwr_rop= NWR_OPEN;
	rep.nwr_dlen= 0;
	(void)write(fd, (char *)&rep, sizeof(rep));
	close(fd);
}

/* Accept a new channel announced on the rendezvous FIFO. */
PRIVATE void sr_accept()
{
	sr_hello_t hello;
	int i;

	if (read(sr_rv_fd, (char *)&hello, sizeof(hello)) != sizeof(hello))
		return;
	for (i= 0; i<N_CHANS && sr_chans[i].src_inuse; i++)
		;
	if (i == N_CHANS)
	{
		sr_refuse(&hello);	/* table full */
		return;
	}

	/*
	 * A free slot is not a free channel: the request FIFO below needs a
	 * descriptor that is never given back, and one more has to remain for
	 * replies to be written through.  fdc_room() asks the kernel whether
	 * both exist; without that question the table (sixteen) would be
	 * admitting channels the descriptors (twenty, four already spent)
	 * cannot carry, and the last few would be unanswerable rather than
	 * refused.
	 */
	if (!fdc_room() || !fdc_spare())
	{
		sr_refuse(&hello);	/* out of descriptors */
		return;
	}

	/*
	 * The request FIFO is opened O_RDONLY, and that is what makes a channel
	 * reclaimable.  A channel is freed when its request FIFO reports end of
	 * file (sr_chan_req below), which happens when the last writer -- the
	 * client -- goes away.  Held O_RDWR the daemon is a writer on it itself,
	 * so end of file can never arrive and no channel is ever freed: every
	 * client that exited without closing its socket, which is every client
	 * that simply exits, took a slot and its descriptor with it for the
	 * lifetime of the daemon.  The client opens both FIFOs before it
	 * announces itself, so there is a writer already and this open does not
	 * block.
	 *
	 * The reply FIFO is not opened here: coh_fdc.c opens it when there is
	 * something to say, and holds it only while descriptors are spare.
	 */
	sr_chans[i].src_reqfd= open(hello.sh_req, O_RDONLY);
	if (sr_chans[i].src_reqfd < 0)
	{
		sr_refuse(&hello);	/* out of descriptors */
		return;
	}
	/*
	 * The request FIFO's NAME is finished with the moment it is open: the
	 * client already holds it open too, and nothing ever opens it again by
	 * path.  Unlinking it here is what stops /tmp filling up.
	 *
	 * ichan_close() unlinks both FIFOs, but it runs from libsocket's
	 * close() override -- and exit(2) does not call close(2) on anything,
	 * it drops the process's descriptors in the kernel.  So every client
	 * that simply exits abandoned a pair of inodes.  /tmp has a thousand of them,
	 * so about five hundred client invocations exhausted the filesystem, and the
	 * symptom was a network that stopped working.  Tidying up HERE fixes it for
	 * clients that crash and clients that are killed as well, which no amount
	 * of care on the client side can.
	 *
	 * The reply FIFO's name cannot go the same way: coh_fdc.c reopens it
	 * by path whenever it has had to give the descriptor back.  It is
	 * unlinked when the channel ends, in fdc_forget().
	 */
	(void)unlink(hello.sh_req);
	fdc_hold(i, hello.sh_repl);
	sr_chans[i].src_inuse= 1;
	sr_chans[i].src_minor= -1;
	sr_chans[i].src_lfd= -1;
	/* Every per-request field, not just the suspension flags: channel slots
	 * are
	 * recycled, so anything left set by the previous occupant applies to the
	 * new client's first request. */
	sr_chans[i].src_pendrd= 0;
	sr_chans[i].src_pendwr= 0;
	sr_chans[i].src_pendio= 0;
	sr_chans[i].src_curop= 0;
	sr_chans[i].src_avail= 0;
	sr_chans[i].src_dataok= 0;
	sr_chans[i].src_replied= 0;
}

/* Process one request from channel `chan'. */
PRIVATE void sr_chan_req(chan)
int chan;
{
	nwreq_t req;
	sr_chan_t *ch;
	sr_minor_t *m;
	int r;

	NWMARK('q'); NWMARK('0' + chan);

	ch= &sr_chans[chan];
	if (read(ch->src_reqfd, (char *)&req, sizeof(req)) != sizeof(req))
	{
		NWMARK('e');
		/* EOF: client gone.  Close the layer fd if open, free channel. */
		if (ch->src_minor >= 0)
			(*sr_minors[ch->src_minor].srm_close)(ch->src_lfd);
		close(ch->src_reqfd);
		fdc_forget(chan);
		ch->src_inuse= 0;
		return;
	}

	ch->src_curop= req.nwr_op;	/* tag this request's replies */
	ch->src_avail= req.nwr_dlen;	/* what the client says follows */
	ch->src_dataok= 0;		/* nothing answered for it yet	*/
	ch->src_replied= 0;
	sr_trace("req ", chan, req.nwr_op, (int)req.nwr_count);

	if (req.nwr_op == NWR_OPEN)
	{
		NWMARK('o');
		if (req.nwr_minor < 0 || req.nwr_minor >= N_MINORS ||
			!sr_minors[req.nwr_minor].srm_inuse)
		{
			sr_reply(chan, EGENERIC);
			return;
		}
		m= &sr_minors[req.nwr_minor];
		r= (*m->srm_open)(m->srm_port, chan,
			sr_get_userdata, sr_put_userdata, sr_put_pkt);
		if (r < 0)
		{
			sr_reply(chan, r);
			return;
		}
		ch->src_minor= req.nwr_minor;
		ch->src_lfd= r;
		sr_reply(chan, OK);
		return;
	}

	if (ch->src_minor < 0)
	{
		NWMARK('x');
		sr_reply(chan, EGENERIC);
		return;
	}
	m= &sr_minors[ch->src_minor];

	switch (req.nwr_op)
	{
	case NWR_CLOSE:
		NWMARK('c');
		(*m->srm_close)(ch->src_lfd);
		sr_reply(chan, OK);
		close(ch->src_reqfd);
		fdc_forget(chan);
		ch->src_inuse= 0;
		return;

	case NWR_READ:
		NWMARK('1');
		r= (*m->srm_read)(ch->src_lfd, (size_t)req.nwr_count);
		if (r == SUSPEND)		/* completed via put_userdata */
			ch->src_pendrd= 1;
		else if (!ch->src_replied)
			sr_reply(chan, r);
		return;

	case NWR_WRITE:
		NWMARK('2');
		r= (*m->srm_write)(ch->src_lfd, (size_t)req.nwr_count);
		if (r == SUSPEND)
			ch->src_pendwr= 1;
		else if (!ch->src_replied)
			sr_reply(chan, r);
		return;

	case NWR_IOCTL:
		NWMARK('3');
		r= (*m->srm_ioctl)(ch->src_lfd, (ioreq_t)req.nwr_req);
		/*
		 * Drain the argument NOW, even if the ioctl suspended.  A layer
		 * fetches an ioctl argument synchronously or never -- it needs the
		 * value to decide what to do -- so anything still unread here will
		 * never be read.  Waiting for the reply is too late: an ioctl that
		 * suspends (NWIOTCPCONN, NWIOTCPLISTEN) leaves those bytes sitting
		 * in the request FIFO, which keeps the channel select()-readable,
		 * and the daemon then parses the argument as the next request.
		 * That is what tore the channel down under accept().
		 *
		 * A WRITE is different and is NOT drained here: its payload may be
		 * fetched after the suspension, so it is left for the reply.
		 */
		sr_drain(chan);
		if (r == SUSPEND)
			ch->src_pendio= 1;
		else if (!ch->src_replied)
			sr_reply(chan, r);
		return;

	case NWR_CANCEL:
		/*
		 * Only ask the layer to cancel something it is actually holding.
		 *
		 * A cancel races the completion it is chasing: the client sends it
		 * because a signal ended its wait, and the operation may have been
		 * answered in the meantime -- its reply already on the reply FIFO,
		 * unread.  The layers' cancel entry points assume the operation is
		 * in progress and do not check (tcp_cancel() dereferences
		 * tf_conn for a suspended NWIOTCPLISTEN, and the asserts that
		 * would have caught the other case are compiled out under NDEBUG),
		 * so calling one for an operation that has finished dereferences a
		 * connection that has been given up: the whole stack, for every
		 * client, on a request a program can send.
		 *
		 * The channel is what knows, so the test belongs here.  With
		 * nothing suspended the cancel is simply answered -- the client
		 * gets the completed operation's own reply, which is the truth: it
		 * finished before the signal arrived.
		 */
		switch (req.nwr_mode) {
		case SR_CANCEL_READ:	r= ch->src_pendrd; break;
		case SR_CANCEL_WRITE:	r= ch->src_pendwr; break;
		case SR_CANCEL_IOCTL:	r= ch->src_pendio; break;
		default:		r= 0; break;
		}
		if (r)
			(void)(*m->srm_cancel)(ch->src_lfd, req.nwr_mode);
		sr_reply(chan, EINTR);
		return;

	default:
		sr_reply(chan, EGENERIC);
		return;
	}
}

/*
 * get_userdata -- the layer wants `count' bytes of the client's write/ioctl
 * data.  They follow the request on the channel's request FIFO.
 */
/*
 * Which request does a completion from the layer answer?
 *
 * The layer calls get_userdata/put_userdata with no way of naming the request
 * -- it only has the channel -- so the channel has to work it out, and the
 * answer decides which reply tag the client demultiplexes on.
 *
 * DIRECTION decides it, and does so unambiguously.  put_userdata moves data,
 * or a final status, toward the CLIENT: of the non-ioctl ops only a READ ever
 * receives anything, so it answers the READ.  get_userdata moves data the
 * other way, and its count==0 form is how a layer acknowledges an inject
 * (psip_write, tcp_reply_write), so it answers the WRITE.  An ioctl names
 * itself in either direction.
 *
 * The rule this replaces asked WHEN the layer answered -- inside a dispatch,
 * tag the completion with the request being dispatched.  That is wrong exactly
 * when a suspended READ completes inside a WRITE on the same channel, which is
 * not a corner case but how the psip interface works: psipping injects a
 * packet and the stack's echo reply is delivered on that channel's armed READ,
 * still inside psip_write.  The reply went out tagged NWR_WRITE, the client
 * discarded it as its inject's acknowledgement, and psipping reported 0/2 with
 * a healthy stack at both ends.  slip drives the channel the same way, so the
 * whole boot-time network came up and answered nothing.
 */
PRIVATE int sr_whichop(chan, for_ioctl, toclient)
int chan;
int for_ioctl;
int toclient;
{
	if (for_ioctl)
		return NWR_IOCTL;
	return toclient ? NWR_READ : NWR_WRITE;
}

/* A reply has gone out for `rop': it is no longer suspended. */
PRIVATE void sr_pendclr(chan, rop)
int chan;
int rop;
{
	sr_chan_t *ch= &sr_chans[chan];

	if (rop == NWR_READ)
		ch->src_pendrd= 0;
	else if (rop == NWR_WRITE)
		ch->src_pendwr= 0;
	else if (rop == NWR_IOCTL)
		ch->src_pendio= 0;
}

PRIVATE acc_t *sr_get_userdata(chan, offset, count, for_ioctl)
int chan;
size_t offset;
size_t count;
int for_ioctl;
{
	acc_t *acc;
	char *data;
	int rop;

	/*
	 * count == 0 IS THE REPLY.  This entry point has two jobs, told apart by
	 * count, and only one of them fetches anything:
	 *
	 *	count > 0   fetch count bytes of the client's data
	 *	count == 0  complete a SUSPENDED operation, with `offset' carrying
	 *		    its status
	 *
	 * The second is how every asynchronous completion answers the client --
	 * tcp.c's reply_thr_get() is literally
	 *
	 *	(*tf_get_userdata)(tf_srfd, reply, (size_t)0, for_ioctl)
	 *
	 * and it is reached by tcp_reply_ioctl (connect, listen/accept, shutdown)
	 * and tcp_reply_write, and by psip_write to acknowledge an inject.
	 *
	 * Returning NULL here instead sent nothing at all, so a client that had
	 * been suspended stayed suspended: connect() and accept() never returned,
	 * with the connection ESTABLISHED and the channel left marked pending
	 * forever.  Nothing about that is visible from outside -- the handshake
	 * completes on the wire, the daemon keeps running, and the application
	 * simply never wakes.  It cost a long chase through TCP, the sequence-number
	 * comparisons, the event queue and the serial line before a trace at
	 * tcp_restart_connect showed the connect completing with no reply behind it.
	 *
	 * sr_put_userdata has had the mirror case (data == 0) all along; this is the
	 * same thing for the other direction, tagged the same way.
	 */
	if (count == 0)
	{
		/* Same rule as sr_put_userdata's completion: not if a data
		 * reply for this operation has already gone out. */
		rop= sr_whichop(chan, for_ioctl, 0);
		if (sr_chans[chan].src_dataok)
		{
			NWMARK('d');	/* suppressed: data already answered */
			sr_chans[chan].src_dataok= 0;
			sr_pendclr(chan, rop);
			return (acc_t *)0;
		}
		NWMARK('r');		/* the LAYER is answering */
		sr_send_reply(chan, (int)offset, rop);
		sr_pendclr(chan, rop);
		return (acc_t *)0;
	}
	acc= bf_memreq(count);
	data= ptr2acc_data(acc);
	/*
	 * Loop: a channel is a FIFO and a FIFO read returns what is AVAILABLE, not
	 * what was asked for.  A client writes its request record and payload as
	 * separate writes, so asking for the whole payload in one read routinely
	 * gets part of it -- and treating that as failure discarded complete, valid
	 * packets.  The client side had the same bug (inet_chan.c readn), where it
	 * was invisible under the fast instruction-level emulator and cost a day on
	 * the simulator.
	 */
	if (sr_readn(sr_chans[chan].src_reqfd, data, (int)count) != (int)count)
	{
		bf_afree(acc);
		return (acc_t *)0;
	}
	sr_chans[chan].src_avail -= (int)count;
	return acc;
}

/*
 * put_userdata -- the layer returns read/ioctl result data (or, with data==0,
 * a final status in `offset').  Reply, then stream any bytes to the client.
 * This is also the async-completion point for a suspended read.
 */
PRIVATE int sr_put_userdata(chan, offset, data, for_ioctl)
int chan;
size_t offset;
acc_t *data;
int for_ioctl;
{
	acc_t *acc;
	int size, n, rop;

	/* Tag by direction: this call answers the client, so it answers the
	 * READ unless it is an ioctl's own reply. */
	rop= sr_whichop(chan, for_ioctl, 1);
	if (data == 0)
	{
		/*
		 * The COMPLETION of an operation, carrying its status.
		 *
		 * Send it only if the operation is still outstanding.  A layer
		 * may answer with data and then complete: psip_send() does
		 *
		 *	put_userdata(srfd, 0, pack, FALSE)	the packet
		 *	put_userdata(srfd, result, NULL, FALSE)	the completion
		 *
		 * and the first call already replied -- with a record whose
		 * status IS the byte count and whose payload follows.  Sending
		 * a second record for the same READ put a header on the channel
		 * that promised `status' more bytes and had none behind it, so
		 * the client read the record and then blocked forever waiting
		 * for the payload.  That is what stopped slip dead after its
		 * first outbound frame, with the reader child still delivering
		 * into a pipe nobody came back to read.
		 *
		 * src_dataok is exactly the right test: it is set when a data
		 * reply goes out, so a set flag means "already replied".  A
		 * WRITE or an ioctl, whose data was fetched by get_userdata
		 * rather than replied to, never sets it and still completes.
		 */
		if (sr_chans[chan].src_dataok)
		{
			sr_chans[chan].src_dataok= 0;
			sr_pendclr(chan, rop);
			return OK;
		}
		sr_send_reply(chan, (int)offset, rop);
		sr_pendclr(chan, rop);
		return OK;
	}
	/*
	 * Record AND payload in ONE write.  Coherent guarantees a pipe write of
	 * less than PIPSIZE is atomic (sys/coh/pipe.c pwrite holds the inode across
	 * the transfer for exactly that reason), so writing them together makes a
	 * reply indivisible: a client that finds the channel readable and reads the
	 * record is then guaranteed the payload is already there.
	 *
	 * Written separately, the payload was a second, later write, and a client
	 * that got the record first had to block waiting for bytes that had not been
	 * produced yet -- which is a hang if they never are, and was a short read
	 * before the readers were taught to loop.  Nothing about the protocol needs
	 * two writes.
	 */
	size= bf_bufsize(data);
	if (size >= 0 && size <= (int)sizeof(sr_obuf) - (int)sizeof(nwrepl_t))
	{
		nwrepl_t *rp= (nwrepl_t *)sr_obuf;
		char *q= sr_obuf + sizeof(nwrepl_t);

		sr_drain(chan);

		rp->nwr_status= size;
		rp->nwr_rop= rop;
		rp->nwr_dlen= size;	/* the payload is in this same write */
		for (acc= data; acc; acc= acc->acc_next)
		{
			n= acc->acc_length;
			if (n)
			{
				memcpy(q, ptr2acc_data(acc), n);
				q += n;
			}
		}
		write(fdc_fd(chan), sr_obuf, (int)(q - sr_obuf));
		sr_chans[chan].src_dataok= 1;
		sr_chans[chan].src_replied= 1;
	}
	else
	{
		/* Too big for one write: fall back, and say so -- a client may
		 * block on the split.  The descriptor is taken ONCE and reused
		 * for the whole split reply: asked for again between the writes
		 * it could be a reopened one, and a channel that loses its
		 * descriptor half way through a reply leaves the header on the
		 * wire without its payload. */
		int rfd;

		rfd= fdc_fd(chan);
		{
			nwrepl_t rep;

			rep.nwr_status= size;
			rep.nwr_rop= rop;
			rep.nwr_dlen= size;
			sr_drain(chan);
			write(rfd, (char *)&rep, sizeof(rep));
		}
		sr_chans[chan].src_dataok= 1;
		for (acc= data; acc; acc= acc->acc_next)
		{
			n= acc->acc_length;
			if (n)
				write(rfd, ptr2acc_data(acc), n);
		}
	}
	bf_afree(data);
	sr_pendclr(chan, rop);
	return OK;
}

/* put_pkt -- packet-mode delivery (raw frame devices); same channel path. */
PRIVATE void sr_put_pkt(chan, data, datalen)
int chan;
acc_t *data;
size_t datalen;
{
	(void)sr_put_userdata(chan, 0, data, 0);
}


/*
 * Discard whatever the layer did not read of this request's payload.
 *
 * BEFORE the reply, not after: the client is blocked on that reply and writes
 * its next request the moment it arrives, so bytes discarded afterwards would
 * be the next request's, not this one's.
 */
PRIVATE void sr_drain(chan)
int chan;
{
	char scrap[64];
	int n, k;

	for (n= sr_chans[chan].src_avail; n > 0; n -= k)
	{
		k= (n > (int)sizeof(scrap)) ? (int)sizeof(scrap) : n;
		if (sr_readn(sr_chans[chan].src_reqfd, scrap, k) != k)
			break;
	}
	sr_chans[chan].src_avail= 0;
}

PRIVATE void sr_send_reply(chan, status, rop)
int chan;
int status;
int rop;
{
	nwrepl_t rep;

	sr_drain(chan);
	sr_chans[chan].src_replied= 1;	/* the request is answered */

	sr_trace("repl", chan, status, rop);
	rep.nwr_status= status;
	rep.nwr_rop= rop;
	rep.nwr_dlen= 0;		/* a bare status: nothing follows */
	write(fdc_fd(chan), (char *)&rep, sizeof(rep));
}

/* Synchronous reply: tagged with the op currently being processed. */
PRIVATE void sr_reply(chan, status)
int chan;
int status;
{
	NWMARK('R');
	sr_send_reply(chan, status, sr_chans[chan].src_curop);
}
