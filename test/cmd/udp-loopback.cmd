# udp-loopback.cmd -- a UDP datagram round trip with both ends on this machine.
#
#	hostbuild/emu-run.sh tests/cmd/udp-loopback.cmd
#
# ip_write loops a packet addressed to its own interface back internally, so
# this needs no wire, no slip and no peer.  What it checks is ADDRESSING:
# recvfrom() must report the port the datagram actually came from, which is why
# each datagram carries a udp_io_hdr rather than relying on fixed peer options.
/etc/mknod /dev/inet p
/etc/inet &
/etc/ifconfig 10.0.0.2 255.255.255.0
/etc/ifconfig
/bin/udpecho
sleep 5
