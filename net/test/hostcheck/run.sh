#!/bin/sh
# run.sh -- the discrimination matrix for the net tests.
#
# For every program: run it against a system that WORKS (which it must pass),
# and against each broken system this harness can build (which it must fail).
# A check nothing has ever been seen to fail is decoration; every line marked
# `fail' below is a demonstration that a particular defect is now caught.
#
# Each case names the defect, the program, and the exit status wanted.  A case
# that does not get the status it wanted is reported, and the script's own exit
# status is the number of such cases.
#
# IT IS A GATE, NOT A REPORT.  Two things make it one, the same two that
# test/hostcheck enforces for the kernel probes:
#
#   - a program no case here has ever been seen to FAIL is named at the end
#     under "PROVES NOTHING", and the run fails.  A check that cannot fail does
#     not merely miss a defect; it reports the area as tested.
#   - every net/test/*.c must appear either in the matrix below or in the
#     UNCOVERED list with a written reason.  A new probe in neither is reported
#     and fails the run, so a probe cannot be added as decoration without
#     somebody saying why it cannot be discriminated here.
#
# Both checks are skipped when a single program is named on the command line,
# which asks about that program and not about the coverage of the whole set.
#
#	sh run.sh			everything
#	sh run.sh psipping		one program
#
# Host-only.  Nothing here runs on the C900.
set -u
cd "$(dirname "$0")" || exit 2

ONLY=${1:-}
PASSED=0
BAD=0
PORT=${PORT:-7300}
# Which programs the group being run speaks for, which of them a case has been
# run against at all, and which of them a case has been seen to FAIL.
COVERS=""
RAN=""
DISCRIM=""

# case <name> <want-exit> <prog> [args...]
#
# `want' is 0 for a system that works and 1 for one that is broken; anything
# else (a crash, a timeout, a hang) is a failure of the case whichever way.
LOG=${TMPDIR:-/tmp}/hostcheck.$$
trap 'rm -f "$LOG"' 0 1 2 15

report() {			# name want got
	RAN="$RAN $COVERS"
	# Only a case that WANTED a failure and got one is evidence: a program
	# that has only ever been run against a working system has demonstrated
	# nothing about what it would do against a broken one.
	if [ "$2" = 1 ] && [ "$3" = 1 ]; then
		DISCRIM="$DISCRIM $COVERS"
	fi
	if [ "$2" = "$3" ]; then
		echo "  ok   $1 (exit $3)"
		PASSED=$((PASSED + 1))
	else
		echo "  FAIL $1: exit $3, wanted $2"
		sed 's/^/       | /' "$LOG" | tail -6
		BAD=$((BAD + 1))
	fi
}

want() {			# want name -- reads the command from "$@"
	w=$1; n=$2; shift 2
	"$@" > "$LOG" 2>&1 </dev/null
	report "$n" "$w" "$?"
}

# sel <group> [program ...] -- names the group and the net/test programs it
# speaks for.  They differ where one case drives two programs (echoclient with
# echoserver) or where the subject is not a test program at all (fdc, which is
# inet/coh_fdc.c).
sel() {
	COVERS="$*"
	[ -z "$ONLY" ] || [ "$ONLY" = "$1" ]
}

# --------------------------------------------------------------------------
# psipping -- the whole point is that the three NEGATIVE modes are scored as
# negative.  `all' runs echo/runt/badsum/proto together.
# --------------------------------------------------------------------------
if sel psipping; then
echo "psipping (fake IP stack: does it tell a correct stack from a broken one?)"
PSIPSTACK=good		want 0 "correct stack, all four modes"	./psipping-h 1 all
PSIPSTACK=answer-badsum	want 1 "stack ANSWERS a bad checksum"	./psipping-h 1 all
PSIPSTACK=answer-all	want 1 "stack answers anything at all"	./psipping-h 1 all
PSIPSTACK=answer-runt	want 1 "stack answers a runt"		./psipping-h 1 all
PSIPSTACK=deaf		want 1 "stack answers nothing"		./psipping-h 1 all
PSIPSTACK=wrongseq	want 1 "reply carries the wrong seq"	./psipping-h 1 all
# and per mode, so that each defect is pinned to the mode that names it
PSIPSTACK=answer-badsum	want 1 "  badsum mode alone"		./psipping-h 1 badsum
PSIPSTACK=answer-badsum	want 0 "  echo mode unaffected"		./psipping-h 1 echo
PSIPSTACK=answer-all	want 1 "  proto mode alone"		./psipping-h 1 proto
PSIPSTACK=answer-runt	want 1 "  runt mode alone"		./psipping-h 1 runt
PSIPSTACK=deaf		want 1 "  echo mode alone"		./psipping-h 1 echo
fi

