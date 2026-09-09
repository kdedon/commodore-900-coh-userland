# simlock.sh -- at most ONE harness simulator on this host, globally.
#
# Sourced by simctl.sh; the Python harnesses get the same lock from
# simguard.py, which implements this protocol on the same file.
#
# WHY A LOCK AND NOT PORT SCOPING.  simctl.sh scopes its cleanup to its own
# HTTP port, which keeps two harnesses from stealing each other's simulator
# but does nothing about the real cost: a cycle-accurate simulator runs at
# ~100% of a core, renamed private copies escape any port or binary-path
# check, and CPU starvation produces false diagnoses.  So the limit is
# enforced here, once, for everyone who starts a simulator through the tree.
#
# THE LOCK FD IS INHERITED BY THE SIMULATOR ON PURPOSE.  The obvious
# implementation closes fd 9 in the child so the lock belongs to the shell.
# That is wrong: if the harness is SIGKILLed the lock frees instantly and the
# orphaned simulator keeps running unseen, so a second one starts on top of it.
# Letting the simulator hold the fd makes the lock's lifetime exactly the
# simulator's lifetime -- there is never a moment when the lock is free while a
# simulator runs, and the kernel releases it when the last inheritor dies, so
# SIGKILL needs no handling at all.
#
# THE COROLLARY, which is the sharpest thing in this file: EVERY child of the
# holder inherits fd 9, not just the simulator.  A long-running helper -- a
# `sleep', a tail, a background pipeline -- would then hold the lock after the
# simulator is gone.  Short-lived helpers (curl, python3 simtype.py, lsof, ps)
# are harmless because they exit; anything long-lived that a harness backgrounds
# must be spawned with `9>&-'.
#
# Env:
#   SIMWAIT   seconds to wait for the lock; default 0 = fail loudly at once,
#             naming the pid that holds it.  Set e.g. SIMWAIT=1800 to queue.
#   SIMMAX    seconds after which a held lock is considered a hung run and is
#             reclaimed (default 2700; the netboot gate is ~25 min).
#   SIMLOCK   lock path (default ~/.cache/c900/sim.lock).
#   CI        set (as GitHub Actions does) => the lock is a no-op: a runner is
#             one job on one machine, there is nothing to serialise.

SIMLOCK=${SIMLOCK:-$HOME/.cache/c900/sim.lock}
SIMSTATE=${SIMSTATE:-${SIMLOCK%.lock}.state}
SIMWAIT=${SIMWAIT:-0}
SIMMAX=${SIMMAX:-2700}

sim_lock_held=

# Report simulators this lock does not govern.  It reports and never kills:
# the standing rule is that you do not kill a simulator you did not start, and
# a lane cannot tell another lane's legitimate work from a leak.  An announced
# leak gets fixed; a silent one accumulates.  The argv signature catches
# renamed copies, which is the whole point.
sim_scan() {
	held=
	[ -r "$SIMSTATE" ] && held=$(cut -d' ' -f2 "$SIMSTATE")
	ps -eo pid,args 2>/dev/null |
	grep -E '[-]-?http[= ]*(http://)?localhost:[0-9]+' |
	grep -v grep |
	while read -r p rest; do
		[ "$p" = "$$" ] && continue
		[ "$p" = "$held" ] && continue	# the lock's own simulator
		case $rest in
		*7800*)	echo "note: MCP debugger simulator (pid $p) is up -- it is"\
			     "exempt from this lock but costs a core anyway" >&2;;
		*)	echo "note: unsanctioned simulator pid $p: $rest" >&2;;
		esac
	done
}

sim_lock_why() {
	if [ -r "$SIMSTATE" ]; then
		echo "  holder: $(cat "$SIMSTATE")" >&2
	else
		echo "  holder: unknown (no $SIMSTATE); try: lsof $SIMLOCK" >&2
	fi
}

