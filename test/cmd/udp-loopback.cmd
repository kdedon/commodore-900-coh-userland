# udp-loopback.cmd -- a UDP datagram round trip with both ends on this machine.
#
#	hostbuild/emu-run.sh test/cmd/udp-loopback.cmd
#
# ip_write loops a packet addressed to its own interface back internally, so
# this needs no wire, no slip and no peer.  What it checks is ADDRESSING:
# recvfrom() must report the port the datagram actually came from, which is why
# each datagram carries a udp_io_hdr rather than relying on fixed peer options.
#
# rc.net already started the inet daemon and set 10.0.0.2; a second daemon
# would hang.
#% needs testing net
#% expect ^udpecho: B got \[.*\] from port 7001$
#% expect ^udpecho: PASS$
#% reject ^udpecho: (FAIL|WRONG SENDER)
#% reject failed errno
#% reject Segmentation violation
#% reject ^Panic:
/etc/ifconfig
/bin/udpecho
sleep 5
