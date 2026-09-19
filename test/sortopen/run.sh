#!/bin/sh
# tests/sortopen/run.sh -- what sort(1) makes of its operand list: a name it
# cannot open, and `-', the name of the standard input.
#
#	sh run.sh			all five phases
#	SORT=/path/to/sort sh run.sh	test a different build
#
# A filename typed wrong is the commonest thing that happens to sort, and there
# are two places in sort.c that meet it.  copyruns() -- the `-m' merge path --
# prints the diagnostic, records a failure and goes on to the next name.
# sgetc() -- the path every other invocation takes -- printed the same
# diagnostic and then went on to use the null FILE * it had just been handed:
#
#	else if ((fp = fopen(*flist, "r")) == NULL)
#		fprintf(stderr, "sort: cannot open %s\n", *flist);
#	flist++;
#	setbuf(fp, ibuf);		/* fp is NULL here */
#
# so both paths are under test here, together: the defect was one of a pair
# being wrong, and a test that only covered the broken one would not notice the
# working one being broken later.
#
# `-' IS AN OPERAND, NOT AN OPTION.  Both of those loops read the standard
# input for an operand spelt `-', and the default operand list is literally
# `-', so an invocation with no files at all reads the standard input by
# arriving at the same code.  What decides whether a `-' ever reaches them is
# main's option loop, which runs while argv[1] begins with `-' or `+' and takes
# the letters after the `-' one at a time: a lone `-' has no letters, so it set
# no flag and was consumed and discarded, and `sort - file' sorted file alone
# and said nothing.  Phase 4 puts a `-' in every position an operand list can
# put one.  It asserts the LINES, because the status of that invocation was 0
# and the sorted output looked like an answer.
#
# WHAT A PASS IS.  For each case, sort's exit status and the lines it wrote,
# as `<status>|<line>,<line>'.  The status matters as much as the data: a
# script that runs `sort $file > out' has nothing but the status to tell it the
# output is incomplete, which is why a name that could not be opened must end
# in a non-zero one.
#
# AND NO TEMPORARY LEFT BEHIND.  Every case runs with `-T' pointing at an empty
# directory of its own, and phase 5 fails if anything is still in it.  sort
# unlinks its scratch files in rmexit(), so a leftover means sort left by some
# other door.
#
# MUTATE=nodash demonstrates that phase 4 can fail: it compiles sort.c with the
# option loop's test for a lone `-' removed, so the option loop eats it again.
# Phases 1 to 3 stay green and every phase 4 case that has a `-' before another
# operand goes red -- on its LINES, at status 0, which is the shape of the
# defect it is there for.
#
# MUTATE=nocontinue demonstrates that phase 1 can fail: it compiles sort.c with
# sgetc()'s error branch folded back into the fall-through above, and runs the
# same cases.  Phase 1 goes red on the status of every case; phase 2 stays
# green, because copyruns() is untouched.  The mutant does NOT dump core here:
# the process runner has flat memory and no signals, so the null dereference
# reads a zero rather than faulting, and what the gate sees is the wrong status.
# On the machine the same binary answers `Segmentation violation -- core
# dumped' and leaves a /core and a /tmp/sortNNa behind, which is what
# test/cmd/moresort.cmd checks.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
ROOT="$OS"
HB="$OS/hostbuild"

SORT=${SORT:-$HB/build/bin/sort}
MUTATE=${MUTATE:-none}

C900_ROOT=$ROOT
. "$C900_ROOT/mk/emulator.sh"
emu_need "run sort(1), which is a Z8001 binary"

case $MUTATE in
none|nocontinue|nodash) ;;
*) echo "sortopen: unknown MUTATE=$MUTATE" >&2; exit 2;;
esac

WORK=$(mktemp -d "${TMPDIR:-/tmp}/sorto.XXXXXX") || exit 2
trap 'rm -rf "$WORK"' 0 1 2 15
mkdir "$WORK/tmp"

