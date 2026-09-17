#!/bin/sh
# tests/awkrules/run.sh -- pin the pattern-action rule syntax of COHERENT
# awk(1), and nawk's superset of it.
#
# In this awk a rule is TERMINATED BY A NEWLINE and by nothing else -- `;'
# does not separate rules either -- so `awk '{n++} END{print n}'' is refused
# with "awk: Syntax error" while the newline form works.  awk.y has read
#
#	line:	  compound '\n'
#	      |	  pattern '\n'
#	      |	  pattern compound '\n'
#
# through every revision Mark Williams made: a deliberate dialect, not a bug.
# Do not "fix" awk.y to accept the one-line form; this gate exists to refuse
# that edit.  Nine positive cases pin the semantics the newline form must
# keep, four negative cases pin the refusal.
#
# Phase 3 runs the ORIGINAL 1985 C900 awk binary (outside this repository, in
# the hardware holdings) over the identical case list and requires it to agree
# verdict for verdict; a missing oracle SKIPS phase 3.
#
# Phase 4 covers nawk: `extended' ships the 1989 Hirabayashi nawk as
# /bin/nawk with /bin/awk a hard link to it, on the claim that nawk is a
# SUPERSET -- every must-work case gives the same answer, the four one-line
# programs are accepted with the right answers, and the constructs that
# arrived with nawk work.  A missing nawk SKIPS phase 4.
#
# Host-side only: every awk runs under the emulator's process runner
# (`c900 --exec'), one guest process per case.  About 20 s.
#
#	sh run.sh			phases 1-4
#	AWK=/path/to/awk sh run.sh	test a different build
#	NAWK=/path/to/nawk sh run.sh	test a different nawk build
#	C900_AWK1985=/path/to/awk sh run.sh
#					point phase 3 at the oracle explicitly
#
# MUTATE demonstrates that the gate can fail:
#	MUTATE=accept	invert phase 2 (one-liners accepted): 8 cases go red
#	MUTATE=break	invert phase 1 (newline rules fail): 9 cases go red
#	MUTATE=samelang	invert phase 4 (nawk refuses one-liners): 4 go red
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
ROOT="$OS"
HB="$OS/hostbuild"

AWK=${AWK:-$HB/build/bin/awk}
NAWK=${NAWK:-$HB/build/bin/nawk}

# The emulator, resolved the one way this repository resolves it.
C900_ROOT=$ROOT
. "$C900_ROOT/mk/emulator.sh"

# The oracle: /src/bin/awk off the C900's own hard disk, a Z8001 COHERENT
# binary; the runner executes it exactly as it does ours.  It is a recovered
# artefact and lives in the hardware holdings, not this repository.
if [ -z "${C900_AWK1985:-}" ]; then
	for c in "$ROOT/../C900/firmware/hd/extracted/src/bin/awk"; do
		[ -f "$c" ] && { C900_AWK1985=$c; break; }
	done
fi
: "${C900_AWK1985:=}"

MUTATE=${MUTATE:-none}

emu_need "run both awks, which are Z8001 binaries"
[ -f "$AWK" ] || {
	echo "awkrules: no awk at $AWK -- sh $HB/build-awk.sh" >&2; exit 2; }

WORK=$(mktemp -d) || exit 2
trap 'rm -rf "$WORK"' 0 1 2 15
IN=$WORK/in
printf 'a 1\nb 2\nc 3\n' > "$IN"

PASSED=0
BAD=0
fail() { echo "  FAIL $*"; BAD=$((BAD + 1)); }
ok()   { echo "  ok   $*"; PASSED=$((PASSED + 1)); }

# runawk <binary> <program> -- one guest process.  Prints the program's stdout
# with newlines turned into `|' so a case is one comparable line, then `rc=N'.
# stderr is dropped: the runner writes its own `[exit N]' there, and the error
# TEXT is not what is under test (the message itself is checked once, in
# phase 2's first case, from a separate capture).
runawk() {
	_out=$("$C900_EMU" --exec "$1" "$2" "$IN" 2>/dev/null)
	_rc=$?
	printf '%s rc=%d' "$(printf '%s' "$_out" | tr '\n' '|')" "$_rc"
}

# ---------------------------------------------------------------------------
# The case list.  Each entry is three TAB-separated fields
#
#	<name>	<want>	<program>
#
# and `want' is the whole expected "output rc=N" line, where the program's
# output newlines have been folded to `|'.  Tab is the separator because the
# expected outputs contain `|' and the programs contain almost everything else.
# A newline inside a PROGRAM is written \n and expanded when it is run.
#
# Phase 1: what MUST work -- newline-separated rules, in both orders, patterns
# as well as bare actions, and END last however it was written.
# ---------------------------------------------------------------------------
GOOD='
count-then-END	3 rc=0	{n++}\nEND{print n}
END-then-count	3 rc=0	END{print n}\n{n++}
two-patterns	B|C rc=0	/b/{print "B"}\n/c/{print "C"}
two-patterns-swapped	B|C rc=0	/c/{print "C"}\n/b/{print "B"}
BEGIN-rule-END	start|a|b|c|end rc=0	BEGIN{print "start"}\n{print $1}\nEND{print "end"}
END-written-first-still-last	a|b|c|end rc=0	END{print "end"}\n{print $1}
bare-pattern-then-action	1|b 2|2|3 rc=0	/b/\n{print $2}
count-in-END	1|2|3|n=3 rc=0	{print $2}\nEND{print "n=" NR}
pattern-and-action-pair	b 2|B rc=0	/b/\n/b/{print "B"}
'

