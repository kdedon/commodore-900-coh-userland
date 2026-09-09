#!/bin/sh
# tests/shkeyword/run.sh -- where /bin/sh recognises a RESERVED WORD, and where
# it must not.
#
# A Bourne shell recognises `done', `do', `fi', `esac' and the rest only in
# COMMAND position.  Anywhere else they are ordinary words, so `echo done' has
# to run echo with the argument `done'.  Reserved words are ordinary English,
# and scripts print them constantly, so a shell that takes one as a keyword in
# argument position cannot run `echo done' at all -- it answers "Syntax error"
# and executes nothing.
#
# THE OBSERVATION CHANNEL IS BUILT-INS ONLY.  `c900 --exec' is a one-process
# runner: fork(2) is not serviced, so no case here may run an external command.
# Every case therefore drives `set' (which takes the words under test as its
# arguments and makes them positional parameters), assigns one into a shell
# variable, and prints the variable table with a bare `set'.  A `Q=' line is
# the guest's own answer, computed after the parse: a shell that accepted the
# syntax and dropped the word would print a different one, and a shell that
# failed to parse prints none.
#
# `:' IS NOT USABLE AS A TRUE CONDITION here: yylex() treats a `:' word as an
# on-line comment and eats the rest of the line (cmd/sh/lex.c), so
# `if : ; then ... fi' is one unterminated `if'.  The conditions below are an
# assignment (status 0) and `shift' with no parameters left (nonzero).
#
# Phase 4 measures the OTHER half of a syntax error: what the shell tells the
# user about it, and what it tells the program that ran it.
#
#	sh run.sh			all four phases
#	SH=/path/to/sh sh run.sh	test a different build
#
# MUTATE demonstrates that the gate can fail:
#	MUTATE=argfail	 phase 1 expects the keyword to be taken as a keyword:
#			 every argument-position case goes red on a good shell
#	MUTATE=cmdfail	 phase 2 expects the control constructs to fail
#	MUTATE=dialect	 phase 3 expects the three inherited refusals to parse
#	MUTATE=unfixed	 phase 4 expects what the shell said and returned before
#			 the fix: exit 0, no line number at EOF, no token, and
#			 a bare `Syntax error' for -c.  Those expectations are
#			 the third column of the case list, so this run is also
#			 a reading of the original behavior
#	MUTATE=status0	 phase 4 keeps every diagnostic and expects exit 0 for
#			 all of them: an exit status is exactly the thing a
#			 harness can look like it checks while checking the
#			 message only, and this is what says it does not
#
# The strongest negative control is not a MUTATE at all: point SH= at the
# V4.0.6 shell and phase 1 goes red 19 times, the behavior this test
# documents.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
ROOT="$OS"
HB="$OS/hostbuild"

SH=${SH:-$HB/build/bin/sh}
MUTATE=${MUTATE:-none}

# The emulator, resolved the one way this repository resolves it.
C900_ROOT=$ROOT
. "$C900_ROOT/mk/emulator.sh"

emu_need "run the shell, which is a Z8001 binary"

# uname(2) is DECLARED, not tolerated by accident, and for every runner below:
# the shell asks the running kernel for the release at start-up (cmd/sh/var.c),
# and --exec is user mode with no kernel under it, so the call is a gap the
# runner stops the program at -- every case then reporting a shell defect that
# is not there.  The shell carries on when the answer does not come, which is
# what this lets it demonstrate; a syscall it did NOT declare still stops the
# run.  13 by number because the runner's table has no name for uname(2).
export N2ACCEPT=${N2ACCEPT:-13}
[ -f "$SH" ] || {
	echo "shkeyword: no shell at $SH -- sh $HB/build-yacc-cmd.sh sh" >&2
	exit 2; }

# A SHORT working directory: the runner services open(2) against the host
# through the COHERENT directory format, whose names are DIRSIZ=14 characters,
# so a component of mktemp's default `tmp.XXXXXXXXXX' is dropped and every
# script becomes unopenable -- a harness that reports every case failing.
WORK=$(mktemp -d "${TMPDIR:-/tmp}/shkw.XXXXXX") || exit 2
trap 'rm -rf "$WORK"' 0 1 2 15

PASSED=0
BAD=0

