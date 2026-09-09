/* talkserv.c -- from talkd, Copyright Michael Temari 07/22/1996, All Rights
 * Reserved.  The invitation table, the terminal announcement and the utmp
 * lookup are his; the socket, the idle rule and the reporting are this port's.
 */

/*
 * ntalk (518/udp) -- the rendezvous service /bin/talk calls at both ends.
 *
 * WHAT IT DOES.  It does not carry conversation.  A client asks it four things
 * -- leave my invitation, look one up, announce a call, delete an invitation --
 * and the two clients then open a TCP connection to each other directly.  This
 * service's whole job is to hold a table of invitations, to find out whether the
 * called user is logged in, and to write the "would like to talk to you" lines
 * on that user's terminal.  /bin/talk cannot complete a call in either direction
 * without an ntalk service on BOTH machines.
 *
 * WHY IT IS PART OF THE SWITCHBOARD, AND WHERE ITS STATE LIVES.
 *
 * It is an INTERNAL service: inetd binds 518, and on the first datagram forks a
 * child that runs talkserv() on that socket -- fork WITHOUT exec, so the socket
 * arrives with libsocket's framing state intact.  The service is therefore
 * inetd's text, shared with the running switchboard, instead of a 41 KB program
 * image of its own.
 *
 * THE INVITATION TABLE LIVES IN THAT CHILD, NOT IN THE PARENT, and not because
 * the parent could not hold it:
 *
 *   The socket must be owned by exactly ONE process for its whole life.  There
 *   is no accept() for a datagram, so the socket a service reads IS the
 *   listening socket; a second process driving it leaves the first process's
 *   `struct ichan' -- the armed READ, the hold, the reply-FIFO sequencing --
 *   describing a channel that has moved on.  So the socket is handed over once
 *   and for good: the parent sockdrop()s its copy, and when the child is gone
 *   the parent binds a FRESH socket rather than resuming a used one.
 *
 *   And it must not be the parent, because every request does something that can
 *   block for an unbounded time: announce() writes on ANOTHER user's terminal,
 *   which a terminal stopped with ^S never accepts; find_user() reads /etc/utmp;
 *   the caller's name comes out of /etc/hosts.  A switchboard that stalls in any
 *   of those has stopped answering every other port it holds.
 *
 * WHAT THE CHILD'S DEATH COSTS, WHICH IS NOTHING: it exits only when the table
 * is EMPTY and no datagram has arrived for TALKIDLE seconds, so the state that
 * dies with it is state the protocol has already expired -- an entry lives
 * MAX_LIFE seconds from the last request that touched it.  While a call is being
 * set up the child is resident and holds every invitation; between calls nothing
 * is resident at all, which is the point of serving it from here.
 *
 * ROOT.  Required, and not for the port: 518 is not privileged here.  It is
 * because announce() writes on another user's terminal, which needs a uid that
 * may.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#include "talk.h"
#include "netdb_priv.h"

extern int errno;

/*
 * Everything here that is wider than the int K&R assumes for an undeclared
 * function.  A pointer is wider still on this machine, so a missing declaration
 * does not fail to compile -- it truncates the value and the fault appears
 * somewhere else entirely.
 */
extern long time();
extern struct tm *localtime();

/* inetd's reporting path (net/inetd.c): syslog, and standard error as well when
 * -d was given.  A service under the switchboard has no standard error of its
 * own worth writing to. */
extern void report();
extern int opt_d;

/*
 * "Messages permitted on this terminal" is OWNER EXECUTE here, not group write.
 * /bin/mesg sets and clears S_IEXEC on the terminal (cmd/mesg.c) and /bin/write
 * tests it (cmd/write.c:72,177), so that bit is what a user's `mesg y' and
 * `mesg n' actually mean on this system.  Reading the group-write bit -- which
 * is what BSD's talkd means -- refuses a call to every terminal that said yes
 * and accepts one to every terminal that said no.  The same definition is in
 * /bin/talk, which checks its OWN terminal before ringing anybody.
 */
#define	MESGOK		S_IEXEC

#define	UTMPFILE	"/etc/utmp"

/*
 * How long the child waits for a datagram before it considers going away.  It
 * only goes if the table is empty as well, so this is not the service's memory:
 * it is how long an idle child lingers.  RING_WAIT, because that is the interval
 * at which a caller repeats a request it is still waiting on -- a shorter wait
 * would retire the child between two datagrams of the same call.
 */
