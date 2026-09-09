/*
 * libsocket.c -- BSD socket() veneer for COHERENT/Z8001.
 *
 * Maps the BSD socket calls onto the Minix inet stack's device+ioctl protocol,
 * reached through the inet daemon's control channel (inet_chan.c / inet_ipc.h):
 * each socket is one daemon channel (a request + reply FIFO pair).
 *
 * A socket descriptor IS a real kernel fd -- the channel's reply FIFO fd -- so
 * select()/read()/write()/close() work on it directly.  libsocket overrides
 * read/write/close to route a socket fd over its channel and let every other fd
 * fall through to the raw syscalls (rawsys.s).
 *
 * TCP:  socket -> open /dev/tcp channel; connect -> NWIOSTCPCONF + NWIOTCPCONN;
 *       listen/accept -> NWIOSTCPCONF + NWIOTCPLISTEN; shutdown -> NWIOTCPSHUTDOWN.
 * UDP:  socket -> open /dev/udp channel; NWIOSUDPOPT sets local/remote.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "inet_chan.h"

/*
 * The stack's own headers, not a second copy of them.  These declarations used
 * to be inlined here, and seven bugs came out of the duplication: the whole
 * nwio_udpopt flag block had been transcribed by analogy with the TCP one
 * rather than read off the UDP header, so bind() set bits that mean nothing to
 * the stack (EXCL 0x20 for 0x01, LP_SET 0x80 for 0x08, RP_ANY 0x200 for
 * 0x01000000, RA_ANY 0x800 for 0x02000000, RWDATONLY 0x40000 for 0x1000,
 * DI_BROAD 0x400000 for 0x200000) and UDP had never worked at all.  The TCP
 * block happened to be right, which is why TCP did.
 *
 * <net/ioctl.h> supplies the NWIO* command codes.  On this 16-bit machine
 * _IOW(x,y,t) reduces to (x<<8)|y -- there is no room for the direction and
 * size bits -- so the codes carry neither, and nwio_request() below has to know
 * both.  See minix/ioctl.h.
 */
#include <net/gen/in.h>
#include <net/gen/tcp.h>
#include <net/gen/tcp_io.h>
#include <net/gen/udp.h>
#include <net/gen/udp_io.h>
#include <net/gen/udp_hdr.h>
#include <net/gen/ip_io.h>
#include <net/gen/psip_io.h>
#include <net/gen/route.h>
#include <net/ioctl.h>

/* --- device minors (interface 0: if2minor(0,dev)=dev) --- */
#define PSIP_MINOR	0
#define IP_MINOR	1
#define TCP_MINOR	2
#define UDP_MINOR	3

/* One port type for both protocols: tcpport_t and udpport_t are the same u16_t
 * and a socket here is one or the other, never both. */
typedef tcpport_t	nwport_t;

/*
 * A datagram socket that accepts traffic from anyone cannot use
 * NWUO_RWDATONLY: udp.c rejects that combination outright --
 *
 *	if ((new_flags & NWUO_RWDATONLY) &&
 *	    (... || (new_flags & (NWUO_RP_ANY|NWUO_RA_ANY|NWUO_EN_IPOPT))))
 *		reply EBADMODE
 *
 * -- and it is right to: if the peer is not fixed, every datagram has to carry
 * its own addresses, or a receiver cannot say who sent it and a sender cannot
 * say where it is going.  That is what udp_io_hdr_t (UDP_IO_HDR_SIZE, 16 bytes)
 * is for, and NWUO_RWDATALL asks for it.  bind() used to request
 * RP_ANY|RA_ANY|RWDATONLY together, so it failed with EBADMODE.
 *
 * Its addresses and ports are network order (the stack copies them straight
 * from the IP and UDP headers); the two lengths are HOST order, because
 * CONF_UDP_IO_NW_BYTE_ORDER is not defined in this build.  On a big-endian
 * Z8001 the two agree numerically, but the distinction is what the stack means.
 */
#define MAXSOCK		16

struct sock {
	int		s_type;		/* SOCK_STREAM / SOCK_DGRAM	*/
	int		s_listening;	/* a LISTEN ioctl is outstanding */
	int		s_udpconf;	/* the UDP socket has been configured */
	struct ichan	s_ic;		/* channel to the daemon	*/
	ipaddr_t	s_locaddr, s_remaddr;
	nwport_t	s_locport, s_remport;
};

/*
 * MAXSOCK slots, each a POINTER.  A `struct sock' -- 624 bytes, 512 of them the
 * channel's hold buffer -- exists for exactly as long as its socket is open:
 * sockalloc() makes one, sockfree() destroys it, and a program that opens no
 * socket holds nothing but the pointers.
 *
 * An occupied slot is a non-null pointer, and that is the only record of it.
 * Which slot a socket occupies is private to this file: nothing indexes the
 * table by descriptor or by any number a caller can see, and lookup() and the
 * allocators only ever scan.
 *
 * MAXSOCK is not the limit a caller meets.  A channel costs two descriptors out
 * of NUFILE 20, so the kernel refuses the ninth socket with EMFILE while slots
 * are still free -- net/test/chanmax.c measures that ceiling.  Exhausting the
 * table is ENFILE, from sockalloc().
 */
static struct sock *socks[MAXSOCK];

/* Raw syscall stubs (rawsys.s): the fall-through for read/write/close on a
 * real (non-socket) fd, since those names are overridden below. */
extern int _rawread();
extern int _rawwrite();
extern int _rawclose();

/*
 * A socket's state is on the heap, so its address is a FAR pointer.  Any
 * function that hands one back MUST be declared to return a pointer before it is
 * called: K&R defaults an undeclared function to int, which drops the segment
 * half and leaves an offset into segment zero.  <stdio.h> declares malloc as
 * char *; nothing this file includes declares free.
 */
extern void free();

/*
 * A slot with a zeroed entry in it.  Zeroed because the rest of this file reads
 * an untouched field -- a channel's descriptors, the hold buffer's offsets -- as
 * meaning "not yet", and 0 is what that has to be.
 *
 * ENFILE when every slot is taken, ENOMEM when the heap cannot supply the entry.
 */
