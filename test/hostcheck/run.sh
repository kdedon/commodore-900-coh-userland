#!/bin/sh
# run.sh -- the discrimination matrix for the test probes.
#
# For every probe: run it against a system that WORKS (which it must pass),
# and against each broken system kshim.c can build (which it must fail).
# Every line marked `1' below demonstrates that one particular defect is
# caught by name.
#
# It is a gate, not a report: a probe that no mutation can make fail is named
# at the end, under "PROVES NOTHING", and the run fails; and every directory
# in test must appear either in the matrix below or in the UNCOVERED list
# with a reason, or the run fails.
#
#	sh run.sh			everything but the slow deadline cases
#	sh run.sh sbrkzero		one probe
#	SLOW=1 sh run.sh		including them (about six minutes more)
#
# The deadline cases: some defects have no wrong answer to give -- a select()
# that never wakes, a process that wedges -- and the demonstration is that
# each probe's own alarm turns the hang into a reported failure.  They cost
# that probe's DEADLINE, which is why the long ones are behind $SLOW.
#
# Host-only.  Nothing here runs on the C900.
set -u
cd "$(dirname "$0")" || exit 2

ONLY=${1:-}
SLOW=${SLOW:-0}
PASSED=0
BAD=0
# What did not behave, by name.  The count alone is not a diagnosis: the run
# is read from a sweep that shows its last few lines, and "1 did not" without
# a name sends the reader back to a run of their own to find out which.
WHY=""
why() { WHY="$WHY, $*"; }
DISCRIM=""			# probes seen to fail against some mutation
RAN=""				# probes exercised at all

WORK=$PWD/work
export KWORK=$WORK
LOG=$WORK/.log

rm -rf "$WORK"
mkdir -p "$WORK/tmp"
trap 'rm -f "$LOG"' 0 1 2 15

# case <want> <name> <prog> [args...]
#
# `want' is 0 for a system that works and 1 for one that is broken; 2 is a
# skip.  Anything else -- a crash, a harness timeout -- fails the case whichever
# way it went.
report() {			# name want got
	if [ "$2" = "$3" ]; then
		echo "  ok   $1 (exit $3)"
		PASSED=$((PASSED + 1))
	else
		echo "  FAIL $1: exit $3, wanted $2"
		sed 's/^/       | /' "$LOG" | tail -6
		# The exit status goes in the SUMMARY line too.  A sweep shows the
		# last few lines of a failing run and no more, so a name without a
		# status sends the reader back for an hour-long run of their own to
		# learn whether the case decided anything (124 is the harness's own
		# timeout, and decides nothing).
		why "$CUR: $1 (exit $3, wanted $2)"
		BAD=$((BAD + 1))
	fi
}

# want <want> <name> -- the command follows.  $CAP is the harness's own patience,
# which must always exceed the probe's DEADLINE: the probe is supposed to report
# its own hang, and a harness that killed it first would hide exactly that.
CAP=${CAP:-90}

