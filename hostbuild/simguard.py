"""simguard.py -- the Python end of hostbuild/simlock.sh.

One harness simulator on this host, globally, and no simulator outliving the
harness that started it.  The shell harnesses get this from simctl.sh +
simlock.sh; this is the same protocol on the same lock file, so a shell harness
and a Python harness contend with each other rather than each thinking it is
alone.

    with SimGuard(port=7801, lane="hunt") as g:
        sim = g.spawn([SIM, "--http", "localhost:7801", ...], stdout=log)

WHAT THE THREE FAILURE MODES NEED, since a `finally' covers only the first:

  * ordinary exit / exception -- the `with' block's __exit__.
  * SIGINT -- Python turns it into KeyboardInterrupt, so __exit__ runs.
  * SIGTERM / SIGHUP -- the default disposition kills the interpreter with no
    unwinding at all, so the simulator survives its harness.  install() gives
    them a handler that raises SystemExit, which unwinds like any other exit.
  * SIGKILL -- unhandleable by construction.  So the guard is written to be
    RECOVERABLE instead: the lock fd is inherited by the simulator (its
    lifetime is the lock's lifetime, see simlock.sh), and a later run reclaims
    both the lock (holder gone, simulator alive) and the port (any listener
    left on our own port).

The lock fd is passed to the simulator on purpose and withheld from every other
child: subprocess closes non-standard fds unless they are named in pass_fds,
which is exactly the `9>&-'-on-every-helper discipline the shell version has to
apply by hand.
"""
import errno
import shutil
import fcntl
import os
import signal
import subprocess
import sys
import time

# WHERE THE SIMULATOR BINARY COMES FROM.  Nowhere, unless $SIM says so.  The
# shell equivalent, and the long-form rationale, is mk/simulator.sh: c900sim is a
# development instrument -- no build, no gate and no released image in this
# repository needs it, and it is not one of the checkouts README.md
# lists -- so there is nothing honest to search for and nothing to tell a
# stranger to clone -- only a binary, so the one search is $PATH.  The default
# it replaces was a path under one developer's home directory, which made these
# harnesses work on exactly one machine and fail with a bare "no such file" on
# every other.
def sim_bin(purpose):
    """The c900sim binary, or a refusal naming the variable.  $SIM only."""
    sim = os.environ.get("SIM", "") or shutil.which("c900sim") or ""
    if sim and os.access(sim, os.X_OK):
        return sim
    who = os.path.basename(sys.argv[0]) or "harness"
    if sim:
        head = "%s: no simulator at SIM=%s" % (who, sim)
    else:
        head = ("%s: $SIM is not set, and a c900sim binary is needed to %s."
                % (who, purpose))
    sys.exit(head + """
  c900sim is the full-system simulator (video, both SCC channels, HTTP debug
  endpoint).  It is a development instrument: no build, no gate and no released
  image in this repository needs it, and it is not one of the checkouts
  README.md lists.  Set $SIM to a c900sim binary if you have one, or
  put one on $PATH.
  To run commands on the target without it, use
    hostbuild/emu-run.sh <cmdfile> [dist]
  which needs only a commodore-900-emulator checkout.""")


LOCK = os.environ.get("SIMLOCK",
                      os.path.expanduser("~/.cache/c900/sim.lock"))
STATE = os.environ.get("SIMSTATE", LOCK[:-5] + ".state"
                       if LOCK.endswith(".lock") else LOCK + ".state")
WAIT = float(os.environ.get("SIMWAIT", "0"))    # 0 = fail loudly at once
MAX = float(os.environ.get("SIMMAX", "2700"))   # a run older than this is hung
OFF = bool(os.environ.get("CI") or os.environ.get("SIMLOCK_OFF"))


def scan():
    """Report simulators the lock does not govern -- renamed private copies
    included, which is the case no port or binary-path check can see.  Reports
    and never kills: you do not kill a simulator you did not start."""
    try:
        ps = subprocess.run(["ps", "-eo", "pid,args"],
                            stdout=subprocess.PIPE).stdout.decode()
    except OSError:
        return
    st = _read_state()
    held = str(st[1]) if st else ""
    for line in ps.splitlines():
        if "--http" not in line and "-http" not in line:
            continue
        if "localhost:" not in line or "ps -eo" in line:
            continue
        pid = line.split(None, 1)[0]
        if pid == str(os.getpid()) or pid == held:
            continue        # ourselves, or the lock's own simulator
        if "7800" in line:
            print("note: MCP debugger simulator (pid %s) is up -- exempt from"
                  " the lock, but it still costs a core" % pid)
        else:
            print("note: unsanctioned simulator pid %s: %s"
                  % (pid, line.split(None, 1)[1][:100]))


def _read_state():
    try:
        with open(STATE) as f:
            f = f.read().split(None, 5)
        return int(f[0]), int(f[1]), float(f[2]), f[3], f[4]
    except (OSError, ValueError, IndexError):
        return None


def _alive(pid):
    try:
        os.kill(pid, 0)
    except OSError as e:
        return e.errno == errno.EPERM
    return True