# runsh <script-text> -- one guest process reading one script.  Prints the
# `Q=' line the script computed, or `no-Q', and appends `|SYNTAX' when the
# shell reported a syntax error.  The message is what says the parse failed;
# the status a syntax error exits with is phase 4's subject, not this one's.
#
# ONE STREAM, both of the guest's.  This shell prints the variable table with
# prints(), which writes to the diagnostic output, and the runner writes its
# own notes there too; keeping the streams apart would look for the answer in
# an empty file and report every case as a shell defect.
runsh() {
	printf '%s\n' "$1" > "$WORK/s"
	"$C900_EMU" --exec "$SH" "$WORK/s" > "$WORK/o" 2>&1
	_q=$(sed -n 's/^\(Q=.*\)$/\1/p' "$WORK/o" | sed -n 1p)
	[ -n "$_q" ] || _q=no-Q
	if grep -q 'Syntax error' "$WORK/o" 2>/dev/null; then
		_q="$_q|SYNTAX"
	fi
	printf '%s' "$_q"
}

# runcases <caselist> <label> -- each entry is three TAB-separated fields
#
#	<name>	<want>	<script>
#
# and a newline inside the script is written \n.  Tab is the separator because
# the scripts contain almost every other punctuation character.
runcases() {
	printf '%s\n' "$1" | while IFS="$(printf '\t')" read -r nm want prog; do
		[ -n "$nm" ] || continue
		got=$(runsh "$(printf '%s' "$prog" | sed -e 's/\\n/\n/g')")
		if [ "$got" = "$want" ]; then
			echo "  ok   $2 $nm"
		else
			echo "  FAIL $2 $nm: got [$got] want [$want]"
		fi
	done
}

# The while loop above runs in a subshell, so counters set inside it are lost.
# Count from the output instead, which is also what gets printed.
tally() {
	cat "$1"
	_o=$(grep -c '^  ok   ' "$1" 2>/dev/null); _o=${_o:-0}
	_f=$(grep -c '^  FAIL ' "$1" 2>/dev/null); _f=${_f:-0}
	PASSED=$((PASSED + _o))
	BAD=$((BAD + _f))
}

# rewant <caselist> <new-want> -- keep the name and the script, replace the
# expectation.  This is how MUTATE proves the gate discriminates.
rewant() {
	printf '%s\n' "$1" | while IFS="$(printf '\t')" read -r nm want prog; do
		[ -n "$nm" ] || continue
		printf '%s\t%s\t%s\n' "$nm" "$2" "$prog"
	done
}

# runcheck <how> <script-text> -- one guest shell, run the way <how> says, and
# report BOTH of the shell's answers as
#
#	<exit status>|<what it printed>
#
# `how' is the channel the input arrives on, because the diagnostic differs by
# channel: a named script can name itself, a `-c' string cannot.
#
#	file	 sh <script>
#	dash-c	 sh -c '<text>'
#	stdin	 sh < <script>	  (the text is not a file the shell can name)
#	missing	 sh <a path that does not exist>
#	dash-i	 sh -i < <script>  (-i declares the input interactive whatever it
#		 really is, which is the only way to reach the shell's terminal
#		 wording -- and its prompts, which the transcript keeps -- from a
#		 harness that has no terminal to offer it)
#
# The emulator's own notes -- the DIRSIZ warning, the `[exit n]' line -- are
# dropped, and the working directory is edited out of the message so the
# expectations below do not carry a temporary pathname.  Newlines become
# spaces: an expectation is one line.
runcheck() {
	rm -f "$WORK/s"
	case "$1" in
	file)	printf '%s\n' "$2" > "$WORK/s"
		"$C900_EMU" --exec "$SH" "$WORK/s" > "$WORK/o" 2>&1 ;;
	dash-c)	"$C900_EMU" --exec "$SH" -c "$2" > "$WORK/o" 2>&1 ;;
	stdin)	printf '%s\n' "$2" > "$WORK/s"
		"$C900_EMU" --exec "$SH" < "$WORK/s" > "$WORK/o" 2>&1 ;;
	missing) "$C900_EMU" --exec "$SH" "$WORK/nosuch" > "$WORK/o" 2>&1 ;;
	dash-i)	printf '%s\n' "$2" > "$WORK/s"
		"$C900_EMU" --exec "$SH" -i < "$WORK/s" > "$WORK/o" 2>&1 ;;
	*)	echo "shkeyword: unknown how=$1" >&2; exit 2 ;;
	esac
	_st=$?
	# A note is dropped wherever it lands, not only at the start of a line:
	# the shell's prompt carries no newline, so a note printed while one is
	# on the screen is appended to it and a line-anchored filter walks past.
	_m=$(grep -v '^\[c900:' "$WORK/o" | tr '\n' ' ' \
	     | sed -e "s|$WORK/||g" -e 's/\[exit [0-9]*\]//g' \
		   -e 's/\[c900:[^]]*\]//g' \
		   -e 's/  */ /g' -e 's/^ *//' -e 's/ *$//')
	printf '%s|%s' "$_st" "$_m"
}