# --------------------------------------------------------------------------
# acceptmany -- payload comparison on both sides.
# --------------------------------------------------------------------------
if sel acceptmany; then
echo "acceptmany (does each connection carry its OWN peer's stream?)"
BREAK=none	 want 0 "working stack"			./acceptmany-h $((PORT+1)) 3
BREAK=echo-wrong want 1 "peer echoes the WRONG payload"	./acceptmany-h $((PORT+2)) 3
BREAK=crosswire	 want 1 "accept crosswires two conns"	./acceptmany-h $((PORT+3)) 3
BREAK=short-echo want 1 "echo truncated by one byte"	./acceptmany-h $((PORT+4)) 3
fi

# --------------------------------------------------------------------------
# ephport -- the ephemeral port AND the bytes that arrive on it.
# --------------------------------------------------------------------------
if sel ephport; then
echo "ephport (stack-chosen port, and the right stream on it)"
BREAK=none		    want 0 "working stack"		./ephport-h
BREAK=echo-wrong	    want 1 "server's reply altered"	./ephport-h
BREAK=short-echo	    want 1 "server's reply truncated"	./ephport-h
BREAK=getsockname-verbatim  want 1 "getsockname answers 0"	./ephport-h
fi

# --------------------------------------------------------------------------
# echoclient / echoserver -- a client that looks at what came back.
# --------------------------------------------------------------------------
if sel echo echoclient echoserver; then
echo "echoclient + echoserver (loopback round trip)"
echopair() {			# want name break
	p=$((PORT + 20 + $$ % 40))
	BREAK=$3 ./echoserver-h $p 5 > "$LOG.srv" 2>&1 </dev/null &
	sv=$!
	sleep 1
	BREAK=$3 timeout 20 ./echoclient-h 10.0.0.2 $p > "$LOG" 2>&1 </dev/null
	c=$?
	wait $sv
	s=$?
	report "$2 [client]" "$1" "$c"
	cp "$LOG.srv" "$LOG"
	report "$2 [server]" "$4" "$s"
	rm -f "$LOG.srv"
}
echopair 0 "working stack"		none	    0
echopair 1 "server echoes wrong bytes"	echo-wrong  0
echopair 1 "server echo truncated"	short-echo  1
fi

# --------------------------------------------------------------------------
# rlecho -- handshake shape and a session that carries more than one exchange.
# --------------------------------------------------------------------------
if sel rlecho; then
echo "rlecho (rcmd handshake shape, multi-exchange session)"
# The server is given a SHORT deadline here (5s) so its own timeout, not the
# harness's patience, is what ends the deadlocked cases.
rlcase() {			# want name peermode
	p=$((PORT + 60 + $$ % 30))
	./rlecho-h $p 5 > "$LOG.srv" 2>&1 </dev/null &
	sv=$!
	sleep 1
	timeout 20 ./peers rl $p 3 "$3" > /dev/null 2>&1
	wait $sv
	s=$?
	cp "$LOG.srv" "$LOG"
	report "$2" "$1" "$s"
	rm -f "$LOG.srv"
}
rlcase 0 "a real rlogin session"		ok
rlcase 1 "client sends THREE strings, not four"	three
rlcase 1 "handshake carries no user name"	nouser
rlcase 1 "session carries ONE exchange, then dies" once
fi

# --------------------------------------------------------------------------
# udpserver -- every datagram accounted for, every echo addressed.
# --------------------------------------------------------------------------
if sel udpserver; then
echo "udpserver (per-datagram addressing, all datagrams accounted for)"
udpcase() {			# want name peermode count
	p=$((PORT + 90 + $$ % 30))
	./udpserver-h $p 3 5 > "$LOG.srv" 2>&1 </dev/null &
	sv=$!
	sleep 1
	timeout 20 ./peers udp $p 3 "$3" > /dev/null 2>&1
	wait $sv 2>/dev/null
	s=$?
	cp "$LOG.srv" "$LOG"
	report "$2" "$1" "$s"
	rm -f "$LOG.srv"
}
udpcase 0 "three datagrams, echoed"		ok
udpcase 1 "peer stops one datagram short"	few
fi

