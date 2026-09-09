# fifosig.cmd -- does a caught signal during a blocking FIFO read or open
# corrupt the kernel's reader/writer counts?
#
#	hostbuild/emu-run.sh tests/cmd/fifosig.cmd
#
# The verdict is not a line of output but the machine's survival: on a kernel
# with the bug the close in phase 1 panics ("Out of sync IPR in pclose") and
# phase 2's exit panics in fdclose(), so a transcript that stops at "about to
# close" or "about to exit" IS the result.  A run that reaches "PASS -- released
# its descriptor too" and returns to the shell has passed both.
#
# The watchdog line is the third outcome: neither PASS nor panic, but a wait
# that no signal ever ended.  It reports and exits 1 rather than hanging.
/bin/fifosig
