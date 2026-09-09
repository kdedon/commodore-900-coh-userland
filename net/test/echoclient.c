/*
 * echoclient.c -- minimal libsocket exerciser: TCP connect, send, recv.
 * Links against libsocket-z8001.a to prove the whole client path builds.
 *	echoclient 10.0.0.1 7
 *
 * Exits 0 only if the bytes come back INTACT, so a server echoing the wrong
 * bytes, a truncated reply and a correct round trip are told apart from a
 * script.
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>

extern int errno;

#define MSG	"hello\n"
/* sizeof, not strlen(): no call, so no chance of a K&R width slip. */
#define MSGLEN	((int)sizeof(MSG) - 1)

int main(argc, argv)
int argc;
char **argv;
{
	int s, n;
	struct sockaddr_in sin;
	char buf[256];

	if (argc != 3)
	{
		fprintf(stderr, "usage: echoclient host port\n");
		return 1;
	}

	s= socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
	{
		fprintf(stderr, "socket failed\n");
		return 1;
	}

	sin.sin_family= AF_INET;
	sin.sin_port= htons(atoi(argv[2]));
	sin.sin_addr.s_addr= inet_addr(argv[1]);

	if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		fprintf(stderr, "connect failed\n");
		close(s);
		return 1;
	}

	printf("echoclient: connected\n");
	fflush(stdout);

	/* Plain read/write/close on a socket handle -- libsocket's overrides
	 * route these to the connection (real fds still hit the kernel).
	 *
	 * Both results are reported.  Ignoring write()'s return made a failed
	 * send indistinguishable from a successful one whose reply never came:
	 * the connection reached ESTABLISHED on the wire, no data segment ever
	 * followed, and the program simply closed -- which said nothing about
	 * which of the two had gone wrong. */
	n= write(s, MSG, MSGLEN);
	printf("echoclient: write returned %d (errno %d)\n", n, errno);
	fflush(stdout);
	if (n != MSGLEN)
	{
		printf("echoclient: FAIL -- wrote %d of %d\n", n, MSGLEN);
		close(s);
		return 1;
	}

	n= read(s, buf, sizeof(buf) - 1);
	printf("echoclient: read returned %d (errno %d)\n", n, errno);
	if (n > 0)
	{
		buf[n]= '\0';
		printf("echo: %s", buf);
	}
	fflush(stdout);
	close(s);

	/*
	 * An ECHO client, so the bytes are the point: what comes back must be
	 * what went out, whole.  Printing them and exiting 0 regardless made a
	 * server that answered with the wrong bytes -- or with a truncated
	 * copy of the right ones -- indistinguishable from one that worked, and
	 * left the only failures this program could report to be the ones that
	 * stopped it before it sent anything at all.
	 */
	if (n != MSGLEN || strcmp(buf, MSG) != 0)
	{
		printf("echoclient: FAIL -- sent [%s] (%d bytes), got back"
			" [%s] (%d bytes)\n", MSG, MSGLEN, n > 0 ? buf : "",
			n);
		return 1;
	}
	printf("echoclient: PASS -- %d bytes echoed intact\n", n);
	return 0;
}
