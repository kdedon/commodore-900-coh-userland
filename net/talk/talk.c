/* talk.c Copyright Michael Temari 08/01/1996 All Rights Reserved */

#include <sys/types.h>
#include <sys/stat.h>
#include <ansi.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <utmp.h>
#include <termio.h>
#include <net/gen/netdb.h>
#include <net/hton.h>
#include <net/gen/socket.h>
#include <net/gen/in.h>
#include <net/gen/inet.h>
#include <net/gen/tcp.h>
#include <net/gen/udp.h>

#include <errno.h>
#include <poll.h>

#include "talk.h"
#include "proto.h"
#include "net.h"
#include "screen.h"

/*
 * "Messages permitted on this terminal" is OWNER EXECUTE here, not group write.
 * That is COHERENT's convention and not a choice: /bin/mesg sets and clears
 * S_IEXEC on the terminal (cmd/mesg.c) and /bin/write tests the same bit
 * (cmd/write.c:72,177), so a program that read the group-write bit instead --
 * which is what this Minix client arrived doing -- disagreed with the two
 * commands a user manages this with.  `mesg y' then did not enable a call and
 * `mesg n' did not refuse one.
 */
#define	MESGOK		S_IEXEC

extern int gethostname();
/*
 * sockheld() -- bytes already taken off this socket's reply FIFO and held for a
 * read that has not asked for them yet.  A socket here is an inet CHANNEL, and a
 * completed READ reply is taken by whichever channel operation meets it: the
 * reply to a write parks any data that arrived in front of it in the hold
 * buffer, where poll() cannot see it.  So the loop below asks this first and
 * blocks only when it answers 0 -- otherwise it would wait for a readability
 * that has been and gone.
 */
extern int sockheld();
extern char *getlogin();

_PROTOTYPE(int main, (int argc, char *argv[]));
_PROTOTYPE(void DoTalk, (void));

int main(argc, argv)
int argc;
char *argv[];
{
char *p;
struct hostent *hp;
struct stat st;

   if(argc < 2 || argc > 3) {
   	fprintf(stderr, "Usage: talk user[@host] [tty]\n");
   	return(-1);
   }

   /* get local host name */
   if(gethostname(lhost, HOST_SIZE) < 0) {
   	fprintf(stderr, "talk: Error getting local host name\n");
   	return(-1);
   }

   /* get local user name */
   if((p = getlogin()) == (char *)NULL) {
   	fprintf(stderr, "talk: You are not on a terminal\n");
   	return(-1);
   }
   strncpy(luser, p, USER_SIZE);
   luser[USER_SIZE] = '\0';

   /* get local tty */
   if((p = ttyname(0)) == (char *)NULL) {
   	fprintf(stderr, "talk: You are not on a terminal\n");
   	return(-1);
   }
   strncpy(ltty, p+5, TTY_SIZE);
   ltty[TTY_SIZE] = '\0';

   /* check if local tty is going to be writable */
   if(stat(p, &st) < 0) {
   	perror("talk: Could not stat local tty");
   	return(-1);
   }
   if((st.st_mode & MESGOK) == 0) {
   	fprintf(stderr, "talk: Your terminal is not writable.  Use: mesg y\n");
   	return(-1);
   }

   /* get remote user and host name */
   if((p = strchr(argv[1], '@')) != (char *)NULL)
   	*p++ = '\0';
   else
   	p = lhost;
   strncpy(ruser, argv[1], USER_SIZE);
   ruser[USER_SIZE] = '\0';
   strncpy(rhost, p, HOST_SIZE);
   rhost[HOST_SIZE] = '\0';

   /* get remote tty */
   if(argc > 2)
   	strncpy(rtty, argv[2], TTY_SIZE);
   else
   	rtty[0] = '\0';
   rtty[TTY_SIZE] = '\0';

   if((hp = gethostbyname(rhost)) == (struct hostent *)NULL) {
   	fprintf(stderr, "talk: Could not determine address of %s\n", rhost);
   	return(-1);
   }
   memcpy((char *)&raddr, (char *)hp->h_addr, hp->h_length);

   if(NetInit()) {
   	fprintf(stderr, "talk: Error in NetInit\n");
   	return(-1);
   }

   if(ScreenInit())
   	return(-1);

   if(!TalkInit())
	DoTalk();

   ScreenEnd();

   return(0);
}

