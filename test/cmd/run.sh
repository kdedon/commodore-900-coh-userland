#!/bin/sh
# run.sh -- boot the test image once per test/cmd script and JUDGE the
# transcript, rather than leaving it for somebody to read.
#
#	sh test/cmd/run.sh			every script
#	sh test/cmd/run.sh tarcycle mail	the ones named
#	sh test/cmd/run.sh --selftest		show that the judge can fail
#
# Each script states its own pass condition, in its header, in comment lines
# emu-run.sh already skips.  They are the prose above them made mechanical:
#
#	#% expect <ERE>	must match a WHOLE LINE of the transcript (grep -Ex).
#			The console echoes every command after a `# ' prompt,
#			so `SMOKE=c900smokeok' cannot be satisfied by the echo
#			of the command that prints it -- the echo is a longer
#			line.  That is the "count them at the START of a line"
#			the prose asks a reader for.
#	#% reject <ERE>	must match NO PART of any line (grep -E): a
#			`Segmentation violation', a panic, a `FAIL <tag>'.
#	#% floppy <n>	attach a blank n-block medium as FLOPPY, for a script
#			whose subject is a filesystem it makes from nothing.
#	#% stop <list>	the emulator stop channels for this script, read by
#			emu-run.sh itself: `mark' alone for one that sleeps
#			longer than park can tell from a hang.
#	#% host <why>	the script needs a partner on the host side -- a
#			second machine, a serial peer, a net/ harness -- and
#			cannot run standalone.  It is skipped and listed, with
#			the reason, and never counted as a pass.
#
# IT IS A GATE, NOT A REPORT.  A script with neither an `expect' nor a `host'
# line is itself a FAIL, "no pass condition": a script that nothing judges
# asserts nothing, and a new one must not arrive as decoration.  A `#%' line
# with any other keyword is a FAIL too -- a misspelt `expect' would otherwise
# be a pass condition that silently does not exist.
#
# A run that never reached emu-run.sh's done marker FAILS whatever else it
# printed: a transcript that simply stops is the commonest way a guest dies,
# and every marker it did print is still no evidence about the ones after.
#
# The verdict for each script is one line, PASS or FAIL or HOST, with the
# wall time of its boot; a FAIL names each missing expect and each reject
# that matched.  The exit status is the number of FAILs.  Transcripts are
# kept, one directory per run, named at the end: <name>.out is the raw
# console, <name>.out.lines the same with its line ends and control
# characters normalised (what the patterns are matched against), <name>.log
# emu-run.sh's own output, <name>.verdict the judgement.
#
# The boots run in parallel, JOBS at a time (default: the CPUs, at most 4), in
# listing order.  Each boot runs on emu-run.sh's own copy of the image, so they
# cannot meet.  Nothing here is a wall clock: each boot ends when emu-run.sh's
# own emulator does, on the mark it printed, on a park or idle stall, or
# emu-run.sh would not have exited either.
#
# --selftest boots ONE script, distsmoke, and judges that one transcript
# several ways with the same judge() the real run uses: as it stands (PASS),
# with an expect that cannot appear (FAIL), with a reject that does appear
# (FAIL), with an expect that only the ECHO of a command could satisfy (FAIL),
# as a run that never finished (FAIL), and as a script with no pass condition
# or a misspelt directive (FAIL).  It also boots a SECOND, genuinely hung
# script -- a `cat' with nothing to read -- and shows that a stall is caught
# and named by the channel that caught it (park), not by a clock.  A judge
# that has never been seen to fail proves nothing, and this is what shows it
# can.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
IMG=${IMG:-$ROOT/hostbuild/build/test.bin}
MARK=__EMU_DONE__

# ---------------------------------------------------------------------------
# The directives.  `directive <keyword> <cmdfile>' prints the argument of each
# such line, one per line, with the keyword and the blanks after it removed.
# ---------------------------------------------------------------------------
directive() {
	sed -n "s/^#%[ 	]*$1[ 	][ 	]*//p" "$2"
}

