/*
ttn.c
*/

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE 1
#endif

#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <termio.h>
#include <signal.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/hton.h>
#include <net/netlib.h>
#include <net/gen/in.h>
#include <net/gen/inet.h>
#include <net/gen/netdb.h>
#include <net/gen/tcp.h>
#include <net/gen/tcp_io.h>
#include <net/ioctl.h>
#include "ttn.h"

#if __STDC__
#define PROTOTYPE(func,args) func args
#else
#define PROTOTYPE(func,args) func()
#endif

static int do_read();
static void session();
static int screen();
static int keyboard();
static void send_brk();
static int process_opt ();
static void do_option ();
static void dont_option ();
static void will_option ();
static void wont_option ();
static int writeall ();
static int sb_termtype ();
static void fatal();
static void usage();

#if DEBUG
#define where() (fprintf(stderr, "%s %d:", __FILE__, __LINE__))
#endif

static char *prog_name;
static tcp_fd;
static char *term_env;
static int esc_char= '~';
static enum { LS_NORM, LS_BOL, LS_ESC } line_state= LS_BOL;

int main(argc, argv)
int argc;
char *argv[];
{
	struct hostent *hostent;
	struct servent *servent;
	ipaddr_t host;
	tcpport_t port;
	nwio_tcpconf_t tcpconf;
	int c, r;
	nwio_tcpcl_t tcpconnopt;
	struct termio tio;
	char *tcp_device, *remote_name, *port_name;
	char *e_arg;

	(prog_name=strrchr(argv[0],'/')) ? prog_name++ : (prog_name=argv[0]);

	e_arg= NULL;
	while (c= getopt(argc, argv, "?e:"), c != -1)
	{
		switch(c)
		{
		case '?': usage();
		case 'e': e_arg= optarg; break;
		default:
			fatal("Optind failed: '%c'", c);
		}
	}

	if (optind >= argc)
		usage();
	remote_name= argv[optind++];
	if (optind < argc)
		port_name= argv[optind++];
	else
		port_name= NULL;
	if (optind != argc)
		usage();

	if (e_arg)
	{
		switch(strlen(e_arg))
		{
		case 0: esc_char= -1; break;
		case 1: esc_char= e_arg[0]; break;
		default: fatal("Invalid escape character '%s'", e_arg);
		}
	}

	hostent= gethostbyname(remote_name);
	if (!hostent)
		fatal("Unknown host %s", remote_name);
	host= *(ipaddr_t *)(hostent->h_addr);

	if (!port_name)
		port= htons(TCPPORT_TELNET);
	else
	{
		servent= getservbyname (port_name, "tcp");
		if (!servent)
		{
			port= htons(strtol(port_name, (char **)0, 0));
			if (!port)
				fatal("Unknown port %s", port_name);
		}
		else
			port= (tcpport_t)(servent->s_port);
	}

	fprintf(stderr, "Connecting to %s:%u...\n",
		inet_ntoa(host), ntohs(port));

	tcp_device= getenv("TCP_DEVICE");
	if (tcp_device == NULL)
		tcp_device= TCP_DEVICE;
	tcp_fd= open (tcp_device, O_RDWR);
	if (tcp_fd == -1)
		fatal("Unable to open %s: %s", tcp_device, strerror(errno));

	tcpconf.nwtc_flags= NWTC_LP_SEL | NWTC_SET_RA | NWTC_SET_RP;
	tcpconf.nwtc_remaddr= host;
	tcpconf.nwtc_remport= port;

	r= ioctl (tcp_fd, NWIOSTCPCONF, &tcpconf);
	if (r == -1)
		fatal("NWIOSTCPCONF failed: %s", strerror(errno));

	tcpconnopt.nwtcl_flags= 0;
	do
	{
		r= ioctl (tcp_fd, NWIOTCPCONN, &tcpconnopt);
		if (r == -1 && errno == EAGAIN)
		{
			fprintf(stderr, "%s: Got EAGAIN, sleeping(1s)\n",
				prog_name);
			sleep(1);
		}
	} while (r == -1 && errno == EAGAIN);
	if (r == -1)
		fatal("Unable to connect: %s", strerror(errno));
	printf("Connected\n");
	fflush(stdout);
	ioctl(0, TCGETA, &tio);
	session();
	ioctl(0, TCSETA, &tio);
	exit(0);
}

