/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * ichanprobe.c -- bisect the inet daemon's control-channel handshake.
 *
 * ifconfig dies with "Bad system call -- core dumped" before printing anything,
 * so it fails inside ichan_open() or ichan_ioctl().  On this kernel that message
 * does NOT mean a bad system call number: trap.c raises SIGSYS for any syscall
 * that returns EFAULT (trap.c, u_error == EFAULT -> sendsig(SIGSYS)), so the
 * real fault is a user pointer the kernel would not accept.
 *
 * This walks the same handshake one syscall at a time, printing the return value
 * and errno after each, and then repeats it through ichan_open() itself.  If the
 * open-coded sequence survives and ichan_open() does not, the difference is that
 * ichan_open() reaches its path buffers through a pointer to the caller's frame
 * rather than addressing its own -- which is where a far-pointer segment can be
 * lost.
 *
 * Needs the daemon running (/etc/inet &): the rendezvous open and the reply
 * read both block until it answers.
 *
 * IT NOW HAS A VERDICT.  This was written as a bisecting instrument, to be read
 * by a person, and it printed every step's return value and errno and then
 * exited 0 whatever they were -- so once the fault it was written for was fixed
 * there was nothing left that could report a regression.  The steps that MUST
 * succeed are marked below and counted, and the two handshake statuses the
 * daemon sends back are checked, so a scripted run gets an answer.  The printing
 * stays: when it does fail, which step failed is the whole point of the program.
 *
 * WHAT IT STILL CANNOT DO.  It has no timeout.  The rendezvous open at step 8
 * and the reply reads at 12 and 16 block for ever if the daemon is not there or
 * does not answer, and there is no alarm(2) here to turn that into a verdict --
 * so "hangs" remains a possible outcome that no exit status describes.  Run it
 * under something that can time it out.
 */
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <net/gen/in.h>
#include <net/gen/ip_io.h>
#include <net/ioctl.h>
#include "inet_ipc.h"
#include "inet_chan.h"

#define IP_MINOR	1		/* if2minor(0, IP_DEV_OFF) */

extern int errno;
extern ipaddr_t inet_addr();
extern int _rawread();
extern int _rawwrite();
extern int _rawclose();

/* Report with the raw syscall, not stdio: a buffer that never gets flushed
 * because the process dies would hide the very last step, which is the one
 * that matters. */
static say(s)
char *s;
{
	_rawwrite(1, s, strlen(s));
}

static num(n)
long n;
{
	static char b[16];		/* static, so the printer itself cannot
					 * depend on frame addressing */
	int i;

	i = 15;
	b[i--] = '\0';
	if (n < 0) {
		if (n == 0)
			;
		n = -n;
		if (n == 0)
			b[i--] = '0';
		while (n != 0) {
			b[i--] = '0' + (int)(n % 10);
			n /= 10;
		}
		b[i--] = '-';
	} else if (n == 0)
		b[i--] = '0';
	else while (n != 0) {
		b[i--] = '0' + (int)(n % 10);
		n /= 10;
	}
	say(&b[i + 1]);
}

static step();

/* Mimic ichan_ioctl's pointer handling exactly -- the payload address arrives
 * as a parameter aimed at the CALLER's frame -- but do not wait for a reply.
 * The daemon dies on the first NWIOSIPCONF it is handed, so a probe that read
 * the reply would block there and never report whether the client's own writes
 * were accepted, which is the question. */
static long fwdioctl(fd, req, data, len)
int fd;
int req;			/* int: the NWIO* macros' type (see ichan_ioctl) */
char *data;
int len;
{
	nwreq_t rq;
	long rc;

	rq.nwr_op = NWR_IOCTL;
	rq.nwr_minor = 0; rq.nwr_mode = 0; rq.nwr_dlen = 0;
	rq.nwr_req = req;
	rq.nwr_count = len;
	/* Show the pointer as it ARRIVED: if the far pointer is mangled in
	 * transit the kernel's EFAULT is a symptom, not the cause. */
	say("  fwdioctl: fd=");
	num((long)fd);
	say(" data=");
	num((long)data);
	say(" len=");
	num((long)len);
	say(" req=");
	num((long)req);
	say("\r\n");
	errno = 0; rc = _rawwrite(fd, (char *)&rq, sizeof(rq));	step(20, rc);
	errno = 0; rc = _rawwrite(fd, data, len);		step(21, rc);
	return rc;
}

static int fails;

static step(k, rc)
int k;
long rc;
{
	say("[");
	num((long)k);
	say("] rc=");
	num(rc);
	say(" e=");
	num((long)errno);
	say("\r\n");
}

/* A step whose failure is a failure of the run: report it as step() does, and
 * remember it.  Used for everything except the two unlinks, which are expected
 * to fail when the FIFOs are not there yet. */
static must(k, rc)
int k;
long rc;
{
	step(k, rc);
	if (rc < 0)
	{
		say("probe: FAIL at step ");
		num((long)k);
		say("\r\n");
		fails++;
	}
}

/* A status word the daemon sent back: >=0 is a count or OK, <0 is -errno. */
static status(what, st)
char *what;
long st;
{
	say(what);
	num(st);
	say("\r\n");
	if (st < 0)
	{
		say("probe: FAIL -- ");
		say(what);
		say("is a refusal\r\n");
		fails++;
	}
}