#define	TALKIDLE	RING_WAIT

/*
 * One datagram.  A talk request is 84 bytes, so this is ample, and it is bigger
 * than the 512 the channel layer holds anyway (ICHAN_HOLD): a bigger buffer
 * could not be filled by one read.
 */
static char dgram[512];

struct entry {
	struct entry *prev;
	struct talk_request rq;
	time_t expire;
	struct entry *next;
};

static struct entry *entry = (struct entry *)0;

static int announce();
static struct talk_request *lookup();
/*
 * addreq() and nextid() hand back an id, and an id is 32 bits: it is the u32_t
 * the reply carries and the value a caller quotes back in a DELETE.  Declared
 * int, as upstream had it, it would be truncated to 16 on the way out of the
 * function.
 */
static u32_t addreq();
static u32_t nextid();
static int delete_invite();
static void delete();
static int find_user();
static int live();
static char *callerhost();

/* This machine's name, for the announcement.  Filled once per child. */
static char myhostname[HOST_SIZE + 1];

/*
 * Is the called user logged in, and on which terminal?
 *
 * THE UTMP HERE IS THE V7 ONE, and it is not the record Minix reads.  COHERENT's
 * <utmp.h> is three fields --
 *
 *	char ut_line[8]; char ut_name[DIRSIZ]; time_t ut_time;
 *
 * -- with no ut_type, no ut_user, no ut_pid and no ut_host, so the upstream test
 * `utmp.ut_type != USER_PROCESS' has nothing to read.  The V7 convention it
 * replaces is that a FREE slot is one whose name is empty, which is the same
 * test /bin/who makes, and it is the definition of "logged in" on this system.
 *
 * NEITHER SIDE OF THE COMPARISON IS A C STRING.  ut_name and ut_line are
 * fixed-width fields padded with NULs only when the value is shorter than the
 * field, and the request's luser/ruser/rtty are the same: talk(1) fills them
 * with strncpy() into char[12] and char[16], so a name of exactly USER_SIZE
 * characters arrives with no terminator at all.  Both are therefore copied into
 * buffers one byte longer before anything treats them as text -- which is also
 * why the tty this returns is safe for announce() to interpolate.
 */
static int find_user(name, tty)
char *name;			/* USER_SIZE bytes, may be unterminated	*/
char *tty;			/* TTY_SIZE+1 bytes, in and out		*/
{
	int fd;
	int ret;
	struct utmp utmp;
	char want[USER_SIZE + 1];
	char uname[sizeof(utmp.ut_name) + 1];
	char uline[sizeof(utmp.ut_line) + 1];

	strncpy(want, name, USER_SIZE);
	want[USER_SIZE] = '\0';

	if ((fd = open(UTMPFILE, O_RDONLY)) < 0)
	{
		report(LOG_ERR, "ntalk: %s: cannot open, errno %d", UTMPFILE,
			errno);
		return FAILED;
	}

	ret = NOT_HERE;
	while (read(fd, (char *)&utmp, sizeof(utmp)) == sizeof(utmp))
	{
		if (utmp.ut_name[0] == '\0')
			continue;			/* a free slot	*/
		strncpy(uname, utmp.ut_name, sizeof(utmp.ut_name));
		uname[sizeof(utmp.ut_name)] = '\0';
		if (strcmp(uname, want) != 0)
			continue;
		strncpy(uline, utmp.ut_line, sizeof(utmp.ut_line));
		uline[sizeof(utmp.ut_line)] = '\0';
		/* A caller may name the terminal, as `talk user tty02' does; an
		 * empty rtty means any of the user's sessions will do. */
		if (*tty && strncmp(uline, tty, TTY_SIZE) != 0)
			continue;
		strcpy(tty, uline);
		ret = SUCCESS;
		break;
	}
	close(fd);
	return ret;
}

/*
 * The caller's machine, as a name to put in front of a person.
 *
 * /etc/hosts and then the address written out, which is gethostbyaddr()'s own
 * order minus its last step -- and the step left out is the DNS one.  That is
 * deliberate: _dns_byaddr() is in the archive member that drags net/resolv in,
 * 5272 bytes of static query buffers and some 8 KB of text, into a switchboard
 * that resolves nothing else and pays for it in every process it forks.  What is
 * lost is a name for a host that only DNS knows; what a caller is told then is
 * the address, which is what `talk user@10.0.0.1' takes anyway (gethostbyname
 * reads a dotted quad without touching either database).
 *
 * MACHINE_UNKNOWN is therefore never answered, and was already unreachable:
 * gethostbyaddr() ends by spelling the address out rather than failing.
 */