/*
 * Carry the session until either the keyboard or the connection reaches end of
 * data, waiting on the two of them together.
 *
 * ONE process does both directions.  The connection is not a socket the kernel
 * knows about: it is a pair of FIFOs to the inet daemon, framed by state that
 * lives in this program (net/inet_chan.c).  A second process sharing it reads
 * the same reply FIFO, so a reply is taken by whichever of the two the kernel
 * happens to wake, and the one that was waiting for it waits for ever -- which
 * a keyboard child, whose first keystroke is a channel write, hits at once.
 */
static void session()
{
	struct pollfd pfd[2];
	int held;

	for (;;)
	{
		pfd[0].fd= tcp_fd;
		pfd[0].events= POLLIN;
		pfd[0].revents= 0;
		pfd[1].fd= 0;
		pfd[1].events= POLLIN;
		pfd[1].revents= 0;

		/* Bytes already off the reply FIFO are invisible to poll(): a
		 * write's reply parks any data that arrived in front of it. */
		held= sockheld(tcp_fd);
		/* (unsigned long) is required: poll(2)'s count is a long in
		 * this ABI (kernel upoll(), syscall table entry 67) and there
		 * is no prototype to widen it. */
		if (held == 0 && poll(pfd, (unsigned long)2, INFTIM) < 0)
		{
			if (errno == EINTR)
				continue;
			return;
		}

		if ((held != 0 || pfd[0].revents != 0) && screen() <= 0)
			return;
		if (pfd[1].revents != 0 && keyboard() <= 0)
			return;
	}
}

static int do_read(fd, buf, len)
int fd;
char *buf;
unsigned len;
{
	nwio_tcpopt_t tcpopt;
	int count;

	for (;;)
	{
		count= read (fd, buf, len);
		if (count <0)
		{
			if (errno == EURG || errno == ENOURG)
			{
				/* Toggle urgent mode. */
				tcpopt.nwto_flags= errno == EURG ?
					NWTO_RCV_URG : NWTO_RCV_NOTURG;
				if (ioctl(tcp_fd, NWIOSTCPOPT, &tcpopt) == -1)
				{
					return -1;
				}
				continue;
			}
			return -1;
		}
		return count;
	}
}

/*
 * One helping of what the connection has to say: options are acted on, the
 * rest goes to the screen.  Returns what was read, so 0 is end of data and a
 * negative is the end of the session.
 */
static int screen()
{
	char buffer[1024], *bp, *iacptr;
	int count, optsize;

	count= do_read (tcp_fd, buffer, sizeof(buffer));
#if DEBUG && 0
 { where(); fprintf(stderr, "read %d bytes\r\n", count); }
#endif
	if (count <0)
	{
		perror ("read");
		return -1;
	}
	if (!count)
		return 0;
	bp= buffer;
	do
	{
		iacptr= memchr (bp, IAC, count);
		if (!iacptr)
		{
			write(1, bp, count);
			count= 0;
		}
		if (iacptr && iacptr>bp)
		{
#if DEBUG
 { where(); fprintf(stderr, "iacptr-bp= %d\r\n", iacptr-bp); }
#endif
			write(1, bp, iacptr-bp);
			count -= (iacptr-bp);
			bp= iacptr;
			continue;
		}
		if (iacptr)
		{
assert (iacptr == bp);
			optsize= process_opt(bp, count);
#if DEBUG && 0
 { where(); fprintf(stderr, "process_opt(...)= %d\r\n", optsize); }
#endif
			if (optsize<0)
				return -1;
assert (optsize);
			bp += optsize;
			count -= optsize;
		}
	} while (count);
	return 1;
}

/*
 * One character from the keyboard, either swallowed by the escape sequence or
 * sent.  Returns what was read, so 0 is end of input and a negative is the
 * escape command that ends the session.
 */
static int keyboard()
{
	char c, buffer[1024];
	int count;

	count= read (0, buffer, 1 /* sizeof(buffer) */);
	if (count == -1)
		fatal("Read: %s\r\n", strerror(errno));
	if (!count)
		return 0;

	if (line_state != LS_NORM)
	{
		c= buffer[0];
		if (line_state == LS_BOL)
		{
			if (c == esc_char)
			{
				line_state= LS_ESC;
				return 1;
			}
			line_state= LS_NORM;
		}
		else if (line_state == LS_ESC)
		{
			line_state= LS_NORM;
			if (c == '.')
				return -1;
			if (c == '#')
			{
				send_brk();
				return 1;
			}

			/* Not a valid command or a repeat of the
			 * escape char
			 */
			if (c != esc_char)
			{
				c= esc_char;
				write(tcp_fd, &c, 1);
			}
		}
	}
	if (buffer[0] == '\n')
		write(tcp_fd, "\r", 1);
	count= write(tcp_fd, buffer, count);
	if (buffer[0] == '\r')
	{
		line_state= LS_BOL;
		write(tcp_fd, "\0", 1);
	}
	if (count<0)
	{
		perror("write");
		fprintf(stderr, "errno= %d\r\n", errno);
		return -1;
	}
	return count;
}

