/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * inet_chan.c -- client helper for the inet daemon's control channel.
 * See inet_chan.h.  Uses the raw syscalls _rawread/_rawwrite/_rawclose
 * (rawsys.s) so it never re-enters libsocket's read/write/close overrides.
 */
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "inet_ipc.h"
#include "inet_chan.h"

extern int _rawread();
extern int _rawwrite();
extern int _rawclose();

static int chan_seq;

/*
 * Translate a status returned by the inet daemon into COHERENT's errno, and set
 * errno to it.  Always returns -1, so callers read `return ichan_fail(status)'.
 *
 * The stack is Minix code and answers with NEGATIVE Minix error numbers.  Those
 * cannot be handed to a user program as-is: perror() does
 *
 *	if (errno < sys_nerr) es = sys_errlist[errno];
 *
 * with no lower bound, so a negative errno indexes sys_errlist[] backwards and
 * prints whatever is in front of it.  (perror() has been given the missing
 * bound as well, but the value still has to be right.)
 *
 * Codes 1..34 are numerically identical in both systems, so they pass through.
 * Past that the sets diverge -- Minix 38 is ENOSYS where COHERENT 38 is EDATTN,
 * "device needs attention", which would be an actively misleading thing to
 * report -- and the two systems number the networking errnos differently:
 * ECONNREFUSED is 59 there and 45 here.  The mapping below is explicit and
 * numeric, naming both sides in the comments, rather than relying on which
 * <errno.h> won the include search: net/include/errno.h shadows COHERENT's for
 * anything built with -Inet/include, which includes this file.
 */
/*
 * Read exactly `n' bytes, looping over short reads.
 *
 * A channel is a FIFO, and a read from a FIFO returns what is AVAILABLE, not what
 * was asked for.  The daemon writes a reply record and then streams the payload as
 * separate writes (coh_sr.c sr_put_userdata walks the acc chain), so a reader that
 * asks for the whole payload in one read routinely gets part of it.
 *
 * Treating that as failure -- `if (_rawread(fd, buf, n) != n) return -1' -- threw
 * away complete, valid packets.  It was invisible under the instruction-level
 * emulator, where the writer always won the race, and showed up on the simulator
 * as "read status -1" for the first ICMP reply and as SLIP moving no packets at
 * all.  The emulator was not wrong about the protocol; it was just too fast to
 * expose a reader bug.
 *
 * Returns n, or -1 on a real end-of-file or error.
 */
static int readn(fd, buf, n)
int fd;
char *buf;
int n;
{
	int got, k;

	for (got= 0; got < n; got += k)
	{
		k= _rawread(fd, buf + got, n - got);
		if (k <= 0)
			return -1;
	}
	return n;
}

/*
 * Throw away `n' payload bytes.  A payload nobody wants still has to leave the
 * FIFO: the channel is a byte stream, so bytes left behind are read as the next
 * reply record and everything after them is parsed at the wrong offset.
 */
static int ichan_drain(c, n)
struct ichan *c;
int n;
{
	char scrap[32];
	int k;

	for (; n > 0; n -= k)
	{
		k= (n > (int)sizeof(scrap)) ? (int)sizeof(scrap) : n;
		if (readn(c->ic_replfd, scrap, k) != k)
			return -1;
	}
	return 0;
}

/*
 * Write a request and its payload as ONE write.
 *
 * The daemon reads the record, sees nwr_dlen, and may go straight on to read
 * the payload -- so two writes leave a window where it is blocked on bytes the
 * client has not sent yet.  Coherent guarantees a pipe write below PIPSIZE
 * (5120) is atomic: pwrite() holds the inode across the transfer for exactly
 * this reason.  One write closes the window, and the daemon does the same thing
 * for replies (coh_sr.c sr_put_userdata).
 *
 * Bigger than the buffer, the two writes are unavoidable; the daemon's readers
 * loop over short reads, so it still works, just without the guarantee.
 */
static char ichan_obuf[2048 + sizeof(nwreq_t)];

