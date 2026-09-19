# udp-loopback.cmd -- a UDP datagram round trip with both ends on this machine.
#
#	hostbuild/emu-run.sh tests/cmd/udp-loopback.cmd
#
# ip_write loops a packet addressed to its own interface back internally, so
# this needs no wire, no slip and no peer.  What it checks is ADDRESSING:
# recvfrom() must report the port the datagram actually came from, which is why
# each datagram carries a udp_io_hdr rather than relying on fixed peer options.
#
# THE GUEST IS MULTI USER, and rc.net has already made /dev/inet, started the
# inet daemon and given the interface 10.0.0.2, so this file starts none of
# them.  It used to, from single user, and on a multi-user boot that is a
# SECOND inet daemon reading the same /dev/inet: the `ifconfig' after it then
# never returned, and the run stopped there.  Ports 7001 and 7002 are udpecho's
# own and nothing rc.net starts holds them.
#
# For test/cmd/run.sh: udpecho's own verdict, and the port B saw the datagram
# come from -- the addressing this file is about.
#% expect ^udpecho: B got \[.*\] from port 7001$
#% expect ^udpecho: PASS$
#% reject ^udpecho: (FAIL|WRONG SENDER)
#% reject failed errno
#% reject Segmentation violation
#% reject ^Panic:
/etc/ifconfig
/bin/udpecho
sleep 5