static void send_brk()
{
	int r;
	unsigned char buffer[2];

	buffer[0]= IAC;
	buffer[1]= IAC_BRK;

	r= writeall(tcp_fd, (char *)buffer, 2);
	if (r == -1)
		fatal("Error writing to TCP connection: %s", strerror(errno));
}

#define next_char(var) \
	if (offset<count) { (var) = bp[offset++]; } \
	else if (do_read(tcp_fd, (char *)&(var), 1) <= 0) \
	{ perror ("read"); return -1; }

static int process_opt (bp, count)
char *bp;
int count;
{
	unsigned char iac, command, optsrt, sb_command;
	int offset, result;	;
#if DEBUG && 0
 { where(); fprintf(stderr, "process_opt(bp= 0x%x, count= %d)\r\n",
	bp, count); }
#endif

	offset= 0;
assert (count);
	next_char(iac);
assert (iac == IAC);
	next_char(command);
	switch(command)
	{
	case IAC_NOP:
		break;
	case IAC_DataMark:
		/* Ought to flush input queue or something. */
		break;
	case IAC_BRK:
fprintf(stderr, "got a BRK\r\n");
		break;
	case IAC_IP:
fprintf(stderr, "got a IP\r\n");
		break;
	case IAC_AO:
fprintf(stderr, "got a AO\r\n");
		break;
	case IAC_AYT:
fprintf(stderr, "got a AYT\r\n");
		break;
	case IAC_EC:
fprintf(stderr, "got a EC\r\n");
		break;
	case IAC_EL:
fprintf(stderr, "got a EL\r\n");
		break;
	case IAC_GA:
fprintf(stderr, "got a GA\r\n");
		break;
	case IAC_SB:
		next_char(sb_command);
		switch (sb_command)
		{
		case OPT_TERMTYPE:
#if DEBUG && 0
fprintf(stderr, "got SB TERMINAL-TYPE\r\n");
#endif
			result= sb_termtype(bp+offset, count-offset);
			if (result<0)
				return result;
			else
				return result+offset;
		default:
fprintf(stderr, "got an unknown SB (skiping)\r\n");
			for (;;)
			{
				next_char(iac);
				if (iac != IAC)
					continue;
				next_char(optsrt);
				if (optsrt == IAC)
					continue;
if (optsrt != IAC_SE)
	fprintf(stderr, "got IAC %d\r\n", optsrt);
				break;
			}
		}
		break;
	case IAC_WILL:
		next_char(optsrt);
		will_option(optsrt);
		break;
	case IAC_WONT:
		next_char(optsrt);
		wont_option(optsrt);
		break;
	case IAC_DO:
		next_char(optsrt);
		do_option(optsrt);
		break;
	case IAC_DONT:
		next_char(optsrt);
		dont_option(optsrt);
		break;
	case IAC:
fprintf(stderr, "got a IAC\r\n");
		break;
	default:
fprintf(stderr, "got unknown command (%d)\r\n", command);
	}
	return offset;
}

static void do_option (optsrt)
int optsrt;
{
	unsigned char reply[3];
	int result;

	switch (optsrt)
	{
	case OPT_TERMTYPE:
		if (WILL_ttype)
			return;
		if (!WILL_ttype_allowed)
		{
			reply[0]= IAC;
			reply[1]= IAC_WONT;
			reply[2]= optsrt;
		}
		else
		{
			WILL_ttype= TRUE;
			term_env= getenv("TERM");
			if (!term_env)
				term_env= "unknown";
			reply[0]= IAC;
			reply[1]= IAC_WILL;
			reply[2]= optsrt;
		}
		break;
	default:
#if DEBUG
		fprintf(stderr, "got a DO (%d)\r\n", optsrt);
		fprintf(stderr, "WONT (%d)\r\n", optsrt);
#endif
		reply[0]= IAC;
		reply[1]= IAC_WONT;
		reply[2]= optsrt;
		break;
	}
	result= writeall(tcp_fd, (char *)reply, 3);
	if (result<0)
		perror("write");
}