static struct sock *sockalloc()
{
	struct sock *sk;
	int i;

	for (i= 0; i < MAXSOCK && socks[i]; i++)
		;
	if (i == MAXSOCK)
	{
		errno= ENFILE;
		return (struct sock *)0;
	}
	if ((sk= (struct sock *)malloc(sizeof(struct sock)))
			== (struct sock *)0)
	{
		errno= ENOMEM;
		return (struct sock *)0;
	}
	memset((char *)sk, 0, sizeof(struct sock));
	socks[i]= sk;
	return sk;
}

/*
 * Give up an entry.  This is the ONLY way one goes away, and it clears the slot
 * and frees the block together, so neither can outlive the other: a slot left
 * set is a socket nothing can close, and a block left allocated is a leak in
 * every server that closes a connection.
 *
 * The slot is found by comparing far pointers for equality, which is exact --
 * both halves must match.  Nothing here does arithmetic across entries or orders
 * them, and every value in the table came from one malloc() and is compared only
 * against itself, so the comparison means what it reads as.
 */
static void sockfree(sk)
struct sock *sk;
{
	int i;

	for (i= 0; i < MAXSOCK; i++)
	{
		if (socks[i] == sk)
		{
			socks[i]= (struct sock *)0;
			break;
		}
	}
	free((char *)sk);
}

/* The socket "descriptor" is the channel's reply fd (a real kernel fd), so
 * look sockets up by it. */
static struct sock *lookup(fd)
int fd;
{
	int i;

	for (i= 0; i < MAXSOCK; i++)
	{
		if (socks[i] && socks[i]->s_ic.ic_replfd == fd)
			return socks[i];
	}
	return (struct sock *)0;
}

/* int req, matching the NWIO* macros' type -- see ichan_ioctl. */
static int chan_ioctl(sk, req, data, len)
struct sock *sk;
int req;
char *data;
int len;
{
	return ichan_ioctl(&sk->s_ic, req, data, len);
}

/*
 * Record the local address and port the STACK settled on.
 *
 * Whenever the port was left to the stack (LP_SEL), only the stack knows what
 * it chose, and a server has to be able to find out: the BSD idiom for a
 * daemon that does not want a fixed port is bind() to port 0, listen(), then
 * getsockname() to learn the number and publish it.  hunt's driver does
 * exactly that -- twice -- and announces both ports in reply to a client's
 * discovery datagram.
 *
 * Without this, getsockname() answered from our own record of the bind, so it
 * reported port 0 and the driver advertised itself on port 0.  The client's
 * list of drivers is terminated by an entry with sin_port == 0, so the very
 * first driver looked like the end of the list: `hunt -S' printed nothing at
 * all against a healthy game, and `hunt' concluded no driver was running.
 *
 * A failed query is not an error.  The port is only wrong if the stack picked
 * it, and callers that named their own port already have the right answer.
 */
static void tcp_learnlocal(sk)
struct sock *sk;
{
	struct nwio_tcpconf conf;

	memset((char *)&conf, 0, sizeof(conf));
	if (ichan_ioctl_get(&sk->s_ic, NWIOGTCPCONF, (char *)&conf,
			sizeof(conf)) > 0)
	{
		sk->s_locaddr= conf.nwtc_locaddr;
		sk->s_locport= conf.nwtc_locport;
	}
}

static void udp_learnlocal(sk)
struct sock *sk;
{
	struct nwio_udpopt opt;

	memset((char *)&opt, 0, sizeof(opt));
	if (ichan_ioctl_get(&sk->s_ic, NWIOGUDPOPT, (char *)&opt,
			sizeof(opt)) > 0)
	{
		sk->s_locaddr= opt.nwuo_locaddr;
		sk->s_locport= opt.nwuo_locport;
	}
}

/* --- BSD API --- */

int socket(domain, type, protocol)
int domain, type, protocol;
{
	struct sock *sk;
	int minor;

	if (domain != AF_INET)
	{
		errno= EINVAL;
		return -1;
	}
	if ((sk= sockalloc()) == (struct sock *)0)
		return -1;		/* sockalloc set errno */
	sk->s_type= type;
	minor= (type == SOCK_DGRAM) ? UDP_MINOR : TCP_MINOR;
	if (ichan_open(&sk->s_ic, minor) < 0)
	{
		/* ichan_open set errno; keep it across the free */
		int e= errno;

		sockfree(sk);
		errno= e;
		return -1;
	}
	return sk->s_ic.ic_replfd;		/* a real kernel fd */
}

static int udp_conf();

int bind(s, addr, addrlen)
int s;
struct sockaddr *addr;
int addrlen;
{
	struct sock *sk= lookup(s);
	struct sockaddr_in *sin= (struct sockaddr_in *)addr;

	if (!sk) { errno= EBADF; return -1; }
	sk->s_locaddr= sin->sin_addr.s_addr;
	sk->s_locport= sin->sin_port;
	if (sk->s_type == SOCK_DGRAM)
		return udp_conf(sk);
	return 0;
}

/*
 * Configure a datagram socket with the stack.
 *
 * Split out of bind() because a UDP socket does not have to be bound to send.
 * BSD gives an unbound socket an ephemeral local port on its first send, and
 * that is the ordinary client shape -- socket(), then straight to sendto().
 * Here nothing had configured the socket at all, so the write was refused
 * (EBADMODE, surfacing as EINVAL) and hunt's `-q' could not probe for a game
 * driver running on its own machine.  TCP's connect() has always had this,
 * as LP_SET-or-LP_SEL; this is the same thing for UDP.
 */