# --------------------------------------------------------------------------
# chanmax -- the ceiling is now scored, not merely printed.
# --------------------------------------------------------------------------
if sel chanmax; then
echo "chanmax (the connection ceiling)"
CHANLIMIT=15 want 0 "fifteen channels, then ENFILE"	./chanmax-h
CHANLIMIT=8  want 0 "eight, the per-process limit"	env CHANERR=24 ./chanmax-h
CHANLIMIT=8  want 1 "refused with EIO: not a limit"	env CHANERR=5 ./chanmax-h
CHANLIMIT=1  want 1 "daemon collapses to ONE channel"	./chanmax-h
BREAK=chanmax-none want 1 "daemon refuses the first"	./chanmax-h
CHANLIMIT=30 want 1 "no refusal within range: unmeasured" ./chanmax-h
fi

# --------------------------------------------------------------------------
# devtcp -- the FAIL that fell through to PASS.
# --------------------------------------------------------------------------
if sel devtcp; then
echo "devtcp (/dev/tcp ioctls)"
BREAK=none	    want 0 "working stack"		./devtcp-h 10.0.0.1 7
BREAK=locport0	    want 1 "stack reports local port 0"	./devtcp-h 10.0.0.1 7
BREAK=gtcpconf-fail want 1 "NWIOGTCPCONF fails"		./devtcp-h 10.0.0.1 7
BREAK=echo-wrong    want 1 "echo comes back altered"	./devtcp-h 10.0.0.1 7
BREAK=echo-short    want 1 "echo comes back short"	./devtcp-h 10.0.0.1 7
BREAK=conn-fail	    want 1 "NWIOTCPCONN fails"		./devtcp-h 10.0.0.1 7
fi

# --------------------------------------------------------------------------
# netdbtest -- the one query on that page only the stack can answer.
# --------------------------------------------------------------------------
if sel netdbtest; then
echo "netdbtest (lookups; the ephemeral port is the only stack query)"
BREAK=none		   want 0 "working stack"		 ./netdbtest-h
BREAK=getsockname-verbatim want 1 "getsockname answers from the bind" ./netdbtest-h
fi

# --------------------------------------------------------------------------
# udpecho -- the datagram round trip, where the ADDRESS is the subject: a
# datagram delivered with the wrong sender on it is still a delivered datagram.
# --------------------------------------------------------------------------
if sel udpecho; then
echo "udpecho (datagram round trip: bytes AND sender)"
BREAK=none		want 0 "working stack"			./udpecho-h
BREAK=udp-from-wrong	want 1 "recvfrom names the wrong sender"	./udpecho-h
BREAK=udp-payload-wrong	want 1 "the datagram arrives altered"	./udpecho-h
fi

# --------------------------------------------------------------------------
# udppoll -- readiness.  Both mutations are ANSWERS, not errors: poll returns,
# so a test that asks whether poll returned passes against either of them.
# --------------------------------------------------------------------------
if sel udppoll; then
echo "udppoll (poll() on a socket: does readiness mean anything?)"
BREAK=none	  want 0 "working stack"			./udppoll-h
BREAK=poll-never  want 1 "poll never reports the datagram"	./udppoll-h
BREAK=poll-always want 1 "poll reports an idle socket readable"	./udppoll-h
fi

# --------------------------------------------------------------------------
# repeatclient -- is the service still there for the SECOND caller?  The
# defect it exists for is a daemon that answers once and then has no listening
# socket, so the discriminating peer is one that serves a single caller and
# closes; the third case keeps the service and takes the ANSWER away instead,
# which separates "the port was open" from "the port answered".
# --------------------------------------------------------------------------
if sel repeatclient; then
echo "repeatclient (does a service go on answering?)"
repcase() {			# want name rounds peermode break
	p=$((PORT + 120 + $$ % 30))
	./peers serve $p "$3" "$4" > "$LOG.srv" 2>&1 </dev/null &
	sv=$!
	sleep 1
	BREAK=$5 timeout 30 ./repeatclient-h -n "$3" 10.0.0.2 $p:hello \
		> "$LOG" 2>&1 </dev/null
	c=$?
	kill $sv 2>/dev/null
	wait $sv 2>/dev/null
	rm -f "$LOG.srv"
	report "$2" "$1" "$c"
}
repcase 0 "three callers, each answered"	3 ok   none
repcase 1 "service answers one caller, then its listener is gone" 3 once none
repcase 1 "the answer is never reported readable"		1 ok   poll-never
fi

# --------------------------------------------------------------------------
# fdc -- the number of connections this machine has, measured at COHERENT's
# NOFILE against real FIFOs.  The shipped coh_fdc.c must reach fifteen and
# every one of them must be answerable; the policy it replaced reaches eight,
# and the same cache without its reply reserve reaches sixteen and can answer
# none of them.
# --------------------------------------------------------------------------
if sel fdc; then
echo "fdc (descriptor policy: how many connections, and are they answerable?)"
want 0 "one descriptor a channel"		./fdc-h
want 1 "two descriptors a channel (the old policy)"	./fdcold-h
want 1 "no descriptor kept back for replies"	./fdcgreedy-h
fi

