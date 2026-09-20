"""kermit2.py -- two C900s on one serial wire, moving a file with kermit(1).

    python3 kermit2.py [image] [--text] [--wild] [--cut] [--trace] [--keep]

WHAT IT PROVES.  That kermit(1) transfers a file, end to end, between two real
targets: the sender reads it off one machine's disk, the KERMIT protocol carries
it over a serial line, and the receiver writes it to the other machine's disk
with the same sum(1) checksum.  Nothing in the path is a stub -- both ends are
the shipped binary out of the image, and the line is the guest's own /dev/tty51
(SCC channel A) with the driver, the tty discipline and the packet timers all
doing their real work.

That is the test #324 needed and #282 had not got.  Until #324 the program could
not send a byte at all: ttoc(), conoc() and zchout() take the address of a char
PARAMETER, the compiler retyped such a parameter to int, and `&c' therefore
addressed the high half of the promoted word -- a NUL for every ASCII character.

  --text    transfer /etc/termcap as text rather than a binary with -i
  --wild    name the file by a wildcard kermit itself has to expand
  --cut     NEGATIVE CONTROL: both ends attach to a bridge that relays nothing.
            The transfer must then FAIL; a --cut run that passes means the test
            is measuring something other than the wire.
  --trace   hexdump the wire
  --keep    keep the working directory (images and console logs)

THE LINE.  /etc/rc.net gives /dev/tty51 to slip -- the same line the wire is
on -- and a working boot now runs rc.net to completion, so both guests come up
with slip already attached to it.  free_line() kills it on each guest before
the transfer; /usr is still mounted by hand.

WHAT IT WAITS ON.  The guest's own shell prompt, never a wall-clock window and
never an echoed marker.  The one question a prompt cannot answer -- whether B's
BACKGROUNDED receiver has opened the line yet -- is put to the uucp lock file it
creates, in a poll whose every probe is itself a prompt-driven command.

The emulator, not the simulator: this needs two machines and a serial link
between them, and many emulators may run at once.  wire.py (net/twohost) is
the bridge, as it is for the two-host network tests.
"""
import atexit
import os
import shutil
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
OS = os.path.normpath(os.path.join(HERE, ".."))               # the repository root
WIREPY = os.path.join(OS, "net", "twohost", "wire.py")

# net/twohost says which image the guests boot and where the emulator is.
sys.path.insert(0, os.path.join(OS, "net", "twohost"))
from twohost import test_image, EMU
LINE = "/dev/tty51"                     # SCC channel A -- the emulator's --wire
LCK = "/usr/spool/uucp/LCK..tty51"


def say(s):
    sys.stdout.write(s + "\n")
    sys.stdout.flush()


# Every child this script starts, so that ALL of them are shut down and waited
# for on every exit path.  A Popen without a wait() leaves a zombie per run, and
# a killed script leaves the emulators themselves running -- which is how one
# lane got a leaked emulator holding a disk image and results from the run
# before.  atexit covers a normal return and an exception; the signal handlers
# cover `timeout' and Ctrl-C, which would otherwise orphan the emulators alive.
# (process, signal): the bridge is asked to stop with SIGINT rather than SIGTERM
# because that is the one it catches, and catching it is how it gets to print its
# per-direction byte count -- the evidence the run is quoted on.
CHILDREN = []


def reap_all():
    for p, sig in CHILDREN:
        if p.poll() is None:
            p.send_signal(sig)
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
        else:
            p.wait()                    # already dead: collect the zombie
    del CHILDREN[:]


def _onsignal(sig, frame):
    reap_all()
    sys.exit(128 + sig)


atexit.register(reap_all)
for _s in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
    signal.signal(_s, _onsignal)


class Guest:
    def __init__(self, name, img, sock, workdir, trace=False):
        self.name, self.buf, self.cursor = name, bytearray(), 0
        self.log = open(os.path.join(workdir, "%s.console" % name), "wb")
        self.err = open(os.path.join(workdir, "%s.err" % name), "wb")
        cmd = [EMU, "--disk", img, "--wire", sock, "--stop-on", "none"]
        if trace:
            cmd.append("--wire-trace")
        self.p = subprocess.Popen(cmd, cwd=os.path.dirname(EMU),
                                  stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=self.err)
        CHILDREN.append((self.p, signal.SIGTERM))
        os.set_blocking(self.p.stdout.fileno(), False)

    def expect(self, pat, timeout, why=""):
        pat = pat.encode()
        end = time.time() + timeout
        while time.time() < end:
            i = bytes(self.buf).find(pat, self.cursor)
            if i >= 0:
                self.cursor = i + len(pat)
                return True
            d = self.p.stdout.read(65536)
            if d:
                self.buf.extend(d)
                self.log.write(d)
                self.log.flush()
            elif self.p.poll() is not None:
                # The emulator is gone; waiting out the timeout would only
                # delay the report.  Its stderr says why (a --wire that could
                # not connect, a missing ROM, a bad image).
                say("%s: the emulator exited (status %d) -- see %s.err"
                    % (self.name, self.p.returncode, self.name))
                return False
            else:
                time.sleep(0.05)
        say("%s: TIMEOUT after %ds waiting for %r %s"
            % (self.name, timeout, pat.decode("latin1"), why))
        return False

    def line(self, s):
        self.p.stdin.write((s + "\r").encode("latin1"))
        self.p.stdin.flush()

    def mark(self):
        return len(self.buf)

    def since(self, m):
        return bytes(self.buf[m:]).decode("latin1").replace("\r", "")

    def cmd(self, s, timeout=300):
        m = self.mark()
        self.line(s)
        ok = self.expect("# ", timeout, "(after %r)" % s)
        return ok, self.since(m)

    def close_logs(self):
        self.log.close()
        self.err.close()


