/*
 * dropclient.c -- open connections and lose them the way a crashed client
 * does: no close(2), no protocol shutdown, the descriptors torn down by
 * process exit alone.
 *
 *	dropclient [-w text] [-r] host hold port [port ...]
 *
 * `hold' is how many seconds the connections are kept before they are dropped,
 * so a `ps' taken while this program is in the background sees every service
 * that was started for them.  Then _exit(2) with every socket still open: the
 * kernel closes them, this program tells the stack nothing, and what the far
 * end sees is a connection that went away rather than one that was ended.
 * That is the case a service has to survive being on the wrong side of --
 * inetd's relay closes the program's standard input, and a program that does
 * not act on end of file there stays for ever.
 *
 * -w writes a line on each connection once it is open, for a service that
 * answers nothing until it is asked (echo).  -r then reads one buffer back and
 * prints what came, which is what says the service was alive and talking at the
 * moment the connection was dropped: without a reply, "nothing was left behind"
 * is also what a refused connection produces.  -r WAITS: ask it only of a
 * service that speaks first or has been given what it is waiting for, since a
 * daemon reading a query it will never get holds this read for ever.
 *
 *
 * Every port is reported by number, connected or not, and the last line counts
 * them -- a batch is only as good as the number of connections it really made.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>

extern int errno;

#define MAXCONN		16
#define BUFLEN		256

static int oneround();

int main(argc, argv)
int argc;
char **argv;
{
	char *host, *text;
	int hold, want_read, ai, first;

	text = (char *)0;
	want_read = 0;
	ai = 1;
	while (ai < argc && argv[ai][0] == '-' && argv[ai][1] != '\0')
	{
		if (argv[ai][1] == 'w' && ai + 1 < argc)
			text = argv[++ai];
		else if (argv[ai][1] == 'r')
			want_read = 1;
		else
			break;
		ai++;
	}
	if (argc - ai < 3)
	{
		fprintf(stderr,
		"usage: dropclient [-w text] [-r] host hold port ...\n");
		return 1;
	}
	host = argv[ai++];
	hold = atoi(argv[ai++]);
	first = ai;

	/*
	 * This process is the one that holds the connections, so that
	 * `dropclient ... &' in the background is what a `ps' finds holding
	 * them, and the drop is this process ending.
	 */
	return oneround(host, hold, text, want_read, argv, first, argc);
}

/*
 * Open every port named, hold them, and then leave them: _exit(2) with the
 * sockets still open, so the kernel is what closes them and this program tells
 * the stack nothing.  Never returns.
 */
static int oneround(host, hold, text, want_read, argv, first, argc)
char *host;
int hold;
char *text;
int want_read;
char **argv;
int first;
int argc;
{
	int fds[MAXCONN];
	char buf[BUFLEN];
	struct sockaddr_in sin;
	int nopen, s, n, i, ai;

	nopen = 0;
	for (ai = first; ai < argc; ai++)
	{
		if (nopen >= MAXCONN)
		{
			printf("dropclient: %d connections is all this holds\n",
				MAXCONN);
			break;
		}
		if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		{
			printf("dropclient: port %s: no socket (errno %d)\n",
				argv[ai], errno);
			continue;
		}
		sin.sin_family = AF_INET;
		sin.sin_port = htons(atoi(argv[ai]));
		sin.sin_addr.s_addr = inet_addr(host);
		if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		{
			printf("dropclient: port %s: connect failed (errno %d)\n",
				argv[ai], errno);
			close(s);
			continue;
		}
		printf("dropclient: port %s: connected\n", argv[ai]);
		fflush(stdout);
		if (text != (char *)0)
		{
			n = strlen(text);
			for (i = 0; i < n && i < BUFLEN - 2; i++)
				buf[i] = text[i];
			buf[i++] = '\r';
			buf[i++] = '\n';
			(void)write(s, buf, i);
		}
		if (want_read)
		{
			n = read(s, buf, sizeof(buf) - 1);
			if (n > 0)
			{
				buf[n] = '\0';
				for (i = 0; i < n; i++)
					if (buf[i] < ' ' && buf[i] != '\t')
						buf[i] = ' ';
				printf("dropclient: port %s: answered %d [%s]\n",
					argv[ai], n, buf);
			}
			else
				printf("dropclient: port %s: answered %d\n",
					argv[ai], n);
			fflush(stdout);
		}
		fds[nopen++] = s;
	}

	printf("dropclient: holding %d connection(s) for %d s\n", nopen, hold);
	fflush(stdout);
	if (hold > 0)
		sleep((unsigned)hold);
	/*
	 * fflush BEFORE _exit: _exit(2) runs no atexit and flushes no stdio, and
	 * that is the whole point of using it -- the sockets go with the process
	 * and nothing in this program says goodbye on any of them.
	 */
	printf("dropclient: dropping %d connection(s) uncontrolled\n", nopen);
	fflush(stdout);
	_exit(0);
	/*NOTREACHED*/
	return 0;
}
