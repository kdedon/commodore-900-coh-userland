#!/bin/sh
# run.sh -- boot the test image once per check and judge the transcript.
#
#	sh test/cmd/run.sh			every script
#	sh test/cmd/run.sh tarcycle mail	the ones named
#	sh test/cmd/run.sh --force ...		boot even a check the cache says passed
#	sh test/cmd/run.sh --selftest		show that the judge can fail
#
# Each script states its pass condition in `#%' header lines:
#
#	#% expect <ERE>	must match a whole transcript line (grep -Ex), so the
#			echoed command can't satisfy it.
#	#% reject <ERE>	must match no part of any line (grep -E).
#	#% floppy <n>	attach a blank n-block FLOPPY.
#	#% stop <list>	emu-run.sh's stop channels for this script.
#	#% host <why>	needs a host-side partner; skipped, never a pass.
#	#% needs <list>	the components this script tests, keying its skip
#			cache; none declared means all.
#
# A script with no `expect' or `host', or with an unknown `#%' keyword,
# fails.  So does a run that never reached emu-run.sh's done marker.
#
# Each check gets one verdict line (PASS, FAIL, HOST or SKIP) with its wall
# time; the exit status is the number of FAILs.  Transcripts are kept per
# run: <name>.out raw, .out.lines normalised (what is matched), .log
# emu-run.sh's output, .verdict the judgement.
#
# Boots run JOBS at a time (default the CPUs, at most 4), longest first by
# durations.tab.  external.list adds the checks with their own drivers to
# the same pool; --list prints the whole set in order.
#
# THE SKIP CACHE.  A check that passed is skipped while its key holds: its
# own file (or harness sources), the archives of the components it needs,
# the resolved kernel/kboot/tools releases, and run.sh and emu-run.sh.  Only
# passes are cached, in hostbuild/build/cmd-cache.tab.  --force boots anyway.
#

# --selftest judges one distsmoke transcript several ways that must FAIL
# (impossible expect, matching reject, echo-only expect, unfinished run, no
# pass condition, misspelt directive), and boots a hung `cat' to show park
# catches it.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
IMG=${IMG:-$ROOT/hostbuild/build/test.bin}
MARK=__EMU_DONE__
EXT=$HERE/external.list
DUR=$HERE/durations.tab
CACHE=${CACHE:-$ROOT/hostbuild/build/cmd-cache.tab}
PKGDIR=${PKGDIR:-$ROOT/hostbuild/build/packages}
FORCE=
while [ "${1:-}" = --force ]; do FORCE=1; shift; done

# ---------------------------------------------------------------------------
# External checks: booted checks with their own driver, from external.list.
# ---------------------------------------------------------------------------
is_ext() {
	[ -f "$EXT" ] && awk -v n="$1" '!/^#/ && NF && $1==n{f=1} END{exit !f}' "$EXT"
}
extfield() {	# extfield <name> <column: 2 expect, 3 tag, 4 command, 5 needs>
	awk -F'\t' -v n="$1" -v c="$2" \
		'!/^#/ && NF && $1==n { print $c; exit }' "$EXT"
}
extnames() {
	[ -f "$EXT" ] && awk '!/^#/ && NF { print $1 }' "$EXT"
}

# ---------------------------------------------------------------------------
# THE SKIP CACHE -- see the file header.  key_for() decides a SKIP.
# ---------------------------------------------------------------------------

# allcomps -- test/image/build.sh's COMPONENTS default.
allcomps() {
	sed -n 's/^COMPONENTS=\${COMPONENTS:-"\(.*\)"}$/\1/p' "$ROOT/test/image/build.sh"
}

# needs <name> -- a check's declared components, or allcomps().
needs() {
	_n=$1 _c=$HERE/$_n.cmd _l=
	if [ -f "$_c" ]; then
		_l=$(directive needs "$_c" | tr '\n' ' ')
	else
		_l=$(extfield "$_n" 5)
	fi
	_l=$(echo "$_l" | sed 's/^[ 	]*//;s/[ 	]*$//')
	[ -n "$_l" ] && [ "$_l" != - ] && echo "$_l" || allcomps
}

# contentid <component> -- the content id in its -bin package's .provenance,
# or `?' if uncut, which never matches, so the check boots.
contentid() {
	_v=$(sh "$ROOT/hostbuild/version.sh")
	_p=$PKGDIR/c900-$1-bin-v$_v.tar.gz
	[ -f "$_p" ] || { echo '?'; return; }
	tar xzfO "$_p" "c900-$1-bin-v$_v/.provenance" 2>/dev/null |
		sed -n 's/^contentid=//p'
}

# extsrc <name> -- an external check's sources: its `make -C' directory, or
# the script it runs.
extsrc() {
	_cmd=$(extfield "$1" 4)
	_dir=$(echo "$_cmd" | sed -n 's/.*-C  *\([^ 	]*\).*/\1/p')
	if [ -n "$_dir" ]; then
		find "$ROOT/$_dir" -maxdepth 1 -type f 2>/dev/null
	else
		for _w in $_cmd; do
			case $_w in *.py) echo "$ROOT/$_w" ;; esac
		done
	fi
}