static int ichan_send(fd, rq, data, len)
int fd;
nwreq_t *rq;
char *data;
int len;
{
	if (len > 0 && data != (char *)0 &&
	    len <= (int)sizeof(ichan_obuf) - (int)sizeof(nwreq_t))
	{
		memcpy(ichan_obuf, (char *)rq, sizeof(*rq));
		memcpy(ichan_obuf + sizeof(*rq), data, len);
		return (_rawwrite(fd, ichan_obuf, (int)sizeof(*rq) + len)
			== (int)sizeof(*rq) + len) ? 0 : -1;
	}
	if (_rawwrite(fd, (char *)rq, sizeof(*rq)) != sizeof(*rq))
		return -1;
	if (len > 0 && data != (char *)0 && _rawwrite(fd, data, len) != len)
		return -1;
	return 0;
}

int ichan_fail(status)
int status;
{
	int e;

	e= (status < 0) ? -status : status;
	if (e >= 1 && e <= 34)
		;				/* identical in both systems */
	else switch (e) {
	case 35: e= 40; break;	/* EDEADLK       -> EDEADLK */
	case 37: e= 41; break;	/* ENOLCK        -> ENOLCK */
	case 51: e= 12; break;	/* EOUTOFBUFS    -> ENOMEM */
	case 54: e= 11; break;	/* EWOULDBLOCK   -> EAGAIN */
	case 56: e= 48; break;	/* EDSTNOTRCH    -> EDSTNOTRCH */
	case 57: e= 50; break;	/* EISCONN       -> EISCONN */
	case 58: e= 44; break;	/* EADDRINUSE    -> EADDRINUSE */
	case 59: e= 45; break;	/* ECONNREFUSED  -> ECONNREFUSED */
	case 60: e= 46; break;	/* ECONNRESET    -> ECONNRESET */
	case 61: e= 47; break;	/* ETIMEDOUT     -> ETIMEDOUT */
	case 64: e= 49; break;	/* ENOTCONN      -> ENOTCONN */
	case 65: e= 51; break;	/* ESHUTDOWN     -> ESHUTDOWN */
	case 66: e= 52; break;	/* ENOCONN       -> ENOCONN */
	case 36:		/* ENAMETOOLONG */
	case 38:		/* ENOSYS */
	case 39:		/* ENOTEMPTY */
	case 50:		/* EPACKSIZE */
	case 52:		/* EBADIOCTL */
	case 53: e= 22; break;	/* EBADMODE      -> EINVAL */
	case 55: e= 22; break;	/* EBADDEST      -> EINVAL */
	case 62: e= 42; break;	/* EURG          -> EURG */
	case 63: e= 43; break;	/* ENOURG        -> ENOURG */
	default: e= 5; break;	/* everything else, incl. EGENERIC, ELOCKED
				 * and EBADCALL -> EIO */
	}
	errno= e;
	return -1;
}

/*
 * Abandon a half-built channel: close whatever descriptors it got and remove
 * the two FIFOs, then report failure.
 *
 * Only for the paths where the DAEMON is known not to hold the channel -- an
 * open that failed before the rendezvous, or one the daemon refused.  Once a
 * channel is accepted the FIFOs belong to the daemon's lifetime, not this
 * function's: the reply FIFO in particular is reopened by path whenever the
 * daemon has had to give its descriptor back.
 */
static int ichan_giveup(c)
struct ichan *c;
{
	if (c->ic_reqfd >= 0)
		_rawclose(c->ic_reqfd);
	if (c->ic_replfd >= 0)
		_rawclose(c->ic_replfd);
	c->ic_reqfd= -1;
	c->ic_replfd= -1;
	(void)unlink(c->ic_reqpath);
	(void)unlink(c->ic_replpath);
	return -1;
}

