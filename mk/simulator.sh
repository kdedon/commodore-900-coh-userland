# mk/simulator.sh -- resolve the full-system simulator, `c900sim'.  SOURCE
# this file (`. "$C900_ROOT/mk/simulator.sh"'); do not exec it.
#
# c900sim is a full-machine simulator with an HTTP control endpoint: it models
# the video boards, both SCC channels, the disk controller and the keyboard,
# and it can be single-stepped.  It is a development instrument, not a build
# tool: no build, gate or released artifact needs it, and it is not published
# anywhere this file can point at -- so the only search is $PATH.
#
#   $SIM   the c900sim binary.  Unset, a `c900sim' on $PATH is used.
#
# For running commands on the target without a simulator, use emu-run.sh: it
# needs only a commodore-900-emulator checkout (mk/emulator.sh) and lands in a
# single-user root shell.

if [ -z "${SIM:-}" ]; then
	SIM=$(command -v c900sim 2>/dev/null) || SIM=
fi
: "${SIM:=}"

# The caller's name for messages -- see mk/emulator.sh.
_c9simwho=$(basename "$(dirname "$0")")/$(basename "$0")

# sim_need <what it is needed for>: refuse, naming the variable and what the
# thing is, and pointing at the harness that does not need it.
sim_need() {
	if [ -n "$SIM" ] && [ -x "$SIM" ]; then
		return 0
	fi
	{
		if [ -n "$SIM" ]; then
			echo "${_c9simwho}: no simulator at SIM=$SIM"
		else
			echo "${_c9simwho}: \$SIM is not set, and a c900sim binary is"
			echo "  needed to $1."
		fi
		echo "  c900sim is the full-system simulator (video, both SCC channels,"
		echo "  HTTP debug endpoint).  It is a development instrument: no build,"
		echo "  no gate and no released image in this repository needs it, and it"
		echo "  is not one of the checkouts README.md lists.  Set \$SIM to"
		echo "  a c900sim binary if you have one, or put one on \$PATH."
		echo "  To run commands on the target without it, use"
		echo "    hostbuild/emu-run.sh <cmdfile> [dist]"
		echo "  which needs only a commodore-900-emulator checkout."
	} >&2
	exit 2
}
# end of simulator.sh
