/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * talkping.c -- does the ntalk service answer, and does it REMEMBER?
 *
 *	talkping [-k] [addr [caller callee tty]]  default 10.0.0.2 root guest ""
 *
 * -k stops after step 3 and LEAVES THE INVITATION.  That is how a caller pins the
 * service down long enough to be looked at from outside: inetd's ntalk child
 * retires once it holds no invitation and has been idle, so after a full run
 * there is nothing to see.  It stops rather than merely skipping steps 5 and 6
 * because an invitation lives MAX_LIFE seconds from the last request that TOUCHED
 * it, and on a 6 MHz machine the two steps after it can take longer than that --
 * measured: the entry had expired by the time the child's next idle poll came
 * round, which is the service behaving correctly and the test asking too late.
 *
 * ntalk (518/udp) is the rendezvous half of talk(1): a client leaves an
 * invitation with the daemon on one machine and the other party's client looks it
 * up seconds later, from a different datagram and often a different host.  So the
 * service is not answerable by anything stateless, and that is the property this
 * exercises -- inetd serves it INTERNALLY, in a child forked without exec, and
 * the invitation table lives in that child (net/talkserv.c).
 *
 * SEVEN STEPS, IN AN ORDER WHERE EACH IS THE OTHERS' CONTROL:
 *
 *	1  LOOK_UP with nothing left		must answer NOT_HERE
 *	2  LEAVE_INVITE				must answer SUCCESS with an id
 *	3  LOOK_UP as the OTHER party		must answer SUCCESS with THAT id
 *	   (-k stops here, holding the invitation)
 *	4  ANNOUNCE				answer reported, not asserted
 *	5  DELETE the id			must answer SUCCESS
 *	6  LOOK_UP again			must answer NOT_HERE
 *	7  a request with version 9		must answer BADVERSION
 *
 * Step 1 is what makes step 3 mean something: a service that answered SUCCESS to
 * everything would fail step 1, and one that answered NOT_HERE to everything
 * would fail step 3.  Step 3 is the assertion no per-datagram service can pass,
 * because the id it must quote was minted while a different datagram was being
 * answered.  Step 6 says the state can also be given back.  Step 7 says the
 * bytes are being parsed rather than reflected.
 *
 * Step 4's answer is REPORTED AND NOT ASSERTED because it depends on the machine
 * rather than on the service: it is SUCCESS only if the callee is in /etc/utmp
 * and that terminal has `mesg y' (owner execute here, not group write).  What
 * the announcement WROTE is the thing worth asserting, and it is asserted where
 * it lands -- on the terminal -- by whatever drives this.
 *
 * Every answer is waited for with a TIMEOUT, so a service that is not there
 * fails in WAIT seconds instead of hanging: a test that cannot fail is worse
 * than no test.
 *
 * Every line starts with "talkping:" so a scripted run can pick it out.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "../talk.h"

extern int errno;
extern unsigned long inet_addr();

#define	NTALK_PORT	518
#define	WAIT		5000		/* ms to wait for one answer	*/

static char *ansname(a)
int a;
{
	switch (a) {
	case SUCCESS:		return "SUCCESS";
	case NOT_HERE:		return "NOT_HERE";
	case FAILED:		return "FAILED";
	case MACHINE_UNKNOWN:	return "MACHINE_UNKNOWN";
	case PERMISSION_DENIED:	return "PERMISSION_DENIED";
	case UNKNOWN_REQUEST:	return "UNKNOWN_REQUEST";
	case BADVERSION:	return "BADVERSION";
	case BADADDR:		return "BADADDR";
	case BADCTLADDR:	return "BADCTLADDR";
	}
	return "?";
}

static int sock;			/* the control socket		*/
static struct sockaddr_in serv;		/* where the service is		*/
static unsigned long myaddr;
static int myport;			/* network order		*/
static int npass, nfail;

/*
 * Send one request and collect the reply.  Returns 1 with the reply in *rp, or 0
 * if nothing came back before WAIT ran out.
 */
static int ask(rq, rp)
struct talk_request *rq;
struct talk_reply *rp;
{
	struct pollfd set[1];
	int n;

	rq->addr.sa_family = htons(AF_INET);
	rq->addr.sin_port = htons(4000);	/* the data port a real client
						 * would then listen on */
	rq->addr.sin_addr = myaddr;
	rq->ctl_addr.sa_family = htons(AF_INET);
	rq->ctl_addr.sin_port = myport;
	rq->ctl_addr.sin_addr = myaddr;

	if (sendto(sock, (char *)rq, (int)sizeof(*rq), 0,
			(struct sockaddr *)&serv, sizeof(serv)) < 0)
	{
		printf("talkping: sendto failed errno %d\n", errno);
		return 0;
	}
	set[0].fd = sock;
	set[0].events = POLLIN;
	set[0].revents = 0;
	/* The (unsigned long) cast is REQUIRED: upoll() declares that argument
	 * unsigned long and K&R has no prototype to widen an int for it. */
	if ((n = poll(set, (unsigned long)1, WAIT)) < 0)
	{
		printf("talkping: poll failed errno %d\n", errno);
		return 0;
	}
	if (n == 0)
	{
		printf("talkping: no answer in %d ms\n", WAIT);
		return 0;
	}
	if ((n = recvfrom(sock, (char *)rp, (int)sizeof(*rp), 0,
			(struct sockaddr *)0, (int *)0)) < 0)
	{
		printf("talkping: recvfrom failed errno %d\n", errno);
		return 0;
	}
	if (n != sizeof(*rp))
	{
		printf("talkping: a %d-byte answer is not a %d-byte reply\n",
			n, (int)sizeof(*rp));
		return 0;
	}
	return 1;
}

/* One step: ask, and score the answer against the one required. */
static int step(what, rq, rp, want)
char *what;
struct talk_request *rq;
struct talk_reply *rp;
int want;
{
	if (!ask(rq, rp))
	{
		printf("talkping: %s: FAIL (no reply)\n", what);
		nfail++;
		return 0;
	}
	if (want >= 0 && rp->answer != want)
	{
		printf("talkping: %s: FAIL (answered %s, wanted %s)\n", what,
			ansname(rp->answer), ansname(want));
		nfail++;
		return 0;
	}
	printf("talkping: %s: %s answer %s id %08lx\n", what,
		want >= 0 ? "PASS" : "reports", ansname(rp->answer),
		(unsigned long)rp->id);
	if (want >= 0)
		npass++;
	return 1;
}

/* Fill a request: type, who is calling whom, and on which terminal. */
static void mkreq(rq, type, luser, ruser, tty)
struct talk_request *rq;
int type;
char *luser, *ruser, *tty;
{
	memset((char *)rq, 0, sizeof(*rq));
	rq->version = TALK_VERSION;
	rq->type = (u8_t)type;
	rq->pid = 4242L;			/* a real client's own pid */
	strncpy(rq->luser, luser, USER_SIZE);
	strncpy(rq->ruser, ruser, USER_SIZE);
	strncpy(rq->rtty, tty, TTY_SIZE);
}

int main(argc, argv)
int argc;
char **argv;
{
	struct talk_request rq;
	struct talk_reply rp;
	struct sockaddr_in sin;
	char *host, *caller, *callee, *tty;
	int len, keep, a;
	u32_t id;

	keep = 0;
	a = 1;
	if (argc > 1 && strcmp(argv[1], "-k") == 0)
	{
		keep = 1;
		a = 2;
	}
	host = (argc > a) ? argv[a] : "10.0.0.2";
	caller = (argc > a + 1) ? argv[a + 1] : "root";
	callee = (argc > a + 2) ? argv[a + 2] : "guest";
	tty = (argc > a + 3) ? argv[a + 3] : "";

	if ((myaddr = inet_addr(host)) == (unsigned long)0xFFFFFFFFL)
	{
		printf("talkping: %s is not an address\n", host);
		return 1;
	}
	if (sizeof(struct talk_request) != 84 || sizeof(struct talk_reply) != 24)
	{
		printf("talkping: FAIL -- this build's request/reply are %d/%d"
			" bytes, not 84/24\n", (int)sizeof(struct talk_request),
			(int)sizeof(struct talk_reply));
		return 1;
	}

	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		printf("talkping: socket failed errno %d\n", errno);
		return 1;
	}
	/* Bound to a port the stack chooses, and then asked which: the reply
	 * comes back to the control address the request carries, so this program
	 * has to be able to name its own. */
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = 0;
	sin.sin_addr.s_addr = myaddr;
	if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		printf("talkping: bind failed errno %d\n", errno);
		return 1;
	}
	len = sizeof(sin);
	if (getsockname(sock, (struct sockaddr *)&sin, &len) < 0)
	{
		printf("talkping: getsockname failed errno %d\n", errno);
		return 1;
	}
	myport = sin.sin_port;
	memset((char *)&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_port = htons(NTALK_PORT);
	serv.sin_addr.s_addr = myaddr;
	printf("talkping: %s ntalk, control port %u, %s calling %s\n", host,
		(unsigned)ntohs(myport), caller, callee);

	/* 1: nothing has been left, so there is nothing to find. */
	mkreq(&rq, LOOK_UP, callee, caller, "");
	(void)step("lookup-before", &rq, &rp, NOT_HERE);

	/* 2: the caller leaves its invitation. */
	mkreq(&rq, LEAVE_INVITE, caller, callee, "");
	id = 0;
	if (step("leave-invite", &rq, &rp, SUCCESS))
	{
		id = rp.id;
		if (id == 0)
		{
			printf("talkping: leave-invite: FAIL (id is zero)\n");
			nfail++;
			npass--;
		}
	}

	/* 3: THE ONE A STATELESS SERVICE CANNOT PASS.  A different datagram, the
	 * other party's names, and the answer must carry the id minted above. */
	mkreq(&rq, LOOK_UP, callee, caller, "");
	if (step("lookup-after", &rq, &rp, SUCCESS))
	{
		if (rp.id != id)
		{
			printf("talkping: lookup-after: FAIL (id %08lx, the"
				" invitation was %08lx)\n",
				(unsigned long)rp.id, (unsigned long)id);
			nfail++;
			npass--;
		}
		else if (rp.addr.sin_port != htons(4000))
			printf("talkping: lookup-after: the data port came back"
				" as %u\n", (unsigned)ntohs(rp.addr.sin_port));
	}

	if (keep)
	{
		printf("talkping: keeping the invitation, as -k asks\n");
		printf("talkping: %d of %d %s\n", npass, npass + nfail,
			nfail == 0 ? "PASS" : "PASS, and it FAILED");
		(void)soclose(sock);
		return nfail == 0 ? 0 : 1;
	}

	/* 4: the announcement.  Its answer depends on utmp and on mesg, so it is
	 * reported; what it WROTE is asserted on the terminal by the caller. */
	mkreq(&rq, ANNOUNCE, caller, callee, tty);
	(void)step("announce", &rq, &rp, -1);

	/* 5 and 6: the invitation is given back, and then it is gone. */
	mkreq(&rq, DELETE, caller, callee, "");
	rq.id = id;
	(void)step("delete", &rq, &rp, SUCCESS);
	mkreq(&rq, LOOK_UP, callee, caller, "");
	(void)step("lookup-deleted", &rq, &rp, NOT_HERE);

	/* 7: the bytes are parsed, not reflected. */
	mkreq(&rq, LOOK_UP, callee, caller, "");
	rq.version = 9;
	(void)step("bad-version", &rq, &rp, BADVERSION);

	printf("talkping: %d of %d %s\n", npass, npass + nfail,
		nfail == 0 ? "PASS" : "PASS, and it FAILED");
	(void)soclose(sock);
	return nfail == 0 ? 0 : 1;
}