# runcases4 <caselist> -- five TAB-separated fields
#
#	<name>	<want>	<want-before-the-fix>	<how>	<script>
#
# The third field is not used by a default run.  It is the pre-fix expectation,
# kept beside the one the shell meets now so that the two can be read against
# each other, and selected by MUTATE=unfixed.
runcases4() {
	printf '%s\n' "$1" | while IFS="$(printf '\t')" read -r nm want old how prog; do
		[ -n "$nm" ] || continue
		got=$(runcheck "$how" "$(printf '%s' "$prog" | sed -e 's/\\n/\n/g')")
		if [ "$got" = "$want" ]; then
			echo "  ok   diag $nm"
		else
			echo "  FAIL diag $nm: got [$got] want [$want]"
		fi
	done
}

# The two phase-4 mutations.  `unfixed' promotes the third column to the
# expectation; `status0' keeps the message and demands exit 0 for it.
useold() {
	printf '%s\n' "$1" | while IFS="$(printf '\t')" read -r nm want old how prog; do
		[ -n "$nm" ] || continue
		printf '%s\t%s\t%s\t%s\t%s\n' "$nm" "$old" "$old" "$how" "$prog"
	done
}

zerostat() {
	printf '%s\n' "$1" | while IFS="$(printf '\t')" read -r nm want old how prog; do
		[ -n "$nm" ] || continue
		printf '%s\t0|%s\t%s\t%s\t%s\n' "$nm" "${want#*|}" "$old" "$how" "$prog"
	done
}

# ---------------------------------------------------------------------------
# Phase 1: ARGUMENT position.  Every word in the keyword table of
# cmd/sh/lex.c, one per case, handed to a command as its argument.  The word
# must arrive intact and no syntax error may be reported.
#
# The last three cases are the shape the defect was reported as: a
# semicolon-separated list whose final command has a reserved word for its
# argument.  The list itself is the CONTROL -- `set a ; set b' with no keyword
# in it must pass, or a shell that had stopped running lists at all would look
# like this defect.
# ---------------------------------------------------------------------------
ARGS='
arg-case	Q=case	set case\nQ=$1\nset
arg-do	Q=do	set do\nQ=$1\nset
arg-done	Q=done	set done\nQ=$1\nset
arg-elif	Q=elif	set elif\nQ=$1\nset
arg-else	Q=else	set else\nQ=$1\nset
arg-esac	Q=esac	set esac\nQ=$1\nset
arg-fi	Q=fi	set fi\nQ=$1\nset
arg-for	Q=for	set for\nQ=$1\nset
arg-if	Q=if	set if\nQ=$1\nset
arg-in	Q=in	set in\nQ=$1\nset
arg-return	Q=return	set return\nQ=$1\nset
arg-then	Q=then	set then\nQ=$1\nset
arg-until	Q=until	set until\nQ=$1\nset
arg-while	Q=while	set while\nQ=$1\nset
arg-obrace	Q={	set {\nQ=$1\nset
arg-cbrace	Q=}	set }\nQ=$1\nset
arg-several-keywords	Q=do.fi.esac.then	set do fi esac then\nQ=$1.$2.$3.$4\nset
arg-keyword-not-first	Q=x.done	set x done\nQ=$1.$2\nset
list-control-no-keyword	Q=b	set a\nset b\nQ=$1\nset
list-keyword-last	Q=done	set daet\nset who\nset done\nQ=$1\nset
list-trailing-semicolon	Q=done	set done ;\nQ=$1\nset
'

