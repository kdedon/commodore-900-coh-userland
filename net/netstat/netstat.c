/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * netstat -- report this machine's network state.
 *
 *	netstat			the interfaces, then the routing tables
 *	netstat -i		interfaces only
 *	netstat -r		routing tables only
 *	netstat -a		the open inet channels (one per socket)
 *	netstat -s		per-protocol statistics
 *	netstat -n		addresses as numbers, never as names
 *	netstat -I <dev>	interrogate a named ip device
 *
 * The stack is Minix 2.0.4's inet, a USER PROCESS, so there is no kernel table
 * to read: everything here comes from four ioctls the daemon answers --
 * NWIOGIPCONF (address and netmask), NWIOGIPOROUTE and NWIOGIPIROUTE (the
 * outgoing and incoming routing tables, one entry per call), NWIOGETHSTAT (an
 * ethernet interface's counters) -- plus /etc/inet.conf, which is the only
 * statement of which interfaces this machine should have.
 *
 * That bounds three options.  -a cannot show protocol, port or peer, because
 * the daemon never reports them; it lists the per-socket channels clients
 * mknod() to reach it (net/include/inet_ipc.h), skipping any whose owner is
 * gone, tested with kill(pid, 0).  -s reports the ethernet interface and
 * nothing else, since ip, icmp, tcp and udp keep no readable counters.  -m
 * (mbuf statistics) is not implemented and not accepted, because an option
 * that always answers zero is worse than no option.
 *
 * TWO HAZARDS.  NWIOGIPCONF does not answer "unconfigured": ip_ioctl()
 * SUSPENDS the request until the interface has an address, so netstat before
 * /etc/ifconfig has run waits forever with no output -- which is why the
 * interface section runs first and says what it is doing.  And the ioctl
 * request code is an int, never a long: with _WORD_SIZE 2 the NWIO* macros
 * expand to ((x<<8)|y), an int, and with no prototype in between a long
 * argument pushes four bytes where ioctl() reads two.  Same trap pr_routes
 * documents.
 */

#include <sys/types.h>
#include <sys/dir.h>
#include <sys/socket.h>		/* AF_INET, for gethostbyaddr()		*/
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <net/netlib.h>
#include <net/hton.h>
#include <net/ioctl.h>
#include <net/gen/in.h>
#include <net/gen/ether.h>
#include <net/gen/eth_io.h>
#include <net/gen/ip_io.h>
#include <net/gen/route.h>
#include <net/gen/netdb.h>
#include <net/gen/inet.h>

#define INET_CONF	"/etc/inet.conf"
#define CHAN_DIR	"/tmp"		/* where inet_chan.c mknod()s	*/

/*
 * How many interfaces /etc/inet.conf may declare.  The stack's own limit is
 * IP_PORT_MAX, but only interface 0 can be INTERROGATED from here: libsocket's
 * device table ignores the unit suffix, so /dev/ip, /dev/ip0 and /dev/ip1 all
 * open the same channel (the reason pr_routes scans one device and not 64).
 * The extra rows are still worth printing -- a declared interface with no
 * address is exactly the fault somebody runs netstat to find -- so they are
 * listed from the configuration file with the address column left blank.
 */
#define MAXIF	8

struct ifent {
	char	if_type[12];		/* psip0, eth0 ...		*/
	int	if_no;			/* the unit number in that name	*/
	int	if_default;		/* `default' in its statement	*/
};

extern int optind;
extern char *optarg;
extern int errno;
extern int kill();
/*
 * getenv() returns a POINTER.  Undeclared it is an implicit int, which on this
 * machine is 16 bits and throws away the segment -- the port's most recurring
 * bug class, and one that shows up as a wild address rather than a diagnostic.
 */
extern char *getenv();
extern char *strerror();

char *prog_name;
int n_flag;				/* -n: no name lookups		*/

static int do_if();
static int do_routes();
static int do_stats();
static int do_chans();
static int read_conf();
static char *addr_name();
static char *quad();
static char *cidr();
static usage();

main(argc, argv)
int argc;
char *argv[];
{
	int c, a_flag, i_flag, r_flag, s_flag;
	char *I_arg, *ip_device;
	int ip_fd, bad;

	prog_name= argv[0];
	a_flag= i_flag= r_flag= s_flag= n_flag= 0;
	I_arg= (char *)0;
	bad= 0;

	while ((c= getopt(argc, argv, "?aiI:nrs")) != -1)
	{
		switch (c)
		{
		case 'a':	a_flag= 1; break;
		case 'i':	i_flag= 1; break;
		case 'I':	I_arg= optarg; break;
		case 'n':	n_flag= 1; break;
		case 'r':	r_flag= 1; break;
		case 's':	s_flag= 1; break;
		default:	usage();
		}
	}
	if (optind != argc)
		usage();

	/* No selection at all: the two things this stack can always answer. */
	if (!a_flag && !i_flag && !r_flag && !s_flag)
		i_flag= r_flag= 1;

	ip_device= I_arg;
	if (ip_device == (char *)0)
		ip_device= getenv("IP_DEVICE");
	if (ip_device == (char *)0)
		ip_device= IP_DEVICE;

	/*
	 * -a alone needs no daemon: it reads the channel directory, and one of
	 * the things it is FOR is a machine whose daemon has stopped answering.
	 * Opening the ip device in that case would block on the rendezvous FIFO
	 * and the report would never be printed.
	 */
	if (!i_flag && !r_flag && !s_flag)
		return do_chans();

	if ((ip_fd= open(ip_device, O_RDWR)) == -1)
	{
		fprintf(stderr, "%s: cannot open %s: %s\n", prog_name,
			ip_device, strerror(errno));
		return 1;
	}

	if (i_flag && do_if(ip_fd) != 0)
		bad= 1;
	if (r_flag)
	{
		if (do_routes(ip_fd, NWIOGIPOROUTE, "Outgoing routes") != 0)
			bad= 1;
		if (do_routes(ip_fd, NWIOGIPIROUTE, "Incoming routes") != 0)
			bad= 1;
	}
	if (s_flag && do_stats() != 0)
		bad= 1;
	/*
	 * Close the ip device EXPLICITLY, before -a runs and before exiting.
	 *
	 * exit() closes descriptors in the kernel; it does not go through
	 * libsocket's close() override, and that override is what sends
	 * NWR_CLOSE and unlinks the channel's two FIFOs.  A network client that
	 * just exits therefore leaves /tmp/ic<pid>.<seq>.{q,r} behind for good
	 * -- 22 runs of this program left 22 pairs, measured -- so a tool whose
	 * whole job is to report that litter must not add to it.  Doing it
	 * before -a also means netstat never lists itself.
	 */
	close(ip_fd);

	if (a_flag && do_chans() != 0)
		bad= 1;
	return bad;
}

/*
 * The interface table: what /etc/inet.conf declares, and the address the daemon
 * is actually holding for interface 0.
 */
static int do_if(ip_fd)
int ip_fd;
{
	struct ifent iftab[MAXIF];
	nwio_ipconf_t conf;
	ipaddr_t net;
	int n, i;
	char flags[8];
	char nbuf[24];

	n= read_conf(iftab, MAXIF);

	if (ioctl(ip_fd, NWIOGIPCONF, (char *)&conf) == -1)
	{
		fprintf(stderr, "%s: NWIOGIPCONF: %s\n", prog_name,
			strerror(errno));
		return 1;
	}

	printf("Name  Link     Address          Netmask          Network          Flags\n");
	for (i= 0; i < n; i++)
	{
		printf("ip%-3d %-8s ", iftab[i].if_no, iftab[i].if_type);
		if (iftab[i].if_no == 0)
		{
			net= conf.nwic_ipaddr & conf.nwic_netmask;
			printf("%-16s ", addr_name(conf.nwic_ipaddr));
			strcpy(nbuf, quad(conf.nwic_netmask));
			printf("%-16s ", nbuf);
			printf("%-16s ", addr_name(net));
			flags[0]= '\0';
			if (conf.nwic_flags & NWIC_IPADDR_SET)
				strcat(flags, "U");
			if (conf.nwic_flags & NWIC_NETMASK_SET)
				strcat(flags, "M");
		}
		else
		{
			/* Declared, but this program cannot reach it: see
			 * MAXIF above.  A dash is the honest column. */
			printf("%-16s %-16s %-16s ", "-", "-", "-");
			flags[0]= '\0';
		}
		if (iftab[i].if_default)
			strcat(flags, "D");
		printf("%s\n", flags);
	}
	if (n == 0)
		fprintf(stderr, "%s: %s declares no interfaces\n", prog_name,
			INET_CONF);
	else if (n > 1)
		fprintf(stderr,
	"%s: only interface 0 can be interrogated (one channel per ip device)\n",
			prog_name);
	printf("Flags: U address set, M netmask set, D the default network\n");
	return 0;
}

/*
 * One routing table.  Entry 0's reply carries the total in nwr_ent_count, which
 * is the only way to know how many entries to ask for; this is an _IORW ioctl,
 * a struct out and the same struct back.
 */
static int do_routes(ip_fd, cmd, title)
int ip_fd;
int cmd;
char *title;
{
	nwio_route_t route;
	int nr, i, shown;
	char flags[8];
	char dbuf[40];

	route.nwr_ent_no= 0;
	if (ioctl(ip_fd, cmd, (char *)&route) == -1)
	{
		fprintf(stderr, "%s: %s: %s\n", prog_name, title,
			strerror(errno));
		return 1;
	}
	nr= (int)route.nwr_ent_count;

	printf("\n%s\n", title);
	printf("Destination            Gateway          Flags Dist Pref  Ent\n");
	shown= 0;
	for (i= 0; i < nr; i++)
	{
		route.nwr_ent_no= (u32_t)i;
		if (ioctl(ip_fd, cmd, (char *)&route) == -1)
		{
			fprintf(stderr, "%s: %s entry %d: %s\n", prog_name,
				title, i, strerror(errno));
			return 1;
		}
		if (!(route.nwr_flags & NWRF_INUSE))
			continue;
		shown++;

		strcpy(dbuf, cidr(route.nwr_dest, route.nwr_netmask));
		printf("%-22s ", dbuf);
		printf("%-16s ", quad(route.nwr_gateway));

		flags[0]= '\0';
		strcat(flags, "U");
		if (route.nwr_gateway != 0)
			strcat(flags, "G");
		if (route.nwr_flags & NWRF_STATIC)
			strcat(flags, "S");
		if (route.nwr_flags & NWRF_UNREACHABLE)
			strcat(flags, "X");
		printf("%-5s ", flags);
		printf("%-4ld ", (long)route.nwr_dist);
		printf("%-5ld ", (long)route.nwr_pref);
		printf("%ld\n", (long)route.nwr_ent_no);
	}
	if (shown == 0)
		printf("(none)\n");
	return 0;
}

/*
 * Per-protocol statistics.  See the file header: the ethernet driver is the
 * only counter source a client can read, so this is an ethernet report or an
 * explanation, and never a table of zeroes.
 */
static int do_stats()
{
	nwio_ethstat_t es;
	int fd;

	if ((fd= open(ETH_DEVICE, O_RDWR)) == -1)
	{
		fprintf(stderr,
"%s: no statistics: ip, icmp, tcp and udp keep no counters a client can read,\n",
			prog_name);
		fprintf(stderr,
"%s: and %s (the one layer that does) is not configured in %s\n",
			prog_name, ETH_DEVICE, INET_CONF);
		return 1;
	}
	if (ioctl(fd, NWIOGETHSTAT, (char *)&es) == -1)
	{
		fprintf(stderr, "%s: NWIOGETHSTAT: %s\n", prog_name,
			strerror(errno));
		close(fd);
		return 1;
	}
	printf("\nEthernet (%s)\n", ETH_DEVICE);
	printf("  %10lu packets received\n", es.nwes_stat.ets_packetR);
	printf("  %10lu packets transmitted\n", es.nwes_stat.ets_packetT);
	printf("  %10lu receive errors\n", es.nwes_stat.ets_recvErr);
	printf("  %10lu send errors\n", es.nwes_stat.ets_sendErr);
	printf("  %10lu CRC errors\n", es.nwes_stat.ets_CRCerr);
	printf("  %10lu misaligned frames\n", es.nwes_stat.ets_frameAll);
	printf("  %10lu packets missed\n", es.nwes_stat.ets_missedP);
	printf("  %10lu buffer overwrites\n", es.nwes_stat.ets_OVW);
	printf("  %10lu collisions\n", es.nwes_stat.ets_collision);
	printf("  %10lu transmissions deferred\n", es.nwes_stat.ets_transDef);
	printf("  %10lu transmissions aborted\n", es.nwes_stat.ets_transAb);
	printf("  %10lu carrier sense lost\n", es.nwes_stat.ets_carrSense);
	printf("  %10lu fifo underruns\n", es.nwes_stat.ets_fifoUnder);
	printf("  %10lu fifo overruns\n", es.nwes_stat.ets_fifoOver);
	close(fd);
	return 0;
}

/*
 * The open inet channels.  Read CHAN_DIR the old way -- open(2) and read(2) of
 * struct direct -- because <dirent.h> here is `#ifdef _I386' and opendir() does
 * not exist on this machine.
 *
 * A name is a channel's request FIFO if it is ic<digits>.<digits>.q; the reply
 * FIFO of the same pair is the same name ending .r, and counting only the .q
 * side is what makes one line per socket rather than two.
 */
static int do_chans()
{
	struct direct d;
	int fd, live, stale;
	long pid, seq;
	char *cp;
	char name[DIRSIZ+1];

	if ((fd= open(CHAN_DIR, O_RDONLY)) == -1)
	{
		fprintf(stderr, "%s: cannot read %s: %s\n", prog_name,
			CHAN_DIR, strerror(errno));
		return 1;
	}
	live= stale= 0;

	printf("\nActive inet channels\n");
	printf("   Owner Channel\n");

	while (read(fd, (char *)&d, sizeof(d)) == sizeof(d))
	{
		if (d.d_ino == 0)
			continue;
		strncpy(name, d.d_name, DIRSIZ);
		name[DIRSIZ]= '\0';
		if (name[0] != 'i' || name[1] != 'c')
			continue;
		cp= name + 2;
		if (*cp < '0' || *cp > '9')
			continue;
		pid= 0;
		while (*cp >= '0' && *cp <= '9')
			pid= pid * 10 + (*cp++ - '0');
		if (*cp++ != '.')
			continue;
		if (*cp < '0' || *cp > '9')
			continue;
		seq= 0;
		while (*cp >= '0' && *cp <= '9')
			seq= seq * 10 + (*cp++ - '0');
		if (strcmp(cp, ".q") != 0)
			continue;

		/*
		 * Signal 0 is an existence test.  EPERM means the process is
		 * there and owned by somebody else, which is still there; only
		 * ESRCH means the channel outlived its owner.
		 */
		errno= 0;
		if (kill((int)pid, 0) == -1 && errno == ESRCH)
		{
			stale++;
			continue;
		}
		live++;
		printf("%8ld ic%ld.%ld\n", pid, pid, seq);
	}
	close(fd);

	if (live == 0)
		printf("(none)\n");
	if (stale != 0)
		printf("%d abandoned channel%s in %s (owner gone)\n",
			stale, (stale == 1) ? "" : "s", CHAN_DIR);
	printf(
"A channel is one open socket.  Its protocol, ports and peer are held inside\n");
	printf(
"the inet daemon and are not readable from here; see netstat(1).\n");
	return 0;
}

/*
 * Read /etc/inet.conf into iftab.  The file is a sequence of statements
 *
 *	psip0 { default; };
 *	eth0 { default; } 0 { };
 *
 * and inet_config.c's read_conf() scans it as whitespace-separated words with
 * the punctuation ignored, so this does the same rather than trying to be a
 * grammar.  A word matching eth<n> or psip<n> starts an interface; `default'
 * belongs to whichever interface is current.  Returns the number found.
 */
static int read_conf(iftab, max)
struct ifent *iftab;
int max;
{
	FILE *fp;
	int c, n, i, cur;
	char word[32];

	if ((fp= fopen(INET_CONF, "r")) == (FILE *)0)
	{
		fprintf(stderr, "%s: %s: %s\n", prog_name, INET_CONF,
			strerror(errno));
		return 0;
	}
	n= 0;
	cur= -1;
	for (;;)
	{
		while ((c= getc(fp)) != EOF &&
		       (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
			c == ';' || c == '{' || c == '}' || c == ','))
			;
		if (c == EOF)
			break;
		i= 0;
		do {
			if (i < (int)sizeof(word) - 1)
				word[i++]= c;
		} while ((c= getc(fp)) != EOF &&
			 c != ' ' && c != '\t' && c != '\n' && c != '\r' &&
			 c != ';' && c != '{' && c != '}' && c != ',');
		word[i]= '\0';

		if (strcmp(word, "default") == 0)
		{
			if (cur >= 0)
				iftab[cur].if_default= 1;
			continue;
		}
		if (strncmp(word, "eth", 3) != 0 &&
		    strncmp(word, "psip", 4) != 0)
			continue;
		if (n >= max)
			continue;
		cur= n++;
		strncpy(iftab[cur].if_type, word,
			sizeof(iftab[cur].if_type) - 1);
		iftab[cur].if_type[sizeof(iftab[cur].if_type) - 1]= '\0';
		iftab[cur].if_default= 0;
		iftab[cur].if_no= 0;
		for (i= 0; word[i] != '\0'; i++)
			if (word[i] >= '0' && word[i] <= '9')
			{
				iftab[cur].if_no= atoi(word + i);
				break;
			}
	}
	fclose(fp);
	return n;
}

/*
 * An address as a dotted quad.  Not inet_ntoa(): that returns a pointer into
 * ONE static buffer, so two of them in one printf print the same address twice.
 * Three rotating buffers is enough for the widest line here (address, netmask,
 * network) and is why every caller can be an argument rather than a statement.
 */
static char *quad(a)
ipaddr_t a;
{
	static char buf[3][16];
	static int which;
	char *p;

	p= buf[which];
	which= (which + 1) % 3;
	sprintf(p, "%d.%d.%d.%d",
		(int)((a >> 24) & 0xFF), (int)((a >> 16) & 0xFF),
		(int)((a >> 8) & 0xFF), (int)(a & 0xFF));
	return p;
}

/*
 * The same, but as a name when one is known and -n was not given.
 * gethostbyaddr() reads /etc/hosts before it asks the network, and answers with
 * the dotted quad when neither knows -- so this never fails and never prints an
 * empty column.  With /etc/resolv.conf present an unknown address costs a DNS
 * query and its retries, which is what -n is for.
 */
static char *addr_name(a)
ipaddr_t a;
{
	static char buf[2][64];
	static int which;
	struct hostent *hp;
	ipaddr_t tmp;
	char *p;

	if (n_flag || a == 0)
		return quad(a);
	tmp= a;
	hp= gethostbyaddr((char *)&tmp, 4, AF_INET);
	if (hp == (struct hostent *)0 || hp->h_name == (char *)0)
		return quad(a);
	p= buf[which];
	which= (which + 1) % 2;
	strncpy(p, hp->h_name, 63);
	p[63]= '\0';
	return p;
}

/*
 * "dest/prefixlen", or "dest/dotted-mask" when the mask is not contiguous.
 * n counts down from 32 while the trial mask shifts left, so it stops at the
 * prefix length -- and at -1 for a mask no shift of ~0 ever matches.  Same
 * routine pr_routes uses, and deliberately: two tools printing one table should
 * spell it the same way.
 */
static char *cidr(addr, mask)
ipaddr_t addr;
ipaddr_t mask;
{
	ipaddr_t testmask= 0xFFFFFFFFL;
	int n;
	static char result[sizeof("255.255.255.255/255.255.255.255")];

	for (n= 32; n >= 0; n--)
	{
		if (mask == htonl(testmask))
			break;
		testmask= (testmask << 1) & 0xFFFFFFFFL;
	}
	sprintf(result, "%s/%-2d", quad(addr), n);
	if (n == -1)
		strcpy(strchr(result, '/') + 1, quad(mask));
	return result;
}

static usage()
{
	fprintf(stderr, "Usage: %s [-airsn] [-I <ip-device>]\n", prog_name);
	exit(1);
}