# Reclaim a lock whose owner is gone or whose run has overrun SIMMAX.  With the
# fd inherited (see above) a held lock means SOMETHING is still alive holding
# it, so reclaiming means killing the recorded simulator pid -- which releases
# the lock as a side effect.  Returns 0 if it killed something worth retrying
# for.
sim_lock_reclaim() {
	[ -r "$SIMSTATE" ] || return 1
	# state: <holder-pid> <sim-pid> <epoch> <port> <lane> <cmd...>
	read -r hpid spid started port lane rest < "$SIMSTATE"
	case $hpid$spid$started in *[!0-9]*|'') return 1;; esac
	now=$(date +%s)
	age=$((now - started))
	if kill -0 "$hpid" 2>/dev/null && [ "$age" -lt "$SIMMAX" ]; then
		return 1			# a live owner inside its budget
	fi
	kill -0 "$spid" 2>/dev/null || return 1
	if kill -0 "$hpid" 2>/dev/null; then
		echo "simlock: reclaiming: simulator $spid (lane ${lane:-?},"\
		     "port ${port:-?}) has run ${age}s > SIMMAX=${SIMMAX}s" >&2
	else
		echo "simlock: reclaiming: holder $hpid is gone but its"\
		     "simulator $spid is still running (lane ${lane:-?},"\
		     "port ${port:-?}, ${age}s)" >&2
	fi
	kill -9 "$spid" 2>/dev/null
	sleep 1
	return 0
}

# sim_lock_acquire <port> [lane]
sim_lock_acquire() {
	[ -n "${CI:-}" ] && return 0
	[ -n "${SIMLOCK_OFF:-}" ] && return 0
	mkdir -p "$(dirname "$SIMLOCK")" || return 1
	exec 9>>"$SIMLOCK" || return 1
	sim_scan
	if [ "$SIMWAIT" -gt 0 ]; then
		flock -w "$SIMWAIT" 9 && { sim_lock_note "$1" "${2:-}"; return 0; }
	else
		flock -n 9 && { sim_lock_note "$1" "${2:-}"; return 0; }
	fi
	echo "simlock: another simulator holds $SIMLOCK" >&2
	sim_lock_why
	if sim_lock_reclaim; then
		# The wait is not politeness: the dead harness's OTHER children
		# inherited fd 9 too (the corollary above), so the lock frees a
		# moment after the simulator does -- once the `sleep' it was in
		# the middle of exits.  Every sleep in these harnesses is <= 4s.
		if flock -w "${SIMRECLAIM:-15}" 9; then
			sim_lock_note "$1" "${2:-}"
			return 0
		fi
		echo "simlock: the simulator was reclaimed but the lock is still"\
		     "held, so something else inherited it.  Holders:" >&2
		lsof -t "$SIMLOCK" 2>/dev/null |
		while read -r p; do
			echo "  pid $p: $(ps -o args= -p "$p" 2>/dev/null)" >&2
		done
	fi
	echo "simlock: refusing to start a second simulator.  Wait for it, or"\
	     "set SIMWAIT=<seconds> to queue, or SIMLOCK_OFF=1 to override"\
	     "(and then own the consequences)." >&2
	exec 9>&-
	return 1
}

sim_lock_note() {
	sim_lock_held=1
	# Written before the simulator exists, so a crash between here and the
	# spawn still names a holder; sim_lock_sim() fills the sim pid in.
	printf '%s %s %s %s %s %s\n' "$$" 0 "$(date +%s)" "${1:-?}" \
		"${LANE:-$(pwd)}" "$0" > "$SIMSTATE"
}

# Record the simulator's pid once it is running, so a later reclaim knows what
# to kill.
sim_lock_sim() {
	[ -n "$sim_lock_held" ] || return 0
	printf '%s %s %s %s %s %s\n' "$$" "$1" "$(date +%s)" "${2:-?}" \
		"${LANE:-$(pwd)}" "$0" > "$SIMSTATE"
}

sim_lock_release() {
	[ -n "$sim_lock_held" ] || return 0
	sim_lock_held=
	rm -f "$SIMSTATE"
	exec 9>&-
}