# --------------------------------------------------------------------------
# The two bookkeeping checks.  Neither looks at a network; both look at this
# matrix and ask whether it is evidence.
# --------------------------------------------------------------------------
#
# A program named here is one no case above can make fail, with the reason.
# Each is a program whose subject is outside what a host stand-in can build --
# not one that is merely inconvenient to cover.
UNCOVERED=$(cat <<'EOT'
discotime	measures elapsed time against hunt's 1000 ms discovery window.
		The quantity under test is the C900's own clock (utimer, which
		wraps every 655 s); a host stand-in either answers instantly or
		answers what it is told, and neither says anything about the
		target.  Its own PASS/SLOW verdict stands; the wire run is
		hunt(6) played at the console.
ichanprobe	bisects the inet daemon's control-channel handshake, step by
		step, over the real FIFO pair.  The handshake IS the subject,
		so a stand-in for it would be a stand-in for the thing being
		measured.  Driven on the guest by tests/ichan/*-cmds.
sntpsrv		not a probe.  It is the fixed-answer peer the sntp CLIENT is
		judged against, so it has no verdict of its own to discriminate
		-- breaking it is how the client's cases are built, not a case
		of its own.
talkping	asks the ntalk service to REMEMBER: the invitation it looks up in
		step 3 was minted while a different datagram was being answered,
		and the table holding it lives in the child inetd forks WITHOUT
		exec (net/talkserv.c).  That child is the subject, so a host
		stand-in for it would be a stand-in for the thing being measured,
		and the shim here answers each datagram on its own.  Driven on the
		guest by tests/cmd/inetd.cmd, where step 1 and step 3 are each
		other's control.
dropclient	is not a probe.  It abandons connections without closing them and
		returns 0 whatever happens; what the drop does to a SERVICE is
		the subject, and that verdict is the daemon's, read on the guest
		by tests/cmd/inetd.cmd.  A host stand-in could
		only drop connections on itself.
fdhog		is not a probe either.  It fills its own descriptor table, gives
		back a stated few and execs the program under test into what is
		left, exiting with whatever that program returns -- so it makes no
		claim of its own to break.  The scarcity it constructs is the
		target's (NOFILE 20, three descriptors to a channel); on the host
		it would hand over a number that says nothing about the C900.
portholder	takes a port with NWTC_EXCL through /dev/tcp and holds it, which
		is the inet stack's own exclusion rule: tcp_setconf() refuses an
		exclusive claim beside any other descriptor on the port, and a
		SHARED one beside it.  Host sockets have no such claim -- SO_REUSE
		and two binds are not it -- so a stand-in could neither take the
		port in the way that matters nor be refused for the reason that
		matters.  Driven on the guest by tests/cmd/inetd.cmd.
sockcycle	measures whether libsocket gives a closed socket's block back to
		the heap, read off sbrk(0).  Its subject is libsocket.c's own
		allocation, and nothing here compiles libsocket.c -- the socket
		programs are linked against sockshim.c, which is real host
		sockets and has no per-socket block to leak.  A stand-in could
		only leak memory of its own, which would prove nothing about
		the library.  Scored on the guest as inetd-test's T_LEAK.
EOT
)

if [ -z "$ONLY" ]; then
	# Coverage: every program in net/test either has cases or a reason.
	miss=""
	for f in ../*.c; do
		n=$(basename "$f" .c)
		case " $RAN " in *" $n "*) continue;; esac
		case "$UNCOVERED" in *"$n	"*) continue;; esac
		miss="$miss $n"
	done
	if [ -n "$miss" ]; then
		echo
		echo "NO CASES AND NO REASON --$miss"
		echo "  Give each one a case above, or a line in UNCOVERED saying"
		echo "  what about it a host stand-in cannot construct."
		BAD=$((BAD + 1))
	fi

	# Proof: every program a case ran must have been seen to fail at least
	# once, against a defect built on purpose.
	noproof=""
	for n in $RAN; do
		case " $noproof " in *" $n "*) continue;; esac
		case " $DISCRIM " in *" $n "*) continue;; esac
		noproof="$noproof $n"
	done
	if [ -n "$noproof" ]; then
		echo
		echo "PROVES NOTHING --$noproof"
		echo "  Each was run and never once failed.  A check that cannot"
		echo "  fail does not miss a defect quietly; it reports the area"
		echo "  as tested."
		BAD=$((BAD + 1))
	fi
fi

echo
echo "hostcheck: $PASSED case(s) behaved, $BAD did not"
exit $((BAD > 0))
