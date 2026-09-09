# simctl.sh -- start a simulator that belongs to one harness alone.
#
# Sourced, never run: sim_start installs an EXIT trap in the CALLER's shell.
#
#	. "$HERE/simctl.sh"
#	sim_start "$SIM" "$S" headless
#
# The HTTP port is what identifies a simulator as a harness's own.  Each
# harness owns exactly one, so a listener on that port is this harness's
# previous run and is reclaimed, while a simulator on any other port belongs to
# another harness, another session or the MCP debugger and is left alone.
#
# A simulator ignores SIGTERM while it is free-running, so the kill is -9.
#
# The port is who OWNS a simulator; simlock.sh, sourced below, is how many may
# run at once (one, host-wide).  The two are separate questions and are kept in
# separate files.

# Sourced from the caller's HERE, the convention every harness here already
# follows (`HERE=$(cd "$(dirname "$0")" && pwd)'), because POSIX sh gives a
# sourced file no way to find itself.
. "${HERE:-$(dirname "$0")}/simlock.sh"

sim_pid=

sim_stop() {
	if [ -n "$sim_pid" ]; then
		kill -9 "$sim_pid" 2>/dev/null
		sim_pid=
	fi
	sim_lock_release
}

# The MCP debugger serves its own simulator and always talks to port 7800.  It
# is a session-length process a harness must not reclaim, so its port is
# refused rather than cleared: pass S=http://localhost:<other> to move.
sim_port_clear() {
	pids=$(lsof -tiTCP:"$1" -sTCP:LISTEN 2>/dev/null) || return 0
	[ -n "$pids" ] || return 0
	for p in $pids; do
		owner=$(ps -o ppid= -p "$p" 2>/dev/null | tr -d ' ')
		case $(ps -o comm= -p "${owner:-0}" 2>/dev/null) in
		*c900mcp*)
			echo "port $1 serves the MCP debugger; set S=http://localhost:<port>" >&2
			return 1
			;;
		esac
	done
	kill -9 $pids 2>/dev/null
	sleep 1
}

# The traps cover an ordinary exit, a Ctrl-C and a TERM.  They cannot cover a
# SIGKILL, so the guard has to be recoverable rather than merely careful: on
# the next run sim_port_clear reclaims a listener left on this harness's own
# port, and simlock's reclaim path (holder gone, simulator still alive) frees
# the global lock.  Nothing here requires the previous run to have exited well.
sim_start() {
	sim_bin=$1
	sim_url=$2
	sim_disp=${3:-headless}
	sim_port=${sim_url##*:}

	# Trap first, so a signal arriving between the acquire and the spawn
	# still releases the lock.
	trap 'sim_stop' EXIT
	trap 'sim_stop; exit 130' INT
	trap 'sim_stop; exit 143' TERM
	sim_lock_acquire "$sim_port" || return 1
	# Reclaiming the port is safe only because the global lock is already
	# ours: anything still listening here is by construction an orphan, not
	# another harness's live run.
	sim_port_clear "$sim_port" || { sim_lock_release; return 1; }
	# fd 9 (the lock) is inherited DELIBERATELY -- see simlock.sh.
	nohup "$sim_bin" --http "${sim_url#http://}" -display "$sim_disp" \
		>/dev/null 2>&1 &
	sim_pid=$!
	sim_lock_sim "$sim_pid" "$sim_port"
	sleep 3
}
