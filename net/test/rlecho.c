/*
 * rlecho.c -- the far end of an rlogin(1) session, without a login on it.
 *
 *	rlecho [port [seconds]]		default 513, the login service; the
 *					second argument is the deadline below
 *
 * There is no rlogind on this machine, so the client had nothing to talk to and
 * nothing that could be said about it.  This is the smallest thing that IS the
 * far end as far as the client is concerned: it answers the rcmd(3) handshake
 * -- the three NUL-terminated strings and the single zero byte that says they
 * were accepted -- and from then on echoes the session back, which is what a
 * shell on a real rlogind would appear to do to anyone typing at it.
 *
 * What a run of it proves, with `rlogin c900' on the other side of the same
 * machine (the stack loops a connection to its own address back internally, so
 * no wire, no slip and no peer are involved): that the client carries BOTH
 * directions -- keyboard to connection and connection to screen -- for a whole
 * session.  That is the thing worth proving, because a connection here is a
 * request FIFO and a reply FIFO framed by state inside the client, so the
 * classic one-process-per-direction shape breaks on it: two processes read the
 * one reply FIFO and a reply goes to whichever of them the kernel wakes.  The
 * symptom of that bug is a session that carries exactly one exchange and then
 * goes silent for ever, so a test has to send SEVERAL, spaced out, and get all
 * of them back.
 *
 * Every line it prints starts with "rlecho:" so a scripted run can pick it out.
 *
 * WHAT THIS CAN AND CANNOT CHECK.  It is the far END of somebody else's
 * session, so it cannot say whether the client got its own keystrokes back --
 * only the client knows what it typed.  What it CAN check, and now does, is
 * everything it sees itself: that the handshake arrives with the shape rcmd(3)
 * gives it (four strings, the first empty, the user names non-empty), that
 * every echo goes out whole, and that the session carried the SEVERAL exchanges
 * the header above says a one-exchange-then-silence bug is what this exists to
 * catch.  A run that carries one exchange and stops is now a failure rather
 * than a line of output nothing reads.
 *
 * MINEXCH is that threshold.  It is the one number here that has to be agreed
 * with whatever is driving the session from the other side.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>

extern int errno;
extern unsigned long inet_addr();

/*
 * A DEADLINE.  Every read below blocks with nothing to interrupt it, so a peer
 * that connects and then says less than the rcmd(3) handshake expects leaves
 * this process waiting for ever -- and a test that hangs has no verdict at all,
 * which is worse than one that fails.  60 seconds is far longer than any
 * exchange here takes and far shorter than a person's patience.  The alarm is
 * re-armed before each blocking wait, so a long healthy session is not cut off.
 *
 * A CLIENT THAT SENDS THREE HANDSHAKE STRINGS INSTEAD OF FOUR IS EXACTLY THIS
 * CASE: getstr() waits for a fourth that never comes while the client waits for
 * the go-ahead byte, and the two of them wait on each other for ever.
 */
#define DEADLINE	60

static void expired()
{
	/* Not printf: this runs from a signal, and the verdict has to get out
	 * even if stdio was in the middle of something. */
	write(2, "rlecho: FAIL -- timed out with nothing more arriving\n", 53);
	_exit(1);
}

/* How many separate reads a session must carry before it counts.  One is what
 * the bug in the header comment produced, so one is not enough. */
#define MINEXCH	2

/*
 * Read one NUL-terminated string off the connection, a byte at a time.  Byte at
 * a time because the three of them arrive in the same stream as the session
 * data that follows, and reading further than the last NUL would swallow the
 * first thing typed.
 */
static int getstr(fd, buf, size)
int fd;
char *buf;
int size;
{
	int n, i;
	char c;

	for (i = 0; i < size - 1; i++)
	{
		if ((n = read(fd, &c, 1)) <= 0)
			return -1;
		if (c == '\0')
			break;
		buf[i] = c;
	}
	buf[i] = '\0';
	return i;
}