# ---------------------------------------------------------------------------
# judge <cmdfile> <transcript> <finished>
#
# The whole of the verdict, and the one piece of this file the selftest
# exercises by name.  <finished> is 1 when the run reached the done marker.
# Prints one line per reason the script failed and returns 1, or prints
# nothing and returns 0.
#
# A run that did not finish is named by the channel that ended it, read from
# emu-run.sh's own "=== stop channel: X" line in <transcript-without-.out>.log
# next to it -- "stalled: park" or "stalled: idle" rather than the bare "did
# not finish" that is all that is left to say when no such log is at hand (the
# selftest's own truncated-transcript cases have none).
#
# The console ends its lines CR-LF, and a program may return the carriage
# alone to overwrite a line; a bare CR is taken as the start of a new line,
# which is where the text after it begins on the screen.  The other control
# characters are dropped, since a terminal prints none of them: mail's `you
# have mail' arrives behind a BEL, and the login prompt does too.
# ---------------------------------------------------------------------------
judge() {
	_cmd=$1 _tr=$2 _fin=$3
	_lines=$_tr.lines
	_why=$_tr.why
	_log=${_tr%.out}.log
	: > "$_why"
	sed 's/\r$//' "$_tr" | tr '\r' '\n' | tr -d '\000-\010\013-\037' > "$_lines"

	if [ "$_fin" != 1 ]; then
		_chan=''
		[ -f "$_log" ] &&
			_chan=$(sed -n 's/^=== stop channel: \([a-z]*\).*/\1/p' "$_log" | tail -1)
		if [ -n "$_chan" ] && [ "$_chan" != mark ]; then
			echo "stalled: $_chan" >> "$_why"
		else
			echo "did not finish (no $MARK in the transcript)" >> "$_why"
		fi
	fi

	# Any other keyword is a typo, and a typo in an `expect' is a pass
	# condition that is not there.
	sed -n 's/^#%[ 	]*\([^ 	]*\).*/\1/p' "$_cmd" | while read -r _k; do
		case $_k in
		expect|reject|floppy|host|stop) ;;
		*) echo "unknown directive \`#% $_k'" ;;
		esac
	done >> "$_why"

	if [ -z "$(directive expect "$_cmd")" ]; then
		echo "no pass condition (no \`#% expect' and no \`#% host' line)" >> "$_why"
	fi

	directive expect "$_cmd" > "$_tr.expect"
	while IFS= read -r _p; do
		grep -E -x -q -e "$_p" "$_lines" 2>/dev/null
		case $? in
		0) ;;
		1) echo "missing expect: $_p" ;;
		*) echo "expect is not a valid ERE: $_p" ;;
		esac
	done < "$_tr.expect" >> "$_why"

	directive reject "$_cmd" > "$_tr.reject"
	while IFS= read -r _p; do
		grep -E -n -e "$_p" "$_lines" > "$_tr.hit" 2>/dev/null
		case $? in
		0) echo "matched reject: $_p -- line $(head -1 "$_tr.hit")" ;;
		1) ;;
		*) echo "reject is not a valid ERE: $_p" ;;
		esac
	done < "$_tr.reject" >> "$_why"

	rm -f "$_tr.expect" "$_tr.reject" "$_tr.hit"
	cat "$_why"
	[ ! -s "$_why" ]
	_rc=$?
	rm -f "$_why"
	return $_rc
}

# ---------------------------------------------------------------------------
# boot <cmdfile> <dir> -- one run through emu-run.sh, its files kept under
# <dir> by the script's name, the wall time in <name>.time.  The status is
# emu-run.sh's: 0 for a run that reached the marker.
# ---------------------------------------------------------------------------
boot() {
	_n=$(basename "$1" .cmd)
	_fl=$(directive floppy "$1" | tail -1)
	if [ -n "$_fl" ]; then
		# Blank, and made fresh for each run: emu-run.sh does not copy a
		# floppy, so the medium carries whatever the last run wrote.
		dd if=/dev/zero of="$2/$_n.fd" bs=512 count="$_fl" 2>/dev/null
		FLOPPY=$2/$_n.fd
		export FLOPPY
	fi
	_t0=$(date +%s)
	OUT=$2/$_n.out ERR=$2/$_n.err \
		sh "$ROOT/hostbuild/emu-run.sh" "$1" "$IMG" > "$2/$_n.log" 2>&1
	_st=$?
	echo $(( $(date +%s) - _t0 )) > "$2/$_n.time"
	return $_st
}

# The done marker is in the transcript AND emu-run.sh said so: either alone
# could be fooled -- the one by a marker typed ahead, the other by a change to
# emu-run.sh's exit status.
finished() {	# finished <status> <transcript>
	[ "$1" = 0 ] && grep -q "$MARK" "$2" && echo 1 || echo 0
}