static int udp_conf(sk)
struct sock *sk;
{
	struct nwio_udpopt opt;
	{
		memset((char *)&opt, 0, sizeof(opt));
		/*
		 * EVERY option group must be named, or the socket is not
		 * configured at all.  udp_setopt only grants UFF_OPTSET when
		 * access, local port, local address, broadcast, remote port,
		 * remote address, read/write mode AND IP options have each been
		 * decided; miss one and the ioctl still succeeds while every
		 * later write answers EBADMODE.  DI_IPOPT was the missing one.
		 *
		 * RWDATALL, not RWDATONLY: with RP_ANY|RA_ANY the peer is not
		 * fixed, so each datagram must carry its own addresses -- and
		 * udp.c rejects RWDATONLY with either of those.
		 */
		/*
		 * EN_LOC and EN_BROAD, not DI_.  udp_arrived classifies an
		 * arriving datagram as EN_LOC when its destination is this
		 * interface's own address and EN_BROAD otherwise, then skips
		 * any socket without that bit:
		 *
		 *	if (!(flags & dst_type)) continue;
		 *
		 * With DI_LOC every ordinary unicast datagram addressed to us
		 * was dropped -- delivered to IP, matched against every socket,
		 * and discarded for want of a flag.  DI_BROAD would likewise
		 * throw away the broadcast discovery hunt(6) is built on.
		 */
		/* LP_SET when the caller named a port, LP_SEL to have the
		 * stack choose one -- the same choice connect() makes. */
		opt.nwuo_flags= NWUO_EXCL |
			(sk->s_locport ? NWUO_LP_SET : NWUO_LP_SEL) |
			NWUO_RP_ANY |
			NWUO_RA_ANY | NWUO_EN_LOC | NWUO_EN_BROAD |
			NWUO_RWDATALL | NWUO_DI_IPOPT;
		opt.nwuo_locport= sk->s_locport;
		if (chan_ioctl(sk, NWIOSUDPOPT, (char *)&opt, sizeof(opt)) < 0)
			return -1;
		udp_learnlocal(sk);
		sk->s_udpconf= 1;
		/* A configured datagram socket is ready to receive, so arm it now:
		 * its owner's next move is poll(), and until a READ is
		 * outstanding there is nothing on the reply FIFO for poll() to
		 * find however much has arrived. */
		(void)ichan_arm(&sk->s_ic);
	}
	return 0;
}

int connect(s, addr, addrlen)
int s;
struct sockaddr *addr;
int addrlen;
{
	struct sock *sk= lookup(s);
	struct sockaddr_in *sin= (struct sockaddr_in *)addr;
	struct nwio_tcpconf conf;
	struct nwio_tcpcl cl;

	if (!sk) { errno= EBADF; return -1; }
	sk->s_remaddr= sin->sin_addr.s_addr;
	sk->s_remport= sin->sin_port;

	memset((char *)&conf, 0, sizeof(conf));
	conf.nwtc_flags= NWTC_EXCL |
		(sk->s_locport ? NWTC_LP_SET : NWTC_LP_SEL) |
		NWTC_SET_RA | NWTC_SET_RP;
	conf.nwtc_locport= sk->s_locport;
	conf.nwtc_remaddr= sk->s_remaddr;
	conf.nwtc_remport= sk->s_remport;
	if (chan_ioctl(sk, NWIOSTCPCONF, (char *)&conf, sizeof(conf)) < 0)
		return -1;
	tcp_learnlocal(sk);

	cl.nwtcl_flags= 0;
	cl.nwtcl_ttl= 0;
	if (chan_ioctl(sk, NWIOTCPCONN, (char *)&cl, sizeof(cl)) < 0)
		return -1;
	(void)ichan_arm(&sk->s_ic);	/* pollable from here on */
	return 0;
}

/*
 * listen -- configure for a passive open and START one.
 *
 * NWTC_SHARED, not NWTC_EXCL.  accept() below keeps a second descriptor
 * listening on the same local port while the first carries a connection, and
 * tcp_setconf refuses that outright for an EXCL descriptor: it walks the fd
 * table and answers EADDRINUSE for any other fd with this local port unless
 * both are SHARED.  Exclusive access to a port a server means to accept on
 * more than once is a contradiction -- Minix servers are SHARED for exactly
 * this reason.
 *
 * The passive open is POSTED, not waited for.  NWIOTCPLISTEN does not return
 * until a peer connects, so issuing it here synchronously would block inside
 * listen(); posting it makes the reply fd readable when a connection arrives,
 * which is what lets a server poll() its listening socket alongside its
 * clients (huntd polls all three at once).
 */
int listen(s, backlog)
int s, backlog;
{
	struct sock *sk= lookup(s);
	struct nwio_tcpconf conf;
	struct nwio_tcpcl cl;

	if (!sk) { errno= EBADF; return -1; }
	memset((char *)&conf, 0, sizeof(conf));
	/* LP_SEL when no port was named: bind() to port 0 then listen() is how
	 * a BSD server asks for any free port, and LP_SET with a zero port
	 * asks for port zero -- which is not a port. */
	conf.nwtc_flags= NWTC_SHARED | NWTC_UNSET_RA | NWTC_UNSET_RP |
		(sk->s_locport ? NWTC_LP_SET : NWTC_LP_SEL);
	conf.nwtc_locport= sk->s_locport;
	if (chan_ioctl(sk, NWIOSTCPCONF, (char *)&conf, sizeof(conf)) < 0)
		return -1;
	/* Before the passive open, so relisten() -- which must name the same
	 * port explicitly -- has a real one to name. */
	tcp_learnlocal(sk);
	cl.nwtcl_flags= 0;
	cl.nwtcl_ttl= 0;
	if (ichan_post_ioctl(&sk->s_ic, NWIOTCPLISTEN, (char *)&cl,
			sizeof(cl)) < 0)
		return -1;
	sk->s_listening= 1;
	return 0;
}

/*
 * Open another descriptor listening on the same port as `sk', for accept() to
 * leave behind when it hands the caller a connection.  Returns it, or null.
 */
static struct sock *relisten(sk)
struct sock *sk;
{
	struct sock *ns;
	struct nwio_tcpconf conf;
	struct nwio_tcpcl cl;

	if ((ns= sockalloc()) == (struct sock *)0)
		return (struct sock *)0;	/* sockalloc set errno */
	ns->s_type= SOCK_STREAM;
	if (ichan_open(&ns->s_ic, TCP_MINOR) < 0)
	{
		int e= errno;

		sockfree(ns);
		errno= e;
		return (struct sock *)0;
	}
	ns->s_locaddr= sk->s_locaddr;
	ns->s_locport= sk->s_locport;

