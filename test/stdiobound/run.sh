#!/bin/sh
# tests/stdiobound/run.sh -- build stdiobound and run it under the emulator's
# process runner (`c900 --exec'), which services its read/write/lseek against
# the host filesystem.  One guest process, about a second.
#
# The subject is libc/stdio compiled for a 16-bit int, so the host cannot
# stand in for it: on a 32-bit host every count in the test fits an int and
# every assertion passes whatever the library does.  This is the
# discrimination instead, and it mutates the library rather than a stand-in --
# each MUTATE below compiles a copy of the real source with one type changed
# and links it ahead of libc, so the guest runs the mutated routine:
#
#	sh run.sh			the shipped library, all cases must pass
#	MUTATE=signed sh run.sh		fread's byte counter as `int': the
#					partial-read count goes wrong
#	MUTATE=ungoteof sh run.sh	ungetc(EOF) stored as data
#	MUTATE=eofsticky sh run.sh	ungetc/fseek leave _FEOF set
#	MUTATE=noungot sh run.sh	fseek/ftell ignore a pending ungot char
#	MUTATE=errsticky sh run.sh	rewind leaves _FERR set
#	MUTATE=boffsigned sh run.sh	finit's buffer offset in 16-bit signed
#	MUTATE=padsign sh run.sh	a zero pad emitted before the sign
#	MUTATE=signstring sh run.sh	the sign hoist applied to %s and %c too
#	MUTATE=ladjpad sh run.sh	a left-adjusted field padded with '0'
#	MUTATE=errnoclear sh run.sh	fflush/_fgetb/_fgetc leave errno at 0
#
# Every mutation must make the run FAIL; a mutation that passes means the case
# it belongs to is not testing what it claims.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
ROOT="$OS"
STDIO=$OS/libc/stdio

C900_ROOT=$ROOT
. "$C900_ROOT/mk/emulator.sh"
. "$OS/hostbuild/toolchain.sh"
CCZ="$TC/ccz"

MUTATE=${MUTATE:-none}
emu_need "run stdiobound, which is a Z8001 binary"

# The library under test is the one in $TCB, so bring it up to date here: an
# edit to libc/stdio that has not been relinked is invisible to this run and
# the transcript looks like the answer.  NODEPS keeps it to libc -- without it
# the dist dependency fragment is remade and pulls in every other stamp.
make -C "$OS/hostbuild" NODEPS=1 libc > /dev/null || exit 2

WORK=$(mktemp -d) || exit 2
trap 'rm -rf "$WORK"' 0 1 2 15

# The mutated source, when there is one.  sed on the real file, so a mutation
# that no longer applies is caught here rather than passing quietly.
extra=""
mutate() {			# mutate <file> <sed script>
	src=$STDIO/$1
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
signed)		mutate fread.c 's/^unsigned int	size;/int	size;/
			        s/^unsigned int	nitems;/int	nitems;/
			        s/unsigned int	nb = size\*nitems;/int	nb = size*nitems;/';;
ungoteof)	mutate ungetc.c '/if (c == EOF)/,+1d';;
eofsticky)	mutate ungetc.c '/_ff &= ~_FEOF/d'
		mutate fseek.c  '/_ff &= ~_FEOF/d';;
noungot)	mutate fseek.c  '/offset--;/d'
		mutate ftell.c  '/--offset;/d';;
errsticky)	mutate rewind.c 's/\tclearerr(fp);//';;
boffsigned)	mutate finit.c  's/(int)(off%(long)BUFSIZ)/(int)off%BUFSIZ/';;
padsign)	mutate printf.c '/isnumeric && pad/,+1d';;
signstring)	mutate printf.c 's/isnumeric && pad/pad/';;
ladjpad)	mutate printf.c "s/putc(' ', fp)/putc(pad, fp)/";;
errnoclear)	mutate fflush.c '/if (errno == 0)/,+1d'
		mutate _fgetb.c '/if (errno == 0)/,+1d'
		mutate _fgetc.c '/if (errno == 0)/,+1d';;
*)	echo "run.sh: unknown MUTATE=$MUTATE" >&2; exit 2;;
esac

$CCZ -i -I "$OS/include" -I "$OS/include/sys" -o "$WORK/stdiobound" \
	"$HERE/stdiobound.c" $extra || exit 2

echo "stdiobound: MUTATE=$MUTATE"
( cd "$WORK" && "$C900_EMU" --exec ./stdiobound . )
got=$?

# A mutation must be caught; the shipped library must pass.  Either way the
# verdict is stated here rather than left to the reader of the transcript.
case "$MUTATE" in
none)	if [ "$got" = 0 ]; then
		echo "stdiobound: PASS"; exit 0
	fi
	echo "stdiobound: FAIL (exit $got)"; exit 1;;
*)	if [ "$got" = 0 ]; then
		echo "stdiobound: MUTATE=$MUTATE was NOT caught -- exit 0"
		exit 1
	fi
	echo "stdiobound: MUTATE=$MUTATE caught (exit $got)"; exit 0;;
esac