static void will_option (optsrt)
int optsrt;
{
	unsigned char reply[3];
	int result;

	switch (optsrt)
	{
	case OPT_ECHO:
		if (DO_echo)
			break;
		if (!DO_echo_allowed)
		{
			reply[0]= IAC;
			reply[1]= IAC_DONT;
			reply[2]= optsrt;
		}
		else
		{
			struct termio tio;

			/* Raw: the remote end echoes and interprets, so the
			 * local line must not.  IEXTEN is a POSIX flag with no
			 * termio counterpart -- there is no local extended
			 * processing here to turn off. */
			ioctl(0, TCGETA, &tio);
			tio.c_iflag &= ~(ICRNL|IGNCR|INLCR|IXON|IXOFF);
			tio.c_oflag &= ~(OPOST);
			tio.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG);
			ioctl(0, TCSETA, &tio);

			DO_echo= TRUE;
			reply[0]= IAC;
			reply[1]= IAC_DO;
			reply[2]= optsrt;
		}
		result= writeall(tcp_fd, (char *)reply, 3);
		if (result<0)
			perror("write");
		break;
	case OPT_SUPP_GA:
		if (DO_sga)
			break;
		if (!DO_sga_allowed)
		{
			reply[0]= IAC;
			reply[1]= IAC_DONT;
			reply[2]= optsrt;
		}
		else
		{
			DO_sga= TRUE;
			reply[0]= IAC;
			reply[1]= IAC_DO;
			reply[2]= optsrt;
		}
		result= writeall(tcp_fd, (char *)reply, 3);
		if (result<0)
			perror("write");
		break;
	default:
#if DEBUG
		fprintf(stderr, "got a WILL (%d)\r\n", optsrt);
		fprintf(stderr, "DONT (%d)\r\n", optsrt);
#endif
		reply[0]= IAC;
		reply[1]= IAC_DONT;
		reply[2]= optsrt;
		result= writeall(tcp_fd, (char *)reply, 3);
		if (result<0)
			perror("write");
		break;
	}
}

static int writeall (fd, buffer, buf_size)
int fd;
char *buffer;
int buf_size;
{
	int result;

	while (buf_size)
	{
		result= write (fd, buffer, buf_size);
		if (result <= 0)
			return -1;
assert (result <= buf_size);
		buffer += result;
		buf_size -= result;
	}
	return 0;
}

static void dont_option (optsrt)
int optsrt;
{
	switch (optsrt)
	{
	default:
#if DEBUG
		fprintf(stderr, "got a DONT (%d)\r\n", optsrt);
#endif
		break;
	}
}

static void wont_option (optsrt)
int optsrt;
{
	switch (optsrt)
	{
	default:
#if DEBUG
		fprintf(stderr, "got a WONT (%d)\r\n", optsrt);
#endif
		break;
	}
}

static int sb_termtype (bp, count)
char *bp;
int count;
{
	unsigned char command, iac, optsrt;
	unsigned char buffer[4];
	int offset, result;

	offset= 0;
	next_char(command);
	if (command == TERMTYPE_SEND)
	{
		buffer[0]= IAC;
		buffer[1]= IAC_SB;
		buffer[2]= OPT_TERMTYPE;
		buffer[3]= TERMTYPE_IS;
		result= writeall(tcp_fd, (char *)buffer,4);
		if (result<0)
			return result;
		count= strlen(term_env);
		if (!count)
		{
			term_env= "unknown";
			count= strlen(term_env);
		}
		result= writeall(tcp_fd, term_env, count);
		if (result<0)
			return result;
		buffer[0]= IAC;
		buffer[1]= IAC_SE;
		result= writeall(tcp_fd, (char *)buffer,2);
		if (result<0)
			return result;

	}
	else
	{
#if DEBUG
 where();
#endif
		fprintf(stderr, "got an unknown command (skipping)\r\n");
	}
	for (;;)
	{
		next_char(iac);
		if (iac != IAC)
			continue;
		next_char(optsrt);
		if (optsrt == IAC)
			continue;
		if (optsrt != IAC_SE)
		{
#if DEBUG
 where();
#endif
			fprintf(stderr, "got IAC %d\r\n", optsrt);
		}
		break;
	}
	return offset;
}

/*
 * %r is this library's varargs printf: it takes the address of the argument
 * list rather than a va_list, so &fmt names the whole tail.  vfprintf and
 * <stdarg.h> do not exist here.
 */
static void fatal(fmt)
char *fmt;
{
	fprintf(stderr, "%s: ", prog_name);
	fprintf(stderr, "%r", &fmt);
	fprintf(stderr, "\n");
	exit(1);
}

static void usage()
{
	fprintf(stderr, "Usage: %s [-e esc-char] host [port]\r\n",
		prog_name);
	exit(1);
}

/*
 * $PchId: ttn.c,v 1.5 2002/05/07 12:06:41 philip Exp $
 */
