/*
inet/coh_inet.c -- COHERENT main loop for the inet daemon (/etc/inet).

Replaces Minix inet.c.  Minix's inet was a microkernel server whose main loop
blocked in receive(ANY) and dispatched kernel messages (FS device requests,
the synchronous-alarm tick, Ethernet-task replies).  COHERENT is monolithic:
the stack is a userland daemon, so the loop instead blocks in select() over

  - the client control channel (coh_sr.c): a readable channel = a client
    open/read/write/ioctl request -> sr_process();
  - each enabled Ethernet port's /dev/eth fd (coh_eth.c): readable = a frame
    waiting -> eth_recv();
  - a timeout computed from the timer chain (coh_clock.c): expiry -> fire the
    due timers.

The stack's own internal event queue (ev_*) and expired-timer flag are drained
first each iteration, exactly as in Minix.

Copyright 1995 Philip Homburg (original); COHERENT port.
*/

#include "inet.h"

#include "mq.h"
#include "generic/type.h"
#include "generic/assert.h"
#include "generic/buf.h"
#include "generic/clock.h"
#include "generic/event.h"
#include "osdep_eth.h"
#include "generic/eth.h"
#include "generic/eth_int.h"
#include "generic/arp.h"
#include "generic/ip.h"
#include "generic/psip.h"
#include "generic/sr.h"
#include "generic/tcp.h"
#include "generic/udp.h"

#include <sys/select.h>
#include <stdlib.h>

THIS_FILE

int this_proc;			/* pid of this daemon			*/

/* coh_eth.c: deliver one waiting frame from a readable /dev/eth fd. */
void eth_recv ARGS(( eth_port_t *eth_port ));

/* coh_sr.c: the client control channels. */
void sr_fill_fdset ARGS(( fd_set *rd, int *pnfds ));	/* add rv + channels */
void sr_handle_fds ARGS(( fd_set *rd ));		/* service readable   */
int sr_ceiling ARGS(( void ));				/* channels available */

/* coh_clock.c: absolute tick time of the next timer, or 0 if idle. */
time_t clck_next_deadline ARGS(( void ));

/*
 * Longest single select() wait, in ticks.
 *
 * poll(2) carries its timeout as an int number of milliseconds -- the kernel's
 * upoll() declares it so -- which caps a representable wait at about 32.7
 * seconds, and a value past that reads as negative, i.e. "block forever, no
 * timer armed".  The stack's own timeouts run to twenty minutes (the TCP dead
 * timer), so the wait is split instead: waking early costs one pass round this
 * loop and fires nothing, since clck_expire_timers() runs a timer only when its
 * own deadline has arrived.
 */
#define MAX_WAIT	(10L*HZ)

FORWARD void nw_init ARGS(( void ));

PUBLIC void main()
{
	fd_set rd;
	struct timeval tv;
	struct timeval *tvp;
	int i, nfds, fd, r;
	time_t deadline, now, wait;
	eth_port_t *eth_port;

	nw_init();

	/* Announce, once the stack is initialised and the rendezvous FIFO exists,
	 * that clients may connect.  Scripted bring-up should wait for this line
	 * rather than sleep: ifconfig's first act is to open a channel, and if it
	 * gets there before nw_init() has finished there is nothing listening.
	 *
	 * write() rather than printf(): this file is Minix stack code and does not
	 * include <stdio.h>, so there is no stdout to flush -- and a readiness
	 * announcement still sitting in a buffer is worse than none.
	 *
	 * The connection count is on the same line because it is a property of
	 * this run and nothing else can report it: it is measured from the
	 * descriptors the daemon actually has (coh_fdc.c), so the number depends
	 * on how it was started and how many /dev/eth ports it opened, and a
	 * machine whose ceiling has collapsed says so here rather than at the
	 * moment some client cannot get a socket.  "inet: ready" is the prefix
	 * scripts wait for. */
	{
		static char rdy[]= "inet: ready, NN connections\n";
		int n;

		n= sr_ceiling();
		rdy[13]= (n >= 10) ? '0' + (n / 10) : ' ';
		rdy[14]= '0' + (n % 10);
		(void)write(1, rdy, sizeof(rdy) - 1);
	}

	while (TRUE)
	{
		/* Drain the stack's internal work first. */
		if (ev_head)
		{
			NWMARK('E');
			ev_process();
			continue;
		}
		if (clck_call_expire)
		{
			clck_expire_timers();
			continue;
		}

		/* Assemble the read set: client channels + enabled eth ports. */
		FD_ZERO(&rd);
		nfds= 0;
		sr_fill_fdset(&rd, &nfds);
		for (i= 0, eth_port= eth_port_table; i < eth_conf_nr;
			i++, eth_port++)
		{
			fd= eth_port->etp_osdep.etp_fd;
			if (fd >= 0 && (eth_port->etp_flags & EPF_ENABLED))
			{
				FD_SET(fd, &rd);
				if (fd >= nfds)
					nfds= fd + 1;
			}
		}

		/* Wake at the next timer deadline. */
		deadline= clck_next_deadline();
		if (deadline == 0)
			tvp= (struct timeval *)0;	/* idle: block */
		else
		{
			now= get_time();
			if (deadline > now)
			{
				wait= deadline - now;
				if (wait > MAX_WAIT)
					wait= MAX_WAIT;
				tv.tv_sec= wait / HZ;
				tv.tv_usec= (wait % HZ) * (1000000L / HZ);
			}
			else
			{
				tv.tv_sec= 0;
				tv.tv_usec= 0;
			}
			tvp= &tv;
		}

		r= select(nfds, &rd, (fd_set *)0, (fd_set *)0, tvp);
		reset_time();
		if (r < 0)
			continue;			/* interrupted */
		if (r == 0)
		{
			clck_call_expire= 1;		/* timeout: fire timers */
			continue;
		}
		sr_handle_fds(&rd);
		for (i= 0, eth_port= eth_port_table; i < eth_conf_nr;
			i++, eth_port++)
		{
			fd= eth_port->etp_osdep.etp_fd;
			if (fd >= 0 && FD_ISSET(fd, &rd))
				eth_recv(eth_port);
		}
	}
	ip_panic(("task is not allowed to terminate"));
}