static char *callerhost(addr)
u32_t addr;
{
	static char dotted[16];
	struct hostent *hp;
	unsigned long a;

	a = (unsigned long)addr;
	if ((hp = _hosts_lookup((char *)0, a)) != (struct hostent *)0)
		return hp->h_name;
	sprintf(dotted, "%d.%d.%d.%d", (int)((a >> 24) & 0xFF),
		(int)((a >> 16) & 0xFF), (int)((a >> 8) & 0xFF),
		(int)(a & 0xFF));
	return dotted;
}

/*
 * Write the invitation on the called user's terminal.
 */
static int announce(request, rhost)
struct talk_request *request;
char *rhost;
{
	char tty[5 + TTY_SIZE + 1];
	struct stat st;
	FILE *fp;
	time_t now;
	struct tm *tm;

	sprintf(tty, "/dev/%.*s", TTY_SIZE, request->rtty);

	if (stat(tty, &st) < 0)
		return PERMISSION_DENIED;
	if (!(st.st_mode & MESGOK))
		return PERMISSION_DENIED;
	if ((fp = fopen(tty, "w")) == (FILE *)0)
		return PERMISSION_DENIED;

	(void)time(&now);
	tm = localtime(&now);

	/*
	 * %.*s on every field that came off the wire: luser is 12 bytes with no
	 * terminator of its own, and CR-LF because the terminal this is written
	 * to belongs to somebody else's session and may be in raw mode.
	 */
	fprintf(fp,
		"\007\007\007\rtalkd: Message from talkd@%s at %d:%02d:%02d\r\n",
		myhostname, tm->tm_hour, tm->tm_min, tm->tm_sec);
	fprintf(fp, "talkd: %.*s@%s would like to talk to you\r\n",
		USER_SIZE, request->luser, rhost);
	fprintf(fp, "talkd: to answer type:  talk %.*s@%s\r\n",
		USER_SIZE, request->luser, rhost);
	fclose(fp);
	return SUCCESS;
}

/*
 * Find the entry a request refers to, expiring anything stale on the way past.
 *
 * `type' 0 is a LOOK_UP -- the callee asking whether anybody has called, so the
 * names are matched CROSSED (my luser against the entry's ruser) and only
 * against invitations left by a caller.  `type' 1 is the caller's own repeat of
 * a request it already made, matched on pid and both names, and it renews the
 * entry's life.
 *
 * NEXT IS TAKEN BEFORE THE ENTRY IS FREED: malloc reuses a block immediately, so
 * a pointer read out of a freed one is whatever the free list wrote there.
 */
static struct talk_request *lookup(request, type)
struct talk_request *request;
int type;
{
	time_t now;
	struct entry *e, *next;

	(void)time(&now);
	for (e = entry; e != (struct entry *)0; e = next)
	{
		next = e->next;
		if (now > e->expire)
		{
			delete(e);
			continue;
		}
		if (type == 0)
		{
			if (strncmp(request->luser, e->rq.ruser, USER_SIZE) == 0
			 && strncmp(request->ruser, e->rq.luser, USER_SIZE) == 0
			 && e->rq.type == LEAVE_INVITE)
				return &e->rq;
		}
		else
		{
			if (request->type == e->rq.type
			 && request->pid == e->rq.pid
			 && strncmp(request->luser, e->rq.luser, USER_SIZE) == 0
			 && strncmp(request->ruser, e->rq.ruser, USER_SIZE) == 0)
			{
				e->expire = now + MAX_LIFE;
				return &e->rq;
			}
		}
	}
	return (struct talk_request *)0;
}

static u32_t addreq(request)
struct talk_request *request;
{
	time_t now;
	struct entry *e;

	(void)time(&now);
	request->id = nextid();
	if ((e = (struct entry *)malloc(sizeof(struct entry)))
			== (struct entry *)0)
	{
		report(LOG_ERR, "ntalk: out of memory for an invitation");
		return htonl(0L);
	}
	e->expire = now + MAX_LIFE;
	memcpy((char *)&e->rq, (char *)request, sizeof(struct talk_request));
	e->next = entry;
	if (e->next != (struct entry *)0)
		e->next->prev = e;
	e->prev = (struct entry *)0;
	entry = e;
	return request->id;
}