int ichan_open(c, minor)
struct ichan *c;
int minor;
{
	nwreq_t rq;
	nwrepl_t rp;
	sr_hello_t hello;
	int rvfd, pid, seq;

	/* Before anything can fail: ichan_giveup() closes whatever these name,
	 * and a caller whose struct was not zeroed -- or was zeroed, which
	 * makes them both fd 0 -- would otherwise have standard input closed
	 * on the way out of a failed open. */
	c->ic_reqfd= -1;
	c->ic_replfd= -1;

	pid= getpid();
	seq= chan_seq++;
	sprintf(c->ic_reqpath, "/tmp/ic%d.%d.q", pid, seq);
	sprintf(c->ic_replpath, "/tmp/ic%d.%d.r", pid, seq);
	(void)unlink(c->ic_reqpath);
	(void)unlink(c->ic_replpath);
	if (mknod(c->ic_reqpath, 010000 | 0600, 0) < 0 ||
	    mknod(c->ic_replpath, 010000 | 0600, 0) < 0)
		return ichan_giveup(c);
	if ((c->ic_reqfd= open(c->ic_reqpath, O_RDWR)) < 0 ||
	    (c->ic_replfd= open(c->ic_replpath, O_RDWR)) < 0)
		return ichan_giveup(c);

	hello.sh_id= (long)pid * 100 + seq;
	strcpy(hello.sh_req, c->ic_reqpath);
	strcpy(hello.sh_repl, c->ic_replpath);
	if ((rvfd= open(INET_RENDEZVOUS, O_WRONLY)) < 0)
		return ichan_giveup(c);
	_rawwrite(rvfd, (char *)&hello, sizeof(hello));
	_rawclose(rvfd);

	rq.nwr_op= NWR_OPEN;
	rq.nwr_minor= minor;
	rq.nwr_mode= 0; rq.nwr_dlen= 0; rq.nwr_req= 0; rq.nwr_count= 0;
	_rawwrite(c->ic_reqfd, (char *)&rq, sizeof(rq));
	if (_rawread(c->ic_replfd, (char *)&rp, sizeof(rp)) != sizeof(rp))
		return -1;
	if (rp.nwr_status < 0)
	{
		/*
		 * REFUSED: the daemon has no channel, so nothing will ever
		 * open these FIFOs again and nobody else will remove them.
		 * The accepted case is the daemon's to tidy (coh_sr.c
		 * sr_accept, coh_fdc.c fdc_forget) -- and must NOT be tidied
		 * here, because the reply FIFO is reopened by path for as
		 * long as the channel lives.
		 */
		(void)ichan_giveup(c);
		return ichan_fail((int)rp.nwr_status);
	}
	return 0;
}

/*
 * Take one reply record off the channel and, if it answers the READ this
 * channel has armed, park its payload in the hold buffer.
 *
 * Returns the record's op, or -1.  Every caller that waits for a PARTICULAR
 * reply has to go through here now that a READ can be outstanding while
 * something else is in flight: the reply FIFO is in order, so a read that
 * completes first puts its record in front of the one being waited for, and a
 * caller that read it as its own would take a byte count for a result and
 * then leave the payload behind to be parsed as the next record.
 */
static int ichan_take(c, want, buf, max)
struct ichan *c;
int want;
char *buf;
int max;
{
	nwrepl_t rp;
	int room, n;

	c->ic_got= 0;
	/*
	 * errno is cleared first because a caller reads it to tell a SIGNAL from
	 * a dead channel (ichan_await), and an end-of-file leaves whatever was
	 * there.  A stale EINTR would then be taken for a fresh one.
	 */
	errno= 0;
	if (_rawread(c->ic_replfd, (char *)&rp, sizeof(rp)) != sizeof(rp))
		return -1;
	c->ic_status= (int)rp.nwr_status;
	if (rp.nwr_rop == NWR_READ)
		c->ic_posted= 0;
	/*
	 * A READ's payload always goes to the hold buffer, whoever is waiting:
	 * it is stream data, and the only place it can be kept for the read
	 * that will eventually ask for it.  Any other reply's payload belongs
	 * to the caller if this is the reply it wanted, and is discarded if it
	 * is not -- but it must leave the FIFO either way.
	 */
	if (rp.nwr_rop != NWR_READ)
	{
		n= (int)rp.nwr_dlen;
		if (rp.nwr_rop == want && buf && max > 0)
		{
			c->ic_got= (n > max) ? max : n;
			if (readn(c->ic_replfd, buf, c->ic_got) != c->ic_got)
				return -1;
			n -= c->ic_got;
		}
		if (ichan_drain(c, n) < 0)
			return -1;
		return (int)rp.nwr_rop;
	}
	if (rp.nwr_dlen > 0)
	{
		/* Compact first: the room this reply was posted against was
		 * measured after the same compaction (ichan_arm). */
		if (c->ic_hoff > 0)
		{
			int held= c->ic_hlen - c->ic_hoff;

			if (held > 0)
				memcpy(c->ic_hold, c->ic_hold + c->ic_hoff,
					held);
			c->ic_hlen= held;
			c->ic_hoff= 0;
		}
		room= ICHAN_HOLD - c->ic_hlen;
		n= (int)rp.nwr_dlen;
		if (n > room)
			n= room;
		if (n > 0 && readn(c->ic_replfd, c->ic_hold + c->ic_hlen, n)
				!= n)
			return -1;
		c->ic_hlen += n;
		/* Anything that did not fit still has to leave the FIFO, or
		 * every later reply on this channel is misparsed. */
		if ((int)rp.nwr_dlen > n
		    && ichan_drain(c, (int)rp.nwr_dlen - n) < 0)
			return -1;
	}
	return (int)rp.nwr_rop;
}

