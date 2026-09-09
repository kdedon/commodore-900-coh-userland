# tcp-loopback.cmd -- TCP with both ends on this machine.
#
#	hostbuild/emu-run.sh tests/cmd/tcp-loopback.cmd
#
# No serial line, no slip, no host peer: the stack routes a connection to its
# own address internally.  A pass says the TCP state machine, libsocket and the
# daemon channel are sound, and narrows any wire-side failure to the wire.  A
# failure says the opposite, and is reproducible in half a minute instead of the
# twenty the simulator needs.
#
# ifconfig needs no sleep before it: its first act is open("/dev/inet",
# O_WRONLY), which blocks until the inet daemon opens the same pipe for reading.  That IS
# the rendezvous.  echoclient does need one, because nothing makes it wait for
# echoserver's listen().
/etc/mknod /dev/inet p
/etc/inet &
/etc/ifconfig 10.0.0.2 255.255.255.0
/etc/ifconfig
/bin/echoserver 7 &
sleep 5
/bin/echoclient 10.0.0.2 7
sleep 5