/*
 * One process, multiplexing the terminal and the connection.
 *
 * The donor client forks: a parent writing the connection and a child reading
 * the SAME descriptor, with a pipe carrying screen updates back.  That cannot
 * work here, because a descriptor on this stack is not a socket but an inet
 * CHANNEL -- two FIFOs plus per-process state in struct ichan (the hold buffer,
 * its offset and length, and whether a request is posted), demultiplexed by
 * inet_chan.c's ichan_take() reading the reply FIFO.  Two processes on one
 * channel race that FIFO: the parent takes the CHILD's read reply and parks the
 * payload in the PARENT's hold buffer, where nothing will ever ask for it, and
 * the child takes the parent's write reply and discards it.  There is no
 * cross-process routing and there cannot be one -- fork() copied the buffer, so
 * the payload is in the wrong address space, not merely on the wrong queue.
 * Measured: B's window advanced three columns for three typed characters while
 * ONE byte left for the wire.
 *
 * poll(2) serves both halves (it works on sockets and on terminals), so the two
 * sources can be waited on together in the process that owns the channel, and
 * the screen is written by the only process that has a curses image of it.  This
 * is the shape cmd/rlogin/rlogin.c session() already uses, for the same reason.
 * select() is not an option: it clamps a long timeout to 32 s and reports the
 * clamp as a timeout.
 */
void DoTalk()
{
int s;
int held;
struct pollfd pfd[2];
struct termio tio;
char lcc[3];
char rcc[3];
char buffer[64];

   ScreenMsg("");
   ScreenWho(ruser, rhost);

   /* Get and send edit characters */
   s = ioctl(0, TCGETA, &tio);
   if(s < 0) {
   	perror("talk: ioctl TCGETA");
   	return;
   }
   lcc[0] = tio.c_cc[VERASE];
   lcc[1] = tio.c_cc[VKILL];
   lcc[2] = 0x17; /* Control - W */
   s = write(tcp_fd, lcc, sizeof(lcc));
   if(s != sizeof(lcc)) {
   	ScreenMsg("Connection Closing due to error");
   	return;
   }
   s = read(tcp_fd, rcc, sizeof(rcc));
   if(s != sizeof(rcc)) {
   	ScreenMsg("Connection Closing due to error");
   	return;
   }
   ScreenEdit(lcc, rcc);

   while(!ScreenDone) {
   	pfd[0].fd = tcp_fd;
   	pfd[0].events = POLLIN;
   	pfd[0].revents = 0;
   	pfd[1].fd = 0;
   	pfd[1].events = POLLIN;
   	pfd[1].revents = 0;

   	/* Held bytes are invisible to poll(): ask for them before waiting. */
   	held = sockheld(tcp_fd);

   	/* (unsigned long) is required: poll(2)'s count is a long in this ABI
   	 * (kernel upoll(), syscall table entry 67) and there is no prototype
   	 * to widen a 16-bit int argument for it. */
   	if(held == 0 && poll(pfd, (unsigned long)2, INFTIM) < 0) {
   		if(errno == EINTR)
   			continue;
   		break;
   	}

   	/* The connection first: what the other end said is on the screen
   	 * before this end's next keystroke moves the cursor. */
   	if(held != 0 || pfd[0].revents != 0) {
   		s = read(tcp_fd, buffer, sizeof(buffer));
   		if(s < 0 && errno == EINTR)
   			continue;
   		if(s <= 0)
   			break;
   		ScreenPut(buffer, s, REMOTEWIN);
   	}

   	if(pfd[1].revents != 0) {
   		s = read(0, buffer, sizeof(buffer));
   		if(s < 0 && errno == EINTR)
   			continue;
   		if(s <= 0)
   			break;
   		ScreenPut(buffer, s, LOCALWIN);
   		if(write(tcp_fd, buffer, s) != s)
   			break;
   	}
   }

   close(tcp_fd);
   return;
}
