/*
 * TNET		A server program for MINIX which implements the TCP/IP
 *		suite of networking protocols.  It is based on the
 *		TCP/IP code written by Phil Karn et al, as found in
 *		his NET package for Packet Radio communications.
 *
 *		Handle the TERMINAL module.
 *
 * Author:	Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 *              Michael Temari, <temari@temari.ae.ge.com>
 *
 * 07/29/92 MT  Telnet options hack which seems to work okay
 * 01/12/93 MT  Better telnet options processing instead of hack
 */
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <poll.h>
#include <net/netlib.h>
#include "telnet.h"
#include "telnetd.h"

static char buff[1024];

/*
 * Announce the options that make the connection an 8-bit clean character
 * stream with this end doing the echoing.
 */
void term_init(netfd)
int netfd;
{
  tel_init();

  telopt(netfd, WILL, TELOPT_SGA);
  telopt(netfd, DO,   TELOPT_SGA);
  telopt(netfd, WILL, TELOPT_BINARY);
  telopt(netfd, DO,   TELOPT_BINARY);
  telopt(netfd, WILL, TELOPT_ECHO);
}

/*
 * Shuttle between the connection and the pty master until either end reaches
 * end of data.
 *
 * ONE process drives both directions, waiting on the two descriptors together.
 * The connection is not a socket the kernel knows about: it is a request FIFO
 * and a reply FIFO to the inet daemon, with the state that frames them held in
 * this program (net/inet_chan.c).  Two processes sharing that channel each read
 * the one reply FIFO, so a reply is taken by whichever of them the kernel
 * happens to wake -- and the process that was waiting for it then waits for
 * ever.
 *
 * THE CONNECTION IS TWO DESCRIPTORS.  Standalone they are the same channel; in
 * inetd mode `net_in' is the pipe carrying the peer's bytes down and `net_out'
 * the pipe carrying this end's back up, and `net_is_sock' is 0 -- which is what
 * suppresses the sockheld() call, since sockheld() answers about a libsocket
 * channel and a pipe holds nothing back that poll() cannot see.
 */
void term_inout(net_in, net_out, net_is_sock, pty_fd)
int net_in;
int net_out;
int net_is_sock;
int pty_fd;
{
register int i;
struct pollfd pfd[2];
int held;

  for (;;) {
	pfd[0].fd = net_in;
	pfd[0].events = POLLIN;
	pfd[0].revents = 0;
	pfd[1].fd = pty_fd;
	pfd[1].events = POLLIN;
	pfd[1].revents = 0;

	/* Data already off the reply FIFO is invisible to poll(), so a wait
	 * would be for an event that has been and gone. */
	held = net_is_sock ? sockheld(net_in) : 0;
	/* (unsigned long) is required: poll(2)'s count is a long in this
	 * ABI (kernel upoll(), syscall table entry 67) and there is no
	 * prototype to widen it. */
	if (held == 0 && poll(pfd, (unsigned long)2, INFTIM) < 0) {
		if (errno == EINTR)
			continue;
		break;
	}

	if (held != 0 || pfd[0].revents != 0) {
		/* network -> login process */
		if ((i = read(net_in, buff, sizeof(buff))) <= 0)
			break;
		tel_in(pty_fd, net_out, buff, i);
	}

	if (pfd[1].revents != 0) {
		/* login process -> network */
		if ((i = read(pty_fd, buff, sizeof(buff))) <= 0)
			break;
		tel_out(net_out, buff, i);
	}
  }
}
