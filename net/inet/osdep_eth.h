/*
inet/osdep_eth.h -- COHERENT per-port Ethernet osdep state.

Replaces the Minix version (which held message/event/iovec state for the DL_*
data-link message protocol).  On COHERENT the inet daemon reaches each NIC
through a raw-frame char device /dev/eth<N>, so a port needs only its open file
descriptor, its interface number, the current receive mode, and a re-arm event
for the daemon's read loop.  See coh_eth.c.
*/

#ifndef INET__OSDEP_ETH_H
#define INET__OSDEP_ETH_H

#include "generic/event.h"

typedef struct osdep_eth_port
{
	int	etp_fd;		/* open /dev/eth<ifno>, or -1		*/
	int	etp_ifno;	/* interface number (device minor group) */
	int	etp_recvconf;	/* current NWEO_EN_* receive flags	*/
	event_t	etp_recvev;	/* re-arm-read event			*/
} osdep_eth_port_t;

#endif /* INET__OSDEP_ETH_H */