# Phase 2: what the shipped language REFUSES -- two rules on one line, with or
# without a `;' between them.  rc=1 and no output.
BAD_CASES='
oneline-action-END	 rc=1	{n++} END{print n}
oneline-two-patterns	 rc=1	/b/{print "B"} /c/{print "C"}
oneline-semicolon	 rc=1	{n++}; END{print n}
oneline-BEGIN-END	 rc=1	BEGIN{print "s"} END{print "e"}
'

# Phase 4: the same four programs under nawk, which ACCEPTS them -- with the
# right answers, not just rc=0.
NAWK_ACCEPTS='
oneline-action-END	3 rc=0	{n++} END{print n}
oneline-two-patterns	B|C rc=0	/b/{print "B"} /c/{print "C"}
oneline-semicolon	3 rc=0	{n++}; END{print n}
oneline-BEGIN-END	s|e rc=0	BEGIN{print "s"} END{print "e"}
'

# Phase 4, second half: what nawk brings that the 1985 language does not have
# at all -- the constructs a user reaches for the moment this is a nawk.
#
# The printf cases pin the conversions nawk hands libc: %X %D %U %O are the
# long forms of %x %d %u %o.  An unknown conversion is copied literally and
# eats its argument, so these also check the arguments stay in step; one ends
# the format string.
NAWK_ONLY='
user-function	40320 rc=0	function f(n){if(n<=1)return 1; return n*f(n-1)}\nBEGIN{print f(8)}
split-and-array	3|a|c rc=0	BEGIN{n=split("a:b:c",A,":"); print n; print A[1]; print A[3]}
gsub	hell0 w0rld rc=0	BEGIN{s="hello world"; gsub(/o/,"0",s); print s}
sub-returns-count	1|baa rc=0	BEGIN{t="aaa"; print sub(/a/,"b",t); print t}
substr-index-length	ell|3|5 rc=0	BEGIN{print substr("hello",2,3); print index("hello","ll"); print length("hello")}
match-sets-RSTART	2|2|2 rc=0	BEGIN{print match("foobar",/o+/); print RSTART; print RLENGTH}
sprintf	a-2 rc=0	BEGIN{print sprintf("%s-%d","a",2)}
printf-hex	ff rc=0	BEGIN{printf "%x", 255}
printf-hex-upper	FF rc=0	BEGIN{printf "%X", 255}
printf-long-decimal	70000 rc=0	BEGIN{printf "%D", 70000}
printf-long-unsigned	70000 rc=0	BEGIN{printf "%U", 70000}
printf-long-octal	210560 rc=0	BEGIN{printf "%O", 70000}
printf-upper-then-text	FF! rc=0	BEGIN{printf "%X!", 255}
printf-upper-then-string	FF-ok rc=0	BEGIN{printf "%X-%s", 255, "ok"}
printf-conversions-in-step	ff|FF|10|7 rc=0	BEGIN{printf "%x|%X|%o|%u", 255, 255, 8, 7}
delete-and-in	1|0 rc=0	BEGIN{a["x"]=1; print ("x" in a); delete a["x"]; print ("x" in a)}
do-while	5 rc=0	BEGIN{i=0; do{i++}while(i<5); print i}
getline-var-from-file	a 1 rc=0	BEGIN{getline ln < "@IN@"; print ln}
real-division	0.333333 rc=0	BEGIN{print 1/3}
modulo-via-fmod	1 rc=0	BEGIN{print 7%3}
'

runcases() {			# runcases <binary> <caselist> <label>
	_bin=$1; _list=$2; _label=$3
	printf '%s\n' "$_list" | while IFS="$(printf '\t')" read -r nm want prog; do
		[ -n "$nm" ] || continue
		# \n is a rule separator (the whole point of phase 1); @IN@ is the
		# input file, which a program that opens it by name must be told.
		got=$(runawk "$_bin" "$(printf '%s' "$prog" |
			sed -e 's/\\n/\n/g' -e "s|@IN@|$IN|g")")
		if [ "$got" = "$want" ]; then
			echo "  ok   $_label $nm"
		else
			echo "  FAIL $_label $nm: got [$got] want [$want]"
		fi
	done
}

# The while loop above runs in a subshell, so counters set inside it are lost.
# Count from the output instead, which is also what gets printed.
tally() {			# tally <file>
	cat "$1"
	_o=$(grep -c '^  ok   ' "$1" 2>/dev/null); _o=${_o:-0}
	_f=$(grep -c '^  FAIL ' "$1" 2>/dev/null); _f=${_f:-0}
	PASSED=$((PASSED + _o))
	BAD=$((BAD + _f))
}