int main(argc, argv)
int argc;
char **argv;
{
	int s, c, n, port, total, fails, exch, dl;
	struct sockaddr_in sin;
	char errport[16], locuser[64], remuser[64], term[128];
	char buf[256];
	char zero = '\0';

	port = argc > 1 ? atoi(argv[1]) : 513;
	dl = argc > 2 ? atoi(argv[2]) : DEADLINE;
	fails = 0;
	exch = 0;

	if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("rlecho: socket errno %d\n", errno);
		return 1;
	}
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = 0;		/* any local address */
	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		printf("rlecho: bind errno %d\n", errno);
		return 1;
	}
	if (listen(s, 1) < 0)
	{
		printf("rlecho: listen errno %d\n", errno);
		return 1;
	}
	printf("rlecho: listening on port %d\n", port);
	fflush(stdout);
	signal(SIGALRM, expired);
	alarm(dl);

	if ((c = accept(s, (struct sockaddr *)0, (int *)0)) < 0)
	{
		printf("rlecho: accept errno %d\n", errno);
		return 1;
	}

	/* FOUR strings, and the first is empty.  A client that wants a separate
	 * error connection names its port here; rlogin does not, and rcmd(3)
	 * then sends the empty string rather than nothing at all, so a reader
	 * that expects only three takes the terminal name for the session's
	 * first characters. */
	if (getstr(c, errport, sizeof(errport)) < 0 ||
	    getstr(c, locuser, sizeof(locuser)) < 0 ||
	    getstr(c, remuser, sizeof(remuser)) < 0 ||
	    getstr(c, term, sizeof(term)) < 0)
	{
		printf("rlecho: handshake truncated\n");
		return 1;
	}
	printf("rlecho: errport [%s] locuser [%s] remuser [%s] term [%s]\n",
		errport, locuser, remuser, term);
	fflush(stdout);

	/*
	 * The SHAPE of the handshake, which is the part this end can judge.
	 * rcmd(3) sends an empty error-port string, then two user names, then
	 * the terminal.  A reader that expected three strings instead of four
	 * took the terminal name for the session's first characters and every
	 * field after the first was one place out -- which prints plausibly and
	 * is completely wrong, so the fields are checked rather than displayed.
	 */
	if (errport[0] != '\0')
	{
		printf("rlecho: FAIL -- error port [%s], wanted the empty"
			" string rcmd(3) sends\n", errport);
		fails++;
	}
	if (locuser[0] == '\0' || remuser[0] == '\0')
	{
		printf("rlecho: FAIL -- empty user name: local [%s] remote"
			" [%s]\n", locuser, remuser);
		fails++;
	}
	if (term[0] == '\0')
	{
		printf("rlecho: FAIL -- no terminal type\n");
		fails++;
	}

	/* The zero byte is the client's go-ahead: anything else is taken for the
	 * first line of a refusal and printed instead of the session. */
	if (write(c, &zero, 1) != 1)
	{
		printf("rlecho: cannot answer the handshake\n");
		return 1;
	}
	printf("rlecho: session open\n");
	fflush(stdout);

	total = 0;
	alarm(dl);
	while ((n = read(c, buf, sizeof(buf) - 1)) > 0)
	{
		alarm(dl);
		total += n;
		exch++;
		buf[n] = '\0';
		printf("rlecho: got %d [%s]\n", n, buf);
		fflush(stdout);
		if (write(c, buf, n) != n)
		{
			printf("rlecho: FAIL -- echo of %d bytes short or"
				" refused, errno %d\n", n, errno);
			fails++;
			break;
		}
	}
	alarm(0);
	if (n < 0)
	{
		printf("rlecho: FAIL -- read errno %d\n", errno);
		fails++;
	}
	/*
	 * SEVERAL exchanges, spaced out.  A channel two processes read from
	 * carries the first reply to whichever of them the kernel wakes and
	 * then goes silent for ever, and a session that carried exactly one
	 * exchange is what that looks like from here.
	 */
	if (exch < MINEXCH)
	{
		printf("rlecho: FAIL -- the session carried %d exchange(s),"
			" wanted at least %d\n", exch, MINEXCH);
		fails++;
	}
	printf("rlecho: done, %d bytes in %d exchanges -- %s\n", total, exch,
		fails ? "FAIL" : "PASS");
	close(c);
	close(s);
	return fails ? 1 : 0;
}
