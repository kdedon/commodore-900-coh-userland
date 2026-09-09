/*
 * sockshim.c -- host stand-in for libsocket, with injectable defects.
 *
 * The socket programs in net/test are compiled here VERBATIM and run over
 * real loopback sockets on the build machine.  This file does two things:
 *
 *   1. makes them runnable at all -- inet_addr() maps the target's 10.0.0.x
 *	onto 127.0.0.1, and open("/dev/tcp") becomes a countable fake channel
 *	so chanmax has something to exhaust;
 *
 *   2. injects the defects the tests claim to catch, at the read/write
 *	boundary, so each new check can be SEEN to fail.
 *
 * $BREAK selects one:
 *
 *	none		behave.  Every test must pass.
 *	echo-wrong	a server's echo goes out with a byte changed: the peer
 *			answers with the WRONG payload.  This is what a test
 *			that moves bytes and never looks at them cannot see.
 *	crosswire	a client's read returns the payload a DIFFERENT client
 *			sent (the trailing digit is rotated).  That is the
 *			observable of a crosswired accept -- connection A's
 *			stream arriving on connection B.  The shim delivers the
 *			bytes; it does not model the daemon's descriptor table,
 *			and it does not need to, because the bytes are the only
 *			thing the program can see.
 *	short-echo	a server's echo is truncated by one byte: a length check
 *			catches it, a "did anything come back" check does not.
 *	chanmax-none	open("/dev/tcp") is refused on the very first call.
 *	udp-from-wrong	recvfrom() delivers the datagram but names a different
 *			sender port.  A datagram test that only looks at the
 *			bytes cannot see this, and addressing is the whole
 *			reason udpecho exists.
 *	udp-payload-wrong  a datagram goes out with one byte changed.
 *	poll-never	poll() always reports the timeout, whatever arrived.
 *			This is the defect udppoll was written for: a stack that
 *			never arms the read says "not readable" forever.
 *	poll-always	poll() always reports POLLIN, whether or not anything is
 *			there.  A poll loop that always fires is as useless as
 *			one that never does, and only the idle checks see it.
 *
 * $CHANLIMIT sets how many fake /dev/tcp channels may be opened (default 15,
 * this machine's real ceiling: two FIFOs per connection against NOFILE 20).
 *
 * Host-only scaffolding.  Nothing here is compiled for the C900 and nothing
 * here ships.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>

#define MAXTRACK	64

static int accepted[MAXTRACK];		/* fds accept() handed back	*/
static int connected[MAXTRACK];		/* fds connect() completed on	*/
static int naccept, nconnect;
static int chanopen;
static char *bmode;

static char *mode()
{
	if (!bmode)
	{
		bmode = getenv("BREAK");
		if (!bmode)
			bmode = "none";
	}
	return bmode;
}

static int is(m)
char *m;
{
	return strcmp(mode(), m) == 0;
}

static int intrack(v, n, fd)
int *v, n, fd;
{
	int i;

	for (i = 0; i < n; i++)
		if (v[i] == fd)
			return 1;
	return 0;
}

/* --- the real calls, reached past our own definitions --- */

static ssize_t (*real_read)();
static ssize_t (*real_write)();
static int (*real_accept)();
static int (*real_connect)();
static int (*real_open)();

static void resolve()
{
	if (real_read)
		return;
	real_read = (ssize_t (*)())dlsym(RTLD_NEXT, "read");
	real_write = (ssize_t (*)())dlsym(RTLD_NEXT, "write");
	real_accept = (int (*)())dlsym(RTLD_NEXT, "accept");
	real_connect = (int (*)())dlsym(RTLD_NEXT, "connect");
	real_open = (int (*)())dlsym(RTLD_NEXT, "open");
}

/* --- interposed --- */

int accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
	int fd;

	resolve();
	fd = (*real_accept)(s, addr, addrlen);
	if (fd >= 0 && naccept < MAXTRACK)
		accepted[naccept++] = fd;
	return fd;
}

int connect(int s, const struct sockaddr *addr, socklen_t addrlen)
{
	int r;

	resolve();
	r = (*real_connect)(s, addr, addrlen);
	if (r == 0 && nconnect < MAXTRACK)
		connected[nconnect++] = s;
	return r;
}

ssize_t write(int fd, const void *buf, size_t n)
{
	char tmp[4096];

	resolve();
	if (n > 0 && n <= sizeof(tmp) && intrack(accepted, naccept, fd))
	{
		if (is("echo-wrong"))
		{
			memcpy(tmp, buf, n);
			/* Change a byte that is part of the message, not its
			 * terminating newline. */
			tmp[(n > 1) ? n - 2 : 0] ^= 0x20;
			return (*real_write)(fd, tmp, n);
		}
		if (is("short-echo") && n > 1)
			return (*real_write)(fd, buf, n - 1);
	}
	return (*real_write)(fd, buf, n);
}

ssize_t read(int fd, void *buf, size_t n)
{
	ssize_t k;
	char *p;
	int i;

	resolve();
	k = (*real_read)(fd, buf, n);
	if (k > 0 && is("crosswire") && intrack(connected, nconnect, fd))
	{
		p = (char *)buf;
		for (i = 0; i < (int)k; i++)
			if (p[i] >= '0' && p[i] <= '9')
			{
				p[i] = '0' + ((p[i] - '0' + 1) % 10);
				break;
			}
	}
	return k;
}

/*
 * open() -- everything as usual, except the target's channel device, which
 * does not exist here.  Each successful "channel" is a real descriptor (a
 * second handle on /dev/null) so the program can close it, and the supply is
 * finite so chanmax has a ceiling to find.  ENFILE is what the daemon answers
 * when it has no descriptors left (coh_sr.c sr_refuse), so that is the errno by
 * default; $CHANERR sets another, because the two refusals chanmax accepts mean
 * different things -- ENFILE is the machine full, EMFILE is the calling process
 * full at two descriptors a channel -- and anything else is a failure it must
 * not read as a ceiling.
 */