	memset((char *)&conf, 0, sizeof(conf));
	conf.nwtc_flags= NWTC_SHARED | NWTC_LP_SET | NWTC_UNSET_RA |
		NWTC_UNSET_RP;
	conf.nwtc_locport= ns->s_locport;
	if (chan_ioctl(ns, NWIOSTCPCONF, (char *)&conf, sizeof(conf)) < 0)
	{
		int e= errno;

		ichan_close(&ns->s_ic);
		sockfree(ns);
		errno= e;
		return (struct sock *)0;
	}
	cl.nwtcl_flags= 0;
	cl.nwtcl_ttl= 0;
	if (ichan_post_ioctl(&ns->s_ic, NWIOTCPLISTEN, (char *)&cl,
			sizeof(cl)) < 0)
	{
		int e= errno;

		ichan_close(&ns->s_ic);
		sockfree(ns);
		errno= e;
		return (struct sock *)0;
	}
	ns->s_listening= 1;
	return ns;
}

/*
 * There is no room for a second channel, so the CONNECTION is what is given up
 * and the listening socket is what is kept.
 *
 * That way round because of what the two are worth to a caller.  A peer whose
 * connection is aborted retries, and accept() returning -1 is a failure every
 * server already handles; a server that loses its listening socket has no way
 * to know it and no way to get it back, and the port stops being answered for
 * the life of the process.  Handing the connection back on `s' instead is that
 * second case with no error to see: a caller cannot tell that number from a
 * fresh connection, so it serves the caller and then closes what it believes is
 * a connection and is really its own listening socket.
 *
 * The recovery can always run when relisten() could not.  ichan_abort() gives
 * the connection's channel back to the daemon and closes its REQUEST
 * descriptor, so there is one more descriptor free here than there was a moment
 * ago, and it keeps the reply descriptor -- `s' itself -- so that nothing else
 * can take the number the caller is holding while the replacement listener is
 * opened onto it.
 *
 * Always -1, so a caller reads `return acceptlost(sk, s)'.  The errno is
 * ECONNRESET, set through ichan_fail() and not by assignment: the <errno.h> on
 * this file's include path is the STACK's, whose connection numbers are Minix's
 * and not the ones a user program's perror() reads -- ichan_fail() is the
 * translation, and 60 there is 46 here.
 */
static int acceptlost(sk, s)
struct sock *sk;
int s;
{
	struct sock *ns;

	ichan_abort(&sk->s_ic);
	sk->s_ic.ic_replfd= -1;		/* `s' is nobody's until ns claims it */
	sk->s_listening= 0;
	if ((ns= relisten(sk)) == (struct sock *)0)
	{
		/*
		 * Out of descriptors even for that.  The table entry is given up,
		 * so `s' is a descriptor with no socket behind it and every
		 * later call on it answers EBADF rather than appearing to work
		 * -- which is what lets a server notice that it has no listener
		 * and bind a fresh one.
		 */
		sockfree(sk);
		return ichan_fail(ECONNRESET);
	}
	if (dup2(ns->s_ic.ic_replfd, s) < 0)
	{
		int e= errno;

		ichan_close(&ns->s_ic);
		sockfree(ns);
		sockfree(sk);
		errno= e;
		return -1;
	}
	_rawclose(ns->s_ic.ic_replfd);
	ns->s_ic.ic_replfd= s;		/* the caller's fd: listening again */
	sockfree(sk);
	return ichan_fail(ECONNRESET);
}

/*
 * accept -- wait for the posted passive open, and hand back the connection on
 * a NEW descriptor while `s' goes on listening.
 *
 * In the underlying device model one descriptor IS one connection: the passive
 * open turns the listening descriptor itself into the connected one.  So this
 * used to return `s', and a server could accept exactly once -- which is no
 * server at all.
 *
 * What makes the BSD shape possible is that the two descriptors can be
 * swapped underneath the caller.  A fresh channel is opened and set listening
 * on the same port (relisten, SHARED so the stack permits both), and then:
 *
 *	dup   the connected channel's fd	-> the number accept() returns
 *	dup2  the new listening channel's fd onto `s'
 *
 * The dup comes FIRST because dup2 closes its target, and its target is the
 * connection.  Afterwards the caller's `s' refers to the new listening
 * channel and the returned number to the connection, which is exactly what a
 * caller written against BSD expects of both.
 */
int accept(s, addr, addrlen)
int s;
struct sockaddr *addr;
int *addrlen;
{
	struct sock *sk= lookup(s), *ns;
	struct nwio_tcpconf conf;
	int connfd;

	if (!sk) { errno= EBADF; return -1; }
	if (!sk->s_listening)
	{
		/* accept() without listen().  BSD calls that an error; here the
		 * passive open is the same operation either way, so start one
		 * rather than fail on a technicality. */
		struct nwio_tcpcl cl;

		cl.nwtcl_flags= 0;
		cl.nwtcl_ttl= 0;
		if (ichan_post_ioctl(&sk->s_ic, NWIOTCPLISTEN, (char *)&cl,
				sizeof(cl)) < 0)
			return -1;
		sk->s_listening= 1;
	}
	if (ichan_wait_ioctl(&sk->s_ic) < 0)	/* blocks until a peer arrives */
		return -1;
	sk->s_listening= 0;			/* sk IS the connection now */

	/* Who connected.  The stack knows; we have never asked before, and a
	 * server that logs or filters by peer needs the answer. */
	memset((char *)&conf, 0, sizeof(conf));
	if (ichan_ioctl_get(&sk->s_ic, NWIOGTCPCONF, (char *)&conf,
			sizeof(conf)) > 0)
	{
		sk->s_remaddr= conf.nwtc_remaddr;
		sk->s_remport= conf.nwtc_remport;
		sk->s_locaddr= conf.nwtc_locaddr;
		sk->s_locport= conf.nwtc_locport;
	}
	if (addr && addrlen && *addrlen >= (int)sizeof(struct sockaddr_in))
	{
		struct sockaddr_in *sin= (struct sockaddr_in *)addr;

		memset((char *)sin, 0, sizeof(*sin));
		sin->sin_family= AF_INET;
		sin->sin_addr.s_addr= sk->s_remaddr;
		sin->sin_port= sk->s_remport;
		*addrlen= sizeof(*sin);
	}

	/* Every failure from here on gives the CONNECTION up and keeps `s'
	 * listening (acceptlost).  None of them returns `s': a caller cannot
	 * tell that number from a connection, and the one it would then close
	 * is its own listening socket. */
	if ((ns= relisten(sk)) == (struct sock *)0)
		return acceptlost(sk, s);
	if ((connfd= dup(sk->s_ic.ic_replfd)) < 0)
	{
		ichan_close(&ns->s_ic);
		sockfree(ns);
		return acceptlost(sk, s);
	}
	if (dup2(ns->s_ic.ic_replfd, s) < 0)
	{
		_rawclose(connfd);
		ichan_close(&ns->s_ic);
		sockfree(ns);
		return acceptlost(sk, s);
	}
	_rawclose(ns->s_ic.ic_replfd);
	ns->s_ic.ic_replfd= s;		/* the caller's fd: still listening */
	sk->s_ic.ic_replfd= connfd;	/* the connection, on its own fd	*/
	(void)ichan_arm(&sk->s_ic);
	return connfd;
}

