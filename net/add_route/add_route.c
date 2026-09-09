/*
 * add_route / del_route -- add or remove an entry in the IP routing table.
 *
 * From Minix 2.0.4 (vmd/cmd/simple/add_route.c, Philip Homburg).  Which of the
 * two it is comes from argv[0], so the same program is installed under both
 * names.
 *
 *	add_route -g <gateway> [-d <dest>[/<len>] [-n <netmask>]] [-m <metric>]
 *	del_route -g <gateway> [-d <dest>[/<len>] [-n <netmask>]] [-D]
 *
 * With no -d the destination is 0.0.0.0/0, which is how the stack spells the
 * default route (ipr_add_oroute).  -i works on the INPUT table, which decides
 * which source addresses are accepted on an interface, and there -d and -n are
 * required.
 *
 * ifconfig sets the address and, given a third argument, one default route;
 * this is the general form, and the only way to remove a route again.
 */

#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/hton.h>
#include <net/netlib.h>
#include <net/ioctl.h>
#include <net/gen/netdb.h>
#include <net/gen/in.h>
#include <net/gen/inet.h>
#include <net/gen/route.h>
#include <net/gen/socket.h>
#include <net/gen/ip_io.h>

#define ADD	0
#define DEL	1

extern int optind;
extern char *optarg;

static char *prog_name;
static int action;

static usage();
static int name_to_ip();
static int parse_cidr();

main(argc, argv)
int argc;
char *argv[];
{
	ipaddr_t gateway, destination, netmask, defaultmask;
	unsigned high_byte;
	nwio_route_t route;
	int ip_fd, itab;
	int r;
	int metric;
	int req;
	char *check;
	char *ip_device;
	char *netmask_str, *metric_str, *destination_str, *gateway_str;
	int c;
	char *d_arg, *g_arg, *m_arg, *n_arg, *I_arg;
	int i_flag, o_flag, D_flag, v_flag;
	int cidr;

	prog_name= strrchr(argv[0], '/');
	if (prog_name == (char *)0)
		prog_name= argv[0];
	else
		prog_name++;

	if (strcmp(prog_name, "del_route") == 0)
		action= DEL;
	else
		action= ADD;

	defaultmask= 0;
	i_flag= 0;
	o_flag= 0;
	D_flag= 0;
	v_flag= 0;
	g_arg= (char *)0;
	d_arg= (char *)0;
	m_arg= (char *)0;
	n_arg= (char *)0;
	I_arg= (char *)0;
	while ((c= getopt(argc, argv, "iovDg:d:m:n:I:?")) != -1)
	{
		switch(c)
		{
		case 'i':
			if (i_flag)
				usage();
			i_flag= 1;
			break;
		case 'o':
			if (o_flag)
				usage();
			o_flag= 1;
			break;
		case 'v':
			if (v_flag)
				usage();
			v_flag= 1;
			break;
		case 'D':
			if (D_flag)
				usage();
			D_flag= 1;
			break;
		case 'g':
			if (g_arg)
				usage();
			g_arg= optarg;
			break;
		case 'd':
			if (d_arg)
				usage();
			d_arg= optarg;
			break;
		case 'm':
			if (m_arg)
				usage();
			m_arg= optarg;
			break;
		case 'n':
			if (n_arg)
				usage();
			n_arg= optarg;
			break;
		case 'I':
			if (I_arg)
				usage();
			I_arg= optarg;
			break;
		default:
			usage();
		}
	}
	if (optind != argc)
		usage();
	if (i_flag && o_flag)
		usage();
	itab= i_flag;

	if (i_flag)
	{
		if (g_arg == (char *)0 || d_arg == (char *)0 ||
		    m_arg == (char *)0)
			usage();
	}
	else
	{
		if (g_arg == (char *)0 ||
		    (d_arg == (char *)0 && n_arg != (char *)0))
			usage();
	}

	gateway_str= g_arg;
	destination_str= d_arg;
	metric_str= m_arg;
	netmask_str= n_arg;
	ip_device= I_arg;

	if (!name_to_ip(gateway_str, &gateway))
	{
		fprintf(stderr, "%s: unknown host '%s'\n", prog_name,
			gateway_str);
		exit(1);
	}

	destination= 0;
	netmask= 0;
	cidr= 0;

	if (destination_str)
	{
		/*
		 * The donor also consulted getnetbyname().  There is no
		 * networks database in this system -- netdb.c implements hosts
		 * and services only -- so a name here is a HOST name, and a
		 * network is written as a dotted quad or in CIDR form.
		 */
		if (parse_cidr(destination_str, &destination, &netmask))
			cidr= 1;
		else if (inet_aton(destination_str, &destination))
			;
		else if (!name_to_ip(destination_str, &destination))
		{
			fprintf(stderr, "%s: unknown network/host '%s'\n",
				prog_name, destination_str);
			exit(1);
		}
		/* The Z8001 is big-endian, so the first byte of an address in
		 * network order is its high byte. */
		high_byte= *(unsigned char *)&destination;
		if (!(high_byte & 0x80))		/* class A or 0	*/
		{
			if (destination)
				defaultmask= HTONL(0xff000000L);
		}
		else if (!(high_byte & 0x40))		/* class B	*/
		{
			defaultmask= HTONL(0xffff0000L);
		}
		else if (!(high_byte & 0x20))		/* class C	*/
		{
			defaultmask= HTONL(0xffffff00L);
		}
		else					/* class D up	*/
		{
			fprintf(stderr, "%s: warning: martian address '%s'\n",
				prog_name, inet_ntoa(destination));
			defaultmask= HTONL(0xffffffffL);
		}
		if (destination & ~defaultmask)
			defaultmask= HTONL(0xffffffffL);	/* host route */
		if (!cidr)
			netmask= defaultmask;
	}

	if (netmask_str)
	{
		if (cidr)
			usage();
		if (inet_aton(netmask_str, &netmask) == 0)
		{
			fprintf(stderr, "%s: illegal netmask '%s'\n",
				prog_name, netmask_str);
			exit(1);
		}
	}

	if (metric_str)
	{
		metric= (int)strtol(metric_str, &check, 0);
		if (check[0] != '\0' || metric < 1)
		{
			fprintf(stderr, "%s: illegal metric %s\n",
				prog_name, metric_str);
			exit(1);
		}
	}
	else
		metric= 1;

	if (!ip_device)
		ip_device= getenv("IP_DEVICE");
	if (!ip_device)
		ip_device= IP_DEVICE;

	ip_fd= open(ip_device, O_RDWR);
	if (ip_fd == -1)
	{
		fprintf(stderr, "%s: unable to open '%s': %s\n",
			prog_name, ip_device, strerror(errno));
		exit(1);
	}

	if (v_flag)
	{
		printf("%s %s route to %s ",
			action == ADD ? "adding" : "deleting",
			itab ? "input" : "output",
			inet_ntoa(destination));
		printf("with netmask %s ", inet_ntoa(netmask));
		printf("using gateway %s", inet_ntoa(gateway));
		if (itab && action == ADD)
			printf(" at distance %d", metric);
		printf("\n");
	}

	route.nwr_ent_no= 0;
	route.nwr_ent_count= 0;
	route.nwr_dest= destination;
	route.nwr_netmask= netmask;
	route.nwr_gateway= gateway;
	route.nwr_dist= (u32_t)(action == ADD ? metric : 0);
	route.nwr_flags= (action == DEL && D_flag) ? 0 : NWRF_STATIC;
	route.nwr_pref= 0;
	route.nwr_mtu= 0;
	route.nwr_ifaddr= 0;

	if (action == ADD)
		req= itab ? NWIOSIPIROUTE : NWIOSIPOROUTE;
	else
		req= itab ? NWIODIPIROUTE : NWIODIPOROUTE;
	r= ioctl(ip_fd, req, (char *)&route);
	if (r == -1)
	{
		fprintf(stderr, "%s: NWIO%cIP%cROUTE: %s\n",
			prog_name,
			action == ADD ? 'S' : 'D',
			itab ? 'I' : 'O',
			strerror(errno));
		exit(1);
	}
	exit(0);
}