int open(const char *path, int flags, ...)
{
	int lim;
	char *e;

	resolve();
	if (path && strcmp(path, "/dev/tcp") == 0)
	{
		lim = (e = getenv("CHANLIMIT")) ? atoi(e) : 15;
		if (is("chanmax-none"))
			lim = 0;
		if (chanopen >= lim)
		{
			errno = (e = getenv("CHANERR")) ? atoi(e) : ENFILE;
			return -1;
		}
		chanopen++;
		return (*real_open)("/dev/null", O_RDWR, 0);
	}
	{ va_list ap; int m = 0; va_start(ap, flags); if (flags & O_CREAT) m = va_arg(ap, int); va_end(ap); return (*real_open)(path, flags, m); }
}

/*
 * bind/getsockname -- normally the host's own, but `getsockname-verbatim'
 * answers with the port the program itself asked bind() for.  That is what
 * libsocket did before udp_learnlocal(): a socket bound to a fixed port looked
 * perfectly correct, and one bound to port 0 got port 0 back.
 */
static int bport[MAXTRACK];
static int bfd[MAXTRACK];
static int nbind;
static int (*real_bind)();
static int (*real_getsockname)();

int bind(int s, const struct sockaddr *addr, socklen_t len)
{
	int r;

	resolve();
	if (!real_bind)
		real_bind = (int (*)())dlsym(RTLD_NEXT, "bind");
	r = (*real_bind)(s, addr, len);
	if (r == 0 && nbind < MAXTRACK)
	{
		bfd[nbind] = s;
		bport[nbind] = ((struct sockaddr_in *)addr)->sin_port;
		nbind++;
	}
	return r;
}

int getsockname(int s, struct sockaddr *addr, socklen_t *len)
{
	int i;

	resolve();
	if (!real_getsockname)
		real_getsockname = (int (*)())dlsym(RTLD_NEXT, "getsockname");
	/* Backwards: a closed descriptor is handed out again, so the LAST bind
	 * on this number is the one that is still in force. */
	if (is("getsockname-verbatim"))
		for (i = nbind - 1; i >= 0; i--)
			if (bfd[i] == s)
			{
				if ((*real_getsockname)(s, addr, len) < 0)
					return -1;
				((struct sockaddr_in *)addr)->sin_port =
					bport[i];
				return 0;
			}
	return (*real_getsockname)(s, addr, len);
}

/*
 * sendto/recvfrom -- the datagram path.  udpecho and udppoll never call
 * read()/write(), so the defects injected above cannot reach them; these are
 * the same two injections (a changed byte, a mis-reported peer) at the call
 * the datagram programs actually make.
 */
static ssize_t (*real_sendto)();
static ssize_t (*real_recvfrom)();

ssize_t sendto(int s, const void *buf, size_t n, int flags,
	const struct sockaddr *to, socklen_t tolen)
{
	static int nsent;
	char tmp[4096];

	if (!real_sendto)
		real_sendto = (ssize_t (*)())dlsym(RTLD_NEXT, "sendto");
	/* The FIRST datagram only.  Corrupting every one of them flips the same
	 * bit again on the echo and hands the sender its own message back
	 * intact, so the injection cancels itself and the case reads as a pass. */
	if (is("udp-payload-wrong") && nsent++ == 0 && n > 0 && n <= sizeof(tmp))
	{
		memcpy(tmp, buf, n);
		tmp[n - 1] ^= 0x20;
		return (*real_sendto)(s, tmp, n, flags, to, tolen);
	}
	return (*real_sendto)(s, buf, n, flags, to, tolen);
}

ssize_t recvfrom(int s, void *buf, size_t n, int flags,
	struct sockaddr *from, socklen_t *fromlen)
{
	ssize_t k;

	if (!real_recvfrom)
		real_recvfrom = (ssize_t (*)())dlsym(RTLD_NEXT, "recvfrom");
	k = (*real_recvfrom)(s, buf, n, flags, from, fromlen);
	if (k >= 0 && from && is("udp-from-wrong"))
		((struct sockaddr_in *)from)->sin_port =
			htons(ntohs(((struct sockaddr_in *)from)->sin_port) + 1);
	return k;
}

/*
 * poll -- the readiness answer itself.  Both defects here are answers, not
 * errors: the call succeeds and reports something, which is why a program that
 * asks "did poll return" rather than "did it return the right thing" passes
 * against either one.
 */
int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	static int (*real_poll)();
	nfds_t i;

	if (!real_poll)
		real_poll = (int (*)())dlsym(RTLD_NEXT, "poll");
	if (is("poll-never"))
	{
		for (i = 0; i < nfds; i++)
			fds[i].revents = 0;
		return 0;
	}
	if (is("poll-always"))
	{
		for (i = 0; i < nfds; i++)
			fds[i].revents = POLLIN;
		return (int)nfds;
	}
	return (*real_poll)(fds, nfds, timeout);
}

/*
 * inet_addr -- the target's addresses, mapped onto loopback.  The programs name
 * 10.0.0.2 because that is what ifconfig gives the C900; here every one of them
 * is this machine.
 */
unsigned long inet_addr(const char *s)
{
	unsigned int a, b, c, d;

	if (s && strncmp(s, "10.0.0.", 7) == 0)
		return htonl(0x7F000001);
	if (s && sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
		return htonl((a << 24) | (b << 16) | (c << 8) | d);
	return htonl(0x7F000001);
}