/*
 * getsockname -- the local address this socket is bound to.
 *
 * Answered from our own record of the bind, not from the stack: the channel
 * has no query for it, and for a socket whose port the stack chose (NWTC_LP_SEL
 * on a connect with no bind) we do not learn it either -- so that case reports
 * port 0 rather than inventing one.
 *
 * A NON-socket fd is an error, and that matters: a BSD daemon asks
 * getsockname(0) to find out whether inetd handed it a connection on its
 * standard input, and takes failure as "no, started from a shell".  Answering
 * anything at all for fd 0 would make every such daemon believe it was
 * inetd-spawned.
 *
 * On this machine the answer for a service /etc/inetd started is ALWAYS "no",
 * and correctly so: the switchboard cannot hand over a socket at all (a
 * connection is FIFOs plus framing state, and exec discards the state), so it
 * runs the service on a pipe and relays.  A service that needs to know it was
 * spawned, or who the peer is, reads INETD_LOCADDR/INETD_LOCPORT/
 * INETD_REMADDR/INETD_REMPORT out of its environment instead -- see
 * net/inetd.c makeenv().
 *
 * EBADF, not BSD's ENOTSOCK, and EINVAL below rather than ENOPROTOOPT: this
 * system's errno set predates sockets and has neither, and a number outside it
 * would index sys_errlist[] past its end when the program printed it.  EBADF is
 * what every other call here answers for a non-socket fd.
 */
int getsockname(s, addr, addrlen)
int s;
struct sockaddr *addr;
int *addrlen;
{
	struct sock *sk= lookup(s);
	struct sockaddr_in *sin= (struct sockaddr_in *)addr;

	if (!sk) { errno= EBADF; return -1; }
	if (!addr || !addrlen || *addrlen < (int)sizeof(*sin))
	{ errno= EINVAL; return -1; }
	memset((char *)sin, 0, sizeof(*sin));
	sin->sin_family= AF_INET;
	sin->sin_addr.s_addr= sk->s_locaddr;
	sin->sin_port= sk->s_locport;
	*addrlen= sizeof(*sin);
	return 0;
}

int getpeername(s, addr, addrlen)
int s;
struct sockaddr *addr;
int *addrlen;
{
	struct sock *sk= lookup(s);
	struct sockaddr_in *sin= (struct sockaddr_in *)addr;

	if (!sk) { errno= EBADF; return -1; }
	if (!addr || !addrlen || *addrlen < (int)sizeof(*sin))
	{ errno= EINVAL; return -1; }
	memset((char *)sin, 0, sizeof(*sin));
	sin->sin_family= AF_INET;
	sin->sin_addr.s_addr= sk->s_remaddr;
	sin->sin_port= sk->s_remport;
	*addrlen= sizeof(*sin);
	return 0;
}

/*
 * setsockopt / getsockopt -- only the options this stack actually has.
 *
 * The underlying model has no per-option control: a socket's behaviour is
 * fixed by the flag word handed to NWIOSUDPOPT/NWIOSTCPCONF when it was bound,
 * and cannot be changed afterwards.  So the honest thing is to succeed for the
 * options that flag word ALREADY grants and to fail for the rest, rather than
 * accepting everything and silently doing nothing:
 *
 *	SO_BROADCAST	bind() sets NWUO_EN_BROAD on every datagram socket, so
 *			broadcast is already enabled -- this is what hunt(6)
 *			needs before its discovery datagram.
 *	SO_USELOOPBACK	ip_write() loops a packet addressed to our own address
 *			back internally, always.
 *	SO_REUSEADDR	there is no TIME_WAIT hold on a local port here.
 *
 * Anything else answers EINVAL, which lets a caller tell "not supported" from
 * "done" -- BSD would say ENOPROTOOPT, which this system has no number for.
 */
int setsockopt(s, level, name, val, len)
int s, level, name, len;
char *val;
{
	if (!lookup(s)) { errno= EBADF; return -1; }
	if (level != SOL_SOCKET)
	{ errno= EINVAL; return -1; }
	switch (name) {
	case SO_BROADCAST:
	case SO_USELOOPBACK:
	case SO_REUSEADDR:
	case SO_KEEPALIVE:
		return 0;
	}
	errno= EINVAL;
	return -1;
}

int getsockopt(s, level, name, val, len)
int s, level, name;
char *val;
int *len;
{
	struct sock *sk= lookup(s);

	if (!sk) { errno= EBADF; return -1; }
	if (level != SOL_SOCKET || !val || !len || *len < (int)sizeof(int))
	{ errno= EINVAL; return -1; }
	switch (name) {
	case SO_BROADCAST:
	case SO_USELOOPBACK:
	case SO_REUSEADDR:
		*(int *)val= 1;
		*len= sizeof(int);
		return 0;
	case SO_TYPE:
		*(int *)val= sk->s_type;
		*len= sizeof(int);
		return 0;
	case SO_ERROR:
		*(int *)val= 0;
		*len= sizeof(int);
		return 0;
	}
	errno= EINVAL;
	return -1;
}