def single_user(g):
    """Log in as root at the multi-user getty a working boot reaches."""
    if not g.expect("login:", 900, "(multi-user getty)"):
        return False
    g.line("root")
    if not g.expect("# ", 300, "(root shell)"):
        return False
    say("%s: at a root shell" % g.name)
    return True


def free_line(g):
    """Stop any slip holding LINE, so kermit is its only reader.

    /etc/rc.net puts SLIP on this line at boot.
    """
    # rc runs rc.net in the background, so slip can start after login, and a
    # survivor eats kermit's packets.  rc starts update after rc.net.
    for _ in range(60):
        m = g.mark()
        g.line("/bin/ps -ax | /bin/grep /etc/ | /bin/grep -v grep")
        g.expect("# ", 120, "(ps for slip)")
        ps = g.since(m).split("\n")[1:]
        pids = [l.split()[1] for l in ps if "/etc/slip" in l]
        if pids:
            say("%s: rc.net started slip (pid %s) -- stopping it"
                % (g.name, " ".join(pids)))
            g.cmd("/bin/kill %s" % " ".join(pids))
        elif (any("/etc/update" in l for l in ps)
              and not any("/etc/rc.net" in l for l in ps)):
            return
        else:
            g.cmd("/bin/sleep 2")
    say("%s: slip or rc.net still running" % g.name)


def transfer(A, B, text):
    """Send a file A -> B and compare sum(1) at both ends.  Returns a verdict."""
    for g in (A, B):
        if not g.cmd("/etc/mount /dev/hd6 /usr")[0]:
            return "FAIL: %s could not mount /usr" % g.name
        say("%s: %s" % (g.name, g.cmd("/bin/ls -l /usr/bin/kermit")[1].strip()))

    src = "/etc/termcap" if text else "/usr/bin/minirb"
    dst = "/" + os.path.basename(src)
    mode = "" if text else "-i "
    ok, out = A.cmd("/bin/sum %s; /bin/ls -l %s" % (src, src))
    say("A: source %s\n%s" % (src, out.strip()))
    asum = [w for w in out.split() if w.isdigit()]
    if not asum:
        return "FAIL: no checksum for %s on A" % src

    B.cmd("cd /; /bin/rm -f %s" % dst)
    ok, out = B.cmd("/usr/bin/kermit %s-r -l %s -b 9600 >/krecv.log 2>&1 &"
                    % (mode, LINE))
    say("B: receiver launched:\n%s" % out.strip())
    for _ in range(30):
        ok, out = B.cmd("/bin/ls -l %s" % LCK)
        if "LCK..tty51" in out.split("\n", 1)[-1]:       # past the command echo
            say("B: the receiver holds the line (%s)" % LCK)
            break
    else:
        say("B: no lock file appeared; going ahead anyway")

    m = A.mark()
    A.line("/usr/bin/kermit %s-s %s -l %s -b 9600" % (mode, src, LINE))
    sent = A.expect("# ", 3600, "(sender finished)")
    say("=== A, sending ===\n%s" % A.since(m))

    # `wait' is the receiver's own statement that it is finished: the shell does
    # not print another prompt until the background job is reaped.
    B.cmd("wait", timeout=1200)
    ok, out = B.cmd("/bin/cat /krecv.log; /bin/ls -l %s; /bin/sum %s" % (dst, dst))
    say("=== B, receiving ===\n%s" % out.strip())
    bsum = [w for w in out.split() if w.isdigit()]

    if not sent:
        return "FAIL: the sender never returned to the shell"
    if asum[0] in bsum:
        return "PASS: %s moved A -> B, sum %s at both ends" % (src, asum[0])
    return "FAIL: A sum %s, B reported %s" % (asum[:2], bsum[:4])