static usage()
{
	fprintf(stderr,
		"Usage: %s [-o] -g gw [-d dst [-n netmask]] [-I ipdev] [-v]\n",
		prog_name);
	fprintf(stderr,
		"       %s -i -g gw -d dst [-n netmask] -m metric [-v]\n",
		prog_name);
	fprintf(stderr, "       <dst> may be in CIDR notation\n");
	exit(1);
}

/*
 * A name or a dotted quad to an address.  inet_aton() first, because
 * gethostbyname() reads /etc/hosts and would otherwise be asked about every
 * literal address.
 */
static int name_to_ip(name, addr)
char *name;
ipaddr_t *addr;
{
	struct hostent *hostent;

	if (!inet_aton(name, addr))
	{
		if ((hostent= gethostbyname(name)) == (struct hostent *)0)
			return 0;
		if (hostent->h_addrtype != AF_INET)
			return 0;
		if (hostent->h_length != sizeof(*addr))
			return 0;
		memcpy((char *)addr, hostent->h_addr, sizeof(*addr));
	}
	return 1;
}

/* "10.0.0.0/8" -> address and mask.  Returns 0 for anything without a slash,
 * leaving the string as it found it. */
static int parse_cidr(cidr, addr, mask)
char *cidr;
ipaddr_t *addr;
ipaddr_t *mask;
{
	char *slash, *check;
	ipaddr_t a;
	int ok;
	unsigned long len;

	if ((slash= strchr(cidr, '/')) == (char *)0)
		return 0;

	*slash++= 0;
	ok= 1;

	if (!inet_aton(cidr, &a))
		ok= 0;

	len= strtoul(slash, &check, 10);
	if (check == slash || *check != 0 || len > 32)
		ok= 0;

	*--slash= '/';
	if (!ok)
		return 0;
	*addr= a;
	*mask= htonl(len == 0 ? (ipaddr_t)0 :
		((ipaddr_t)0xFFFFFFFFL << (32-len)) & (ipaddr_t)0xFFFFFFFFL);
	return 1;
}