# MUTATE=break inverts phase 1's expectations; MUTATE=accept inverts phase 2's;
# MUTATE=samelang inverts phase 4's.
rewant() {			# rewant <caselist> <new-want> -- keep name+program
	printf '%s\n' "$1" | while IFS="$(printf '\t')" read -r nm want prog; do
		[ -n "$nm" ] || continue
		printf '%s\t%s\t%s\n' "$nm" "$2" "$prog"
	done
}
G=$GOOD
B=$BAD_CASES
N=$NAWK_ACCEPTS
case $MUTATE in
break)	G=$(rewant "$GOOD" " rc=1") ;;
accept)	B=$(rewant "$BAD_CASES" "ACCEPTED rc=0") ;;
samelang) N=$(rewant "$NAWK_ACCEPTS" " rc=1") ;;
none)	;;
*)	echo "awkrules: unknown MUTATE=$MUTATE" >&2; exit 2 ;;
esac

echo "awkrules: $AWK"
[ "$MUTATE" = none ] || echo "  (MUTATE=$MUTATE -- this run is EXPECTED to fail)"

echo "phase 1: newline-separated rules parse and bind"
runcases "$AWK" "$G" ours > "$WORK/p1"; tally "$WORK/p1"

echo "phase 2: two rules on one line are refused (the shipped dialect)"
runcases "$AWK" "$B" ours > "$WORK/p2"; tally "$WORK/p2"

# The message itself, once: a refusal that printed nothing would satisfy the
# rc check above and still be a regression (yyerror's far-pointer argument
# must be declared, or every syntax error comes out blank).
msg=$("$C900_EMU" --exec "$AWK" '{n++} END{print n}' "$IN" 2>&1 >/dev/null |
	sed -n '1p')
case $msg in
'awk: Syntax error')	ok "phase 2 message [$msg]" ;;
*)			fail "phase 2 message: got [$msg], want [awk: Syntax error]" ;;
esac

echo "phase 3: the 1985 original agrees, case for case"
if [ -f "$C900_AWK1985" ]; then
	echo "  oracle: $C900_AWK1985"
	runcases "$C900_AWK1985" "$G" 1985 > "$WORK/p3a"; tally "$WORK/p3a"
	runcases "$C900_AWK1985" "$B" 1985 > "$WORK/p3b"; tally "$WORK/p3b"
else
	echo "  SKIP no 1985 awk (set C900_AWK1985; it is in the C900"
	echo "       hardware holdings at firmware/hd/extracted/src/bin/awk)"
fi

echo "phase 4: nawk is the other dialect, and a superset of this one"
if [ -f "$NAWK" ]; then
	echo "  nawk: $NAWK"
	# The superset half: every must-work case, same answer.
	runcases "$NAWK" "$G" nawk > "$WORK/p4a"; tally "$WORK/p4a"
	# The other-dialect half: the four programs awk refuses, accepted, with
	# the right answers.
	runcases "$NAWK" "$N" nawk > "$WORK/p4b"; tally "$WORK/p4b"
	# What arrived with it.  Note `--exec' gives the guest a host-sized
	# stack; on the target a process gets 4 KB, ungrowing, and an awk-level
	# call costs most of 1 KB, so `user-function' at f(8) proves the
	# feature works here, not that recursion goes that deep on the target.
	runcases "$NAWK" "$NAWK_ONLY" nawk > "$WORK/p4c"; tally "$WORK/p4c"
	# The record bound in r.c: get1rec() reads into a BUFSIZ (512) buffer,
	# and an over-long record must be refused with a message -- unbounded,
	# it HANGS.  The line one character under the bound must still work.
	s=xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
	printf '%s%s%s%s%s%s%s%s\n' $s $s $s $s $s $s $s $s |
		cut -c1-511 > "$WORK/long"
	got=$("$C900_EMU" --exec "$NAWK" '{print length($0)}' "$WORK/long" 2>/dev/null)
	case $got in
	511)	ok "phase 4 record of 511 characters [$got]" ;;
	*)	fail "phase 4 record of 511 characters: got [$got], want [511]" ;;
	esac
	sed 's/$/yyyyyyyyy/' "$WORK/long" > "$WORK/toolong"     # 520
	msg=$("$C900_EMU" --exec "$NAWK" '{print length($0)}' "$WORK/toolong" 2>&1 \
		>/dev/null | sed -n '1p')
	case $msg in
	'awk: record too long')	ok "phase 4 record of 520 characters refused [$msg]" ;;
	*)	fail "phase 4 record of 520 characters: got [$msg], want [awk: record too long].  Unbounded, this HANGS -- do not remove the bound in r.c" ;;
	esac
else
	echo "  SKIP no nawk at $NAWK -- sh $HB/build-nawk.sh"
fi

echo
if [ "$BAD" -eq 0 ]; then
	echo "awkrules: $PASSED ok"
	exit 0
fi
echo "awkrules: $BAD FAILED, $PASSED ok"
exit 1