/*
 * Startup trace, OFF unless -DNWTRACE.  Each step writes one letter to standard
 * output before it runs, so a hang or a silent exit inside nw_init() is located
 * by the last letter seen rather than by bisecting the source.  Left on, a
 * daemon that came up correctly prefixes every boot log with twenty-one
 * characters that spell nothing to a reader who is not holding this file, and
 * the letters arrive interleaved with whatever else rc.net is starting.
 *
 * write() straight to fd 1: this file has no <stdio.h> (it is Minix stack code),
 * and a buffered trace is no trace at all when the thing being traced never
 * returns.
 *
 * NWTRACE also turns on NWMARK (inet.h): one letter per event, straight to the
 * console.
 *
 * Only for a run under the instruction-level emulator, where the console is the
 * only serial line and nothing is reading it back.  On the simulator this is the
 * instrument that destroys its own evidence -- slip's line and the console are
 * two channels of the same chip -- so it is compile-time, not a runtime flag.
 *
 *	E  the daemon drained its event queue
 *	L  ip delivered a packet to itself (the loopback path)
 *	W  tcp_port_write ran
 *	S  a deferred tcp send event was dispatched
 */
#ifndef	NWTRACE
#define	nw_step(c)	/* compiled out; see above */
#else
PUBLIC void nw_mark(c)
int c;
{
	char b;

	b= c;
	(void)write(1, &b, 1);
}

PRIVATE void nw_step(c)
int c;			/* int, NOT char -- see below */
{
	char b;

	/*
	 * The parameter is int and the byte is copied to a local before its
	 * address is taken.  Declared `char c' the trace printed seventeen NULs
	 * instead of seventeen letters: K&R promotes a char argument to int at the
	 * call, so the slot holds a 16-bit word, and on this big-endian machine
	 * &c then pointed at the word's HIGH byte -- the zero half.  This is the
	 * port's most common bug class wearing a different hat, and it silently
	 * destroyed the very diagnostic meant to find another bug.
	 */
	b= c;
	(void)write(1, &b, 1);
}
#endif	/* NWTRACE */

PRIVATE void nw_init()
{
	/* Read configuration and let each layer prepare. */
	nw_step('c'); read_conf();
	nw_step('e'); eth_prep();
	nw_step('a'); arp_prep();
	nw_step('p'); psip_prep();
	nw_step('i'); ip_prep();
	nw_step('t'); tcp_prep();
	nw_step('u'); udp_prep();

	this_proc= getpid();

	/* Initialise, bottom to top. */
	nw_step('M'); mq_init();
	nw_step('B'); bf_init();
	nw_step('C'); clck_init();
	nw_step('S'); sr_init();
	nw_step('E'); eth_init();
	nw_step('A'); arp_init();
	nw_step('P'); psip_init();
	nw_step('I'); ip_init();
	nw_step('T'); tcp_init();
	nw_step('U'); udp_init();
	nw_step('\n');
}

PUBLIC void panic(file, line)
char *file;
int line;
{
	printf("inet panic at %s, %d\n", file, line);
	abort();
}

#if !NDEBUG
PUBLIC void bad_assertion(file, line, what)
char *file;
int line;
char *what;
{
	printf("inet: assertion \"%s\" failed at %s, %d\n", what, file, line);
	panic(file, line);
}

PUBLIC void bad_compare(file, line, lhs, what, rhs)
char *file;
int line;
int lhs;
char *what;
int rhs;
{
	printf("inet: compare (%d) %s (%d) failed at %s, %d\n",
		lhs, what, rhs, file, line);
	panic(file, line);
}
#endif /* !NDEBUG */
