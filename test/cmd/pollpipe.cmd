# pollpipe.cmd -- does select() wake for a write to an anonymous pipe?
#
#	hostbuild/emu-run.sh tests/cmd/pollpipe.cmd
#
# Case A (data already there) is expected to pass and is the control; case B
# (written while select() is blocked) is the one under suspicion.  A run that
# prints "4 child forked" and stops is case B hanging -- which is the finding,
# not a broken test.
/bin/pollpipe
