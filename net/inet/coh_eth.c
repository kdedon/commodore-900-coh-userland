/*
inet/coh_eth.c -- COHERENT osdep for the Ethernet link layer.

Replaces the Minix mnx_eth.c.  In Minix the link layer spoke the DL_* data-link
*message* protocol to a separate Ethernet driver process.  COHERENT is
monolithic: the inet stack is a userland daemon that reaches each NIC through a
raw-frame character device /dev/eth<N> (N = interface number, base /dev/eth0),
served by the in-kernel LANCE driver (drv/lance.c).  The
data-link messages collapse to ordinary file operations:

	DL_INIT       ->  open("/dev/ethN") + ioctl NWIOGETHSTAT (station addr)
	DL_WRITE[V]   ->  write() one frame (the acc chain, packed contiguous)
	DL_READV      ->  read() one frame  -> eth_arrive()
	DL_*_REQ mode ->  ioctl NWIOSETHOPT (broadcast/multicast/promiscuous)
	DL_GETSTAT    ->  ioctl NWIOGETHSTAT

For a SLIP-only configuration eth_conf_nr is 0, so osdep_eth_init registers no
ports and this file is inert -- the stack runs over psip/serial alone.

Receive readiness: eth_recv() reads one frame and delivers it upward.  The inet
daemon's select()/poll loop owns the file descriptors and must call eth_recv()
when a port's etp_fd becomes readable (the single remaining daemon hook; marked
DAEMON-HOOK below).  setup_read() records that the port wants a frame.
*/

#include "inet.h"
#include "osdep_eth.h"
#include "generic/type.h"
#include "generic/assert.h"
#include "generic/buf.h"
#include "generic/clock.h"
#include "generic/eth.h"
#include "generic/eth_int.h"
#include "generic/sr.h"

#include <fcntl.h>

THIS_FILE

FORWARD void setup_read ARGS(( eth_port_t *eth_port ));

PUBLIC void osdep_eth_init()
{
	int i, fd;
	struct eth_conf *ecp;
	eth_port_t *eth_port;
	char devname[16];
	nwio_ethstat_t ethstat;

	for (i= 0, eth_port= eth_port_table, ecp= eth_conf;
		i<eth_conf_nr; i++, eth_port++, ecp++)
	{
		/* /dev/eth<ifno>, base eth0 */
		strcpy(devname, "/dev/eth0");
		devname[8]= '0' + ecp->ec_ifno;

		fd= open(devname, O_RDWR);
		if (fd < 0)
		{
#if !CRAMPED
			printf("osdep_eth_init: cannot open %s\n", devname);
#endif
			continue;
		}

		/* The kernel driver read the station address from the card's
		 * address PROM; fetch it. */
		if (ioctl(fd, NWIOGETHSTAT, &ethstat) < 0)
		{
#if !CRAMPED
			printf("osdep_eth_init: NWIOGETHSTAT failed on %s\n",
				devname);
#endif
			close(fd);
			continue;
		}

		eth_port->etp_osdep.etp_fd= fd;
		eth_port->etp_osdep.etp_ifno= ecp->ec_ifno;
		eth_port->etp_osdep.etp_recvconf= 0;
		ev_init(&eth_port->etp_osdep.etp_recvev);

		eth_port->etp_ethaddr= ethstat.nwes_addr;

		sr_add_minor(if2minor(ecp->ec_ifno, ETH_DEV_OFF),
			i, eth_open, eth_close, eth_read,
			eth_write, eth_ioctl, eth_cancel);

		eth_port->etp_flags |= EPF_ENABLED;
		eth_port->etp_wr_pack= 0;
		eth_port->etp_rd_pack= 0;
		setup_read(eth_port);
	}
}

PUBLIC void eth_write_port(eth_port, pack)
eth_port_t *eth_port;
acc_t *pack;
{
	int fd, size;
	char *data;

	fd= eth_port->etp_osdep.etp_fd;

	/* Pad runts and make the frame one contiguous buffer, then hand its
	 * bytes to the driver in a single write(). */
	pack= bf_packIffLess(pack, ETH_MIN_PACK_SIZE);
	pack= bf_pack(pack);
	eth_port->etp_wr_pack= pack;
	size= bf_bufsize(pack);
	data= ptr2acc_data(pack);

	if (write(fd, data, size) != size)
	{
#if !CRAMPED
		printf("eth_write_port: write error on eth%d\n",
			eth_port->etp_osdep.etp_ifno);
#endif
	}
	/* write() is synchronous on COHERENT: the frame is gone on return. */
	bf_afree(eth_port->etp_wr_pack);
	eth_port->etp_wr_pack= NULL;
}

PUBLIC void eth_set_rec_conf(eth_port, flags)
eth_port_t *eth_port;
u32_t flags;
{
	nwio_ethopt_t ethopt;

	eth_port->etp_osdep.etp_recvconf= flags;

	ethopt.nweo_flags= NWEO_COPY | NWEO_EN_LOC | NWEO_TYPEANY;
	if (flags & NWEO_EN_BROAD)
		ethopt.nweo_flags |= NWEO_EN_BROAD;
	if (flags & NWEO_EN_MULTI)
		ethopt.nweo_flags |= NWEO_EN_MULTI;
	if (flags & NWEO_EN_PROMISC)
		ethopt.nweo_flags |= NWEO_EN_PROMISC;

	if (ioctl(eth_port->etp_osdep.etp_fd, NWIOSETHOPT, &ethopt) < 0)
	{
#if !CRAMPED
		printf("eth_set_rec_conf: NWIOSETHOPT failed on eth%d\n",
			eth_port->etp_osdep.etp_ifno);
#endif
	}
}

PUBLIC int eth_get_stat(eth_port, eth_stat)
eth_port_t *eth_port;
eth_stat_t *eth_stat;
{
	nwio_ethstat_t ethstat;

	if (ioctl(eth_port->etp_osdep.etp_fd, NWIOGETHSTAT, &ethstat) < 0)
		return EGENERIC;
	*eth_stat= ethstat.nwes_stat;
	return OK;
}

/*
 * setup_read -- the port wants the next frame.  The frame itself is picked up
 * by eth_recv() once the daemon reports etp_fd readable (DAEMON-HOOK).
 */
FORWARD void setup_read(eth_port)
eth_port_t *eth_port;
{
	eth_port->etp_flags |= EPF_ENABLED;
}

/*
 * eth_recv -- read one frame from the device and deliver it to the link layer.
 * DAEMON-HOOK: the inet daemon's select()/poll loop calls this when
 * eth_port->etp_osdep.etp_fd is readable.
 */
PUBLIC void eth_recv(eth_port)
eth_port_t *eth_port;
{
	int count;
	acc_t *pack;
	char *data;

	pack= bf_memreq(ETH_MAX_PACK_SIZE);
	data= ptr2acc_data(pack);

	count= read(eth_port->etp_osdep.etp_fd, data, ETH_MAX_PACK_SIZE);
	if (count <= 0)
	{
		bf_afree(pack);
		return;
	}
	pack= bf_cut(pack, 0, count);
	eth_arrive(eth_port, pack, count);
}