main(argc, argv)
int argc;
char **argv;
{
	static struct ichan c, c2;	/* 512-byte hold buffer each: too big
					 * for this machine's user stack */
	sr_hello_t hello;
	nwreq_t rq;
	nwrepl_t rp;
	nwio_ipconf_t conf;
	int pid, rvfd;
	long rc;

	say("probe: start\r\n");
	pid = getpid();
	step(1, (long)pid);

	sprintf(c.ic_reqpath, "/tmp/ip%d.q", pid);
	sprintf(c.ic_replpath, "/tmp/ip%d.r", pid);
	say("probe: paths ");
	say(c.ic_reqpath);
	say(" ");
	say(c.ic_replpath);
	say("\r\n");

	errno = 0; rc = unlink(c.ic_reqpath);			step(2, rc);
	errno = 0; rc = unlink(c.ic_replpath);			step(3, rc);
	errno = 0; rc = mknod(c.ic_reqpath, 010000|0600, 0);	must(4, rc);
	errno = 0; rc = mknod(c.ic_replpath, 010000|0600, 0);	must(5, rc);
	errno = 0; rc = c.ic_reqfd = open(c.ic_reqpath, O_RDWR);	must(6, rc);
	errno = 0; rc = c.ic_replfd = open(c.ic_replpath, O_RDWR); must(7, rc);

	hello.sh_id = (long)pid * 100;
	strcpy(hello.sh_req, c.ic_reqpath);
	strcpy(hello.sh_repl, c.ic_replpath);
	say("probe: opening the rendezvous (blocks until the daemon reads)\r\n");
	errno = 0; rc = rvfd = open(INET_RENDEZVOUS, O_WRONLY);	must(8, rc);
	errno = 0; rc = _rawwrite(rvfd, (char *)&hello, sizeof(hello)); must(9, rc);
	errno = 0; rc = _rawclose(rvfd);			must(10, rc);

	rq.nwr_op = NWR_OPEN;
	rq.nwr_minor = IP_MINOR;
	rq.nwr_mode = 0; rq.nwr_dlen = 0; rq.nwr_req = 0; rq.nwr_count = 0;
	errno = 0; rc = _rawwrite(c.ic_reqfd, (char *)&rq, sizeof(rq)); must(11, rc);
	say("probe: reading the OPEN reply (blocks until the daemon answers)\r\n");
	errno = 0; rc = _rawread(c.ic_replfd, (char *)&rp, sizeof(rp)); must(12, rc);
	status("probe: open-coded status=", rp.nwr_status);

	/* Now the real thing, on a second channel. */
	say("probe: calling ichan_open\r\n");
	errno = 0; rc = ichan_open(&c2, IP_MINOR);		must(13, rc);

	/* The NWIOSIPCONF that ifconfig dies on.  Open-coded first (request
	 * record, then payload, then reply), then through ichan_ioctl -- where
	 * the payload pointer is a parameter aimed at the caller's frame. */
	say("probe: sizeof(conf)=");
	num((long)sizeof(conf));
	say(" NWIOSIPCONF=");
	num((long)NWIOSIPCONF);
	say("\r\n");
	conf.nwic_flags = NWIC_IPADDR_SET | NWIC_NETMASK_SET;
	conf.nwic_ipaddr = inet_addr("10.0.0.2");
	conf.nwic_netmask = inet_addr("255.255.255.0");
	say("probe: addr=");
	num((long)conf.nwic_ipaddr);
	say(" mask=");
	num((long)conf.nwic_netmask);
	say("\r\n");

	/* Client side first, on channel 2, no reply wait: does handing the
	 * kernel a payload pointer that belongs to the caller's frame fault? */
	say("probe: forwarded-pointer ioctl write (no reply wait)\r\n");
	say("  main: &conf=");
	num((long)(char *)&conf);
	say(" fd=");
	num((long)c2.ic_reqfd);
	say("\r\n");
	(void)fwdioctl(c2.ic_reqfd, NWIOSIPCONF, (char *)&conf, sizeof(conf));

	rq.nwr_op = NWR_IOCTL;
	rq.nwr_minor = 0; rq.nwr_mode = 0; rq.nwr_dlen = 0;
	rq.nwr_req = NWIOSIPCONF;
	rq.nwr_count = sizeof(conf);
	errno = 0; rc = _rawwrite(c.ic_reqfd, (char *)&rq, sizeof(rq)); must(14, rc);
	errno = 0; rc = _rawwrite(c.ic_reqfd, (char *)&conf, sizeof(conf)); must(15, rc);
	say("probe: reading the IOCTL reply\r\n");
	errno = 0; rc = _rawread(c.ic_replfd, (char *)&rp, sizeof(rp)); must(16, rc);
	status("probe: open-coded ioctl status=", rp.nwr_status);

	say("probe: calling ichan_ioctl\r\n");
	errno = 0;
	rc = ichan_ioctl(&c2, NWIOSIPCONF, (char *)&conf, sizeof(conf));
								must(17, rc);
	if (fails)
	{
		say("probe: FAIL -- ");
		num((long)fails);
		say(" step(s)\r\n");
		return 1;
	}
	say("probe: PASS\r\n");
	return 0;
}