# The probe's own DEADLINE, read from its source rather than restated here.
# Two numbers that have to stay ordered cannot be kept ordered by hand: three
# cases had drifted the wrong way, and the one that showed it did so only under
# load, as an exit 124 with no diagnosis, in a sweep an hour long.
probe_deadline() {		# ./NAME-h -> seconds, or nothing
	_p=${1#./}; _p=${_p%-h}
	sed -n 's/^#define[ 	]*DEADLINE[ 	]*\([0-9][0-9]*\).*/\1/p' \
		"../$_p/$_p.c" 2>/dev/null | head -1
}

# Refuses a case whose patience does not exceed the probe's own, rather than
# quietly raising it: an inverted cap means the case cannot decide what it was
# written to decide, and correcting it silently would leave the next one to
# drift the same way.
cap_ok() {			# name prog
	_d=$(probe_deadline "$2")
	[ -n "$_d" ] || return 0
	[ "$CAP" -gt "$_d" ] && return 0
	echo "  FAIL $1: CAP=$CAP does not exceed ${2#./}'s own DEADLINE=$_d,"
	echo "       so the harness kills the probe before it can report its"
	echo "       own hang -- which is the defect the probe exists to catch."
	why "$CUR: CAP=$CAP <= DEADLINE=$_d"
	BAD=$((BAD + 1))
	return 1
}

want() {
	w=$1; n=$2; shift 2
	cap_ok "$n" "$1" || return 0
	rm -f "$WORK"/oxtst* "$WORK"/deepstack.out "$WORK"/pp.fifo \
		"$WORK"/tmp/pt "$WORK"/tmp/pollexit.p 2>/dev/null
	timeout "$CAP" "$@" > "$LOG" 2>&1 </dev/null
	got=$?
	RAN="$RAN $CUR"
	[ "$w" = 1 ] && [ "$got" = 1 ] && DISCRIM="$DISCRIM $CUR"
	report "$n" "$w" "$got"
}

sel() { CUR=$1; [ -z "$ONLY" ] || [ "$ONLY" = "$1" ]; }
slow() { [ "$SLOW" = 1 ]; }

# --------------------------------------------------------------------------
# ddtentry -- halt(2) returns a value and it has to be tested; both refusals
# are here.
# --------------------------------------------------------------------------
if sel ddtentry; then
echo "ddtentry (does it tell entering the debugger from being refused?)"
BREAK=none        want 0 "halt(2) enters ddt and returns"	./ddtentry-h
BREAK=halt-unimpl want 1 "halt(2) is not implemented"		./ddtentry-h
BREAK=halt-eperm  want 1 "halt(2) refused: not root"		./ddtentry-h
fi

# --------------------------------------------------------------------------
# sbrkzero -- zero fill, and the break carrying across segments.
# --------------------------------------------------------------------------
if sel sbrkzero; then
echo "sbrkzero (is sbrk memory zero, and does the break carry into the next segment?)"
BREAK=none         want 0 "a kernel that clears and carries"	./sbrkzero-h
BREAK=sbrk-dirty   want 1 "new memory arrives full of 0xA5"	./sbrkzero-h
BREAK=sbrk-refuse  want 1 "sbrk refuses (NULL, not -1)"		./sbrkzero-h
BREAK=sbrk-nocarry want 1 "the break stops at the first segment"	./sbrkzero-h
BREAK=sbrk-wrap    want 1 "the break WRAPS onto memory in use"	./sbrkzero-h
BREAK=sbrk-liar    want 1 "sbrk(0) is not where the last block ended" ./sbrkzero-h
fi

# --------------------------------------------------------------------------
# strnlong -- the subject is strncmp/strncpy/strncat themselves, so the shim
# supplies them: once correct, once with each of the two documented bugs.
# --------------------------------------------------------------------------
if sel strnlong; then
echo "strnlong (counted string routines against an operand over 32767 bytes)"
BREAK=none        want 0 "correct routines"			./strnlong-h
BREAK=strn-signed want 1 "the length/n clamp is SIGNED"		./strnlong-h
BREAK=strn-count0 want 1 "a count of 0 moves 65536 bytes"	./strnlong-h
fi

# --------------------------------------------------------------------------
# tickrate -- a rate is a quotient and both its terms have failure modes.
# --------------------------------------------------------------------------
if sel tickrate; then
echo "tickrate (a rate whose terms are both suspect)"
BREAK=none       want 0 "a clock that advances"			./tickrate-h 1
BREAK=none       want 1 "a sample of 0 seconds measures nothing"	./tickrate-h 0
BREAK=clock-back want 1 "the clock runs backwards"		./tickrate-h 1
if slow; then
CAP=120 BREAK=clock-stuck want 1 "the clock never advances (deadline)" ./tickrate-h 1
fi
fi

# --------------------------------------------------------------------------
# lptest -- three outcomes that mean three different things.
# --------------------------------------------------------------------------
if sel lptest; then
echo "lptest (is the printer driver dispatched on major 3?)"
BREAK=none     want 0 "driver answers EDATTN: no printer"	./lptest-h
BREAK=lp-enxio want 1 "ENXIO: major 3 has no driver"		./lptest-h
BREAK=lp-eio   want 1 "some other failure entirely"		./lptest-h
fi

# --------------------------------------------------------------------------
# The poll family.  Each probe asks a different question of the same subsystem,
# so each is broken in the way it names and not in the others'.
# --------------------------------------------------------------------------
if sel pollhup; then
echo "pollhup (EOF on a pipe nobody can write to)"
CAP=120 BREAK=none          want 0 "a kernel that reports POLLHUP"	./pollhup-h
CAP=120 BREAK=poll-nohup    want 1 "POLLHUP/POLLERR never reported"	./pollhup-h
CAP=120 BREAK=poll-alwayshup want 1 "POLLHUP reported for everything"	./pollhup-h
fi

if sel pollfifo; then
echo "pollfifo (does select() on a FIFO block and then wake?)"
BREAK=none      want 0 "a kernel that waits and wakes"		./pollfifo-h
BREAK=poll-ready want 1 "an empty FIFO answered readable"	./pollfifo-h
BREAK=poll-nval want 1 "the FIFO is not a pollable object"	./pollfifo-h
CAP=120 BREAK=sel-deaf want 1 "select() never wakes (deadline)"	./pollfifo-h
fi

if sel pollpipe; then
echo "pollpipe (the slip shape: a write while select() is already blocked)"
BREAK=none      want 0 "a kernel that wakes for the write"	./pollpipe-h
BREAK=sel-ready want 1 "an empty fifo answered readable"	./pollpipe-h
BREAK=sel-sticky want 1 "readable again after being drained"	./pollpipe-h
CAP=120 BREAK=sel-deaf want 1 "select() never wakes (deadline)"	./pollpipe-h
fi

# --------------------------------------------------------------------------
# selectlong -- does a timed select() wait the length it was given?  Every
# case costs the wait it measures, so only the pass and the defect itself run
# by default; the other three ways of serving the wrong wait are behind $SLOW.
#
# 36 seconds, not the probe's own default of 40: it only has to sit far enough
# past the 32 s a single poll(2) reaches that the probe's 2 s allowance cannot
# confuse the two verdicts -- a correct wait measures 35 or 36, a clamped one
# 31 or 32, and the threshold is 34.  Every second of it is paid for in real
# time, so no more than that.
# --------------------------------------------------------------------------
if sel selectlong; then
echo "selectlong (does select() wait as long as it was asked to?)"
CAP=180 BREAK=none        want 0 "a select() that waits its term"	./selectlong-h 36
CAP=180 BREAK=sel-clamp32 want 1 "a long wait is cut to 32 s and called a timeout" ./selectlong-h 36
if slow; then
CAP=180 BREAK=sel-roundup   want 1 "a short wait is rounded up to 32 s"	./selectlong-h 36
CAP=180 BREAK=sel-chunkwait want 1 "a wakeup inside a piece waits for the piece" ./selectlong-h 36
CAP=180 BREAK=sel-negzero   want 1 "a negative timeout is served, not refused" ./selectlong-h 36
fi
fi

if sel pollexit; then
echo "pollexit (what a signal does to the poll event buffer)"
KSTDIN=pipe CAP=150 BREAK=none      want 0 "a kernel that unlinks on every exit" ./pollexit-h
KSTDIN=pipe CAP=150 BREAK=poll-ready want 1 "poll answers ready, never EINTR"	./pollexit-h
if slow; then
KSTDIN=pipe CAP=150 BREAK=poll-nosig want 1 "an INFTIM poll ignores signals (deadline)" ./pollexit-h
fi
fi

if sel polltty; then
echo "polltty (can poll(2) wait on a terminal?)"
KSTDIN=pipe BREAK=none       want 0 "a tty with a c_poll entry point"	./polltty-h
KSTDIN=pipe BREAK=poll-nval  want 1 "the tty is not pollable (POLLNVAL)"	./polltty-h
KSTDIN=pipe BREAK=poll-ready want 1 "always readable: a game at 100% cpu"	./polltty-h
KSTDIN=pipe BREAK=poll-noout want 1 "the console is never writable"	./polltty-h
fi

# --------------------------------------------------------------------------
# openexcl -- two pairs, and both halves of each pair matter.  Each mutation
# below fails exactly the pair its name predicts.
# --------------------------------------------------------------------------
if sel openexcl; then
echo "openexcl (O_EXCL excludes while open, and only then)"
BREAK=none               want 0 "marks set and cleared correctly"	./openexcl-h
BREAK=excl-never-set     want 1 "the mark is never set (checks 2,3)"	./openexcl-h
BREAK=excl-never-clear   want 1 "the mark is never cleared (1,4)"	./openexcl-h
BREAK=excl-leak-on-death want 1 "cleared on close, not on death (4)"	./openexcl-h
fi

# --------------------------------------------------------------------------
# kalloc -- the arena, from outside.
# --------------------------------------------------------------------------
if sel kalloc; then
echo "kalloc (how much of the kernel arena is left?)"
CAP=150 BREAK=none     want 0 "an arena with room"		./kalloc-h
CAP=150 BREAK=pty-few  want 1 "only two pty masters open"	./kalloc-h
CAP=150 BREAK=fork-cap want 1 "fork refused after five children"	./kalloc-h
if slow; then
CAP=200 BREAK=child-mute want 1 "a child never answers (deadline)"	./kalloc-h
fi
fi

# --------------------------------------------------------------------------
# termio -- the round trip and VMIN/VTIME.
# --------------------------------------------------------------------------
if sel termio; then
echo "termio (a termios round trip, and VMIN/VTIME)"
KSTDIN=pipe BREAK=none          want 0 "a driver that stores and times"	./termio-h
KSTDIN=pipe BREAK=tc-noveol     want 1 "TCSETA loses the VEOL/VTIME slot"	./termio-h
KSTDIN=pipe BREAK=vtime-instant want 1 "the VTIME timer is never armed"	./termio-h
KSTDIN=pipe BREAK=not-a-tty     want 2 "not a terminal: skip, not pass"	./termio-h
if slow; then
KSTDIN=pipe CAP=120 BREAK=tc-setnop want 1 "TCSETA is \`return 0' (deadline)" ./termio-h
KSTDIN=pipe CAP=120 BREAK=vtime-forever want 1 "the read never returns (deadline)" ./termio-h
fi
fi

# --------------------------------------------------------------------------
# hrglyph -- the loadable font bank, five distinct outcomes.
# --------------------------------------------------------------------------
if sel hrglyph; then
echo "hrglyph (the loadable font bank, and its edges)"
BREAK=none          want 0 "a console that loads and protects"	./hrglyph-h
BREAK=font-nomatch  want 1 "the bank reads back different bytes"	./hrglyph-h
BREAK=font-lowwrite want 1 "the read-only bank accepts a write"	./hrglyph-h
BREAK=font-past     want 1 "a write runs past the last slot"	./hrglyph-h
BREAK=font-none     want 2 "no font ioctl: skip, not fail"	./hrglyph-h
fi

# --------------------------------------------------------------------------
# deepstack -- the whole allowance, and a clean fault at it.  `ulimit -s 32'
# gives the host process the 32K allowance MADSIZE gives the target; it has to
# be in force at exec, so it cannot be done from inside the shim.
#
# This runs the probe against the HOST kernel, which grows a stack the way no
# Z8001 can: it decides only that the probe's own arithmetic and verdicts are
# right, and each mutation below is caught.  Whether the C900 kernel hands out
# the allowance is decided by tests/deepstack/run.sh, on a booted system.
# --------------------------------------------------------------------------
if sel deepstack; then
echo "deepstack (stack growth to the ceiling, and a clean fault there)"
deep() { w=$1; n=$2; b=$3; f=$4
	cap_ok "$n" ./deepstack-h || return 0
	rm -f "$WORK/deepstack.out"
	( ulimit -s 32; BREAK=$b timeout "${CAP}" ./deepstack-h "$f" ) \
		> "$LOG" 2>&1 </dev/null
	got=$?
	RAN="$RAN $CUR"
	[ "$w" = 1 ] && [ "$got" = 1 ] && DISCRIM="$DISCRIM $CUR"
	report "$n" "$w" "$got"
}
# 180, not the default 90: deepstack.c waits 120 s for its child and REPORTS a
# wedge, and a harness that killed it at 90 would take that report away.
CAP=180 deep 0 "grows past 4K and faults at the ceiling"	none	     16
CAP=180 deep 0 "  the same with 256-byte frames"	none	     256
CAP=180 deep 0 "  and with 512-byte frames"		none	     512
CAP=180 deep 1 "the stack is never grown"		stack-nogrow 16
CAP=180 deep 1 "the ceiling kills it with SIGBUS"	stack-wrongsig 16
if slow; then
CAP=180 deep 1 "the faulting process wedges (deadline)" stack-wedge 16
fi
fi

# --------------------------------------------------------------------------
# pty -- the channel state machine of sys/drv/pty.c, which is what telnetd and
# any window system stand on.  The host has pseudo-terminals and they move
# bytes; what it has none of is carrier, exclusion, the hangup that outranks
# IONDLY, NUPTY or the arena the channel is drawn from, so the shim decides all
# of those before the host call ever happens.
# --------------------------------------------------------------------------
if sel pty; then
echo "pty (carrier, exclusion, hangup, the channel count and the arena)"
BREAK=none          want 0 "a driver that carries, excludes and hangs up" ./pty-h
BREAK=pty-nocarrier want 1 "a slave opens with no master"	./pty-h
BREAK=pty-noexcl    want 1 "a second master takes the same channel"	./pty-h
BREAK=pty-nohup     want 1 "the slave sees EOF where the line went"	./pty-h
BREAK=pty-hupdeaf   want 1 "a dead channel polls as an idle one"	./pty-h
BREAK=pty-nxio      want 1 "NUPTY stops short of the node set"	./pty-h
BREAK=pty-noreclaim want 1 "a closed channel is not given back"	./pty-h
fi

# --------------------------------------------------------------------------
# swap -- out to disk and back, intact.
# --------------------------------------------------------------------------
if sel swap; then
echo "swap (does a swapped-out process come back intact?)"
CAP=400 BREAK=none          want 0 "segments restored byte for byte"	./swap-h 4 3 8
CAP=400 BREAK=swap-scribble want 1 "a segment comes back changed"	./swap-h 4 3 8
if slow; then
CAP=400 BREAK=swap-mute want 1 "a sleeper never reports (deadline)"	./swap-h 4 3 8
fi
fi

# --------------------------------------------------------------------------
# ascii -- the scan itself.  Its mutation is a planted byte, and the control is
# that the tree is clean without it.
# --------------------------------------------------------------------------
if sel ascii; then
echo "ascii (does the non-ASCII scan see a non-ASCII byte?)"
CAP=300 want 0 "the tree as it stands"	sh ../ascii/check.sh -q
printf 'int x; /* \342\200\224 */\n' > planted.c
CAP=300 want 1 "one planted em dash"	sh ../ascii/check.sh -q
rm -f planted.c
fi

# --------------------------------------------------------------------------
# ptraudit -- the one probe here whose subject is the Z8001 ABI itself, so its
# mutation runs on the target rather than on the host.  See ../ptraudit/Makefile:
# the same source is rebuilt against empty headers, which is what takes the
# declarations away, and the run must then FAIL.  Skipped, loudly, without the
# simulator-enabled n2z8001 that -runexec needs.
# --------------------------------------------------------------------------
if sel ptraudit; then
echo "ptraudit (32-bit returns, under n2z8001 -runexec -- not on the host)"
if [ -x "${N2:-/tmp/n2z8001}" ]; then
	# The verdict is make's own status AND the case count it states.  A
	# `make' that failed for a reason of its own -- no cross compiler, an
	# empty $(N2) -- prints neither "ok" nor "FAIL", and a run that took its
	# verdict from a grep alone would report that as a pass.
	if make -s -C ../ptraudit mutate N2="${N2:-/tmp/n2z8001}" > "$LOG" 2>&1 &&
	   [ "$(grep -c '^  ok' "$LOG")" = 2 ]; then
		echo "  ok   passes with the declarations, fails without them"
		PASSED=$((PASSED + 2))
		RAN="$RAN ptraudit"
		DISCRIM="$DISCRIM ptraudit"
	else
		echo "  FAIL ptraudit: see \`make -C ../ptraudit mutate'"
		sed 's/^/       | /' "$LOG" | tail -10
		why "ptraudit: make -C ../ptraudit mutate"
		BAD=$((BAD + 1))
	fi
else
	echo "  -- skipped: no ${N2:-/tmp/n2z8001}.  The guest runner is now the"
	echo "     C emulator, which takes the same -runexec:"
	echo "       N2=\$(sh \$C900_TOOLCHAIN/host/runner.sh) sh run.sh ptraudit"
	echo "     Without it this probe is UNPROVEN, not passing -- and it is"
	echo "     counted that way below, in PROVES NOTHING."
	RAN="$RAN ptraudit"
fi
fi

# --------------------------------------------------------------------------
# top -- the nlist(3) + /dev/kmem route, and the other probe whose mutations
# cannot be hosted.  What can go wrong with it is a property of
# the kernel's own data layout: a chain link out of the arena, a namelist whose
# addresses now hold something else, an arena the device will not read back.
# The host has none of that -- a PROC is a different size, a far pointer is a
# host pointer, nlist() has no l.out to read -- so the matrix lives in
# ../top/Makefile and runs under n2z8001 -runexec against a forged image and
# the top the userland build produced.  Nothing there relinks the kernel or
# rebuilds top.
# --------------------------------------------------------------------------
if sel top; then
echo "top (a namelist, a chain through /dev/kmem, and the arena bounds)"
# The linked kernel is the KERNEL repository's; this tree links none, and
# ../top/Makefile resolves it through toolchain.mk's $(C900_KERNEL_DIR).  Ask
# make for the path rather than spelling the search a second time here.
# --no-print-directory, and the make[N] lines dropped anyway: this runs inside
# release-check's own make, which exports -w to every make below it, so the
# answer arrives wrapped in `Entering'/`Leaving directory' lines.  Taken whole
# it is not a path, the -f test below fails, and top SKIPS -- reported as
# UNPROVEN, which fails the matrix and the release.  It passes run by hand,
# where there is no outer make, which is what hid it.
_topkern=$(make -s --no-print-directory -C ../top \
	--eval='hostcheck-kern: ; @echo $(KERN)' hostcheck-kern 2>/dev/null |
	grep -v '^make\[')
if [ -x "${N2:-/tmp/n2z8001}" ] && [ -n "$_topkern" ] && [ -f "$_topkern" ] &&
   [ -x ../../hostbuild/build/bin/top ]; then
	# Both halves are needed.  A `make' that failed for a reason of its own
	# -- no cross compiler, a syntax error in mkkmem.c -- prints no "ok" and
	# no "FAIL" either, and a run that took its verdict from a grep alone
	# would call that a pass.  Eight is what the matrix prints: its seven
	# cases, and the line that says the forged table came out the way
	# mkkmem laid it down.
	if make -s -C ../top mutate N2="${N2:-/tmp/n2z8001}" > "$LOG" 2>&1 &&
	   [ "$(grep -c '^  ok' "$LOG")" = 8 ]; then
		echo "  ok   reads a forged arena, and refuses five broken"
		echo "       systems by name (see ../top/Makefile)"
		PASSED=$((PASSED + 6))
		RAN="$RAN top"
		DISCRIM="$DISCRIM top"
	else
		echo "  FAIL top: see \`make -C ../top mutate'"
		sed 's/^/       | /' "$LOG" | tail -10
		why "top: make -C ../top mutate"
		BAD=$((BAD + 1))
	fi
else
	echo "  -- skipped: needs ${N2:-/tmp/n2z8001}, the kernel repository's"
	echo "     linked ${_topkern:-os/hostbuild/kobj/kernel.out} and the built"
	echo "     ../../hostbuild/build/bin/top.  The runner is now the C"
	echo "     emulator, which takes the same -runexec:"
	echo "       N2=\$(sh \$C900_TOOLCHAIN/host/runner.sh) sh run.sh top"
	echo "     Without them this probe is UNPROVEN, not passing -- and it is"
	echo "     counted that way below, in PROVES NOTHING."
	RAN="$RAN top"
fi
fi

# --------------------------------------------------------------------------
# shkeyword -- sh(1)'s reserved words, ordinary in argument position and
# keywords only in command position.  Its own run.sh carries all four phases
# and a MUTATE=argfail hook that flips phase 1's expectation to what a shell
# taking every keyword literally would answer.
# --------------------------------------------------------------------------
if sel shkeyword; then
echo "shkeyword (a reserved word is ordinary in argument position, a keyword in command position)"
MUTATE=none    want 0 "sh(1) as shipped: all four phases"		sh ../shkeyword/run.sh
MUTATE=argfail want 1 "argument position taken as a keyword"		sh ../shkeyword/run.sh
fi

# --------------------------------------------------------------------------
# sortopen -- sort(1)'s two open loops and the lone `-' operand.
# MUTATE=nodash puts the option loop's dropped test for a lone `-' back the
# way it was, which fails every phase-4 case where `-' precedes another operand.
# --------------------------------------------------------------------------
if sel sortopen; then
echo "sortopen (sort's two open loops, the lone-dash operand, and exit status)"
MUTATE=none   want 0 "sort(1) as shipped: all five phases"		sh ../sortopen/run.sh
MUTATE=nodash want 1 "a lone \`-' eaten by the option loop again"	sh ../sortopen/run.sh
fi

# --------------------------------------------------------------------------
# moreterm -- more(1) paging under ten TERM entries on a real pty, grouped by
# capability shape, and the escape sequences it writes around its own prompt.
# MUTATE=noso deletes lr's so/se from the termcap entry more reads, which
# fails only the lr case in phase 5 -- the entry is damaged, not the program.
# --------------------------------------------------------------------------
if sel moreterm; then
echo "moreterm (does more(1) page every termcap shape, and paint its prompt with the entry's own so/se/ce?)"
MUTATE=none want 0 "more(1) as shipped: all five phases"		sh ../moreterm/run.sh
MUTATE=noso  want 1 "lr's so/se removed from the termcap entry"	sh ../moreterm/run.sh
fi

# --------------------------------------------------------------------------
# buildenv -- make(1) compiling and linking ON THE TARGET, from a compiler
# environment mapped in from the host.  Its own run.sh carries the negative
# control: phase 5 replays the identical run with no medium attached and
# requires every claim to fail.  A guest boot either way, so this is skipped
# rather than failed when the compiler environment or the dist image is not built.
# --------------------------------------------------------------------------
if sel buildenv; then
echo "buildenv (make(1) compiling and linking on target, from a host-mapped compiler environment)"
sh ../buildenv/run.sh > "$LOG" 2>&1
rc=$?
case $rc in
0) echo "  ok   the guest built and ran a program from a mapped environment, and"
   echo "       the same claims fail with no medium attached (../buildenv/run.sh"
   echo "       phase 5)"
   PASSED=$((PASSED + 1)); RAN="$RAN buildenv"; DISCRIM="$DISCRIM buildenv" ;;
