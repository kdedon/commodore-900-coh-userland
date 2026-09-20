# tcp-loopback.cmd -- TCP with both ends on this machine.
#
#	hostbuild/emu-run.sh test/cmd/tcp-loopback.cmd
#
# No serial line, no slip, no host peer: the stack routes a connection to its
# own address internally.  A pass says the TCP state machine, libsocket and the
# daemon channel are sound, and narrows any wire-side failure to the wire.  A
# failure says the opposite, and is reproducible in half a minute instead of the
# twenty the simulator needs.
#
# rc.net already started the inet daemon and set 10.0.0.2; a second daemon
# would hang.  Its inetd holds port 7, so echoserver listens on 7007.
#
# echoclient needs the sleep: nothing makes it wait for echoserver's listen().
# echoserver gets 600 seconds, not 60, because typing the lines before the
# connect takes longer than that in guest time.
#
# echoserver runs in the background, so its expects allow text in front.
#% needs testing net
#% expect ^echoclient: PASS -- [0-9]+ bytes echoed intact$
#% expect .*echoserver: listening on port 7007
#% expect .*echoserver: done, [1-9][0-9]* bytes echoed -- PASS
#% reject echo(client|server): .*FAIL
#% reject connect failed
#% reject Segmentation violation
#% reject ^Panic:
/etc/ifconfig
/bin/echoserver 7007 600 &
sleep 5
/bin/echoclient 10.0.0.2 7007
sleep 5