int send(s, buf, len, flags)
int s;
char *buf;
int len, flags;
{
	struct sock *sk= lookup(s);

	if (!sk) { errno= EBADF; return -1; }
	return ichan_write(&sk->s_ic, buf, len);
}

int recv(s, buf, len, flags)
int s;
char *buf;
int len, flags;
{
	struct sock *sk= lookup(s);

	if (!sk) { errno= EBADF; return -1; }
	return ichan_recv(&sk->s_ic, buf, len);
}

/*
 * A datagram plus its header, assembled for one write.  Static because it is
 * 2 KB and this machine's user stack is small.
 */
static char dgbuf[2048];

int sendto(s, buf, len, flags, to, tolen)
int s, len, flags, tolen;
char *buf;
struct sockaddr *to;
{
	struct sock *sk= lookup(s);
	struct sockaddr_in *sin= (struct sockaddr_in *)to;
	struct udp_io_hdr *h;
	int n;

	if (!sk) { errno= EBADF; return -1; }
	if (sin)
	{
		sk->s_remaddr= sin->sin_addr.s_addr;
		sk->s_remport= sin->sin_port;
	}
	if (sk->s_type != SOCK_DGRAM)
		return send(s, buf, len, flags);

	/* Unbound: give it an ephemeral port now, as BSD does. */
	if (!sk->s_udpconf && udp_conf(sk) < 0)
		return -1;

	if (len < 0 || len > (int)sizeof(dgbuf) - UDP_IO_HDR_SIZE)
	{
		/* ichan_fail, not `errno= EINVAL': this file sees Minix's
		 * errno.h, where the values are NEGATIVE, and handing one
		 * straight to a user program is the bug fixed in task #37.
		 * ichan_fail translates and returns -1. */
		return ichan_fail(EINVAL);
	}
	h= (struct udp_io_hdr *)dgbuf;
	h->uih_src_addr= 0;		/* the stack fills in the source */
	h->uih_dst_addr= sk->s_remaddr;
	h->uih_src_port= sk->s_locport;
	h->uih_dst_port= sk->s_remport;
	h->uih_ip_opt_len= 0;
	h->uih_data_len= len;		/* host order: see udp_io_hdr above */
	memcpy(dgbuf + UDP_IO_HDR_SIZE, buf, len);

	n= ichan_write(&sk->s_ic, dgbuf, UDP_IO_HDR_SIZE + len);
	if (n < 0)
		return -1;
	n -= UDP_IO_HDR_SIZE;
	return (n < 0) ? 0 : n;
}

int recvfrom(s, buf, len, flags, from, fromlen)
int s, len, flags;
char *buf;
struct sockaddr *from;
int *fromlen;
{
	struct sock *sk= lookup(s);
	struct udp_io_hdr *h;
	int n, data;

	if (!sk) { errno= EBADF; return -1; }

	if (sk->s_type != SOCK_DGRAM)
	{
		n= recv(s, buf, len, flags);
		if (n >= 0 && from && fromlen &&
			*fromlen >= sizeof(struct sockaddr_in))
		{
			struct sockaddr_in *sin= (struct sockaddr_in *)from;
			sin->sin_family= AF_INET;
			sin->sin_addr.s_addr= sk->s_remaddr;
			sin->sin_port= sk->s_remport;
			*fromlen= sizeof(struct sockaddr_in);
		}
		return n;
	}

	/* A datagram arrives with its header in front of it -- that is what
	 * tells us who sent it, which is the whole point of recvfrom(). */
	n= ichan_recv(&sk->s_ic, dgbuf, (int)sizeof(dgbuf));
	if (n < 0)
		return -1;
	if (n < UDP_IO_HDR_SIZE)
		return ichan_fail(EINVAL);	/* a datagram without its header */
	h= (struct udp_io_hdr *)dgbuf;
	data= n - UDP_IO_HDR_SIZE;
	if (data > len)
		data= len;		/* excess is dropped, as UDP does */
	memcpy(buf, dgbuf + UDP_IO_HDR_SIZE, data);

	sk->s_remaddr= h->uih_src_addr;
	sk->s_remport= h->uih_src_port;
	if (from && fromlen && *fromlen >= sizeof(struct sockaddr_in))
	{
		struct sockaddr_in *sin= (struct sockaddr_in *)from;
		sin->sin_family= AF_INET;
		sin->sin_addr.s_addr= h->uih_src_addr;
		sin->sin_port= h->uih_src_port;
		*fromlen= sizeof(struct sockaddr_in);
	}
	return data;
}

int shutdown(s, how)
int s, how;
{
	struct sock *sk= lookup(s);

	if (!sk) { errno= EBADF; return -1; }
	return chan_ioctl(sk, NWIOTCPSHUTDOWN, (char *)0, 0);
}

int soclose(s)
int s;
{
	struct sock *sk= lookup(s);

	if (!sk) { errno= EBADF; return -1; }
	ichan_close(&sk->s_ic);
	sockfree(sk);
	return 0;
}

/*
 * sockdrop -- give up this process's hold on a socket without closing it.
 *
 * close() on a socket is soclose(), which sends NWR_CLOSE to the daemon and
 * unlinks both FIFOs: it destroys the connection for everybody.  After a fork
 * that is exactly what neither side wants -- the parent of a server child must
 * let go of the connection the child is serving, and the child must let go of
 * the listening sockets that stay the parent's -- so a second verb is needed,
 * and there is no way to build it out of the first.
 *
 * It closes both descriptors of the channel and frees the table slot, and does
 * nothing else: no request to the daemon, no unlink.  A FIFO stays open while
 * any process holds a writer, so the channel survives for as long as the other
 * side of the fork wants it, and dies of request-FIFO EOF when the last one
 * goes -- which is how the daemon reclaims a channel anyway.
 *
 * The socket's descriptors are released, so the caller gets them back for
 * something else; that is the point of calling it in a super-server, where
 * every configured service costs two.
 */
