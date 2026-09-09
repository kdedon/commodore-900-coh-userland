/*
 * coh_ifconfig.c -- give an inet interface its IP address, and optionally a
 * default route, on COHERENT/Z8001.
 *
 *	ifconfig				show the interface
 *	ifconfig <address> <netmask> [<gateway>]	configure it
 *
 * /etc/inet.conf declares WHICH interfaces exist; it carries no addresses (the
 * psip branch of inet_config.c's read_conf() reads only the interface number).
 * In Minix the addresses come from a separate ifconfig run, and nothing else
 * supplies them -- so without this the stack comes up with an unnumbered
 * interface and cannot route.
 *
 * The ip device is served by the inet daemon rather than the kernel, so this
 * reaches it over the daemon's control channel (inet_chan.c) exactly as slip
 * reaches psip -- NOT by opening /dev/ip.  /dev/ip exists only so that
 * read_conf() can learn a major to reuse for its bookkeeping nodes.
 *
 * Setting an address is one NWIOSIPCONF; the default route is one
 * NWIOSIPOROUTE with dest and netmask both zero, which is how ipr_add_oroute()
 * spells "everything not otherwise matched".
 */
#include <sys/types.h>
#include <stdio.h>
#include <net/gen/in.h>
#include <net/gen/ip_io.h>
#include <net/gen/route.h>
#include <net/ioctl.h>
#include "inet_ipc.h"
#include "inet_chan.h"

#define IP_MINOR	1	/* if2minor(0, IP_DEV_OFF) -- ip interface 0 */

extern ipaddr_t inet_addr();
static show();
static dotted();

main(argc, argv)
int argc;
char **argv;
{
	/* static: struct ichan carries a 512-byte hold buffer for the pollable
	 * read path, and this machine's user stack will not take it as a local
	 * -- it faulted on entry, before main ran a line. */
	static struct ichan ip;
	nwio_ipconf_t conf;
	nwio_route_t route;
	ipaddr_t addr, mask, gw;

	if (argc != 1 && argc != 3 && argc != 4)
	{
		fprintf(stderr, "usage: ifconfig [address netmask [gateway]]\n");
		return 1;
	}

	if (ichan_open(&ip, IP_MINOR) < 0)
	{
		fprintf(stderr, "ifconfig: cannot reach the inet daemon\n");
		return 1;
	}

	if (argc == 1)
		return show(&ip);

	addr = inet_addr(argv[1]);
	mask = inet_addr(argv[2]);

	conf.nwic_flags = NWIC_IPADDR_SET | NWIC_NETMASK_SET;
	conf.nwic_ipaddr = addr;
	conf.nwic_netmask = mask;
	if (ichan_ioctl(&ip, NWIOSIPCONF, (char *)&conf, sizeof(conf)) < 0)
	{
		fprintf(stderr, "ifconfig: NWIOSIPCONF failed\n");
		return 1;
	}

	if (argc == 4)
	{
		gw = inet_addr(argv[3]);
		route.nwr_ent_no = 0;
		route.nwr_ent_count = 0;
		route.nwr_dest = 0;		/* 0/0 == the default route */
		route.nwr_netmask = 0;
		route.nwr_gateway = gw;
		route.nwr_dist = 1;
		route.nwr_flags = NWRF_STATIC;
		route.nwr_pref = 0;
		route.nwr_mtu = 0;
		route.nwr_ifaddr = addr;
		if (ichan_ioctl(&ip, NWIOSIPOROUTE, (char *)&route,
		    sizeof(route)) < 0)
		{
			fprintf(stderr, "ifconfig: NWIOSIPOROUTE failed\n");
			return 1;
		}
	}
	return 0;
}

/*
 * Report the interface's current address and netmask, read back from the
 * daemon.  This is the only way to confirm a configuration actually took: the
 * setting ioctl returning 0 says the daemon accepted the request, not that
 * ip_port ended up holding the address.
 *
 * NWIOGIPCONF is deliberately patient in the stack -- ip_ioctl suspends it until
 * an address exists rather than answering "unconfigured" -- so on a daemon that
 * has never been given one this waits instead of printing.
 */
static show(ip)
struct ichan *ip;
{
	nwio_ipconf_t conf;

	if (ichan_ioctl_get(ip, NWIOGIPCONF, (char *)&conf, sizeof(conf)) < 0)
	{
		fprintf(stderr, "ifconfig: NWIOGIPCONF failed\n");
		return 1;
	}
	printf("ip0: address ");
	dotted(conf.nwic_ipaddr);
	if (conf.nwic_flags & NWIC_NETMASK_SET)
	{
		printf(" netmask ");
		dotted(conf.nwic_netmask);
	}
	printf("\n");
	return 0;
}

/* inet_ntoa takes a struct in_addr by value, which is not declared by the
 * stack's own headers (it belongs to the socket veneer), so print the quad
 * directly rather than drag that declaration in. */
static dotted(a)
ipaddr_t a;
{
	printf("%d.%d.%d.%d",
		(int)((a >> 24) & 0xFF), (int)((a >> 16) & 0xFF),
		(int)((a >> 8) & 0xFF), (int)(a & 0xFF));
}
