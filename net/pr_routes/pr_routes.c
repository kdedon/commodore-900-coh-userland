/*
 * pr_routes -- print the IP routing table.
 *
 * From Minix 2.0.4 (vmd/cmd/simple/pr_routes.c, Philip Homburg).  This is the
 * nearest thing this stack has to `netstat -r'; there is no socket-listing
 * netstat to port, because the connection table lives inside the inet daemon
 * rather than in the kernel.
 *
 *	pr_routes		the outgoing routes of this interface
 *	pr_routes -i		the incoming (source) routes instead
 *	pr_routes -a		every interface's routes, not just this one
 *	pr_routes -I <dev>	a named ip device
 *
 * The table is read one entry at a time: NWIOGIPOROUTE takes the entry number
 * in nwr_ent_no and answers with that entry, and entry 0's reply also carries
 * the total in nwr_ent_count.  That is an _IORW -- a struct out and the same
 * struct back -- which the channel library reaches through ichan_ioctl_rw().
 */

#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/netlib.h>
#include <net/hton.h>
#include <net/ioctl.h>
#include <net/gen/in.h>
#include <net/gen/ip_io.h>
#include <net/gen/route.h>
#include <net/gen/netdb.h>
#include <net/gen/inet.h>

/*
 * One interface, not Minix's 64.  libsocket's device table ignores the unit
 * suffix on purpose -- /dev/ip and /dev/ip0 are the same channel -- so a scan
 * of /dev/ip0../dev/ip63 would open sixty-four channels to interface 0 and
 * then report each of them as a duplicate address of the one before.  This
 * port configures exactly one psip interface (/etc/inet.conf), so the scan has
 * one entry and says what it means.
 */
#define N_IF	1

extern int optind;
extern char *optarg;

char *prog_name;
int all_devices;
char *ifname;
ipaddr_t iftab[N_IF];

static print_header();
static print_route();
static fill_iftab();
static char *get_ifname();
static char *cidr2a();
static fatal();
static usage();

main(argc, argv)
int argc;
char *argv[];
{
	int nr_routes, i;
	nwio_route_t route;
	nwio_ipconf_t ip_conf;
	int ioctl_cmd;
	int ip_fd;
	int result;
	int c;
	char *ip_device, *cp;
	int a_flag, i_flag, o_flag;
	char *I_arg;

	prog_name= argv[0];

	a_flag= 0;
	i_flag= 0;
	o_flag= 0;
	I_arg= (char *)0;
	while ((c= getopt(argc, argv, "?aI:io")) != -1)
	{
		switch(c)
		{
		case 'a':
			if (a_flag)
				usage();
			a_flag= 1;
			break;
		case 'I':
			if (I_arg)
				usage();
			I_arg= optarg;
			break;
		case 'i':
			if (i_flag || o_flag)
				usage();
			i_flag= 1;
			break;
		case 'o':
			if (i_flag || o_flag)
				usage();
			o_flag= 1;
			break;
		default:
			usage();
		}
	}
	if (optind != argc)
		usage();

	ip_device= I_arg;
	all_devices= a_flag;

	/*
	 * The request code is an int here, not the donor's unsigned long: with
	 * _WORD_SIZE 2 the NWIO* macros expand to ((x<<8)|y), whose type is
	 * int, and there are no prototypes on the way to ioctl().  A long would
	 * push four bytes where ioctl() reads two and take the argument pointer
	 * with it.
	 */
	if (i_flag)
		ioctl_cmd= NWIOGIPIROUTE;
	else
		ioctl_cmd= NWIOGIPOROUTE;

	if (ip_device == (char *)0)
		ip_device= getenv("IP_DEVICE");
	ifname= ip_device;
	if (ip_device == (char *)0)
		ip_device= IP_DEVICE;

	ip_fd= open(ip_device, O_RDWR);
	if (ip_fd == -1)
	{
		fprintf(stderr, "%s: unable to open %s: %s\n", prog_name,
			ip_device, strerror(errno));
		exit(1);
	}

	if (!all_devices && ifname)
	{
		cp= strrchr(ip_device, '/');
		if (cp)
			ifname= cp+1;
	}
	else
	{
		ifname= (char *)0;
		fill_iftab();
	}

	result= ioctl(ip_fd, NWIOGIPCONF, (char *)&ip_conf);
	if (result == -1)
	{
		fprintf(stderr, "%s: unable to NWIOGIPCONF: %s\n",
			prog_name, strerror(errno));
		exit(1);
	}

	route.nwr_ent_no= 0;
	result= ioctl(ip_fd, ioctl_cmd, (char *)&route);
	if (result == -1)
	{
		fprintf(stderr, "%s: unable to NWIOGIPxROUTE: %s\n",
			prog_name, strerror(errno));
		exit(1);
	}
	print_header();
	nr_routes= (int)route.nwr_ent_count;
	for (i= 0; i<nr_routes; i++)
	{
		route.nwr_ent_no= (u32_t)i;
		result= ioctl(ip_fd, ioctl_cmd, (char *)&route);
		if (result == -1)
		{
			fprintf(stderr, "%s: unable to NWIOGIPxROUTE: %s\n",
				prog_name, strerror(errno));
			exit(1);
		}
		if (all_devices || route.nwr_ifaddr == ip_conf.nwic_ipaddr)
			print_route(&route);
	}
	exit(0);
}

