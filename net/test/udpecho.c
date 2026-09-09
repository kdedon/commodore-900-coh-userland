/*
 * udpecho.c -- a UDP datagram round trip, both ends in this process.
 *
 *	udpecho [addr]			default 10.0.0.2
 *
 * Two bound sockets on the same machine: A sends a datagram to B, B receives it
 * and reports who sent it, echoes it back, and A receives the echo.  ip_write
 * loops a packet addressed to its own interface back internally, so this needs
 * no wire, no slip and no peer -- the same reason echoserver.c exists for TCP,
 * and the same seven-minutes-instead-of-twenty.
 *
 * What it is really checking is ADDRESSING.  A datagram socket that accepts
 * traffic from anyone cannot use NWUO_RWDATONLY -- udp.c answers EBADMODE for
 * RWDATONLY together with RP_ANY or RA_ANY -- so each datagram carries a
 * udp_io_hdr with its own source and destination.  recvfrom() reporting the
 * right sender is the point of the test, not just that bytes arrived.
 *
 * Every line starts with "udpecho:" so a scripted run can pick it out of a
 * shared console.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>

extern int errno;
extern unsigned long inet_addr();
extern char *inet_ntoa();

#define PORT_A	7001
#define PORT_B	7002
#define MSG	"udp-hello"

static int bindport(port, addr)
int port;
unsigned long addr;
{
	int s;
	struct sockaddr_in sin;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		printf("udpecho: socket(%d) failed errno %d\n", port, errno);
		return -1;
	}
	memset((char *)&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = addr;
	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		printf("udpecho: bind(%d) failed errno %d\n", port, errno);
		return -1;
	}
	printf("udpecho: bound port %d\n", port);
	fflush(stdout);
	return s;
}

int main(argc, argv)
int argc;
char **argv;
{
	int a, b, n, alen;
	unsigned long me;
	struct sockaddr_in to, from;
	char buf[128];

	me = inet_addr(argc > 1 ? argv[1] : "10.0.0.2");

	if ((a = bindport(PORT_A, me)) < 0 || (b = bindport(PORT_B, me)) < 0)
		return 1;

	memset((char *)&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_port = htons(PORT_B);
	to.sin_addr.s_addr = me;

	n = sendto(a, MSG, sizeof(MSG) - 1, 0, (struct sockaddr *)&to,
		sizeof(to));
	printf("udpecho: sent %d (errno %d)\n", n, errno);
	fflush(stdout);
	if (n != sizeof(MSG) - 1)
		return 1;

	alen = sizeof(from);
	n = recvfrom(b, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from,
		&alen);
	printf("udpecho: B received %d (errno %d)\n", n, errno);
	if (n <= 0)
		return 1;
	buf[n] = '\0';
	printf("udpecho: B got [%s] from port %d\n", buf, ntohs(from.sin_port));
	fflush(stdout);
	if (ntohs(from.sin_port) != PORT_A)
	{
		printf("udpecho: WRONG SENDER -- wanted port %d\n", PORT_A);
		return 1;
	}

	/* Echo it back to whoever sent it, which is what recvfrom told us. */
	n = sendto(b, buf, n, 0, (struct sockaddr *)&from, sizeof(from));
	printf("udpecho: echoed %d (errno %d)\n", n, errno);
	fflush(stdout);

	alen = sizeof(from);
	n = recvfrom(a, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from,
		&alen);
	if (n <= 0)
	{
		printf("udpecho: A received %d (errno %d)\n", n, errno);
		return 1;
	}
	buf[n] = '\0';
	printf("udpecho: A got [%s] back from port %d\n", buf,
		ntohs(from.sin_port));

	if (strcmp(buf, MSG) != 0 || ntohs(from.sin_port) != PORT_B)
	{
		printf("udpecho: FAIL\n");
		return 1;
	}
	printf("udpecho: PASS\n");
	close(a);
	close(b);
	return 0;
}