# Two files whose words are distinct from each other, so that a case which
# drops one of them is visible in the output rather than hidden behind the
# other's identical lines.
printf 'sss\nppp\n' > "$WORK/a"
printf 'yyy\nqqq\n' > "$WORK/b"
# The same words already in order.  `-m' merges rather than sorts, and `-c'
# only reports, so both need an input that is sorted to begin with.
printf 'ppp\nsss\n' > "$WORK/c"
printf 'qqq\nyyy\n' > "$WORK/d"
# What phase 4 feeds to the standard input.  Its words are distinct from every
# file's again, so a case that drops the standard input is short exactly these
# and a case that reads it twice is long exactly these.
printf 'ttt\nnnn\n' > "$WORK/s"
# Sorted, for the `-m' and `-c' cases, which read the operands as one stream:
# `h' sorts ahead of `c' entirely, so `- c' is an ordered stream and a build
# that reads the standard input can say so.
printf 'aaa\nbbb\n' > "$WORK/h"
# The reverse, for the one `-c' case that must REPORT: a build that skips the
# standard input never sees `zzz' and calls the rest sorted.
printf 'zzz\naaa\n' > "$WORK/f"
# One word in common with `a', for the two cases that ask whether `-u' was
# taken as an option or as a filename.
printf 'ppp\nnnn\n' > "$WORK/g"

# ---------------------------------------------------------------------------
# The binary under test.
# ---------------------------------------------------------------------------
if [ "$MUTATE" != none ]; then
	. "$OS/hostbuild/toolchain.sh"
	python3 - "$MUTATE" "$OS/base/cmd/sort.c" "$WORK/sort.c" <<'EOF' || exit 2
import sys
which, inp, out = sys.argv[1], sys.argv[2], sys.argv[3]
src = open(inp).read()
if which == "nocontinue":
    fixed = ('\t\t\telse if ((fp = fopen(*flist, "r")) == NULL) {\n'
             '\t\t\t\tfprintf(stderr, "sort: cannot open %s\\n", *flist);\n'
             '\t\t\t\topenerr = 1;\n'
             '\t\t\t\tflist++;\n'
             '\t\t\t\tgoto again;\n'
             '\t\t\t}\n')
    broken = ('\t\t\telse if ((fp = fopen(*flist, "r")) == NULL)\n'
              '\t\t\t\tfprintf(stderr, "sort: cannot open %s\\n", *flist);\n')
    gone = "sgetc()'s error path"
else:
    fixed = ("\t\tif (argv[1][0]=='-' && argv[1][1]=='\\0')\n"
             "\t\t\tbreak;\n")
    broken = ''
    gone = "the option loop's test for a lone `-'"
if fixed not in src:
    sys.stderr.write("sortopen: MUTATE=%s found nothing to remove --\n"
                     "  %s is no longer spelt the way this\n"
                     "  mutation expects.\n" % (which, gone))
    sys.exit(2)
open(out, "w").write(src.replace(fixed, broken))
EOF
	"$TC/ccz" -s -i \
		-I "$OS/include" -I "$OS/include/sys" -I "$OS/base/cmd" \
		-o "$WORK/sort" "$WORK/sort.c" > "$WORK/cc.log" 2>&1 || {
		echo "sortopen: the mutant did not compile; see $WORK/cc.log" >&2
		exit 2; }
	SORT=$WORK/sort
fi
[ -f "$SORT" ] || {
	echo "sortopen: no sort at $SORT -- sh $HB/build-userland.sh sort.c" >&2
	exit 2; }

PASSED=0
BAD=0
# Collected in a FILE, not a variable: every case runs inside a command
# substitution and a `while read' loop, both subshells, so an assignment made
# in one of them is gone by the time phase 4 looks at it.
: > "$WORK/leftover"

