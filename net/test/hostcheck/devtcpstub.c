/*
 * devtcpstub.c -- a fake /dev/tcp, for proving that devtcp(8) notices a stack
 * that cannot tell it the local port it chose.
 *
 * devtcp.c is compiled here VERBATIM.  Its open(), ioctl(), read() and write()
 * land here instead of the kernel, and what the fake device does is chosen by
 * $BREAK:
 *
 *	none		connects, chooses a local port, reports it, echoes.
 *	locport0	everything works except that NWIOGTCPCONF answers with
 *			local port 0 -- the stack did not choose one, or cannot
 *			say which.  This is the check that printed FAIL and then
 *			fell through to "PASS devtcp" and exit(0).
 *	gtcpconf-fail	NWIOGTCPCONF fails outright.  Same fall-through.
 *	echo-wrong	the echo comes back with a byte changed.
 *	echo-short	the echo comes back one byte short and then the peer
 *			closes: a single read that compared only what arrived
 *			could not see this.
 *	conn-fail	NWIOTCPCONN fails.  The one failure the program always
 *			did catch -- a negative control that must still fail.
 *
 * Host-only scaffolding.  Nothing here is compiled for the C900.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <net/gen/in.h>
#include <net/gen/tcp.h>
#include <net/gen/tcp_io.h>
#include <net/ioctl.h>

#define FAKEFD	77			/* not a real descriptor	*/

static char *bmode;
static char echobuf[512];
static int echolen, echooff;
static int isopen;

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

int open(const char *path, int flags, ...)
{
	if (path && strcmp(path, "/dev/tcp") == 0)
	{
		isopen = 1;
		echolen = echooff = 0;
		return FAKEFD;
	}
	errno = ENOENT;
	return -1;
}

int close(int fd)
{
	if (fd == FAKEFD)
	{
		isopen = 0;
		return 0;
	}
	return syscall(3 /* SYS_close */, fd);
}

int ioctl(int fd, unsigned long req, void *arg)
{
	struct nwio_tcpconf *cp;

	if (fd != FAKEFD || !isopen)
	{
		errno = EBADF;
		return -1;
	}
	if (req == (unsigned long)NWIOSTCPCONF)
		return 0;
	if (req == (unsigned long)NWIOTCPCONN)
	{
		if (is("conn-fail"))
		{
			errno = EIO;
			return -1;
		}
		return 0;
	}
	if (req == (unsigned long)NWIOGTCPCONF)
	{
		if (is("gtcpconf-fail"))
		{
			errno = EINVAL;
			return -1;
		}
		cp = (struct nwio_tcpconf *)arg;
		memset((char *)cp, 0, sizeof(*cp));
		cp->nwtc_locport = is("locport0") ? 0 : 49152;
		return 0;
	}
	errno = EINVAL;
	return -1;
}

ssize_t write(int fd, const void *buf, size_t n)
{
	if (fd != FAKEFD)
		return syscall(1 /* SYS_write */, fd, buf, n);
	if (n > sizeof(echobuf))
		n = sizeof(echobuf);
	memcpy(echobuf, buf, n);
	echolen = (int)n;
	echooff = 0;
	if (is("echo-wrong") && echolen > 1)
		echobuf[echolen - 2] ^= 0x20;
	if (is("echo-short") && echolen > 1)
		echolen--;
	return (ssize_t)n;
}

ssize_t read(int fd, void *buf, size_t n)
{
	int k;

	if (fd != FAKEFD)
		return syscall(0 /* SYS_read */, fd, buf, n);
	k = echolen - echooff;
	if (k <= 0)
		return 0;			/* the peer closed */
	if (k > (int)n)
		k = (int)n;
	memcpy(buf, &echobuf[echooff], k);
	echooff += k;
	return k;
}