/*
 * Wait for the reply to `op', parking any completed READ that arrives first.
 *
 * Returns 0 once that reply has been taken and -1 if the CHANNEL failed; the
 * reply's own status is c->ic_status, which is a separate question.  Returning
 * the status directly would confuse the two, because a status is a negative
 * Minix errno and -1 is a real one (EPERM).
 */
/*
 * Give up on `op', and tell the DAEMON so.
 *
 * A signal caught while blocked on the reply FIFO ends the read with EINTR and
 * unwinds the caller -- but the daemon has an operation suspended, and nothing
 * has told it that the answer is no longer wanted.  It stays suspended, holding
 * whatever the layer allocated for it, and the next operation the client starts
 * on the same channel runs against a descriptor the daemon still believes is
 * busy.  For NWIOTCPLISTEN with an alarm on it -- which is how talk(1) rings and
 * how any program puts a timeout on a network call -- the abandoned listen left
 * its connection attached to the descriptor, and the retry's tcp_listen()
 * attached a SECOND connection to the same descriptor: the fd then wrote and
 * read on one of them while the peer's handshake had completed on the other.
 * The wire showed a segment ACKed by the far stack, retransmitted forever,
 * and a read that never returned.
 *
 * NWR_CANCEL is the request the daemon already understands for this (coh_sr.c;
 * the layers' cancel entry points have been there all along -- tcp_cancel(),
 * udp_cancel()).  Nothing sent it.
 *
 * Two replies come back: the cancelled operation's own -- status EINTR, tagged
 * with ITS op -- and the cancel's.  ichan_take parks or discards whichever
 * arrives first, so waiting for the cancel's reply collects both and leaves the
 * stream in step.  errno is put back afterwards: this function's job is to
 * report the signal, not whatever the tidying set.
 *
 * The reads here are retried rather than abandoned on a further signal.  The
 * request has been sent, so its replies are coming whether they are read or not,
 * and giving up on them is exactly the desynchronisation this exists to avoid.
 * ICHAN_CANCEL_TRIES bounds it so that a daemon that has died cannot spin here.
 */
#define ICHAN_CANCEL_TRIES	64

static void ichan_cancel(c, op)
struct ichan *c;
int op;
{
	nwreq_t rq;
	int which, tries, rop;

	switch (op) {
	case NWR_READ:	which= NWCAN_READ;  break;
	case NWR_WRITE:	which= NWCAN_WRITE; break;
	case NWR_IOCTL:	which= NWCAN_IOCTL; break;
	default:	errno= EINTR; return;	/* not a suspendable op */
	}

	rq.nwr_op= NWR_CANCEL;
	rq.nwr_minor= 0; rq.nwr_mode= which; rq.nwr_dlen= 0;
	rq.nwr_req= 0; rq.nwr_count= 0;
	if (_rawwrite(c->ic_reqfd, (char *)&rq, sizeof(rq)) == sizeof(rq))
	{
		for (tries= 0; tries < ICHAN_CANCEL_TRIES; tries++)
		{
			rop= ichan_take(c, NWR_CANCEL, (char *)0, 0);
			if (rop == NWR_CANCEL)
				break;
			if (rop < 0 && errno != EINTR)
				break;		/* the channel itself is gone */
		}
	}
	if (op == NWR_READ)
		c->ic_posted= 0;
	errno= EINTR;
}

