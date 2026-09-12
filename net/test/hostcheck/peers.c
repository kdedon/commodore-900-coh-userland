/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * peers.c -- the other end, for the two programs in net/test that ARE the
 * other end: rlecho (the far side of an rlogin session) and udpserver (the
 * guest side of a datagram round trip).  Neither can be run at all without
 * something to talk to, and neither can be shown to FAIL without something that
 * talks to it BADLY.
 *
 *	peers rl    <port> <exchanges> <mode>	drive rlecho
 *	peers udp   <port> <count>     <mode>	drive udpserver
 *	peers serve <port> <rounds>    <mode>	be the service repeatclient calls
 *
 * rl modes:
 *	ok		four handshake strings as rcmd(3) sends them (the first
 *			empty), then <exchanges> lines, each read back.
 *	three		only THREE strings.  rlecho must notice: a reader that
 *			expects three takes the terminal name for the session's
 *			first characters and every field is one place out.
 *	nouser		four strings, but the user names are empty.
 *	once		a correct handshake and exactly ONE exchange, then close.
 *			That is what the two-processes-on-one-reply-FIFO bug
 *			produces, and what a test that counted nothing missed.
 *
 * serve modes -- the other end for repeatclient, which asks whether a service
 * that answered the FIRST caller is still there for the tenth.  Nothing in
 * net/test is that service, so it is here:
 *	ok		accept, echo the line back, close, <rounds> times over.
 *	once		serve exactly ONE caller and then close the listening
 *			socket, which is what a daemon that has lost its
 *			listener looks like from outside: every later round is
 *			refused.
 *
 * udp modes:
 *	ok		<count> datagrams, each echo compared with what was sent.
 *	few		sends one fewer datagram than the server was told to
 *			expect, so the server is left waiting: proves the
 *			server's own count is checked.
 *
 * Exits 0 if THIS end saw what it expected.  The program under test has its own
 * verdict; run.sh reads both.
 *
 * Host-only scaffolding.  Nothing here is compiled for the C900.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int tcpconnect(int port)
{
	struct sockaddr_in sin;
	int s, i;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = htonl(0x7F000001);
	for (i = 0; i < 100; i++)
	{
		if ((s = socket(AF_INET, SOCK_STREAM, 0)) < 0)
			return -1;
		if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) == 0)
			return s;
		close(s);
		usleep(100000);
	}
	return -1;
}

static int putstr(int s, char *p)
{
	return write(s, p, strlen(p) + 1) == (int)strlen(p) + 1 ? 0 : -1;
}

static int rl(int port, int exch, char *m)
{
	char buf[256];
	int s, i, n, want;
	char zero;

	if ((s = tcpconnect(port)) < 0)
	{
		fprintf(stderr, "peers: rl: cannot connect to %d\n", port);
		return 1;
	}
	if (strcmp(m, "three") == 0)
	{
		putstr(s, "");
		putstr(s, "kevin");
		putstr(s, "vt100");
	}
	else if (strcmp(m, "nouser") == 0)
	{
		putstr(s, "");
		putstr(s, "");
		putstr(s, "");
		putstr(s, "vt100");
	}
	else
	{
		putstr(s, "");
		putstr(s, "kevin");
		putstr(s, "kevin");
		putstr(s, "vt100");
	}
	if (read(s, &zero, 1) != 1 || zero != '\0')
	{
		fprintf(stderr, "peers: rl: no go-ahead byte\n");
		close(s);
		return 1;
	}
	if (strcmp(m, "once") == 0)
		exch = 1;
	for (i = 0; i < exch; i++)
	{
		sprintf(buf, "line%d\n", i);
		want = strlen(buf);
		if (write(s, buf, want) != want)
		{
			close(s);
			return 1;
		}
		n = read(s, buf, sizeof(buf) - 1);
		if (n != want)
		{
			fprintf(stderr, "peers: rl: echo %d was %d bytes,"
				" wanted %d\n", i, n, want);
			close(s);
			return 1;
		}
		usleep(200000);		/* spaced out, as the header asks */
	}
	close(s);
	return 0;
}

/*
 * serve -- a TCP service, one caller at a time.  SO_REUSEADDR because a run
 * reuses the port immediately after the previous case left a socket in
 * TIME_WAIT, and a bind that failed for that would be read as a defect.
 */
static int serve(int port, int rounds, char *m)
{
	struct sockaddr_in sin;
	char buf[256];
	int ls, s, i, n, on = 1;

	if ((ls = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return 1;
	setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = htonl(0x7F000001);
	if (bind(ls, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		fprintf(stderr, "peers: serve: cannot bind %d (errno %d)\n",
			port, errno);
		close(ls);
		return 1;
	}
	if (listen(ls, 5) < 0)
	{
		close(ls);
		return 1;
	}
	if (strcmp(m, "once") == 0 && rounds > 1)
		rounds = 1;
	for (i = 0; i < rounds; i++)
	{
		if ((s = accept(ls, (struct sockaddr *)0, (socklen_t *)0)) < 0)
		{
			fprintf(stderr, "peers: serve: accept %d (errno %d)\n",
				i, errno);
			close(ls);
			return 1;
		}
		if ((n = read(s, buf, sizeof(buf))) > 0)
			write(s, buf, n);
		close(s);
	}
	close(ls);
	return 0;
}

static int udp(int port, int count, char *m)
{
	struct sockaddr_in sin;
	char buf[256], sent[256];
	int s, i, n, want;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		return 1;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = htonl(0x7F000001);
	if (strcmp(m, "few") == 0 && count > 1)
		count--;
	for (i = 0; i < count; i++)
	{
		sprintf(sent, "dgram%d", i);
		want = strlen(sent);
		if (sendto(s, sent, want, 0, (struct sockaddr *)&sin,
			sizeof(sin)) != want)
		{
			close(s);
			return 1;
		}
		n = recv(s, buf, sizeof(buf) - 1, 0);
		if (n != want)
		{
			fprintf(stderr, "peers: udp: echo %d was %d bytes,"
				" wanted %d\n", i, n, want);
			close(s);
			return 1;
		}
		buf[n] = '\0';
		if (strcmp(buf, sent) != 0)
		{
			fprintf(stderr, "peers: udp: sent [%s], got [%s]\n",
				sent, buf);
			close(s);
			return 1;
		}
	}
	close(s);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 5)
	{
		fprintf(stderr, "usage: peers rl|udp|serve port n mode\n");
		return 2;
	}
	if (strcmp(argv[1], "rl") == 0)
		return rl(atoi(argv[2]), atoi(argv[3]), argv[4]);
	if (strcmp(argv[1], "serve") == 0)
		return serve(atoi(argv[2]), atoi(argv[3]), argv[4]);
	return udp(atoi(argv[2]), atoi(argv[3]), argv[4]);
}
