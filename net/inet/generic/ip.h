/*
ip.h

Copyright 1995 Philip Homburg
*/

#ifndef INET_IP_H
#define INET_IP_H

/* Prototypes */

struct acc;

void ip_prep ARGS(( void ));
void ip_init ARGS(( void ));
int  ip_open ARGS(( int port, int srfd,
	get_userdata_t get_userdata, put_userdata_t put_userdata,
	put_pkt_t put_pkt ));
int ip_ioctl ARGS(( int fd, ioreq_t req ));
int ip_read ARGS(( int fd, size_t count ));
int ip_write ARGS(( int fd, size_t count ));
int ip_send ARGS(( int fd, struct acc *data, size_t data_len ));

/* The interface address of an ip port, read live from ip_port_table.  Declared
 * here, and not only in the ip-internal header, because udp and tcp must read
 * it on every use rather than keep a copy of their own: `ifconfig' can change
 * it at any moment and there is no notification when it does.  The declaration
 * is load-bearing on this target -- an ipaddr_t is 32 bits and an undeclared
 * function returns a 16-bit int, so a caller that cannot see this prototype
 * silently truncates the address to its low half.
 */
ipaddr_t ip_get_ifaddr ARGS(( int ip_port_nr ));

#endif /* INET_IP_H */

/*
 * $PchId: ip.h,v 1.6 1996/05/07 20:49:28 philip Exp $
 */