2) echo "  --   skipped: $(grep -m1 . "$LOG")"
   echo "       Without it this probe is UNPROVEN, not passing -- and it is"
   echo "       counted that way below, in PROVES NOTHING."
   RAN="$RAN buildenv" ;;
*) echo "  FAIL buildenv: see \`sh ../buildenv/run.sh'"
   sed 's/^/       | /' "$LOG" | tail -15
   why "buildenv: sh ../buildenv/run.sh (rc $rc)"
   BAD=$((BAD + 1)) ;;
esac
fi

# --------------------------------------------------------------------------
# selfhost -- cc compiling and linking on the target from the compiler THE
# IMAGE SHIPS, with nothing mapped in from the host.  Its own run.sh carries
# the negative control: phase 3 replays the identical commands on coherent3-full-test
# -- same media, same userland, no lists/toolchain.list -- and requires every
# claim to fail.
# --------------------------------------------------------------------------
if sel selfhost; then
echo "selfhost (cc compiling and linking on target, from the compiler the dist ships)"
sh ../selfhost/run.sh > "$LOG" 2>&1
rc=$?
case $rc in
0) echo "  ok   the machine compiled and linked with its own installed compiler,"
   echo "       and the same claims fail on a dist that ships none"
   echo "       (../selfhost/run.sh phase 3)"
   PASSED=$((PASSED + 1)); RAN="$RAN selfhost"; DISCRIM="$DISCRIM selfhost" ;;