# ---------------------------------------------------------------------------
# One script, start to verdict.  xargs runs this in parallel as `--one'.
# ---------------------------------------------------------------------------
one() {		# one <dir> <name>
	_c=$HERE/$2.cmd
	boot "$_c" "$1"
	_f=$(finished $? "$1/$2.out")
	_s=$(cat "$1/$2.time")
	if judge "$_c" "$1/$2.out" "$_f" > "$1/$2.why"; then
		_v="PASS  $2 (${_s}s)"
	else
		_v="FAIL  $2 (${_s}s)"
	fi
	{ echo "$_v"; sed 's/^/        /' "$1/$2.why"; } > "$1/$2.verdict"
	rm -f "$1/$2.why"
	cat "$1/$2.verdict"
}

need_image() {
	if [ ! -f "$IMG" ]; then
		echo "run.sh: no test image at $IMG" >&2
		echo "  Pack it from this build first:  sh $ROOT/test/image/build.sh" >&2
		exit 2
	fi
}

ncpu() {
	_c=$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 1)
	[ "$_c" -gt 4 ] && _c=4
	echo "$_c"
}

# ---------------------------------------------------------------------------
# --selftest
# ---------------------------------------------------------------------------
selftest() {
	need_image
	D=$ROOT/hostbuild/build/cmd-selftest
	rm -rf "$D"; mkdir -p "$D"
	bad=0
	src=$HERE/distsmoke.cmd
	echo "selftest: booting distsmoke once ($D)"
	boot "$src" "$D"
	fin=$(finished $? "$D/distsmoke.out")
	tr=$D/distsmoke.out

	# case <name> <want: PASS or FAIL> <reason it must give, or -> <cmdfile> <finished> <transcript>
	case_() {
		judge "$4" "$6" "$5" > "$D/why" && got=PASS || got=FAIL
		if [ "$got" != "$2" ]; then
			echo "  WRONG $1: judged $got, wanted $2"
			sed 's/^/        | /' "$D/why"
			bad=$((bad + 1))
		elif [ "$3" != - ] && ! grep -F -q -e "$3" "$D/why"; then
			echo "  WRONG $1: FAIL, but not for \`$3':"
			sed 's/^/        | /' "$D/why"
			bad=$((bad + 1))
		else
			if [ "$3" = - ]; then echo "  ok    $1: $got"
			else echo "  ok    $1: $got, $3"; fi
		fi
	}
	# Each variant is the real script with lines added, so every other
	# condition it states still holds and the one added is what decides.
	variant() {	# variant <name> <line>...
		_v=$D/$1.cmd; shift
		cp "$src" "$_v"
		for _l; do echo "$_l" >> "$_v"; done
		echo "$_v"
	}

	case_ "the script as it stands" PASS - "$src" "$fin" "$tr"
	case_ "an expect that cannot appear" FAIL "missing expect: ^NEVER-PRINTED$" \
		"$(variant never '#% expect ^NEVER-PRINTED$')" "$fin" "$tr"
	case_ "a reject that does appear" FAIL "matched reject: ^SMOKE=" \
		"$(variant reject '#% reject ^SMOKE=')" "$fin" "$tr"
	# `c900smokeok' is on two lines of the transcript, and on neither as the
	# whole line: `SMOKE=c900smokeok' is the output and `# /bin/echo
	# c900smokeok > /smoke.txt' the echo.  An expect matched anywhere in a
	# line would pass this.
	case_ "an expect only the echoed command holds" FAIL "missing expect: /bin/echo c900smokeok > /smoke.txt" \
		"$(variant echo '#% expect /bin/echo c900smokeok > /smoke.txt')" "$fin" "$tr"
	# The transcript cut before the verdict line, as a guest that died there
	# leaves it: both the marker and the SMOKE line are then missing.
	sed '/^SMOKE=/,$d' "$tr" > "$D/cut.out"
	case_ "a run that stopped part way" FAIL "did not finish" "$src" 0 "$D/cut.out"
	case_ "  ... and its missing marker named too" FAIL "missing expect: ^SMOKE=c900smokeok$" \
		"$src" 0 "$D/cut.out"
	grep -v '^#%' "$src" > "$D/bare.cmd"
	case_ "a script with no pass condition" FAIL "no pass condition" "$D/bare.cmd" "$fin" "$tr"
	case_ "a misspelt directive" FAIL "unknown directive" \
		"$(variant typo '#% expcet ^SMOKE=c900smokeok$')" "$fin" "$tr"
	case_ "an expect that is not an ERE" FAIL "not a valid ERE" \
		"$(variant badre '#% expect ^SMOKE=(c900$')" "$fin" "$tr"

	# A script that genuinely hangs: `cat' with nothing to read blocks
	# forever, so no scripted byte after it -- the marker itself included
	# -- is ever typed.  This is what --stop-on=park is for: catching the
	# stall in the few seconds park's own budget takes, named by channel,
	# rather than an EMUWAIT hour spent finding out the guest died.
	hang=$D/hang.cmd
	{ echo '#% expect ^HANG_NEVER_PRINTED$'; echo 'cat'; } > "$hang"
	echo "selftest: booting a script that hangs ($D/hang.out)"
	boot "$hang" "$D"
	hfin=$(finished $? "$D/hang.out")
	case_ "a guest that hangs" FAIL "stalled: park" "$hang" "$hfin" "$D/hang.out"

	rm -f "$D/why"
	echo "selftest: transcript kept in $D"
	if [ $bad -ne 0 ]; then
		echo "selftest: $bad case(s) judged wrongly -- the judge cannot be trusted"
		exit 1
	fi
	echo "selftest: the judge passes what it should and fails what it should"
	exit 0
}