int sockdrop(s)
int s;
{
	struct sock *sk= lookup(s);

	if (!sk) { errno= EBADF; return -1; }
	_rawclose(sk->s_ic.ic_reqfd);
	_rawclose(sk->s_ic.ic_replfd);
	sockfree(sk);
	return 0;
}

/* "a.b.c.d" -> address in network byte order (Z8001 is big-endian). */
ipaddr_t inet_addr(cp)
char *cp;
{
	ipaddr_t addr;
	int i, byte, digits;

	/*
	 * INADDR_NONE for anything that is not four dotted decimal numbers.
	 *
	 * This used to accept whatever it was given -- it scanned digits, took
	 * whatever it found, and never looked at the rest -- so a string with no
	 * digits in it came back as 0.0.0.0 and looked like a successful parse.
	 * gethostbyname() cannot then tell a dotted quad from a host name, so
	 * "no.such.host" resolved to 0.0.0.0 instead of failing, and every name
	 * lookup answered before it ever reached /etc/hosts.
	 *
	 * 255.255.255.255 is indistinguishable from failure here, as it is in
	 * BSD: the value IS INADDR_NONE.  That is the historical wart, kept
	 * rather than papered over, and it is why a caller that must handle the
	 * broadcast address uses inet_aton() instead.
	 */
	addr= 0;
	for (i= 0; i < 4; i++)
	{
		if (i > 0)
		{
			if (*cp != '.')
				return (ipaddr_t)0xFFFFFFFFL;
			cp++;
		}
		byte= digits= 0;
		while (*cp >= '0' && *cp <= '9')
		{
			byte= byte * 10 + (*cp++ - '0');
			if (++digits > 3 || byte > 255)
				return (ipaddr_t)0xFFFFFFFFL;
		}
		if (digits == 0)
			return (ipaddr_t)0xFFFFFFFFL;
		addr= (addr << 8) | (ipaddr_t)byte;
	}
	return (*cp == '\0') ? addr : (ipaddr_t)0xFFFFFFFFL;
}

/*
 * inet_aton -- the form that can report failure unambiguously.
 *
 * Returns 1 on success, 0 on a malformed address, and is the call to use where
 * 255.255.255.255 has to be told from an error.
 */
int inet_aton(cp, inp)
char *cp;
struct in_addr *inp;
{
	ipaddr_t a;

	if (strcmp(cp, "255.255.255.255") == 0)
	{
		if (inp)
			inp->s_addr= (ipaddr_t)0xFFFFFFFFL;
		return 1;
	}
	if ((a= inet_addr(cp)) == (ipaddr_t)0xFFFFFFFFL)
		return 0;
	if (inp)
		inp->s_addr= a;
	return 1;
}

char *inet_ntoa(in)
struct in_addr in;
{
	static char buf[16];
	ipaddr_t a= in.s_addr;

	sprintf(buf, "%d.%d.%d.%d",
		(int)((a >> 24) & 0xFF), (int)((a >> 16) & 0xFF),
		(int)((a >> 8) & 0xFF), (int)(a & 0xFF));
	return buf;
}

/*
 * read/write/close overrides -- a socket fd is served over its channel, every
 * other fd falls through to the raw syscall stubs _rawread/_rawwrite/_rawclose
 * (rawsys.s).  Since a socket fd is a
 * real kernel fd (the reply FIFO), select() and the rest work on it unchanged.
 * inet_chan.c uses the raw syscalls internally, so there is no recursion.
 */
int read(fd, buf, n)
int fd;
char *buf;
int n;
{
	return lookup(fd) ? recv(fd, buf, n, 0) : _rawread(fd, buf, n);
}

int write(fd, buf, n)
int fd;
char *buf;
int n;
{
	return lookup(fd) ? send(fd, buf, n, 0) : _rawwrite(fd, buf, n);
}

int close(fd)
int fd;
{
	return lookup(fd) ? soclose(fd) : _rawclose(fd);
}

/*
 * Bytes already taken off this socket's reply FIFO and held for the read that
 * has not asked for them yet, or 0 for an fd that is not a socket.
 *
 * A completed READ reply is taken by whatever channel operation happens to
 * meet it -- a send() waiting for its own reply parks the payload in the hold
 * buffer (inet_chan.c ichan_take) -- so data can be present with the fd not
 * readable, and a program that decides what to do by poll() alone waits for a
 * readability that has already happened.  A poll loop therefore asks this
 * first and only blocks when it answers 0.
 */
int sockheld(fd)
int fd;
{
	struct sock *sk= lookup(fd);

	return sk ? sk->s_ic.ic_hlen - sk->s_ic.ic_hoff : 0;
}

/*
 * open()/ioctl() overrides -- the /dev/tcp shim.
 *
 * The Minix net programs (telnet, ftp, talk, the resolver) do not call socket().
 * They open a network device and drive it with NWIO* ioctls -- which is the
 * protocol this library already speaks to the daemon, so the shim is a rename
 * rather than an implementation: open("/dev/tcp") becomes a channel, and each
 * ioctl becomes the ichan_ioctl or ichan_ioctl_get it already maps to.  Nothing
 * is needed in the kernel and nothing is needed per program.
 *
 * The alternative was a /dev/tcp character driver forwarding to the daemon,
 * which costs kernel text and buys the same thing.
 */
struct netdev {
	char	*nd_path;		/* device, without a trailing unit */
	int	nd_minor;		/* if2minor(0, dev) = dev	*/
	int	nd_type;		/* what read/write should mean	*/
};

static struct netdev netdevs[] = {
	"/dev/tcp",	TCP_MINOR,	SOCK_STREAM,
	"/dev/udp",	UDP_MINOR,	SOCK_DGRAM,
	"/dev/ip",	IP_MINOR,	SOCK_STREAM,
	"/dev/psip",	PSIP_MINOR,	SOCK_STREAM,
	(char *)0,	0,		0
};