static int delete_invite(id)
u32_t id;
{
	time_t now;
	struct entry *e, *next;

	(void)time(&now);
	for (e = entry; e != (struct entry *)0; e = next)
	{
		next = e->next;
		if (now > e->expire)
		{
			delete(e);
			continue;
		}
		if (e->rq.id == id)
		{
			delete(e);
			return SUCCESS;
		}
	}
	return NOT_HERE;
}

static void delete(e)
struct entry *e;
{
	if (e == (struct entry *)0)
		return;
	if (entry == e)
		entry = e->next;
	else if (e->prev != (struct entry *)0)
		e->prev->next = e->next;
	if (e->next != (struct entry *)0)
		e->next->prev = e->prev;
	free((char *)e);
}

static u32_t nextid()
{
	static long id = 0;

	id++;
	if (id <= 0)
		id = 1;
	return htonl(id);
}

/*
 * Expire what is stale and say how many invitations are left.  This is what
 * decides whether an idle child may go: an entry nobody can still ask about is
 * not state, and one that can be asked about is.
 */
static int live()
{
	time_t now;
	struct entry *e, *next;
	int n;

	(void)time(&now);
	n = 0;
	for (e = entry; e != (struct entry *)0; e = next)
	{
		next = e->next;
		if (now > e->expire)
			delete(e);
		else
			n++;
	}
	return n;
}

/*
 * One request, answered.  Fills *reply and returns nothing the caller has to
 * act on: an unserviceable request is an ANSWER (BADVERSION, FAILED,
 * UNKNOWN_REQUEST), not a reason to stop serving 518.
 */
static void processrequest(request, reply)
struct talk_request *request;
struct talk_reply *reply;
{
	char *p;
	int n;
	struct talk_request *rq;

	reply->version = TALK_VERSION;
	reply->type = request->type;
	reply->answer = 0;
	reply->junk = 0;
	reply->id = htonl(0L);
	memset((char *)&reply->addr, 0, sizeof(reply->addr));

	if (request->version != TALK_VERSION)
	{
		reply->answer = BADVERSION;
		return;
	}
	if (ntohs(request->addr.sa_family) != AF_INET)
	{
		reply->answer = BADADDR;
		return;
	}
	if (ntohs(request->ctl_addr.sa_family) != AF_INET)
	{
		reply->answer = BADCTLADDR;
		return;
	}
	/*
	 * Check the local name -- printable characters only.  Bounded by
	 * USER_SIZE rather than run to a terminator: luser is a fixed 12-byte
	 * field off the wire and a full-width name has none, so `while (*p)'
	 * would walk on into ruser and rtty and then past the struct.
	 */
	p = request->luser;
	for (n = 0; n < USER_SIZE && *p; n++, p++)
		if (!isprint(*p))
		{
			reply->answer = FAILED;
			return;
		}

	switch (request->type) {
	case ANNOUNCE:
		reply->answer = find_user(request->ruser, request->rtty);
		if (reply->answer != SUCCESS)
			break;
		if ((rq = lookup(request, 1)) == (struct talk_request *)0)
		{
			reply->id = addreq(request);
			reply->answer = announce(request,
				callerhost(request->ctl_addr.sin_addr));
			break;
		}
		if (ntohl(request->id) > ntohl(rq->id))
		{
			rq->id = nextid();
			reply->id = rq->id;
			reply->answer = announce(request,
				callerhost(request->ctl_addr.sin_addr));
		}
		else
		{
			reply->id = rq->id;
			reply->answer = SUCCESS;
		}
		break;

	case LEAVE_INVITE:
		if ((rq = lookup(request, 1)) == (struct talk_request *)0)
			reply->id = addreq(request);
		else
		{
			reply->id = rq->id;
			reply->answer = SUCCESS;
		}
		break;

	case LOOK_UP:
		if ((rq = lookup(request, 0)) == (struct talk_request *)0)
			reply->answer = NOT_HERE;
		else
		{
			reply->id = rq->id;
			memcpy((char *)&reply->addr, (char *)&rq->addr,
				sizeof(reply->addr));
			reply->answer = SUCCESS;
		}
		break;

	case DELETE:
		reply->answer = delete_invite(request->id);
		break;

	default:
		reply->answer = UNKNOWN_REQUEST;
	}
}