# key_for <name> -- one hash over everything its verdict depends on.
key_for() {
	{
		sha1sum "$HERE/run.sh" "$ROOT/hostbuild/emu-run.sh" 2>/dev/null
		_c=$HERE/$1.cmd
		if [ -f "$_c" ]; then
			sha1sum "$_c"
		else
			extsrc "$1" | LC_ALL=C sort | xargs -r sha1sum
		fi
		for _n in $(needs "$1" | tr ' ' '\n' | LC_ALL=C sort -u); do
			[ -n "$_n" ] && echo "comp $_n $(contentid "$_n")"
		done
		for _e in kernel kboot tools; do
			echo "edge $_e $(sh "$ROOT/mk/deps.sh" -k "$_e" 2>/dev/null)"
		done
	} | sha1sum | cut -d' ' -f1
}

# cache_get <name> -- the key its last recorded PASS carried, or nothing.
cache_get() {
	[ -f "$CACHE" ] && awk -F'\t' -v n="$1" '$1==n{print $2; exit}' "$CACHE"
}

# cache_put <name> <key> -- record a PASS, replacing the check's old line.
# Locked: parallel checks finish at once.
cache_put() {
	_n=$1 _k=$2
	mkdir -p "$(dirname "$CACHE")"
	(
		flock 9
		_tmp=$CACHE.$$
		{ [ -f "$CACHE" ] && awk -F'\t' -v n="$_n" '$1!=n' "$CACHE"
		  printf '%s\t%s\n' "$_n" "$_k"
		} > "$_tmp"
		mv -f "$_tmp" "$CACHE"
	) 9>"$CACHE.lock"
}

# dur <name> -- its durations.tab time, or a short default.
dur() {
	_d=
	[ -f "$DUR" ] && _d=$(awk -v n="$1" '!/^#/ && NF && $1==n{print $2; exit}' "$DUR")
	echo "${_d:-30}"
}

# longest_first -- sort stdin's names by dur(), descending.
longest_first() {
	while IFS= read -r _n; do
		printf '%s\t%s\n' "$(dur "$_n")" "$_n"
	done | sort -t "$(printf '\t')" -k1,1rn | cut -f2
}

# ---------------------------------------------------------------------------
# boot_ext <name> <dir> -- run an external check, keeping <name>.log and
# <name>.time as boot() does.  TMPDIR is <dir>, so its scratch lands there.
# A `tag' serialises checks that must not overlap.
# ---------------------------------------------------------------------------
boot_ext() {
	_n=$1 _d=$2
	_cmd=$(extfield "$_n" 4)
	_tag=$(extfield "$_n" 3)
	_t0=$(date +%s)
	if [ -n "$_tag" ] && [ "$_tag" != - ]; then
		( flock 9
		  cd "$ROOT" && TMPDIR=$_d C900_ROOT=$ROOT sh -c "$_cmd" \
			> "$_d/$_n.log" 2>&1
		) 9> "$_d/.lock.$_tag"
	else
		( cd "$ROOT" && TMPDIR=$_d C900_ROOT=$ROOT sh -c "$_cmd" \
			> "$_d/$_n.log" 2>&1 )
	fi
	_st=$?
	echo $(( $(date +%s) - _t0 )) > "$_d/$_n.time"
	return $_st
}

# judge_ext <name> <status> -- exit status against the expected one; prints
# nothing and returns 0 on a pass.
judge_ext() {
	_n=$1 _st=$2
	_exp=$(extfield "$_n" 2); _exp=${_exp:-0}
	if [ "$_st" != "$_exp" ]; then
		echo "exit status $_st, expected $_exp -- see $_n.log"
		return 1
	fi
	return 0
}

# ---------------------------------------------------------------------------
# directive <keyword> <cmdfile> -- the argument of each such line.
# ---------------------------------------------------------------------------
directive() {
	sed -n "s/^#%[ 	]*$1[ 	][ 	]*//p" "$2"
}