def _reclaim():
    """Free a lock whose holder is gone, or whose simulator has overrun SIMMAX.

    With the fd inherited, a held lock means something is still alive holding
    it, so freeing it means killing the recorded simulator.  Returns True if
    something was killed and the lock is worth retrying for."""
    st = _read_state()
    if st is None:
        return False
    hpid, spid, started, port, lane = st
    age = time.time() - started
    if _alive(hpid) and age < MAX:
        return False                    # a live owner inside its budget
    if not spid or not _alive(spid):
        return False
    print("simguard: reclaiming simulator %d (lane %s, port %s, %ds old): %s"
          % (spid, lane, port, age,
             "holder %d is gone" % hpid if not _alive(hpid)
             else "run exceeds SIMMAX=%ds" % MAX), file=sys.stderr)
    try:
        os.kill(spid, signal.SIGKILL)
    except OSError:
        pass
    time.sleep(1)
    return True


def port_clear(port):
    """Reclaim a listener left on OUR port by a previous run that did not exit.

    Only ever called with the global lock already held, which is what makes it
    safe: no other harness can be running, so a listener here is an orphan and
    not somebody's live work.  The MCP debugger's port is refused rather than
    cleared -- it is a session-length process no harness may reclaim."""
    try:
        out = subprocess.run(["lsof", "-tiTCP:%s" % port, "-sTCP:LISTEN"],
                             stdout=subprocess.PIPE).stdout.decode().split()
    except OSError:
        return True
    for pid in out:
        try:
            owner = subprocess.run(["ps", "-o", "ppid=", "-p", pid],
                                   stdout=subprocess.PIPE).stdout.decode()
            comm = subprocess.run(["ps", "-o", "comm=", "-p", owner.strip()],
                                  stdout=subprocess.PIPE).stdout.decode()
        except OSError:
            comm = ""
        if "c900mcp" in comm:
            print("simguard: port %s serves the MCP debugger; set"
                  " SIMHTTP=http://localhost:<other>" % port, file=sys.stderr)
            return False
        print("simguard: clearing orphaned listener pid %s on port %s"
              % (pid, port), file=sys.stderr)
        try:
            os.kill(int(pid), signal.SIGKILL)
        except OSError:
            pass
    if out:
        time.sleep(1)
    return True


_installed = False


def install():
    """Turn SIGTERM/SIGHUP into a normal unwind so `with' blocks still run."""
    global _installed
    if _installed:
        return
    _installed = True

    def die(sig, frame):
        raise SystemExit(128 + sig)

    for sig in (signal.SIGTERM, signal.SIGHUP):
        try:
            signal.signal(sig, die)
        except (OSError, ValueError):
            pass


class SimGuard(object):
    def __init__(self, port, lane="?"):
        self.port = str(port)
        self.lane = lane
        self.fd = None
        self.children = []

    def __enter__(self):
        install()
        if OFF:
            return self
        os.makedirs(os.path.dirname(LOCK), exist_ok=True)
        self.fd = os.open(LOCK, os.O_CREAT | os.O_WRONLY | os.O_APPEND, 0o644)
        os.set_inheritable(self.fd, True)
        scan()
        if not self._flock(WAIT):
            st = _read_state()
            print("simguard: another simulator holds %s" % LOCK,
                  file=sys.stderr)
            print("  holder: %s" % (
                "pid %d, simulator pid %d, port %s, lane %s, %ds ago"
                % (st[0], st[1], st[3], st[4], time.time() - st[2])
                if st else "unknown (no %s); try: lsof %s" % (STATE, LOCK)),
                file=sys.stderr)
            if not (_reclaim() and self._flock(10)):
                os.close(self.fd)
                self.fd = None
                sys.exit("simguard: refusing to start a second simulator."
                         "  Wait for it, or set SIMWAIT=<seconds> to queue.")
        self._state(0)
        if not port_clear(self.port):
            self.__exit__(None, None, None)
            sys.exit("simguard: cannot use port %s" % self.port)
        return self

    def _flock(self, wait):
        deadline = time.time() + wait
        while True:
            try:
                fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return True
            except OSError:
                if time.time() >= deadline:
                    return False
                time.sleep(0.5)

    def _state(self, simpid):
        if self.fd is None:
            return
        try:
            with open(STATE, "w") as f:
                f.write("%d %d %d %s %s %s\n"
                        % (os.getpid(), simpid, time.time(), self.port,
                           self.lane, os.path.basename(sys.argv[0])))
        except OSError:
            pass

    def spawn(self, argv, **kw):
        """Start the simulator holding the lock fd, so the lock cannot outlive
        it and it cannot outlive the lock."""
        if self.fd is not None:
            kw["pass_fds"] = tuple(kw.get("pass_fds", ())) + (self.fd,)
        p = subprocess.Popen(argv, **kw)
        self.children.append(p)
        self._state(p.pid)
        return p

    def __exit__(self, *exc):
        for p in self.children:
            if p.poll() is None:
                p.kill()            # it ignores TERM while free-running
                try:
                    p.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    pass
        self.children = []
        if self.fd is not None:
            try:
                os.unlink(STATE)
            except OSError:
                pass
            os.close(self.fd)
            self.fd = None
        return False