static int ichan_await(c, op, buf, max)
struct ichan *c;
int op;
char *buf;
int max;
{
	int rop;

	for (;;)
	{
		if ((rop= ichan_take(c, op, buf, max)) < 0)
		{
			if (errno == EINTR)
				ichan_cancel(c, op);
			return -1;
		}
		if (rop == op)
			return 0;
	}
}

/* `req' is an int, not a long: with _WORD_SIZE 2 the NWIO* macros expand to
 * ((x << 8) | y), whose type is int, and there are no prototypes here to widen
 * it.  Declaring it long made every caller push 2 bytes where this function
 * read 4, so req arrived shifted left 16 bits and data and len came out of the
 * following stack words -- the kernel then rejected the garbage pointer with
 * EFAULT, which trap.c turns into SIGSYS ("Bad system call"), killing ifconfig
 * before it printed anything.  Anything carrying an ioctl code must match the
 * macro's type. */
int ichan_ioctl(c, req, data, len)
struct ichan *c;
int req;
char *data;
int len;
{
	nwreq_t rq;

	rq.nwr_op= NWR_IOCTL;
	rq.nwr_minor= 0; rq.nwr_mode= 0;
	rq.nwr_req= req;
	rq.nwr_count= len;
	rq.nwr_dlen= (len > 0 && data) ? len : 0;
	if (ichan_send(c->ic_reqfd, &rq, data, (int)rq.nwr_dlen) < 0)
		return -1;
	if (ichan_await(c, NWR_IOCTL, (char *)0, 0) < 0)
		return -1;
	if (c->ic_status < 0)
		return ichan_fail(c->ic_status);
	return c->ic_status;
}

/* A reading ioctl (_IOR/_IORW).  The layer answers with ONE record and its
 * payload: nwr_status is the byte count, nwr_dlen is what follows.  The layer
 * calls put_userdata twice -- the data, then a completion -- but the daemon
 * sends only the first, since the second repeats what the first already said
 * (coh_sr.c src_dataok).  Returns the byte count, or -1.
 *
 * Note the layer may SUSPEND a query it cannot answer yet -- NWIOGIPCONF waits
 * for an address to exist rather than failing -- so this blocks on an
 * unconfigured interface instead of returning. */
int ichan_ioctl_get(c, req, data, len)
struct ichan *c;
int req;
char *data;
int len;
{
	nwreq_t rq;

	rq.nwr_op= NWR_IOCTL;
	rq.nwr_minor= 0; rq.nwr_mode= 0; rq.nwr_dlen= 0;
	rq.nwr_req= req;
	rq.nwr_count= 0;
	_rawwrite(c->ic_reqfd, (char *)&rq, sizeof(rq));
	/*
	 * nwr_dlen ONLY, which is what ichan_take reads.  Falling back to
	 * nwr_status when dlen is zero brings back the ambiguity dlen exists to
	 * remove: a reply whose status is a positive RESULT rather than a byte
	 * count would send this into a read for a payload that was never
	 * written, and block there.  ichan_take also drains whatever does not
	 * fit, so the channel stays in step for every later request on it.
	 */
	if (ichan_await(c, NWR_IOCTL, data, len) < 0)
		return -1;
	if (c->ic_status < 0)
		return ichan_fail(c->ic_status);
	/*
	 * ONE record per request.  This used to read a SECOND record here for a
	 * final status, because the layer calls put_userdata twice -- the data,
	 * then the completion -- and both used to reach the channel.  The
	 * daemon now sends only the first (coh_sr.c src_dataok): the data record
	 * already carries the byte count in nwr_status and the payload length in
	 * nwr_dlen, so the completion adds nothing and its absence would leave
	 * this blocked on a record nobody is going to write.
	 */
	return c->ic_got;
}

