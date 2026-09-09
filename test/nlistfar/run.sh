#!/bin/sh
# tests/nlistfar/run.sh -- build nlistfar and run it under the emulator's process
# runner (`c900 --exec'), which services its open/read/write/lseek against the
# host filesystem.  One guest process, a couple of seconds (it writes 80KB).
#
# The subject is libc/gen/nlist.c compiled for a 16-bit int, so the host
# cannot stand in for it: on a 32-bit host every span in the test fits an int
# and every assertion passes whatever the library does.  This is the
# discrimination instead, and it mutates the library rather than a stand-in --
# each MUTATE below compiles a copy of the real source with one type changed
# and links it ahead of libc, so the guest runs the mutated routine:
#
#	sh run.sh			the shipped library, all cases must pass
#	MUTATE=offunsigned sh run.sh	the table's file offset in 16-bit
#					unsigned
#	MUTATE=offint sh run.sh		the same offset in 16-bit signed
#	MUTATE=value16 sh run.sh	the symbol's value truncated to 16 bits:
#					every segmented address loses its
#					segment
#	MUTATE=valueint sh run.sh	the same through an int
#
# Both offset mutations lose every symbol in every file rather than only the
# ones past their own limit: fseek reads a long, and there are no prototypes
# here, so a 16-bit offset also pushes two bytes where four are read.
#
# Every mutation must make the run FAIL; a mutation that passes means the case
# it belongs to is not testing what it claims.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
ROOT="$OS"
GEN=$OS/libc/gen

C900_ROOT=$ROOT
. "$C900_ROOT/mk/emulator.sh"
. "$OS/hostbuild/toolchain.sh"
CCZ="$TC/ccz"

MUTATE=${MUTATE:-none}
emu_need "run nlistfar, which is a Z8001 binary"

# The library under test is the one in $TCB, so bring it up to date here: an
# edit to libc that has not been relinked is invisible to this run and the
# transcript looks like the answer.  NODEPS keeps it to libc -- without it the
# dist dependency fragment is remade and pulls in every other stamp.
make -C "$OS/hostbuild" NODEPS=1 libc > /dev/null || exit 2

WORK=$(mktemp -d) || exit 2
trap 'rm -rf "$WORK"' 0 1 2 15

# The mutated source, when there is one.  sed on the real file, so a mutation
# that no longer applies is caught here rather than passing quietly.
extra=""
mutate() {			# mutate <file> <sed script>
	src=$GEN/$1
	out=$WORK/$1
	sed "$2" "$src" > "$out" || exit 2
	if cmp -s "$src" "$out"; then
		echo "run.sh: MUTATE=$MUTATE no longer applies to $1" >&2
		exit 2
	fi
	extra="$extra $out"
}

case "$MUTATE" in
none)	;;
offunsigned)	mutate nlist.c 's/fsize_t symsize;/unsigned symsize;/';;
offint)		mutate nlist.c 's/fsize_t symsize;/int symsize;/';;
value16)	mutate nlist.c 's/np->n_value = ste.ls_addr;/np->n_value = (unsigned short)ste.ls_addr;/';;
valueint)	mutate nlist.c 's/np->n_value = ste.ls_addr;/np->n_value = (int)ste.ls_addr;/';;
*)	echo "run.sh: unknown MUTATE=$MUTATE" >&2; exit 2;;
esac

$CCZ -i -I "$OS/include" -I "$OS/include/sys" -o "$WORK/nlistfar" \
	"$HERE/nlistfar.c" $extra || exit 2

echo "nlistfar: MUTATE=$MUTATE"
( cd "$WORK" && "$C900_EMU" --exec ./nlistfar . )
got=$?

# A mutation must be caught; the shipped library must pass.  Either way the
# verdict is stated here rather than left to the reader of the transcript.
case "$MUTATE" in
none)	if [ "$got" = 0 ]; then
		echo "nlistfar: PASS"; exit 0
	fi
	echo "nlistfar: FAIL (exit $got)"; exit 1;;
*)	if [ "$got" = 0 ]; then
		echo "nlistfar: MUTATE=$MUTATE was NOT caught -- exit 0"
		exit 1
	fi
	echo "nlistfar: MUTATE=$MUTATE caught (exit $got)"; exit 0;;
esac
# end of tests/nlistfar/run.sh