int ent_width= 5;
int if_width= 4;
int dest_width= 18;
int gateway_width= 15;
int dist_width= 4;
int pref_width= 5;
int mtu_width= 4;

static print_header()
{
	printf("%*s ", ent_width, "ent #");
	printf("%*s ", if_width, "if");
	printf("%*s ", dest_width, "dest");
	printf("%*s ", gateway_width, "gateway");
	printf("%*s ", dist_width, "dist");
	printf("%*s ", pref_width, "pref");
	printf("%*s ", mtu_width, "mtu");
	printf("%s", "flags");
	printf("\n");
}

/*
 * "dest/prefixlen", or "dest/dotted-mask" for a mask that is not contiguous.
 * n counts down from 32 while the trial mask is shifted left, so it ends at
 * the prefix length -- and at -1 for a mask no shift of ~0 ever matches.
 */
static char *cidr2a(addr, mask)
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

	sprintf(result, "%s/%-2d", inet_ntoa(addr), n);
	if (n == -1)
		strcpy(strchr(result, '/')+1, inet_ntoa(mask));
	return result;
}

static print_route(route)
nwio_route_t *route;
{
	if (!(route->nwr_flags & NWRF_INUSE))
		return 0;

	printf("%*lu ", ent_width, (unsigned long) route->nwr_ent_no);
	printf("%*s ", if_width,
		ifname ? ifname : get_ifname(route->nwr_ifaddr));
	printf("%*s ", dest_width, cidr2a(route->nwr_dest, route->nwr_netmask));
	printf("%*s ", gateway_width, inet_ntoa(route->nwr_gateway));
	printf("%*lu ", dist_width, (unsigned long) route->nwr_dist);
	printf("%*ld ", pref_width, (long) route->nwr_pref);
	printf("%*lu", mtu_width, (unsigned long) route->nwr_mtu);
	if (route->nwr_flags & NWRF_STATIC)
		printf(" static");
	if (route->nwr_flags & NWRF_UNREACHABLE)
		printf(" dead");
	printf("\n");
	return 0;
}

static fill_iftab()
{
	int i, j, r, fd;
	nwio_ipconf_t ip_conf;
	char dev_name[12];	/* /dev/ipXXXX */

	for (i= 0; i<N_IF; i++)
	{
		iftab[i]= 0;

		sprintf(dev_name, "/dev/ip%d", i);
		fd= open(dev_name, O_RDWR);
		if (fd == -1)
		{
			if (errno == EACCES || errno == ENOENT ||
			    errno == ENXIO)
				continue;
			fatal("unable to open a device", dev_name);
		}
		r= ioctl(fd, NWIOGIPCONF, (char *)&ip_conf);
		if (r == -1)
			fatal("NWIOGIPCONF failed", dev_name);

		iftab[i]= ip_conf.nwic_ipaddr;
		close(fd);

		for (j= 0; j<i; j++)
		{
			if (iftab[j] == iftab[i])
				fatal("duplicate address", dev_name);
		}
	}
	return 0;
}

static char *get_ifname(addr)
ipaddr_t addr;
{
	static char name[7];	/* ipXXXX */
	int i;

	for (i= 0; i<N_IF; i++)
	{
		if (iftab[i] != addr)
			continue;
		sprintf(name, "ip%d", i);
		return name;
	}

	return inet_ntoa(addr);
}

/*
 * Two fixed arguments rather than the donor's varargs: there is no <stdarg.h>
 * here, and every call site had a message and one name to put in it.
 */
static fatal(what, name)
char *what;
char *name;
{
	fprintf(stderr, "%s: %s: %s: %s\n", prog_name, what, name,
		strerror(errno));
	exit(1);
}

static usage()
{
	fprintf(stderr, "Usage: %s [-i|-o] [-a] [-I <ip-device>]\n",
		prog_name);
	exit(1);
}