2) echo "  --   skipped: $(grep -m1 . "$LOG")"
   echo "       Without it this probe is UNPROVEN, not passing -- and it is"
   echo "       counted that way below, in PROVES NOTHING."
   RAN="$RAN selfhost" ;;
*) echo "  FAIL selfhost: see \`sh ../selfhost/run.sh'"
   sed 's/^/       | /' "$LOG" | tail -15
   why "selfhost: sh ../selfhost/run.sh (rc $rc)"
   BAD=$((BAD + 1)) ;;
esac
fi

# --------------------------------------------------------------------------
# Coverage.  Every directory in test must be either in the matrix above or
# named here with the reason no mutation can reach it.  A directory in neither
# fails the run: that is what stops a new probe being merged as decoration.
# --------------------------------------------------------------------------
UNCOVERED="
bigtext:a build-and-run probe for text past 64K -- what it measures is the
bigtext:  target linker and loader, and a host build cannot be given that defect.
multiseg:the same, for multi-segment data; its own Makefile builds it.
cmd:emulator command scripts, not programs.  Nothing to mutate.
mkfsgetlink:the subject is the name-matching loop inside mkfs(1M)'s getlink(),
mkfsgetlink:  which is pure string comparison -- it makes no system call, so
mkfsgetlink:  kshim.c sits between it and nothing.  It carries its own
mkfsgetlink:  discrimination instead, and two negative controls rather than
mkfsgetlink:  one: test/mkfsgetlink/run.sh runs the loop mkfs carries and the
mkfsgetlink:  two that were in the tree before it -- \`stock', which reports a
mkfsgetlink:  name that IS there as missing, and \`base', which answers with
mkfsgetlink:  another entry's inode -- and requires each of the two to fail.
mkfsgetlink:  Both were run red before it was committed, on the host and under
mkfsgetlink:  the emulator.  It needs no image and takes a second.
awkrules:the subject is a TARGET awk's grammar, and kshim.c stands in for
awkrules:  system calls -- there is no host stand-in for a yacc parser.  It
awkrules:  carries its own discrimination instead, and two mutations rather
awkrules:  than one: MUTATE=accept expects the one-line form to parse (what
awkrules:  \"fixing\" awk.y would do) and MUTATE=break expects the newline form
awkrules:  to fail (what damaging the rule list would do).  Both were run red
awkrules:  before it was committed.  It also runs the 1985 original as an
awkrules:  oracle, which is an artefact outside this repository, so it is not
awkrules:  driven from here.
stdiobound:the subject is libc/stdio compiled for a 16-bit int.  On a
stdiobound:  32-bit host every count in it fits an int and every case passes
stdiobound:  whatever the library does, so a host stand-in cannot hold the
stdiobound:  defect.  It carries its own discrimination instead, and it mutates
stdiobound:  the LIBRARY rather than a stand-in kernel: tests/stdiobound/run.sh
stdiobound:  compiles a copy of the real stdio source with one type changed,
stdiobound:  links it ahead of libc and requires the run to fail.
nlistfar:the subject is nlist(3) reading a 32-bit symbol value out of an l.out
nlistfar:  whose symbol table starts past 64K.  A host has neither span: the
nlistfar:  value fits an int there and the offset fits a size_t, so every case
nlistfar:  passes whatever the library does.  It carries its own discrimination,
nlistfar:  on the same terms as stdiobound: tests/nlistfar/run.sh compiles a copy
nlistfar:  of libc/gen/nlist.c with one type changed -- the table's offset as
nlistfar:  \`unsigned' and as \`int', the symbol's value through \`unsigned short'
nlistfar:  and through \`int' -- links it ahead of libc and requires the run to
nlistfar:  fail.  All four were run red before it was committed.
ichan:command scripts driving net/ichanprobe; the net lane owns that side.
mouse:another lane is wiring this driver into the kernel; left alone.
fifosig:its verdict is a kernel PANIC (\"Out of sync IPR in pclose\") and, in
fifosig:  phase 2, a process managing to exit at all.  Neither is visible from
fifosig:  userland on any host: a stand-in can refuse a call or return the
fifosig:  wrong bytes, and cannot stop the machine.  Added by another lane;
fifosig:  the emulator is its instrument, not this.
segindex:the subject is Z8001 indexed addressing and a linker relocation.
segindex:  No host stand-in can produce that defect; its own header already
segindex:  says it does not reproduce the relocation bug either.
serialbytes:the guest half of a host-driven check.  Its exit status is a byte
serialbytes:  COUNT; whether the values survived is decided by the host half,
serialbytes:  hostbuild/serial-bytes-test.py, which is where that mutation
serialbytes:  belongs.
hostfs:the host-directory pass-through, whose subject is a MEDIUM the guest
hostfs:  mounts -- there is no userland call for kshim.c to stand in for, and
hostfs:  a host build cannot be given the defect (serving the guest's own disk
hostfs:  instead of the host's).  It carries its own discrimination instead:
hostfs:  run.sh phase 5 replays the identical run with no medium attached and
hostfs:  requires every assertion to fail.  It costs two emulator boots, so it
hostfs:  is not run from here.
duallayout:two systems on one disk, each handed its own wd(4) table.  The
duallayout:  subject is the IMAGE GENERATOR and the loader's config, neither of
duallayout:  which kshim.c sits between.  It carries its own gate instead --
duallayout:  \`sh run.sh gate' mutates copies of mkimage.py, dist.py and the media
duallayout:  descriptor and requires each check to refuse what it claims to; that
duallayout:  half needs no emulator, but the boots after it do, so it is not
duallayout:  driven from here.
loadavg:moved wholesale to commodore-900-coh-kernel3 in the 2026-08-09 split
loadavg:  What is left in this tree is an untracked, pre-split build artifact
loadavg:  -- a stray binary, no source -- not something this repository builds.
rawalign:the same move, on the same terms: source and harness are in
rawalign:  commodore-900-coh-kernel3 now, and this directory holds only a
rawalign:  leftover binary and a stray __pycache__.
stackhw:phase 0 checks that the image's kernel, drivers and kobj/kernel.out
stackhw:  agree.  These are in a separate repository now; kobj/kernel.out and
stackhw:  the *.bin images still here are pre-split fossils, not something
stackhw:  this repository builds.
privsep:phase 0 checks exactly what the device-node/setuid-bit lane
privsep:  currently live in this repository is still changing (the packed
privsep:  image right now answers /dev/hd*, rhd*, kmem, mem and swap at 666
privsep:  and ps/top with no setuid bit -- see phase 0's own output).  Wiring
privsep:  it in here would fail hostcheck on someone else's unfinished work.
privsep:  Its privids phases need tests/rawalign/inject.py, which is in a
privsep:  separate repository.  Left alone for now, the same way mouse and
privsep:  ichan are.
vprintf:the subject is the target's own va_list walk, against a 32-bit long
vprintf:  and a far pointer, on the Z8001 -- the same terms as ptraudit, and
vprintf:  no host stand-in can hold that defect either.  It was run once by
vprintf:  hand under the emulator but has no run.sh and no mutation yet, so
vprintf:  there is no repeatable gate to wire in here.
hostcheck:this harness.
"

echo
echo "coverage"
MISSING=""
for d in ../*/; do
	n=$(basename "$d")
	case " $RAN " in *" $n "*) continue ;; esac
	if echo "$UNCOVERED" | grep -q "^$n:"; then
		[ -n "$ONLY" ] || {
			echo "  -- $n: no mutation, by declaration:"
			echo "$UNCOVERED" | sed -n "s/^$n:/       /p"
		}
		continue
	fi
	[ -z "$ONLY" ] || continue
	echo "  MISSING $n: no mutation case and no declared reason."
	echo "       A probe that nothing can make fail reports its area as"
	echo "       tested.  Add a case to run.sh, or say here why none exists."
	MISSING="$MISSING $n"
	why "$n: no mutation case and no declared reason"
	BAD=$((BAD + 1))
done

# A probe that was RUN but never seen to fail proves nothing, whatever its
# matrix says; a probe that was SKIPPED is in the same position and is named
# here too.  A skip path must never add to DISCRIM.
echo
NOPROOF=""
for n in $(echo "$RAN" | tr ' ' '\n' | sort -u); do
	case " $DISCRIM " in
	*" $n "*)	;;
	*)		NOPROOF="$NOPROOF $n" ;;
	esac
done
if [ -n "$NOPROOF" ]; then
	echo "PROVES NOTHING --$NOPROOF"
	echo "  Each was run and never once failed.  A check that cannot fail"
	echo "  does not miss a defect quietly; it reports the area as tested."
	why "unproven (run, never seen to fail):$NOPROOF"
	BAD=$((BAD + 1))
fi

echo
echo "hostcheck: $PASSED case(s) behaved, $BAD did not"
[ "$BAD" = 0 ] || echo "hostcheck: what did not behave:${WHY#,}"
[ "$SLOW" = 1 ] || echo "hostcheck: the deadline cases are behind SLOW=1"
exit $((BAD > 0))
