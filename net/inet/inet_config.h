/*
inet/inet_config.h

Created:	Nov 11, 1992 by Philip Homburg

Defines values for configurable parameters. The structure definitions for
configuration information are also here.

Copyright 1995 Philip Homburg
*/

#ifndef INET__INET_CONFIG_H
#define INET__INET_CONFIG_H

#define ENABLE_ARP	1
#define ENABLE_IP	1
#define ENABLE_PSIP	1
#define ENABLE_TCP	1
#define ENABLE_UDP	1

/* Inet configuration file. */
#define PATH_INET_CONF	"/etc/inet.conf"

/* Up to this many network devices.  Every per-interface table in the stack is
 * sized from this -- TCP_FD_NR is 10*IP_PORT_MAX, IP_FD_NR is 8*IP_PORT_MAX, and
 * eth/psip/ip/arp all keep IP_PORT_MAX-element arrays -- so it is the single
 * biggest lever on the daemon's data size.
 *
 * The original `1*sizeof(char*)' is a stand-in for the word size: 4 on a 32-bit
 * machine, 2 on a 16-bit one.  That reasoning does not survive the Z8001, where
 * a pointer is 4 bytes because it is FAR (segment plus offset) while int is 2 and
 * the whole data segment is 64K.  Taken literally it sized every table for four
 * interfaces inside a 64K segment, leaving 1284 bytes for the heap; the first
 * allocation then ran off the end of the segment and aliased back onto the
 * daemon's own static data.
 *
 * This machine has one serial line for psip and one optional LANCE, so two.
 */
#define IP_PORT_MAX  2
extern int eth_conf_nr;		/* Number of ethernets */
extern int psip_conf_nr;	/* Number of Pseudo IP networks */
extern int ip_conf_nr;		/* Number of configured TCP/IP layers */

extern dev_t ip_dev;		/* Device number of /dev/ip */

struct eth_conf
{
	char *ec_task;		/* Kernel ethernet task name */
	u8_t ec_port;		/* Task port */
	u8_t ec_ifno;		/* Interface number of /dev/eth* */
};

struct psip_conf
{
	u8_t pc_ifno;		/* Interface number of /dev/psip* */
};

struct ip_conf
{
	u8_t ic_devtype;	/* Underlying device type: Ethernet / PSIP */
	u8_t ic_port;		/* Port of underlying device */
	u8_t ic_ifno;		/* Interface number of /dev/ip*, tcp*, udp* */
};

/* Types of networks. */
#define NETTYPE_ETH	1
#define NETTYPE_PSIP	2

/* To compute the minor device number for a device on an interface. */
#define if2minor(ifno, dev)	((ifno) * 8 + (dev))

/* Offsets of the minor device numbers within a group per interface. */
#define ETH_DEV_OFF	0
#define PSIP_DEV_OFF	0
#define IP_DEV_OFF	1
#define TCP_DEV_OFF	2
#define UDP_DEV_OFF	3

extern struct eth_conf eth_conf[IP_PORT_MAX];
extern struct psip_conf psip_conf[IP_PORT_MAX];
extern struct ip_conf ip_conf[IP_PORT_MAX];
void read_conf();
extern char *sbrk();
char *alloc();

#endif /* INET__INET_CONFIG_H */

/*
 * $PchId: inet_config.h,v 1.6 1998/10/23 20:14:28 philip Exp $
 */
