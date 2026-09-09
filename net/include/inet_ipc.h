/*
 * inet_ipc.h -- wire protocol between the COHERENT inet daemon (coh_sr.c) and
 * the client socket library (libsocket.c).
 *
 * Because a monolithic COHERENT kernel cannot route /dev/tcp opens to a
 * userland daemon, clients reach the daemon over FIFOs.  One channel = one open
 * device (one socket): the client creates a request FIFO (client->daemon) and a
 * reply FIFO (daemon->client), announces both over the well-known rendezvous
 * FIFO, and thereafter each request gets exactly one reply.  Requests that
 * carry data (WRITE/IOCTL-in) append the payload after the record; replies that
 * carry data (READ/IOCTL-out) append it after the reply.
 */
#ifndef INET_IPC_H
#define INET_IPC_H

#define INET_RENDEZVOUS	"/dev/inet"	/* daemon's connect FIFO		*/

/* Request opcodes (client -> daemon). */
#define NWR_OPEN	1
#define NWR_CLOSE	2
#define NWR_READ	3
#define NWR_WRITE	4
#define NWR_IOCTL	5
#define NWR_CANCEL	6

/*
 * Which operation a NWR_CANCEL abandons, in nwr_mode.  These are the SR_CANCEL_*
 * numbers from inet/generic/sr.h, which is the daemon's own header and is not on
 * a client's include path -- the values are the wire protocol, so they are
 * repeated here rather than reached for.
 *
 * A client MUST cancel: an operation the daemon has suspended stays suspended,
 * holding whatever the layer allocated for it, until the daemon is told to give
 * it up.  Abandoning one -- which is what a signal does to a client blocked on
 * the reply FIFO -- leaves the daemon believing an operation is in progress on a
 * descriptor the client is about to use again.  For NWIOTCPLISTEN that meant a
 * connection still claiming the descriptor, and the retry then attached a
 * SECOND one to it.
 */
#define NWCAN_IOCTL	1
#define NWCAN_READ	2
#define NWCAN_WRITE	3

/* Announce a new channel over the rendezvous FIFO. */
typedef struct sr_hello {
	long	sh_id;			/* client-chosen channel id	*/
	char	sh_req[40];		/* client->daemon FIFO path	*/
	char	sh_repl[40];		/* daemon->client FIFO path	*/
} sr_hello_t;

/* One request (client -> daemon over sh_req). */
typedef struct nwreq {
	short	nwr_op;			/* NWR_*			*/
	short	nwr_minor;		/* device minor (open only)	*/
	short	nwr_mode;		/* open mode / cancel which	*/
	/*
	 * How many payload bytes FOLLOW this record on the request FIFO.
	 *
	 * This is not the same as nwr_count, and conflating them desynchronises
	 * the channel permanently.  nwr_count is what the op is ABOUT -- bytes
	 * wanted back for a READ, bytes offered for a WRITE, the argument size for
	 * an ioctl -- while nwr_dlen is what the client actually wrote after the
	 * record.  A reading ioctl sets nwr_count but sends nothing.
	 *
	 * The daemon needs the distinction because a layer is free NOT to fetch an
	 * argument: NWIOTCPCONN and NWIOTCPLISTEN ignore theirs entirely (tcp.c
	 * just calls tcp_connect/tcp_listen).  In Minix that is harmless, because
	 * the argument lives in the client's address space and is copied only on
	 * request; here the channel is a byte stream, so eight unread bytes became
	 * the next "request", were dispatched as an unknown op, and the channel was
	 * torn down on the short read after it.  With nwr_dlen the daemon can
	 * discard whatever the layer left, and the stream stays in step no matter
	 * what any layer chooses to read.
	 */
	short	nwr_dlen;
	long	nwr_req;		/* ioctl request		*/
	long	nwr_count;		/* read/write/ioctl byte count	*/
} nwreq_t;

/* One reply (daemon -> client over sh_repl).  nwr_rop echoes the request op
 * this reply answers, so a client running more than one request at once on a
 * single channel (e.g. a link daemon with an outstanding READ while it injects
 * WRITEs) can demultiplex the replies. */
typedef struct nwrepl {
	long	nwr_status;		/* >=0 count/OK, <0 -errno	*/
	short	nwr_rop;		/* NWR_* this reply answers	*/
	/*
	 * How many payload bytes FOLLOW this reply record.
	 *
	 * nwr_status cannot answer that, and assuming it could is what put junk
	 * on the wire.  A layer may answer a READ with data and then complete
	 * it: psip_send() calls put_userdata twice, once with the packet and
	 * once with the result, and BOTH records come back tagged NWR_READ with
	 * the same positive count.  A client that reads a payload whenever the
	 * count is positive then consumed whatever happened to be next -- or
	 * nothing at all, leaving its buffer as it found it, which slip
	 * dutifully SLIP-framed and transmitted.  That is where
	 * `0.4.0.0 -> 69.0.0.40' came from: a frame of stale zeroes.
	 *
	 * With an explicit length the reply is self-describing, the same way the
	 * request became with nwr_dlen, and a spurious completion is harmless
	 * rather than corrupting.
	 */
	short	nwr_dlen;
} nwrepl_t;

#endif /* INET_IPC_H */
