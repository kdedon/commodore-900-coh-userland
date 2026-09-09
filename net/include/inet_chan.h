/*
 * inet_chan.h -- client helper for the inet daemon's control channel.
 *
 * One channel = one open device (a socket, or a psip/eth endpoint).  Shared by
 * libsocket.c (BSD sockets) and coh_slip.c/coh_ppp.c (serial link layers).  All
 * I/O uses the raw syscalls (rawsys.s), so it is safe even when read/write/close
 * are overridden for socket handles.  See inet_ipc.h for the wire protocol.
 */
#ifndef INET_CHAN_H
#define INET_CHAN_H

/*
 * Bytes held on the client side between a READ completing and the application
 * collecting it.  512 is chosen against what a poll-driven program reads in one
 * go, not against the MSS: it only has to be big enough that an ordinary
 * poll()/read() loop empties it each time round.
 */
#define ICHAN_HOLD	512

struct ichan {
	int	ic_reqfd;		/* client -> daemon FIFO		*/
	int	ic_replfd;		/* daemon -> client FIFO (the fd	*/
					/* callers select()/read() on)		*/
	char	ic_reqpath[40];
	char	ic_replpath[40];
	/*
	 * State for making the reply fd POLLABLE (ichan_arm/ichan_recv).
	 *
	 * A channel is request/reply, so nothing is ever waiting on the reply
	 * FIFO until a request has been sent -- which means poll() on a socket
	 * reported "not readable" no matter how much data had arrived, and every
	 * BSD program that waits before reading (hunt, and any server) would
	 * block forever.  Keeping a READ posted whenever the application is not
	 * inside recv() makes the fd readable exactly when data has arrived,
	 * which is the semantics those programs expect.
	 *
	 * At most ONE read is ever outstanding, so at most one reply can be in
	 * flight and the hold buffer can never be overrun -- the posted count is
	 * whatever room is left in it.
	 */
	int	ic_posted;		/* a READ is outstanding		*/
	int	ic_status;		/* status of the last reply taken	*/
	int	ic_got;			/* payload bytes it put in the caller's	*/
					/* buffer (non-READ replies only)	*/
	int	ic_hlen;		/* bytes held				*/
	int	ic_hoff;		/* of which this many are consumed	*/
	char	ic_hold[ICHAN_HOLD];
};

/* Every call below reports failure as -1 with errno set to a COHERENT error
 * number.  The stack answers in NEGATIVE Minix codes; ichan_fail() translates
 * one, sets errno and returns -1, and is exported because it is the only
 * sanctioned way to turn a stack status into an errno. */
int  ichan_fail();	/* (int minix_status) -> -1, errno set		*/
int  ichan_open();	/* (struct ichan *, int minor) -> 0/-1		*/
int  ichan_ioctl();	/* (struct ichan *, int req, char *data, int len)	*/
			/* req is an int -- the type the NWIO* macros have on	*/
			/* a 16-bit machine.  Never declare it long: with no	*/
			/* prototypes a widened parameter reads 2 bytes of the	*/
			/* next argument (see inet_chan.c).			*/
int  ichan_ioctl_get();	/* (struct ichan *, int req, char *data, int len):	*/
			/* a reading ioctl; returns the byte count	*/
int  ichan_ioctl_rw();	/* (struct ichan *, int req, char *data, int len):	*/
			/* an ioctl that sends `data' and fills it from the	*/
			/* answer (_IORW: the route queries); byte count		*/
int  ichan_write();	/* (struct ichan *, char *buf, int n) -> status	*/
int  ichan_post_read();	/* (struct ichan *, int n): async READ request	*/
int  ichan_read_reply();/* (struct ichan *, char *buf, int max): its reply */
int  ichan_read();	/* (struct ichan *, char *buf, int n): sync read */

/*
 * The pollable read path.  ichan_arm() keeps a READ outstanding so the reply fd
 * becomes readable when data arrives; ichan_recv() then collects it, from the
 * hold buffer if it is already there and from the channel if not, and re-arms.
 *
 * poll() reports what it should as long as the application drains the socket on
 * each readable event, which is what a poll()/read() loop does.  An application
 * that instead leaves ICHAN_HOLD bytes unread has no room left to arm, so its
 * next poll() blocks while those bytes sit here -- recv() still returns them
 * immediately, so the data is not lost, but poll() under-reports.
 */
int  ichan_arm();	/* (struct ichan *): post a READ if there is room */
int  ichan_recv();	/* (struct ichan *, char *buf, int max)		*/

/*
 * An ioctl whose completion is an EXTERNAL EVENT rather than an answer --
 * NWIOTCPLISTEN, which does not return until a peer connects.  Posting it and
 * collecting the reply later is what lets a server poll() a listening socket
 * alongside the connections it has already accepted.
 */
int  ichan_post_ioctl();/* (struct ichan *, int req, char *data, int len)	*/
int  ichan_wait_ioctl();/* (struct ichan *): its status, or -1		*/
void ichan_close();	/* (struct ichan *)				*/
void ichan_abort();	/* (struct ichan *): close, KEEPING the reply fd	*/

/* Async forms for a client running >1 request on one channel (a link daemon):
 * fire a WRITE without waiting, and read the next reply demultiplexed by op. */
int  ichan_post_write();/* (struct ichan *, char *buf, int n): async WRITE	*/
int  ichan_reply_op();	/* (struct ichan *, char *buf, int max, int *rop)	*/

#endif /* INET_CHAN_H */