# ---------------------------------------------------------------------------
# Phase 2: COMMAND position.  The same words, where they ARE keywords.  A fix
# that stopped recognising them here would be worse than the defect, so every
# construct computes a value rather than merely parsing: a shell that accepted
# the syntax and ran nothing prints no `Q=' line.
# ---------------------------------------------------------------------------
CMDS='
if-then	Q=then	if Z=1 ; then Q=then ; else Q=else ; fi\nset
if-else	Q=else	if shift ; then Q=then ; else Q=else ; fi\nset
if-elif	Q=elif	if shift ; then Q=a ; elif Z=1 ; then Q=elif ; else Q=c ; fi\nset
for-do-done	Q=ab	for i in a b ; do Q=$Q$i ; done\nset
for-do-done-newlines	Q=ab	for i in a b\ndo\nQ=$Q$i\ndone\nset
while-do-done	Q=.x.x	set a b\nwhile shift ; do Q=$Q.x ; done\nset
until-do-done	Q=once	Q=once\nuntil Z=1 ; do Q=body ; done\nset
case-esac	Q=hit	case a in a) Q=hit ;; b) Q=miss ;; esac\nset
case-esac-newlines	Q=star	case zz in\na) Q=a ;;\n*) Q=star ;;\nesac\nset
brace-group	Q=brace	{ Q=brace ; }\nset
function-call	Q=fn	f() { Q=fn ; }\nf\nset
function-spaced-parens	Q=fn2	f () { Q=fn2 ; }\nf\nset
function-brace-next-line	Q=fn3	f()\n{ Q=fn3 ; }\nf\nset
function-return	Q=g1	g() { Q=g1 ; return ; Q=g2 ; }\ng\nset
function-arguments	Q=p.q	h() { Q=$1.$2 ; }\nh p q\nset
'

# ---------------------------------------------------------------------------
# Phase 3: the three refusals this shell INHERITED, pinned so that a change to
# any of them is visible here rather than as a surprise.  Each is refused
# identically by the 1985 shell this generation replaced, so none of them is a
# regression of the V4.0.6 adoption:
#
#   case-subject	`case done in ...' -- after `case' the subject word is
#			read while keyword lookup is still armed (sh.y's
#			`_CASE name'), so a keyword cannot be a case subject.
#   after-assignment	`X=1 done' -- an assignment does not end the reserved
#			word position, which is also what bash does here.
#   for-word-list	`for i in a done' -- the word list is read with lookup
#			armed, because `do' has to be recognised at its end.
#
# The names say what the construct is, not that it works.  Do not "fix" one of
# these by disarming keyword lookup for the position: `do' and `done'
# terminate the list, and a shell that stopped seeing them there would parse
# no loop at all.
# ---------------------------------------------------------------------------
DIALECT='
case-subject	no-Q|SYNTAX	case done in done) Q=hit ;; esac\nset
after-assignment	no-Q|SYNTAX	X=1 done\nset
for-word-list	no-Q|SYNTAX	for i in a done ; do Q=$Q$i ; done\nset
'