/*
 * An ioctl that both SENDS and RECEIVES a struct (_IORW).
 *
 * The route queries are the only ones: NWIOGIPOROUTE takes an entry number in
 * the caller's nwio_route_t and fills the same struct with that entry.  On the
 * daemon side this is already one request -- ip_ioctl's get_userdata fetches the
 * argument off the request FIFO and its put_userdata streams the answer back,
 * with the trailing completion suppressed (coh_sr.c src_dataok) -- so the whole
 * exchange is one record in each direction, exactly like ichan_ioctl_get.  The
 * only thing missing was a client call that carries a payload OUT and a buffer
 * back IN at the same time.
 *
 * Returns the number of bytes written into `data', or -1.
 */
int ichan_ioctl_rw(c, req, data, len)
struct ichan *c;
int req;
char *data;
int len;
{
	nwreq_t rq;

	rq.nwr_op= NWR_IOCTL;
	rq.nwr_minor= 0; rq.nwr_mode= 0;
	rq.nwr_req= req;
	rq.nwr_count= len;
	rq.nwr_dlen= (len > 0 && data) ? len : 0;
	if (ichan_send(c->ic_reqfd, &rq, data, (int)rq.nwr_dlen) < 0)
		return -1;
	if (ichan_await(c, NWR_IOCTL, data, len) < 0)
		return -1;
	if (c->ic_status < 0)
		return ichan_fail(c->ic_status);
	return c->ic_got;
}

int ichan_write(c, buf, n)
struct ichan *c;
char *buf;
int n;
{
	nwreq_t rq;
	int st;

	rq.nwr_op= NWR_WRITE;
	rq.nwr_minor= 0; rq.nwr_mode= 0; rq.nwr_req= 0;
	rq.nwr_count= n;
	rq.nwr_dlen= (n > 0) ? n : 0;
	if (ichan_send(c->ic_reqfd, &rq, buf, (int)rq.nwr_dlen) < 0)
		return -1;
	/*
	 * Demultiplexed, not `read the next record'.  With a READ armed for
	 * poll(), data arriving before this write completes puts a READ reply
	 * in front of ours -- and hunt's client writes on every keystroke while
	 * its socket is armed, so this is the ordinary case, not a rare one.
	 */
	if (ichan_await(c, NWR_WRITE, (char *)0, 0) < 0)
		return -1;
	if ((st= c->ic_status) < 0)
		return ichan_fail(st);
	return st;
}

/*
 * Post an ioctl WITHOUT waiting for its reply, and collect it later.
 *
 * This exists for the passive open.  NWIOTCPLISTEN does not return until a
 * peer connects, so a synchronous ichan_ioctl() blocks the whole program in
 * accept() -- while a server that wants to poll() a listening socket alongside
 * its clients needs the listen OUTSTANDING and the fd readable when it
 * completes.  That is the same trick as the armed READ, for the one ioctl
 * whose completion is an external event rather than an answer.
 */
int ichan_post_ioctl(c, req, data, len)
struct ichan *c;
int req;
char *data;
int len;
{
	nwreq_t rq;

	rq.nwr_op= NWR_IOCTL;
	rq.nwr_minor= 0; rq.nwr_mode= 0;
	rq.nwr_req= req;
	rq.nwr_count= len;
	rq.nwr_dlen= (len > 0 && data) ? len : 0;
	return ichan_send(c->ic_reqfd, &rq, data, (int)rq.nwr_dlen);
}

/* Collect the reply to an ichan_post_ioctl(); returns its status, or -1. */
int ichan_wait_ioctl(c)
struct ichan *c;
{
	if (ichan_await(c, NWR_IOCTL, (char *)0, 0) < 0)
		return -1;
	if (c->ic_status < 0)
		return ichan_fail(c->ic_status);
	return c->ic_status;
}

/*
 * Keep a READ outstanding, so the reply fd is readable exactly when data has
 * arrived and poll()/select() on a socket mean what a BSD program expects.
 */
int ichan_arm(c)
struct ichan *c;
{
	int room;

