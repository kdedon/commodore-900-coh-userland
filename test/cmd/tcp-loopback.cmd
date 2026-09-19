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
# THE GUEST IS MULTI USER, and rc.net has already made /dev/inet, started the
# inet daemon and given the interface 10.0.0.2, so this file starts none of
# them.  It used to, from single user, and on a multi-user boot that is a
# SECOND inet daemon reading the same /dev/inet: the `ifconfig' after it then
# never returned, and the run stopped there.  rc.net's inetd also holds port 7
# for its internal echo, so echoserver listens on 7007 -- the answer has to
# come from the server this file starts, not from the switchboard.
#
# echoclient needs the sleep, because nothing makes it wait for echoserver's
# listen().  echoserver is given a 600-second deadline rather than its default
# 60: the console is typed one character at a time, paced against the SCC
# receive FIFO, and the two lines between the server's start and the client's
# connect are more than 60 seconds of GUEST time -- at the default the server's
# own watchdog ends it first, and the client's `connect failed' is then about
# the typing and not about TCP.
#
# For test/cmd/run.sh: both ends' own verdicts.  echoserver runs in the
# background, so its lines can land after a prompt or among the echo of what is
# being typed; its expects allow anything in front of them.
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