# ---------------------------------------------------------------------------
# Phase 4: what a failed parse REPORTS.  Two things are measured at once and
# both are in the expectation: the exit status the shell hands back, and the
# diagnostic it prints.
#
# THE EXIT STATUS IS A DIVERGENCE, DELIBERATELY.  Every COHERENT sh returns
# `slret', the status of the last command, and a shell that never ran one
# returns the 0 it was initialized with -- which the Lexicon `sh' article
# describes exactly ("sh returns the exit status of the last command executed
# or the status specified by an exit command") without ever promising a
# nonzero status for a failure of the shell itself.  So `sh /nosuchfile' and
# every syntax error exited 0 on the real system.  That is authentic and it is
# still wrong: it is the one failure no script, no rc file and no Makefile on
# the machine can detect.  2 is the vendor's own later choice -- the 4.20
# rework returns 2 from the unopenable-script path (relic/b/bin/sh.420).
#
# `exit 7' and the clean script are the CONTROLS for the status: a shell that
# returned 2 from everywhere, or 0 from everywhere, fails one of them.
#
# NO CASE HERE MAY RUN AN EXTERNAL COMMAND -- see the note at the top of this
# file -- so the scripts assign variables and use the `exit' builtin.
#
# INTERACTIVE INPUT is reached with `-i', which declares the input interactive
# whatever it really is: `c900 --exec' hands the shell a pipe, so isatty(2) is
# false and every other case here is noninteractive.  Two things about a
# terminal are measured through it.  The line number is LEFT OFF there, because
# the count restarts at every line and would always read 1.  And the two errors
# a line apart are reported SEPARATELY: a command line leaves the parser by
# longjmp rather than by returning, so the recovery counter that suppresses an
# error following closely on another has to be cleared at each parse, or the
# second error is described with the first error's token.
# ---------------------------------------------------------------------------
DIAG=$(cat <<'XEOF'
eof-one-line	2|s: Syntax error at EOF in line 1	0|s: Syntax error at EOF	file	if Q=1
eof-line-3	2|s: Syntax error at EOF in line 3	0|s: Syntax error at EOF	file	Q=1\nQ=2\nif Q=3
token-line-2	2|s: Syntax error near `;;' in line 2	0|s: Syntax error in line 2	file	Q=1\n;;
clean-script	0|	0|	file	Q=1
exit-status-kept	7|	7|	file	exit 7
missing-script	2|Cannot open nosuch	0|Cannot open nosuch	missing	
dashc-token	2|Syntax error near `;;' in line 1	0|Syntax error	dash-c	Q=1 ;; Q=2
dashc-newline	2|Syntax error at newline in line 1	0|Syntax error	dash-c	for
dashc-line-3	2|Syntax error near `;;' in line 3	0|Syntax error	dash-c	Q=1\nQ=2\n;;
stdin-token	2|Syntax error near `;;' in line 2	0|Syntax error	stdin	Q=1\n;;
two-errors-a-line-apart	0|$ $ Syntax error near `;;' $ Syntax error near `)' $ $	0|$ $ Syntax error $ Syntax error $ $	dash-i	Q=1\n;;\n)\nQ=2
XEOF
)

A=$ARGS
C=$CMDS
D=$DIALECT
G=$DIAG
case $MUTATE in
argfail)  A=$(rewant "$ARGS" "no-Q|SYNTAX") ;;
cmdfail)  C=$(rewant "$CMDS" "no-Q|SYNTAX") ;;
dialect)  D=$(rewant "$DIALECT" "Q=hit") ;;
unfixed)  G=$(useold "$DIAG") ;;
status0)  G=$(zerostat "$DIAG") ;;
none)	  ;;
*)	  echo "shkeyword: unknown MUTATE=$MUTATE" >&2; exit 2 ;;
esac

# The instrument before the measurement: a script with no keyword in it at all
# must print its `Q='.  Everything below reads a missing `Q=' as a failure of
# the shell, so a harness that cannot get one out of any shell must say so
# rather than report thirty-six defects.
probe=$(runsh 'Q=probe
set')
[ "$probe" = Q=probe ] || {
	echo "shkeyword: the harness cannot run a script at all (got [$probe])." >&2
	echo "  Nothing below would measure the shell.  $WORK is where it tried." >&2
	exit 2; }

echo "shkeyword: $SH"
[ "$MUTATE" = none ] || echo "  (MUTATE=$MUTATE -- this run is EXPECTED to fail)"

echo "phase 1: a reserved word in ARGUMENT position is an ordinary word"
runcases "$A" arg > "$WORK/p1"; tally "$WORK/p1"

echo "phase 2: the same words in COMMAND position are still keywords"
runcases "$C" cmd > "$WORK/p2"; tally "$WORK/p2"

echo "phase 3: the positions this shell inherited a refusal for"
runcases "$D" dialect > "$WORK/p3"; tally "$WORK/p3"

echo "phase 4: what a failed parse reports, and what it exits with"
runcases4 "$G" > "$WORK/p4"; tally "$WORK/p4"

echo
if [ "$BAD" -eq 0 ]; then
	echo "shkeyword: $PASSED ok"
	exit 0
fi
echo "shkeyword: $BAD FAILED, $PASSED ok"
exit 1
