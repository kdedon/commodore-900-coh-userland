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

# What the objects read.  A traditional make discovers no include, so each
# object names the headers it takes from this tree and from ../include; the
# lists are transitive, which is why inet.h's own closure is a variable.
H	= ../include
INETH	= $(H)/ansi.h $(H)/errno.h $(H)/minix/com.h \
		$(H)/minix/config.h $(H)/minix/const.h $(H)/minix/ioctl.h \
		$(H)/minix/syslib.h $(H)/minix/type.h \
		$(H)/net/gen/eth_hdr.h $(H)/net/gen/eth_io.h \
		$(H)/net/gen/ether.h $(H)/net/gen/icmp.h \
		$(H)/net/gen/icmp_hdr.h $(H)/net/gen/in.h \
		$(H)/net/gen/ip_hdr.h $(H)/net/gen/ip_io.h \
		$(H)/net/gen/oneCsum.h $(H)/net/gen/psip_hdr.h \
		$(H)/net/gen/psip_io.h $(H)/net/gen/route.h \
		$(H)/net/gen/tcp.h $(H)/net/gen/tcp_hdr.h \
		$(H)/net/gen/tcp_io.h $(H)/net/gen/udp.h \
		$(H)/net/gen/udp_hdr.h $(H)/net/gen/udp_io.h \
		$(H)/net/hton.h $(H)/net/ioctl.h const.h inet.h \
		inet_config.h

buf.o: $(INETH) generic/assert.h generic/buf.h generic/type.h
inet_config.o: $(H)/errno.h $(H)/minix/config.h $(H)/minix/syslib.h \
		$(H)/minix/type.h inet_config.h
mq.o: $(INETH) generic/assert.h mq.h
stacktrace.o: $(INETH)
coh_inet.o: $(INETH) generic/arp.h generic/assert.h generic/buf.h \
		generic/clock.h generic/eth.h generic/eth_int.h \
		generic/event.h generic/ip.h generic/psip.h generic/sr.h \
		generic/tcp.h generic/type.h generic/udp.h mq.h \
		osdep_eth.h
coh_clock.o: $(INETH) generic/assert.h generic/buf.h generic/clock.h \
		generic/type.h
coh_sr.o: $(INETH) $(H)/coh_fdc.h $(H)/inet_ipc.h generic/assert.h \
		generic/buf.h generic/sr.h generic/type.h
coh_fdc.o: $(H)/coh_fdc.h
coh_eth.o: $(INETH) generic/assert.h generic/buf.h generic/clock.h \
		generic/eth.h generic/eth_int.h generic/event.h \
		generic/sr.h generic/type.h osdep_eth.h
coh_osdep.o: $(INETH)
generic/arp.o: $(INETH) generic/arp.h generic/assert.h generic/buf.h \
		generic/clock.h generic/eth.h generic/io.h generic/sr.h \
		generic/type.h
generic/eth.o: $(INETH) generic/assert.h generic/buf.h generic/clock.h \
		generic/eth.h generic/eth_int.h generic/event.h \
		generic/io.h generic/sr.h generic/type.h osdep_eth.h
generic/event.o: $(INETH) generic/assert.h generic/event.h
generic/icmp.o: $(INETH) generic/assert.h generic/buf.h generic/event.h \
		generic/icmp.h generic/icmp_lib.h generic/io.h \
		generic/ip.h generic/ip_int.h generic/ipr.h generic/type.h
generic/io.o: $(INETH) generic/io.h
generic/ip.o: $(INETH) generic/arp.h generic/assert.h generic/buf.h \
		generic/clock.h generic/eth.h generic/event.h \
		generic/icmp.h generic/icmp_lib.h generic/io.h \
		generic/ip.h generic/ip_int.h generic/ipr.h generic/sr.h \
		generic/type.h
generic/ip_eth.o: $(INETH) generic/arp.h generic/assert.h generic/buf.h \
		generic/clock.h generic/eth.h generic/event.h generic/ip.h \
		generic/ip_int.h generic/type.h
generic/ip_ioctl.o: $(INETH) generic/arp.h generic/assert.h generic/buf.h \
		generic/clock.h generic/event.h generic/icmp_lib.h \
		generic/ip.h generic/ip_int.h generic/ipr.h generic/type.h
generic/ip_lib.o: $(INETH) generic/assert.h generic/buf.h generic/event.h \
		generic/io.h generic/ip.h generic/ip_int.h generic/type.h
generic/ip_ps.o: $(INETH) generic/assert.h generic/buf.h generic/event.h \
		generic/ip.h generic/ip_int.h generic/psip.h \
		generic/type.h
generic/ip_read.o: $(INETH) generic/assert.h generic/buf.h generic/clock.h \
		generic/event.h generic/icmp_lib.h generic/io.h \
		generic/ip.h generic/ip_int.h generic/ipr.h generic/type.h
generic/ip_write.o: $(INETH) generic/arp.h generic/assert.h generic/buf.h \
		generic/clock.h generic/eth.h generic/event.h \
		generic/icmp_lib.h generic/io.h generic/ip.h \
		generic/ip_int.h generic/ipr.h generic/type.h
generic/ipr.o: $(INETH) generic/assert.h generic/buf.h generic/clock.h \
		generic/event.h generic/io.h generic/ip.h generic/ip_int.h \
		generic/ipr.h generic/type.h
generic/oneC_sum.o: $(H)/ansi.h $(H)/net/gen/oneCsum.h
generic/psip.o: $(INETH) generic/assert.h generic/buf.h generic/event.h \
		generic/ip.h generic/ip_int.h generic/psip.h generic/sr.h \
		generic/type.h
generic/tcp.o: $(INETH) generic/assert.h generic/buf.h generic/clock.h \
		generic/event.h generic/io.h generic/ip.h generic/sr.h \
		generic/tcp.h generic/tcp_int.h generic/type.h
generic/tcp_lib.o: $(INETH) generic/assert.h generic/buf.h generic/clock.h \
		generic/event.h generic/io.h generic/tcp_int.h \
		generic/type.h
generic/tcp_recv.o: $(INETH) generic/assert.h generic/buf.h \
		generic/clock.h generic/event.h generic/io.h generic/tcp.h \
		generic/tcp_int.h generic/type.h
generic/tcp_send.o: $(INETH) generic/assert.h generic/buf.h \
		generic/clock.h generic/event.h generic/io.h generic/ip.h \
		generic/tcp.h generic/tcp_int.h generic/type.h
generic/udp.o: $(INETH) generic/assert.h generic/buf.h generic/clock.h \
		generic/icmp_lib.h generic/io.h generic/ip.h generic/sr.h \
		generic/type.h generic/udp.h

.c.o:
	$(CC) $(CFLAGS) $(XCFLAGS) -c -o $@ $<

.s.o:
	$(AS) -o $@ $<

clean:
	rm -f inet $(CORE) $(OSDEP)