	if (c->ic_posted)
		return 0;
	if (c->ic_hoff > 0)
	{
		int held= c->ic_hlen - c->ic_hoff;

		if (held > 0)
			memcpy(c->ic_hold, c->ic_hold + c->ic_hoff, held);
		c->ic_hlen= held;
		c->ic_hoff= 0;
	}
	room= ICHAN_HOLD - c->ic_hlen;
	if (room <= 0)
		return 0;		/* nothing unread has anywhere to go */
	if (ichan_post_read(c, room) < 0)
		return -1;
	c->ic_posted= 1;
	return 0;
}

/*
 * A read that cooperates with poll(): hand back held data if there is any, and
 * otherwise arm and wait.  Re-arms before returning, so the fd is pollable
 * again the moment the caller goes back to its poll loop.
 */
int ichan_recv(c, buf, max)
struct ichan *c;
char *buf;
int max;
{
	int n;

	while (c->ic_hoff >= c->ic_hlen)
	{
		c->ic_hlen= c->ic_hoff= 0;
		if (ichan_arm(c) < 0)
			return -1;
		if (ichan_await(c, NWR_READ, (char *)0, 0) < 0)
			return -1;
		if (c->ic_status < 0)
			return ichan_fail(c->ic_status);
		if (c->ic_status == 0 && c->ic_hlen == 0)
			return 0;		/* end of data */
	}
	n= c->ic_hlen - c->ic_hoff;
	if (n > max)
		n= max;
	memcpy(buf, c->ic_hold + c->ic_hoff, n);
	c->ic_hoff += n;
	if (c->ic_hoff >= c->ic_hlen)
		c->ic_hlen= c->ic_hoff= 0;
	(void)ichan_arm(c);
	return n;
}

/* Post an asynchronous READ; its reply is collected later by ichan_read_reply
 * (e.g. when the channel's reply fd becomes select()-readable). */
int ichan_post_read(c, n)
struct ichan *c;
int n;
{
	nwreq_t rq;

	rq.nwr_op= NWR_READ;
	rq.nwr_minor= 0; rq.nwr_mode= 0; rq.nwr_dlen= 0; rq.nwr_req= 0;
	rq.nwr_count= n;
	return (_rawwrite(c->ic_reqfd, (char *)&rq, sizeof(rq)) ==
		sizeof(rq)) ? 0 : -1;
}

int ichan_read_reply(c, buf, max)
struct ichan *c;
char *buf;
int max;
{
	nwrepl_t rp;
	int n;

	/*
	 * Skip replies that answer some OTHER op.  A channel may carry more than
	 * one request at a time -- a link daemon keeps a READ outstanding while it
	 * injects WRITEs -- so the next record on the reply FIFO is not necessarily
	 * this read's.  Whatever they carry still has to leave the FIFO, or every
	 * later reply on the channel is parsed at the wrong offset.
	 *
	 * A READ reply with NO payload is skipped for a different reason: it is a
	 * layer completing a read it has already answered with data (psip_send
	 * calls put_userdata twice), repeating the byte count with nothing behind
	 * it.  That is neither data nor end-of-data, and taking it for either was
	 * the source of both known corruptions -- transmitting an untouched
	 * buffer as a packet, and reporting a connection closed that was not.
	 * A status of zero, with no payload promised, IS end of data.
	 */
	for (;;)
	{
		if (_rawread(c->ic_replfd, (char *)&rp, sizeof(rp))
		    != sizeof(rp))
			return -1;
		if (rp.nwr_rop == NWR_READ)
		{
			if (rp.nwr_status < 0)
				return ichan_fail((int)rp.nwr_status);
			if (rp.nwr_status == 0)
				return 0;	/* end of data, not an error */
			if (rp.nwr_dlen > 0)
				break;
			continue;		/* a completion, already answered */
		}
		if (ichan_drain(c, (int)rp.nwr_dlen) < 0)
			return -1;
	}
	n= (int)rp.nwr_dlen;
	if (n > max)
		n= max;
	if (readn(c->ic_replfd, buf, n) != n)
		return -1;
	/* Anything past the caller's buffer still has to go. */
	if ((int)rp.nwr_dlen > n
	    && ichan_drain(c, (int)rp.nwr_dlen - n) < 0)
		return -1;
	return n;
}