def wildcard(A, B):
    """kermit's OWN wildcard expansion, which the command line never reaches.

    `kermit -s' takes its file names literally -- the shell has already expanded
    them -- so zxpand()/fgen(), which reads the directory itself, is only used by
    the SEND command typed at the program's prompt (and by a server's GET).  It
    is a code path of its own and it had a defect of its own: BSD29 selects
    opendir()/readdir(), readdir() was undeclared, and an implicit int does not
    hold a far pointer.  So B is a kermit SERVER and A drives its own prompt.
    """
    for g in (A, B):
        if not g.cmd("/etc/mount /dev/hd6 /usr")[0]:
            return "FAIL: %s could not mount /usr" % g.name
    ok, out = A.cmd("/bin/sum /usr/bin/minirb")
    asum = [w for w in out.split() if w.isdigit()]
    if not asum:
        return "FAIL: no checksum on A"
    say("A: source /usr/bin/minirb sum %s" % asum[0])

    B.cmd("cd /; /bin/rm -f /minirb")
    # -i on the SERVER too: the file type is each side's own setting, and a
    # text-mode receiver strips the CRs out of a binary a binary-mode sender
    # sent -- a shorter file with a different checksum, and not a defect.
    B.cmd("/usr/bin/kermit -i -x -l %s -b 9600 >/ksrv.log 2>&1 &" % LINE)
    for _ in range(30):
        ok, out = B.cmd("/bin/ls -l %s" % LCK)
        if "LCK..tty51" in out.split("\n", 1)[-1]:
            say("B: the server holds the line")
            break

    m = A.mark()
    A.line("/usr/bin/kermit -l %s -b 9600" % LINE)
    if not A.expect("C-Kermit>", 600, "(the command prompt)"):
        return "FAIL: no C-Kermit> prompt on A"
    A.line("set file type binary")
    A.expect("C-Kermit>", 300)
    A.line("send /usr/bin/min*")            # unquoted at kermit's OWN prompt
    got = A.expect("C-Kermit>", 3600, "(send finished)")
    A.line("finish")
    A.expect("C-Kermit>", 600)
    A.line("quit")
    A.expect("# ", 600)
    say("=== A, at its own prompt ===\n%s" % A.since(m))

    B.cmd("wait", timeout=1200)
    ok, out = B.cmd("/bin/cat /ksrv.log; /bin/ls -l /minirb; /bin/sum /minirb")
    say("=== B, serving ===\n%s" % out.strip())
    if not got:
        return "FAIL: the send never came back to the prompt"
    if asum[0] in [w for w in out.split() if w.isdigit()]:
        return "PASS: kermit expanded /usr/bin/min* itself, sum %s at both ends" % asum[0]
    return "FAIL: kermit's own wildcard send did not deliver the file"


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    opts = [a for a in argv[1:] if a.startswith("--")]
    # The test image, or the one named.
    img = test_image(args[0] if args else None)
    if img is None:
        return 2
    cut, trace = "--cut" in opts, "--trace" in opts
    text, wild = "--text" in opts, "--wild" in opts

    if not EMU or not os.path.exists(EMU):
        say("no emulator, and two of them are needed to run this harness.")
        subprocess.call(["sh", os.path.join(OS, "mk", "deps.sh"), "-n", "emu"])
        return 2

    work = os.path.join(os.environ.get("TMPDIR", "/tmp"),
                        "c900-kermit2.%d" % os.getpid())
    os.makedirs(work)
    say("workdir %s" % work)
    # The socket, not the workdir: an AF_UNIX path is capped at about 108 bytes
    # and $TMPDIR is not always short.
    sock = "/tmp/c900k2.%d.sock" % os.getpid()

    imgs = {}
    for n in ("A", "B"):
        imgs[n] = os.path.join(work, "%s.bin" % n)
        # A copy per guest: the emulator writes THROUGH to the image it boots,
        # so two guests on one file is silent mutual corruption.
        subprocess.call(["cp", "--reflink=auto", img, imgs[n]])

    wcmd = [sys.executable, WIREPY, sock,
            "--log=%s" % os.path.join(work, "wire.log")]
    if cut:
        wcmd.append("--cut")
    if trace:
        wcmd.append("--trace")
    CHILDREN.append((subprocess.Popen(wcmd), signal.SIGINT))
    for _ in range(100):
        if os.path.exists(sock):
            break
        time.sleep(0.1)

    guests = {}
    verdict = "FAIL: the run did not finish"
    try:
        for n in ("A", "B"):
            guests[n] = Guest(n, imgs[n], sock, work, trace)
        A, B = guests["A"], guests["B"]
        if single_user(A) and single_user(B):
            free_line(A)
            free_line(B)
            verdict = (wildcard(A, B) if wild
                       else transfer(A, B, text))
        else:
            verdict = "FAIL: a guest did not reach a root shell"
    finally:
        reap_all()
        for g in guests.values():
            g.close_logs()
        try:
            os.unlink(sock)
        except OSError:
            pass
        # The relay's own byte count per direction: independent evidence that
        # the file crossed a wire rather than being conjured at either end.
        try:
            lines = [l for l in open(os.path.join(work, "wire.log"))
                     if l.startswith("wire: ")]
            say("=== wire ===\n%s" % lines[-1].rstrip())
        except (OSError, IndexError):
            pass
        say(verdict)
        if "--keep" not in opts and verdict.startswith("PASS"):
            shutil.rmtree(work, ignore_errors=True)
        else:
            say("kept %s (console logs and images)" % work)
    # --cut is the negative control: there, a FAIL is the pass.
    good = verdict.startswith("FAIL") if cut else verdict.startswith("PASS")
    return 0 if good else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
