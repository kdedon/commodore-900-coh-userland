# pollpipe.cmd -- does select() wake for a write to an anonymous pipe?
#
#	hostbuild/emu-run.sh tests/cmd/pollpipe.cmd
#
# Case A (data already there) is expected to pass and is the control; case B
# (written while select() is blocked) is the one under suspicion.  A run that
# prints "4 child forked" and stops is case B hanging -- which is the finding,
# not a broken test.
#
# For test/cmd/run.sh: the control, case B, and the program's own verdict, and
# none of its FAIL lines (the watchdog's included).
#% expect ^3 case A ok: select saw 1, read 1$
#% expect ^5 case B ok: select saw 1, read 4$
#% expect ^PASS pollpipe$
#% reject ^FAIL
#% reject ^Panic:
#% wait 300
/bin/pollpipe