# runsort <stdin-file> <args...> -- print `<status>|<lines comma-separated>'.
# The diagnostic goes to the guest's stderr and is not part of the answer: what
# is under test is the data and the status, and a message is neither.
runsort() {
	_in=$1; shift
	"$C900_EMU" --exec "$SORT" -T "$WORK/tmp" "$@" \
		< "$_in" > "$WORK/o" 2> "$WORK/e"
	_st=$?
	# The runner prints its own `[exit N]' line on the program's stdout.
	_lines=$(grep -v '^\[exit ' "$WORK/o" | tr '\n' ',' | sed 's/,$//')
	printf '%s|%s' "$_st" "$_lines"
	# Any scratch file that outlived the run belongs to the case that made
	# it, so it is collected here rather than at the end.
	for _t in "$WORK"/tmp/*; do
		[ -e "$_t" ] || continue
		basename "$_t" >> "$WORK/leftover"
		rm -f "$_t"
	done
}

# runcases <caselist> <label>: TAB-separated <name> <want> <stdin> <args>.
# <stdin> is `-' for an empty standard input.
runcases() {
	printf '%s\n' "$1" | while IFS="$(printf '\t')" read -r nm want inf args; do
		[ -n "$nm" ] || continue
		case $inf in -) inf=/dev/null;; *) inf=$WORK/$inf;; esac
		got=$(runsort "$inf" $args)
		if [ "$got" = "$want" ]; then
			echo "  ok   $2 $nm"
		else
			echo "  FAIL $2 $nm: got [$got] want [$want]"
		fi
	done
}

tally() {
	cat "$1"
	_o=$(grep -c '^  ok   ' "$1" 2>/dev/null); _o=${_o:-0}
	_f=$(grep -c '^  FAIL ' "$1" 2>/dev/null); _f=${_f:-0}
	PASSED=$((PASSED + _o))
	BAD=$((BAD + _f))
}

# The instrument before the measurement: sorting one ordinary file.  Every case
# below reads a wrong answer as a defect in sort, so a harness that cannot get
# a sorted file out of any build must say so instead of reporting six.
probe=$(runsort /dev/null "$WORK/a")
[ "$probe" = "0|ppp,sss" ] || {
	echo "sortopen: the harness cannot sort a file at all (got [$probe])." >&2
	echo "  Nothing below would measure sort.  $WORK is where it tried." >&2
	exit 2; }

echo "sortopen: $SORT"
[ "$MUTATE" = none ] || echo "  (MUTATE=$MUTATE -- this run is EXPECTED to fail)"

# ---------------------------------------------------------------------------
# Phase 1: sgetc()'s path -- an ordinary sort, with a name it cannot open.
# The good operand's records must still come out, in order, and the status must
# say that something was missed.
# ---------------------------------------------------------------------------
BAD_OPEN='
only-missing	1|	-	/nosuchfile
good-then-missing	1|ppp,sss	-	@a /nosuchfile
missing-then-good	1|ppp,sss	-	/nosuchfile @a
missing-between-two	1|ppp,qqq,sss,yyy	-	@a /nosuchfile @b
two-missing	1|ppp,sss	-	/nosuchfile @a /alsonothere
check-missing	1|	-	-c /nosuchfile
unique-missing	1|ppp,sss	-	-u /nosuchfile @a
'

# ---------------------------------------------------------------------------
# Phase 2: copyruns()'s path -- `-m', which had the branch all along.  Pinned
# so that a later edit to one of the pair cannot quietly change the other.
# `-m' abandons the merge on a name it cannot open rather than merging what is
# left; that is what this sort has always done and the case records it.
# ---------------------------------------------------------------------------
MERGE='
merge-missing	1|	-	-m /nosuchfile @a
merge-missing-last	1|	-	-m @a /nosuchfile
merge-good	0|ppp,qqq,sss,yyy	-	-m @c @d
'

# ---------------------------------------------------------------------------
# Phase 3: the controls.  Every one of these opens everything it was given, so
# a build that had started refusing files it CAN open would show up here rather
# than as a phase 1 success.
# ---------------------------------------------------------------------------
GOOD='
one-file	0|ppp,sss	-	@a
two-files	0|ppp,qqq,sss,yyy	-	@a @b
check-sorted	0|	-	-c @c
'

# ---------------------------------------------------------------------------
# Phase 4: `-' names the standard input, in every position an operand list can
# put it.  The standard input is `s' (ttt, nnn) except where a case names
# another fixture, and its two words appear in the wanted lines of every case
# that must read it.  Both open loops are represented, because the option loop
# that decides this runs before either of them: the plain sort cases go through
# sgetc(), `-m' through copyruns(), and `-c' through sgets() alone.
#
# Two cases fix which side of the operand list `-u' falls on, and they are the
# reason `g' shares a word with `a': `-u - a' is `-u' the option, and the
# repeated ppp comes out once; `- -u a' is `-u' a filename, so it cannot be
# opened, the status says so, and BOTH ppp survive.  That is the same reading
# every other operand already gets -- `sort a -u' has always looked for a file
# called `-u' -- and the pair holds it there.
# ---------------------------------------------------------------------------
DASH='
dash-alone	0|nnn,ttt	s	-
dash-alone-empty	0|	-	-
dash-first	0|nnn,ppp,sss,ttt	s	- @a
dash-last	0|nnn,ppp,sss,ttt	s	@a -
dash-middle	0|nnn,ppp,qqq,sss,ttt,yyy	s	@a - @b
dash-first-and-last	0|nnn,ppp,sss,ttt	s	- @a -
dash-twice-adjacent	0|nnn,ttt	s	- -
dash-first-empty-stdin	0|ppp,sss	-	- @a
dash-first-missing-after	1|nnn,ttt	s	- /nosuchfile
option-before-dash	0|nnn,ppp,sss	g	-u - @a
option-after-dash	1|nnn,ppp,ppp,sss	g	- -u @a
skip-then-dash	0|nnn,ppp,sss,ttt	s	+0 - @a
merge-dash-first	0|aaa,bbb,ppp,sss	h	-m - @c
merge-dash-last	0|aaa,bbb,ppp,sss	h	-m @c -
check-dash-first-ordered	0|	h	-c - @c
check-dash-first-not	1|	f	-c - @c
'

A=$BAD_OPEN
M=$MERGE
G=$GOOD
D=$DASH
# The operand lists carry `@name' for a file in the work directory, because the
# directory is named at run time and the case list is a literal.
subst() { printf '%s\n' "$1" | sed "s|@|$WORK/|g"; }

echo "phase 1: an ordinary sort meets a file it cannot open"
runcases "$(subst "$A")" open > "$WORK/p1"; tally "$WORK/p1"

# The status says something was missed; the diagnostic says WHICH name, and it
# is the only thing that does.  Checked once, on the plainest case.
runsort /dev/null /nosuchfile > /dev/null
if grep -q '^sort: cannot open /nosuchfile$' "$WORK/e"; then
	echo "  ok   open diagnostic-names-the-file"
	PASSED=$((PASSED + 1))
else
	echo "  FAIL open diagnostic-names-the-file: got [$(cat "$WORK/e")]"
	BAD=$((BAD + 1))
fi

echo "phase 2: the same, on the -m path that has its own open loop"
runcases "$(subst "$M")" merge > "$WORK/p2"; tally "$WORK/p2"

echo "phase 3: controls -- everything opens"
runcases "$(subst "$G")" good > "$WORK/p3"; tally "$WORK/p3"

echo "phase 4: \`-' names the standard input wherever it appears"
runcases "$(subst "$D")" dash > "$WORK/p4"; tally "$WORK/p4"

echo "phase 5: no scratch file outlived any of the above"
LEFTOVER=$(tr '\n' ' ' < "$WORK/leftover")
if [ -z "$LEFTOVER" ]; then
	echo "  ok   tmp -T directory is empty"
	PASSED=$((PASSED + 1))
else
	echo "  FAIL tmp left behind: $LEFTOVER"
	BAD=$((BAD + 1))
fi

echo
if [ "$BAD" -eq 0 ]; then
	echo "sortopen: $PASSED ok"
	exit 0
fi
echo "sortopen: $BAD FAILED, $PASSED ok"
exit 1