/*
 * Which network device is this, if any?
 *
 * The unit suffix is optional and ignored: rc.net makes both /dev/tcp and
 * /dev/tcp0 and either name reaches interface 0.  The suffix must be digits, so
 * that a prefix match cannot claim an unrelated name -- /dev/ipfoo is not
 * /dev/ip, and the four-character prefix of /dev/tty is not /dev/tcp.
 */
static struct netdev *netdev(path)
char *path;
{
	struct netdev *nd;
	char *p;
	int n;

	for (nd= netdevs; nd->nd_path; nd++)
	{
		n= strlen(nd->nd_path);
		if (strncmp(path, nd->nd_path, n) != 0)
			continue;
		for (p= path + n; *p; p++)
		{
			if (*p < '0' || *p > '9')
				break;
		}
		if (*p == '\0')
			return nd;
	}
	return (struct netdev *)0;
}

/*
 * The argument size and direction of an NWIO* request.
 *
 * _IOW(x,y,t) reduces to (x<<8)|y on a 16-bit machine, so the code carries
 * neither -- see minix/ioctl.h.  The library has to supply both, and the low
 * byte of the code is the request number the table keys on.
 *
 * nd_get: 0 = the caller's struct goes TO the stack, 1 = it comes back, 2 = it
 * goes both ways.  The two route queries are _IORW -- an entry number in, a
 * route out -- and are the only members of the third class; they reach the
 * daemon through ichan_ioctl_rw().
 */
struct nwioreq {
	int	nr_req;			/* the whole (x<<8)|y code	*/
	int	nr_len;			/* argument size, 0 for _IO	*/
	int	nr_get;			/* 0 write, 1 read, 2 both	*/
};

static struct nwioreq nwioreqs[] = {
	NWIOSIPCONF,	sizeof(struct nwio_ipconf),	0,
	NWIOGIPCONF,	sizeof(struct nwio_ipconf),	1,
	NWIOSIPOPT,	sizeof(struct nwio_ipopt),	0,
	NWIOGIPOPT,	sizeof(struct nwio_ipopt),	1,
	NWIOSIPOROUTE,	sizeof(struct nwio_route),	0,
	NWIODIPOROUTE,	sizeof(struct nwio_route),	0,
	NWIOSIPIROUTE,	sizeof(struct nwio_route),	0,
	NWIODIPIROUTE,	sizeof(struct nwio_route),	0,
	NWIOGIPOROUTE,	sizeof(struct nwio_route),	2,
	NWIOGIPIROUTE,	sizeof(struct nwio_route),	2,
	NWIOSTCPCONF,	sizeof(struct nwio_tcpconf),	0,
	NWIOGTCPCONF,	sizeof(struct nwio_tcpconf),	1,
	NWIOTCPCONN,	sizeof(struct nwio_tcpcl),	0,
	NWIOTCPLISTEN,	sizeof(struct nwio_tcpcl),	0,
	NWIOTCPATTACH,	sizeof(struct nwio_tcpatt),	0,
	NWIOTCPSHUTDOWN, 0,				0,
	NWIOSTCPOPT,	sizeof(struct nwio_tcpopt),	0,
	NWIOGTCPOPT,	sizeof(struct nwio_tcpopt),	1,
	NWIOSUDPOPT,	sizeof(struct nwio_udpopt),	0,
	NWIOGUDPOPT,	sizeof(struct nwio_udpopt),	1,
	NWIOSPSIPOPT,	sizeof(struct nwio_psipopt),	0,
	NWIOGPSIPOPT,	sizeof(struct nwio_psipopt),	1,
	0,		0,				0
};

static struct nwioreq *nwioreq(req)
int req;
{
	struct nwioreq *nr;

	for (nr= nwioreqs; nr->nr_req; nr++)
	{
		if (nr->nr_req == req)
			return nr;
	}
	return (struct nwioreq *)0;
}

/*
 * A third argument is declared but not named: open() is variadic in C, and a
 * caller that is not creating a file passes two.  It is only read when O_CREAT
 * is set, which is exactly when the caller supplied it -- the same contract
 * libc's own open() stub has.
 */
int open(path, flags, perm)
char *path;
int flags, perm;
{
	struct netdev *nd= netdev(path);
	struct sock *sk;

	if (!nd)
		return _rawopen(path, flags, perm);
	if ((sk= sockalloc()) == (struct sock *)0)
		return -1;		/* sockalloc set errno */
	sk->s_type= nd->nd_type;
	if (ichan_open(&sk->s_ic, nd->nd_minor) < 0)
	{
		/* ichan_open's errno is the answer a caller gets -- ENFILE when
		 * the daemon is full, EMFILE when this process is out of
		 * descriptors -- so it has to survive the free. */
		int e= errno;

		sockfree(sk);
		errno= e;
		return -1;
	}
	return sk->s_ic.ic_replfd;
}

int ioctl(fd, req, arg)
int fd;
int req;
char *arg;
{
	struct sock *sk= lookup(fd);
	struct nwioreq *nr;

	if (!sk)
		return _rawioctl(fd, req, arg);
	if ((nr= nwioreq(req)) == (struct nwioreq *)0)
	{
		errno= EINVAL;
		return -1;
	}
	if (nr->nr_get == 2)
		return ichan_ioctl_rw(&sk->s_ic, req, arg, nr->nr_len) < 0
			? -1 : 0;
	if (nr->nr_get)
		return ichan_ioctl_get(&sk->s_ic, req, arg, nr->nr_len) < 0
			? -1 : 0;
	if (ichan_ioctl(&sk->s_ic, req, arg, nr->nr_len) < 0)
		return -1;
	/*
	 * A connection now exists on this channel, so keep a READ outstanding
	 * from here on: until one is posted the daemon has no reason to write
	 * to the reply FIFO, and poll() reports "nothing to read" however much
	 * has arrived.  connect() and accept() arm the BSD path at exactly this
	 * point; these two ioctls are the same event for a client that reaches
	 * the stack through /dev/tcp.
	 */
	if (req == NWIOTCPCONN || req == NWIOTCPLISTEN)
		(void)ichan_arm(&sk->s_ic);
	return 0;
}