# ---------------------------------------------------------------------------
# judge <cmdfile> <transcript> <finished>
#
# <finished> is 1 when the run reached the done marker.  Prints each reason
# the script failed and returns 1, or prints nothing and returns 0.  An
# unfinished run is named by the stop channel in its .log, when there is one.
#
# A bare CR starts a new line, as on screen; other control characters are
# dropped (mail's `you have mail' arrives behind a BEL).
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

	# A typo in an `expect' would be a missing pass condition.
	sed -n 's/^#%[ 	]*\([^ 	]*\).*/\1/p' "$_cmd" | while read -r _k; do
		case $_k in
		expect|reject|floppy|host|stop|needs) ;;
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
# boot <cmdfile> <dir> -- one run through emu-run.sh, its files under <dir>
# and its wall time in <name>.time.  Returns emu-run.sh's status.
# ---------------------------------------------------------------------------
boot() {
	_n=$(basename "$1" .cmd)
	_fl=$(directive floppy "$1" | tail -1)
	if [ -n "$_fl" ]; then
		# Fresh each run: emu-run.sh does not copy a floppy.
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

# Both the marker in the transcript and emu-run.sh's status: the marker
# could be typed ahead, the status could change meaning.
finished() {	# finished <status> <transcript>
	[ "$1" = 0 ] && grep -q "$MARK" "$2" && echo 1 || echo 0
}

# ---------------------------------------------------------------------------
# One script, start to verdict.  xargs runs this in parallel as `--one'.
# ---------------------------------------------------------------------------
one() {		# one <dir> <name>
	_c=$HERE/$2.cmd
	if [ -f "$_c" ]; then
		boot "$_c" "$1"
		_f=$(finished $? "$1/$2.out")
		_s=$(cat "$1/$2.time")
		judge "$_c" "$1/$2.out" "$_f" > "$1/$2.why"
	else
		boot_ext "$2" "$1"
		_est=$?
		_s=$(cat "$1/$2.time")
		judge_ext "$2" "$_est" > "$1/$2.why"
	fi
	if [ -s "$1/$2.why" ]; then
		_v="FAIL  $2 (${_s}s)"
	else
		_v="PASS  $2 (${_s}s)"
		cache_put "$2" "$(key_for "$2")"
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
	# `c900smokeok' appears only inside longer lines (the output and the
	# echo), so a whole-line expect must fail.
	case_ "an expect only the echoed command holds" FAIL "missing expect: /bin/echo c900smokeok > /smoke.txt" \
		"$(variant echo '#% expect /bin/echo c900smokeok > /smoke.txt')" "$fin" "$tr"
	# Cut before the verdict line, as a dead guest leaves it.
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

	# `cat' with nothing to read hangs, so the marker is never typed; park
	# must catch it within seconds.
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
# --list -- every check this run would boot, longest first; shard.sh reads it.
list_all() {
	_names="$(cd "$HERE" && ls *.cmd | sed 's/\.cmd$//') $(extnames)"
	for _n in $_names; do
		_c=$HERE/$_n.cmd
		if [ -f "$_c" ]; then
			[ -n "$(directive host "$_c")" ] && continue
			[ -z "$(directive expect "$_c")" ] && continue
		fi
		echo "$_n"
	done | longest_first
}

case ${1:-} in
--one)		shift; one "$@"; exit ;;
--selftest)	selftest ;;
--list)		list_all; exit ;;
-*)		echo "usage: run.sh [--force] [--selftest] [--list] [name...]" >&2; exit 2 ;;
esac

need_image
if [ $# -gt 0 ]; then
	names=
	for n; do
		n=${n%.cmd}; n=${n##*/}
		if [ -f "$HERE/$n.cmd" ] || is_ext "$n"; then
			:
		else
			echo "run.sh: no script or external check named $n" >&2
			echo "  The checks are:" $(cd "$HERE" && ls *.cmd | sed 's/\.cmd$//') $(extnames) >&2
			exit 2
		fi
		names="$names $n"
	done
else
	names="$(cd "$HERE" && ls *.cmd | sed 's/\.cmd$//') $(extnames)"
fi

RES=${RESULTS:-$ROOT/hostbuild/build/cmd-results}
rm -rf "$RES"; mkdir -p "$RES"
JOBS=${JOBS:-$(ncpu)}

# A host script is listed, not run; one with no pass condition fails without
# a boot; a check whose cache key matches is SKIPped here.
list=$RES/.torun
: > "$list"
for n in $names; do
	c=$HERE/$n.cmd
	if [ -f "$c" ]; then
		h=$(directive host "$c" | head -1)
		if [ -n "$h" ]; then
			echo "HOST  $n: $h" > "$RES/$n.verdict"
			continue
		fi
		if [ -z "$(directive expect "$c")" ]; then
			{ echo "FAIL  $n"
			  echo "        no pass condition (no \`#% expect' and no \`#% host' line)"
			} > "$RES/$n.verdict"
			continue
		fi
	fi
	if [ -z "$FORCE" ]; then
		key=$(key_for "$n")
		if [ "$(cache_get "$n")" = "$key" ]; then
			echo "SKIP  $n: unchanged since its last recorded pass (key ${key})" \
				> "$RES/$n.verdict"
			continue
		fi
	fi
	echo "$n" >> "$list"
done

# Longest first, so the slowest never starts last.
longest_first < "$list" > "$list.ord"; mv "$list.ord" "$list"

echo "run.sh: $(wc -l < "$list") check(s) to boot, $JOBS at a time, image $IMG"
# Each output line is a verdict as it lands; the summary repeats them in
# name order.  xargs runs once even on no input, so skip an empty list.
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
		# The worker died before writing one: count it as a failure.
		echo "FAIL  $n" > "$RES/$n.verdict"
		echo "        no verdict was written (the worker died)" >> "$RES/$n.verdict"
		cat "$RES/$n.verdict"
	fi
	head -1 "$RES/$n.verdict" | grep -q '^FAIL' && fails=$((fails + 1))
done
echo "==== $fails failure(s); transcripts under $RES"
exit $fails
