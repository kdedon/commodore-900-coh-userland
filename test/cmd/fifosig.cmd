# fifosig.cmd -- does a caught signal during a blocking FIFO read or open
# corrupt the kernel's reader/writer counts?
#
#	hostbuild/emu-run.sh test/cmd/fifosig.cmd
#
# The verdict is not a line of output but the machine's survival: on a kernel
# with the bug the close in phase 1 panics ("Out of sync IPR in pclose") and
# phase 2's exit panics in fdclose(), so a transcript that stops at "about to
# close" or "about to exit" IS the result.  A run that reaches "PASS -- released
# its descriptor too" and returns to the shell has passed both.
#
# The watchdog line is the third outcome: neither PASS nor panic, but a wait
# that no signal ever ended.  It reports and exits 1 rather than hanging.
#
# For test/cmd/run.sh: both phases' PASS lines, the second of which is only
# printed on the far side of the close, and then the exit path reached the
# prompt if the run finished.  A panic, the watchdog's FAIL, or an INCONCLUSIVE
# (the alarm never fired, so nothing was interrupted) is not a pass.
#% expect ^fifosig: PASS -- balanced across an interrupted read$
#% expect ^fifosig: PASS -- released its descriptor too$
#% reject ^Panic:
#% reject ^fifosig: FAIL
#% reject ^fifosig: INCONCLUSIVE
/bin/fifosig
