# mk/emulator.sh -- resolve the Commodore 900 emulator.  SOURCE this file
# (`. "$C900_ROOT/mk/emulator.sh"'); do not exec it.
#
# The emulator is not in this repository; this tree consumes a checkout of
#
#	https://github.com/MichalPleban/commodore-900-emulator
#
# Two different things are wanted from that checkout:
#
#   bin/c900 --exec <l.out>	a PROCESS RUNNER.  Runs one linked Z8001
#				program against the host filesystem, which is
#				what lets a test run a Z8001 program this tree
#				built.
#   bin/c900 + rom/		a MACHINE, for booting an image and typing at
#				its console.
#
#   $C900_EMU	the c900 BINARY.  Unset, the first candidate that has a
#		bin/c900 in it wins: the release `make deps' unpacks
#		into deps/, then a `c900' on $PATH, then a checkout beside this
#		repository or beside one of its three enclosing directories,
#		then one inside a `repos/' directory beside it.  Either the
#		checkout or the binary inside it may be named: a directory is
#		resolved to its bin/c900.  mk/emulator.mk beside this file
#		searches the same list in the same order.
#   $C900_EMU_ROM	the firmware directory.  Defaults to `rom' beside the
#		binary's checkout, which is where the emulator keeps it.
#
# Sets $C900_EMU and $C900_EMU_ROM when a checkout is found, and leaves them
# EMPTY when none is.  Empty is not fatal here: the refusal belongs at the
# command that wanted the emulator, which is what emu_need is for.
#
# $C900_ROOT must already name this repository's root -- a sourced file cannot
# portably find its own path, and that is how this one finds the search list.

if [ -z "${C900_ROOT:-}" ] || [ ! -d "$C900_ROOT/mk" ]; then
	echo "emulator.sh: \$C900_ROOT must name the repository root before sourcing" >&2
	exit 2
fi

# Candidates, in order: the release deps/ holds, a c900 on $PATH, a
# checkout beside this repository or beside one of its three enclosing
# directories, then one inside a `repos/' directory beside it.  Walking outward
# is what lets the same list serve a side-by-side clone and a staging layout,
# where the sibling repositories sit under `repos/' and the emulator -- which is
# consumed, not staged -- sits outside it.
#
# THREE parents, not `until /'.  Three is what reaches the enclosing workspace
# from a repository staged at <workspace>/repos/<repo>, which is the layout this
# project uses; anything further out is not a sibling, it is a coincidence.  An
# unbounded walk finds another job's checkout on a CI runner, or whatever is in
# $HOME on a laptop, and reports a false success -- which is a failure this tree
# has already had.
#
# Normalised, because these paths are printed in the refusal message.
C900_EMU_SEARCH="$C900_ROOT/deps/commodore-900-emulator"
_c9p=$(command -v c900 2>/dev/null) &&
	C900_EMU_SEARCH="$C900_EMU_SEARCH $(dirname "$(dirname "$_c9p")")"
_c9d=$C900_ROOT
_c9n=0
while [ $_c9n -lt 3 ] && [ "$_c9d" != / ]; do
	_c9d=$(cd "$_c9d/.." && pwd)
	C900_EMU_SEARCH="$C900_EMU_SEARCH $_c9d/commodore-900-emulator"
	_c9n=$((_c9n + 1))
done
C900_EMU_SEARCH="$C900_EMU_SEARCH $C900_ROOT/repos/commodore-900-emulator"

# A caller's own spelling of the variable (EMU, EMUBIN -- documented in the
# harnesses that use them) wins over the search, in that order.
if [ -z "${C900_EMU:-}" ]; then
	C900_EMU=${EMU:-${EMUBIN:-}}
fi

# A directory names the checkout; a file names the binary.  Both spellings
# are accepted.
if [ -n "${C900_EMU:-}" ] && [ -d "$C900_EMU" ]; then
	C900_EMU="$C900_EMU/bin/c900"
fi

if [ -z "${C900_EMU:-}" ]; then
	for _c9e in $C900_EMU_SEARCH; do
		[ -x "$_c9e/bin/c900" ] || continue
		C900_EMU=$_c9e/bin/c900
		break
	done
fi
: "${C900_EMU:=}"

# The firmware, for the harnesses that boot a machine rather than run one
# program.  It sits beside bin/ in the same checkout.
if [ -z "${C900_EMU_ROM:-}" ] && [ -n "$C900_EMU" ]; then
	_c9e=$(dirname "$(dirname "$C900_EMU")")
	[ -d "$_c9e/rom" ] && C900_EMU_ROM=$_c9e/rom
fi
: "${C900_EMU_ROM:=}"

# The caller's name for messages: bare `run.sh' identifies nothing, so the
# containing directory comes with it (e.g. `hostbuild/emu-run.sh').
_c9who=$(basename "$(dirname "$0")")/$(basename "$0")

# emu_need <what it is needed for>: refuse, once, naming the variable, the
# repository and the paths tried.  Call it from the command that wants an
# emulator -- not at the top of a script that might not reach one.
emu_need() {
	if [ -n "$C900_EMU" ] && [ -x "$C900_EMU" ]; then
		return 0
	fi
	{
		if [ -n "$C900_EMU" ]; then
			echo "${_c9who:-$0}: no emulator at C900_EMU=$C900_EMU"
		else
			echo "${_c9who:-$0}: no emulator found, and it is needed to $1."
		fi
		echo "  Clone https://github.com/MichalPleban/commodore-900-emulator"
		echo "  and \`make' it, to one of:"
		for _c9e in $C900_EMU_SEARCH; do echo "    $_c9e"; done
		echo "  -- or put its c900 on \$PATH, or set C900_EMU to the"
		echo "  checkout or to its bin/c900."
		echo "  \`make deps DEP=emu' unpacks the release DEPS pins into"
		echo "  the deps/ path above."
		echo "  Nothing that only PACKS an image needs it:"
		echo "  \`sh test/image/build.sh' packs the test image without one."
	} >&2
	exit 2
}

# emu_need_rom <what it is needed for>: as emu_need, and also a firmware
# directory.  Booting a machine needs both; --exec needs only the binary.
emu_need_rom() {
	emu_need "$1"
	if [ ! -d "$C900_EMU_ROM" ]; then
		echo "${_c9who:-$0}: no emulator firmware at C900_EMU_ROM=$C900_EMU_ROM;" \
		     "set C900_EMU_ROM to the \`rom' directory of the" \
		     "commodore-900-emulator checkout (it is needed to $1)." >&2
		exit 2
	fi
	return 0
}

# _c9who is NOT unset: it is read inside emu_need, which runs long after this
# file has been sourced.
unset _c9e _c9up _c9d _c9n _c9p
# end of emulator.sh