int ichan_read(c, buf, n)
struct ichan *c;
char *buf;
int n;
{
	if (ichan_post_read(c, n) < 0)
		return -1;
	return ichan_read_reply(c, buf, n);
}

/* Fire a WRITE without waiting for its reply (the demux loop collects it). */
int ichan_post_write(c, buf, n)
struct ichan *c;
char *buf;
int n;
{
	nwreq_t rq;

	rq.nwr_op= NWR_WRITE;
	rq.nwr_minor= 0; rq.nwr_mode= 0; rq.nwr_req= 0;
	rq.nwr_count= n;
	rq.nwr_dlen= (n > 0) ? n : 0;
	return ichan_send(c->ic_reqfd, &rq, buf, (int)rq.nwr_dlen);
}

/* Read the next reply; *rop gets the op it answers (NWR_READ/NWR_WRITE/...).
 * For a READ reply the data follows and is returned in buf. */
int ichan_reply_op(c, buf, max, rop)
struct ichan *c;
char *buf;
int max;
int *rop;
{
	nwrepl_t rp;
	int n;

	if (_rawread(c->ic_replfd, (char *)&rp, sizeof(rp)) != sizeof(rp))
		return -1;
	*rop= rp.nwr_rop;
	if (rp.nwr_status < 0)
		return ichan_fail((int)rp.nwr_status);
	/*
	 * nwr_dlen, not nwr_status, says whether a payload follows: a READ
	 * answered with data and the completion that follows it are both
	 * tagged NWR_READ with the same positive count, and the completion
	 * carries no payload.  For a READ the answer is how many bytes went
	 * into `buf'; any other op's status IS its result, so it passes
	 * through.
	 */
	if (rp.nwr_dlen <= 0)
		return (rp.nwr_rop == NWR_READ) ? 0 : (int)rp.nwr_status;
	n= (int)rp.nwr_dlen;
	if (n > max)
		n= max;
	if (readn(c->ic_replfd, buf, n) != n)
		return -1;
	/* Discard anything past the caller's buffer, or the channel
	 * desynchronises for every later reply on it. */
	if ((int)rp.nwr_dlen > n
	    && ichan_drain(c, (int)rp.nwr_dlen - n) < 0)
		return -1;
	return n;
}

void ichan_close(c)
struct ichan *c;
{
	nwreq_t rq;

	rq.nwr_op= NWR_CLOSE;
	rq.nwr_minor= 0; rq.nwr_mode= 0; rq.nwr_dlen= 0; rq.nwr_req= 0;
	rq.nwr_count= 0;
	_rawwrite(c->ic_reqfd, (char *)&rq, sizeof(rq));
	_rawclose(c->ic_reqfd);
	_rawclose(c->ic_replfd);
	(void)unlink(c->ic_reqpath);
	(void)unlink(c->ic_replpath);
}

/*
 * Finish with the channel but KEEP its reply descriptor open.
 *
 * The descriptor a socket is known by is its reply fd, and there are two things
 * a caller can want from a channel it is finished with: the channel gone, and
 * the NUMBER kept.  accept() wants both -- it has to give a connection back to
 * the daemon in order to have the descriptors to open a replacement listener
 * with, and it must not let go of the number its caller is holding, because
 * anything else opened in between would take it.  So this is ichan_close()
 * minus one _rawclose(), and the fd it leaves is the caller's to dup2() over.
 *
 * The paths are unlinked here as they are there: an open descriptor outlives
 * the name it was opened by, and the reply FIFO's last reader is what ends it.
 */
void ichan_abort(c)
struct ichan *c;
{
	nwreq_t rq;

	rq.nwr_op= NWR_CLOSE;
	rq.nwr_minor= 0; rq.nwr_mode= 0; rq.nwr_dlen= 0; rq.nwr_req= 0;
	rq.nwr_count= 0;
	_rawwrite(c->ic_reqfd, (char *)&rq, sizeof(rq));
	_rawclose(c->ic_reqfd);
	c->ic_reqfd= -1;
	(void)unlink(c->ic_reqpath);
	(void)unlink(c->ic_replpath);
}