/*
 * The service, on the socket inetd bound and this process now owns alone.
 *
 * Returns when the child has nothing left to hold: no invitation in the table
 * and no datagram for TALKIDLE seconds.  It returns on a dead channel too --
 * a socket here is a pair of FIFOs to /etc/inet, so a zero-length read means the
 * stack has gone, and it never comes back for THIS socket.  The switchboard
 * binds a fresh one when it sees this child exit.
 *
 * A datagram that is not a talk request is DISCARDED and the service goes back
 * for the next one: 518/udp is reachable by anything on the network, and one
 * stray packet -- a port scan, a broadcast, a client of another talk version --
 * must not take the service down.  The symptom that would produce is talk(1)
 * reporting the person is not logged in.
 */
void talkserv(fd)
int fd;
{
	struct talk_request request;
	struct talk_reply reply;
	struct sockaddr_in to;
	struct pollfd set[1];
	time_t last, now;
	int n, k;

	if (gethostname(myhostname, HOST_SIZE) < 0)
		strcpy(myhostname, "localhost");
	(void)time(&last);
	if (opt_d)
		report(LOG_DEBUG, "ntalk: serving, host %s", myhostname);

	for (;;)
	{
		/*
		 * Bytes already taken off the channel by an earlier operation
		 * are not a readability event, so a loop that only poll()ed
		 * would wait for a datagram it is already holding.
		 */
		if (sockheld(fd) <= 0)
		{
			set[0].fd = fd;
			set[0].events = POLLIN;
			set[0].revents = 0;
			/* The (unsigned long) cast is REQUIRED: upoll()
			 * declares that argument unsigned long and K&R has no
			 * prototype to widen an int for it. */
			n = poll(set, (unsigned long)1, TALKIDLE * 1000);
			if (n < 0)
			{
				if (errno == EINTR)
					continue;
				report(LOG_ERR, "ntalk: poll: errno %d", errno);
				return;
			}
			if (n == 0)
			{
				/*
				 * How long the wait actually was, and what the
				 * table says, are both reported: the rule this
				 * arm applies is a time comparison against
				 * entries whose life is also a time
				 * comparison, and a wait that is not the length
				 * it asked for retires a child that should have
				 * stayed.  The numbers are what say which.
				 */
				(void)time(&now);
				k = live();
				if (opt_d)
					report(LOG_DEBUG,
						"ntalk: idle %ld s of %d, %d"
						" invitation(s) held",
						(long)(now - last), TALKIDLE, k);
				if (k == 0)
					return;
				last = now;
				continue;
			}
		}

		n = recvfrom(fd, dgram, (int)sizeof(dgram), 0,
			(struct sockaddr *)0, (int *)0);
		if (n < 0)
		{
			report(LOG_ERR, "ntalk: recvfrom: errno %d", errno);
			return;
		}
		if (n == 0)
		{
			report(LOG_ERR, "ntalk: the stack closed the socket");
			return;
		}
		if (n != sizeof(struct talk_request))
		{
			if (opt_d)
				report(LOG_DEBUG,
					"ntalk: a %d-byte datagram is not a"
					" request; discarded", n);
			continue;
		}
		memcpy((char *)&request, dgram, sizeof(request));
		(void)time(&last);

		processrequest(&request, &reply);

		/*
		 * The reply goes to the CONTROL address the request named, not
		 * to wherever the datagram came from: that is the socket the
		 * client is waiting on, and the protocol carries it for exactly
		 * this.  Both halves are already in network order on the wire.
		 */
		memset((char *)&to, 0, sizeof(to));
		to.sin_family = AF_INET;
		to.sin_port = request.ctl_addr.sin_port;
		to.sin_addr.s_addr = request.ctl_addr.sin_addr;
		if (sendto(fd, (char *)&reply, (int)sizeof(reply), 0,
				(struct sockaddr *)&to, sizeof(to)) < 0)
			report(LOG_ERR, "ntalk: sendto: errno %d", errno);
		else if (opt_d)
			report(LOG_DEBUG, "ntalk: type %d from %.*s answered %d",
				request.type, USER_SIZE, request.luser,
				reply.answer);
	}
}
