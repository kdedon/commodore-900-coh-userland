# Makefile -- COHERENT inet daemon (/etc/inet).
#
# The ported Minix 2.0.4 TCP/IP stack (generic/* + the IPC-free top-level files)
# plus the COHERENT osdep (coh_*): main loop, timers, client/device mux, the
# /dev/eth client.  Traditional make; builds natively on the C900 with cc/as.
# Cross-build on a host: make CC=ccz (see ../hostbuild).
#
# System headers (sys/*, netinet/*, errno.h, ...) come from /usr/include; the
# net-local headers (net/gen/*, minix/*, inet_ipc.h) from ../include.

# See the note in ../Makefile: the cross-build sets --no-builtin-rules, which
# also empties .SUFFIXES and would silently disable the .c.o rule below.
.SUFFIXES:
.SUFFIXES: .c .s .o

CC	= cc
AS	= as
CFLAGS	= -O -I. -I../include
# _SYSTEM: the stack is Minix code and wants the Minix convention of NEGATIVE
# internal error codes (callers test for < 0), which ../include/errno.h keys off
# _SYSTEM.  It belongs here and not in that header, which also shadows COHERENT's
# <errno.h> for the CLIENT side -- defining it there gave user programs a
# negative errno (see inet_chan.c's ichan_fail).
#
# Kept OUT of CFLAGS on purpose: the cross build replaces CFLAGS entirely (it
# needs absolute include paths, and -O is not a ccz option), so a flag the stack
# cannot compile correctly without must not live somewhere an override can drop.
XCFLAGS	= -D_SYSTEM

# The whole stack is larger than one 64K segment, so it needs large model.
# -i and -L are ld options that cc passes straight through (LDOPTs): -i =
# separated I/D, -L = large model (ld: "address wraparound, use -L").  These are
# ld flags, NOT the -Ldir library path of a Unix cc.
LFLAGS	= -i -L

# Portable protocol core: the TCP/IP stack itself, OS-independent.
CORE =	generic/arp.o generic/eth.o generic/event.o generic/icmp.o \
	generic/io.o generic/ip.o generic/ip_eth.o generic/ip_ioctl.o \
	generic/ip_lib.o generic/ip_ps.o generic/ipr.o generic/ip_read.o \
	generic/ip_write.o generic/oneC_sum.o generic/psip.o generic/tcp.o \
	generic/tcp_lib.o generic/tcp_recv.o generic/tcp_send.o generic/udp.o \
	buf.o inet_config.o mq.o stacktrace.o version.o

# COHERENT osdep: replaces Minix's inet.c/clock.c/sr.c/mnx_eth.c.  get_bp.o is
# assembly because a C function cannot read its own caller's frame pointer.
OSDEP =	coh_inet.o coh_clock.o coh_sr.o coh_fdc.o coh_eth.o coh_osdep.o get_bp.o

inet:	$(CORE) $(OSDEP)
	$(CC) $(LFLAGS) -o inet $(CORE) $(OSDEP)

.c.o:
	$(CC) $(CFLAGS) $(XCFLAGS) -c -o $@ $<

.s.o:
	$(AS) -o $@ $<

clean:
	rm -f inet $(CORE) $(OSDEP)