# ---------------------------------------------------------------------------
# The run.
# ---------------------------------------------------------------------------
case ${1:-} in
--one)		shift; one "$@"; exit ;;
--selftest)	selftest ;;
-*)		echo "usage: run.sh [--selftest] [name...]" >&2; exit 2 ;;
esac

need_image
if [ $# -gt 0 ]; then
	names=
	for n; do
		n=${n%.cmd}; n=${n##*/}
		[ -f "$HERE/$n.cmd" ] || {
			echo "run.sh: no script $HERE/$n.cmd" >&2
			echo "  The scripts are:" $(cd "$HERE" && ls *.cmd | sed 's/\.cmd$//') >&2
			exit 2; }
		names="$names $n"
	done
else
	names=$(cd "$HERE" && ls *.cmd | sed 's/\.cmd$//')
fi

RES=${RESULTS:-$ROOT/hostbuild/build/cmd-results}
rm -rf "$RES"; mkdir -p "$RES"
JOBS=${JOBS:-$(ncpu)}

# Sort out what boots: a host script is listed, not run, and a script with no
# pass condition is failed without spending a boot on it.
list=$RES/.torun
: > "$list"
for n in $names; do
	c=$HERE/$n.cmd
	h=$(directive host "$c" | head -1)
	if [ -n "$h" ]; then
		echo "HOST  $n: $h" > "$RES/$n.verdict"
	elif [ -z "$(directive expect "$c")" ]; then
		{ echo "FAIL  $n"
		  echo "        no pass condition (no \`#% expect' and no \`#% host' line)"
		} > "$RES/$n.verdict"
	else
		echo "$n" >> "$list"
	fi
done

echo "run.sh: $(wc -l < "$list") script(s) to boot, $JOBS at a time, image $IMG"
# Listing order -- there is no wait ceiling left to sort by, and none of these
# boots has a clock of its own to race: each ends on its own guest's mark, or
# on the channel that caught its stall.  Each line of output is one script's
# verdict as it lands; the summary below repeats them in name order.
# xargs runs its command once even on no input, so an empty list is not
# handed to it.
if [ -s "$list" ]; then
	xargs -n 1 -P "$JOBS" sh "$HERE/run.sh" --one "$RES" < "$list"
fi
rm -f "$list"

echo
echo "==== verdicts"
fails=0
for n in $names; do
	if [ -f "$RES/$n.verdict" ]; then
		cat "$RES/$n.verdict"
	else
		# The worker died before it could write one: that is a failure
		# too, and it must not vanish from the count.
		echo "FAIL  $n" > "$RES/$n.verdict"
		echo "        no verdict was written (the worker died)" >> "$RES/$n.verdict"
		cat "$RES/$n.verdict"
	fi
	head -1 "$RES/$n.verdict" | grep -q '^FAIL' && fails=$((fails + 1))
done
echo "==== $fails failure(s); transcripts under $RES"
exit $fails
