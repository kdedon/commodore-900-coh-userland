"""twohost.py -- two C900s on one serial link, and a login across it.

    python3 twohost.py [--cut] [--trace] [--dist NAME] [--keep] [--quick]

WHAT THIS TESTS.  Everything the multi-user side of this system does with more
than one machine needs a second machine, and until now there was none: the SLIP
harnesses in hostbuild/ put the HOST's TCP/IP at the far end of the wire, which
proves the stack talks to Linux, not that two C900s can serve each other.  Here
both ends are the real thing -- two emulator instances, each booting the shipped
image to multi-user and running its own inet daemon, slip and telnetd -- joined by
wire.py through the emulator's `--wire' (SCC channel A, the guest's /dev/tty51,
which is the line /etc/rc.net gives to SLIP).

THE RUN, and what each step proves:

  1. Both machines boot to multi-user and log in as root on their consoles.
  2. `nonce' machine B writes a random word into a file only it has.
  3. Each machine gets an inet daemon, an address and SLIP on the line -- from
     its own boot if rc.net managed it, otherwise typed (see ensure_stack).
     Both configure as 10.0.0.2 by default, so A takes 10.0.0.1, which is the
     address /etc/hosts already calls `peer'.
  4. ping A -> B.  ICMP over SLIP, both directions of the wire.  A smoke test:
     it is not the point, and it is not enough on its own.
  5. telnet A -> B, log in as root over TCP, and cat the nonce file.  A pty is
     allocated on B, login(1) runs there, a shell runs under it, and its output
     comes back over the connection.  The nonce can only have come from B.

PASS is all three of: ping replies, a login prompt out of B's telnetd, and the
nonce on A's console.  The nonce is the criterion that cannot be faked by a
harness bug -- it is generated per run and never typed at A.

THE NEGATIVE CONTROL.  `--cut' runs everything identically with the bridge
relaying nothing.  The same three criteria must then all FAIL; a --cut run that
"passes" means the test is measuring something other than the network, and the
Makefile's `negative' target exists so that this is checked and not assumed.

Neither guest is modified: no image is patched, and the only thing typed on B is
the nonce file.  Runs are on COPIES of the packed image, because the emulator
writes through to the image it boots.
"""
import os
import random
import shutil
import signal
import string
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
OS = os.path.normpath(os.path.join(HERE, "..", ".."))
C900_ROOT = os.path.normpath(os.path.join(OS, ".."))
# The emulator is a CONSUMED CHECKOUT.  Same two candidates, in the same order,
# as mk/emulator.sh -- a checkout beside this repository, then one inside a
# `repos/' directory beside it -- because this repository is consumed both ways
# and a single relative path is silently wrong in whichever it was not written
# for.  $EMU still names one explicitly, as a checkout or as the binary itself.
# What is gone is the default of /home/<one developer>/git/commodore-900-emulator,
# which made this harness work on exactly one machine.
def emu_search():
    """The candidate CHECKOUTS, in mk/emulator.sh's order.  A `c900' on $PATH
    is this file's last resort rather than its second, which is where find_emu()
    has always looked for it.

    The list has to be that file's, not a shorter one: this repository is
    consumed both side by side and staged under a `repos/' directory, and a
    harness that searched one parent found the emulator in whichever layout it
    was written for and refused to run in the other -- which is how `make talk'
    came to be unrunnable on a machine where every other emulator harness works.

    THREE parents, not `until /', for mk/emulator.sh's reason: three reaches the
    enclosing workspace from <workspace>/repos/<repo>, and anything further out
    is not a sibling but a coincidence.
    """
    out = [os.path.join(C900_ROOT, "deps", "commodore-900-emulator")]
    d = C900_ROOT
    for _ in range(3):
        d = os.path.normpath(os.path.join(d, ".."))
        out.append(os.path.join(d, "commodore-900-emulator"))
    out.append(os.path.join(C900_ROOT, "repos", "commodore-900-emulator"))
    return out


EMU_SEARCH = emu_search()


def dist_search():
    """The candidate CHECKOUTS of commodore-900-dist, in mk/deps.sh's
    order: deps/ (where `make deps DEP=dist' would place one -- there is no
    such release today, but the slot is not this file's to skip), then three
    parents out from this repository, then one under a `repos/' directory
    beside it.

    Mirrored here rather than invoked, for the reason emu_search() gives: a
    Python harness cannot portably source mk/dist.sh, so the two lists are
    kept in the same order by comment rather than by one calling the other.
    The image format itself is not this repository's subject -- see
    mk/dist.sh -- and the images are packed over there, in a directory
    dist_imgdir() asks that repository for.
    """
    out = [os.path.join(C900_ROOT, "deps", "commodore-900-dist")]
    d = C900_ROOT
    for _ in range(3):
        d = os.path.normpath(os.path.join(d, ".."))
        out.append(os.path.join(d, "commodore-900-dist"))
    out.append(os.path.join(C900_ROOT, "repos", "commodore-900-dist"))
    return out


DIST_SEARCH = dist_search()


def find_dist():
    """The commodore-900-dist checkout, or "" -- refusal is at the point
    of use, in dist_image()."""
    given = os.environ.get("C900_DIST", "")
    if given:
        return given
    for cand in DIST_SEARCH:
        if os.path.isfile(os.path.join(cand, "os", "hostbuild", "workimg.sh")):
            return cand
    return ""


C900_DIST = find_dist()


def dist_imgdir():
    """Where a build of these parts writes its images, asked of the dist
    repository rather than spelled here.

    `os/hostbuild/build/<dist>.bin' over there is a symlink to whichever build
    that checkout packed last; the real directory is derived from the parts a
    build resolved, and `make -s imgdir' is what prints it.  This repository is
    passed as the userland -- $OS above, which is this repository's root -- so
    what comes back is the directory of a build made from the programs these
    runs are about to boot.  "" when the question cannot be answered -- the
    refusal is in dist_image(), with the rest.
    """
    if not C900_DIST:
        return ""
    env = dict(os.environ, C900_USERLAND=OS)
    try:
        p = subprocess.Popen(
            ["make", "-s", "--no-print-directory", "-C",
             os.path.join(C900_DIST, "os", "hostbuild"), "imgdir"],
            stdout=subprocess.PIPE, stderr=open(os.devnull, "w"), env=env)
        out = p.communicate()[0]
    except OSError:
        return ""
    if p.returncode != 0:
        return ""
    # make[N] lines dropped as well as suppressed: a make invoked from inside
    # another one inherits -w, so the last line of the output is `Leaving
    # directory' rather than the path, and taken as the image directory it
    # names a file nothing packed.
    lines = [l for l in out.decode("utf-8", "replace").split("\n")
             if l.strip() and not l.startswith("make[")]
    return lines[-1].strip() if lines else ""


C900_IMGDIR = dist_imgdir()


def dist_image(dist):
    """The packed image for `dist', or None (having already said() why): a
    missing CHECKOUT and a missing IMAGE are different failures, and only the
    caller knows whether "build it, or pass --dist" (an image gap) or a clone
    instruction (a checkout gap) is the fix -- so both are named, the same way
    mk/dist.sh's dist_img/dist_need name them for the shell harnesses.
    """
    if not C900_DIST:
        say("cannot boot %s without the distribution repository." % dist)
        say("  Clone commodore-900-dist to one of:")
        for cand in DIST_SEARCH:
            say("    %s" % cand)
        say("  or set C900_DIST to a checkout.")
        return None
    if not C900_IMGDIR:
        say("cannot boot %s: %s could not say where a build of these parts"
            % (dist, C900_DIST))
        say("  writes its images.  Ask it directly:")
        say("      make -C %s/os/hostbuild imgdir" % C900_DIST)
        return None
    img = os.path.join(C900_IMGDIR, "%s.bin" % dist)
    if not os.path.exists(img):
        say("no packed image for %s: %s" % (dist, img))
        say("  Images are packed in the distribution repository:")
        say("      make -C %s/os/hostbuild dist DIST=%s" % (C900_DIST, dist))
        return None
    return img


def find_emu():
    """The c900 binary, or "" -- refusal is at the point of use, in main()."""
    emu = os.environ.get("C900_EMU", "") or os.environ.get("EMU", "")
    if os.path.isdir(emu):
        emu = os.path.join(emu, "bin", "c900")
    if emu:
        return emu
    for d in EMU_SEARCH:
        c = os.path.join(d, "bin", "c900")
        if os.access(c, os.X_OK):
            return c
    # Last, a `c900' on $PATH -- as mk/emulator.sh ends its own search.
    return shutil.which("c900") or ""


EMU = find_emu()
EMUDIR = os.path.dirname(EMU)

ADDR_A, ADDR_B, MASK = "10.0.0.1", "10.0.0.2", "255.255.255.0"
SLIPLINE = "/dev/tty51"          # SCC channel A -- the emulator's --wire


class Guest:
    """One emulator instance, driven through its console pipes.

    Input is not paced by the harness: the emulator takes a byte from stdin only
    when the guest's receiver is empty, so a whole line written here is delivered
    at the rate the guest reads it and cannot overrun the SCC.
    """

    def __init__(self, name, img, sock, workdir, trace=False):
        self.name, self.buf, self.cursor = name, bytearray(), 0
        self.log = open(os.path.join(workdir, "%s.console" % name), "wb")
        cmd = [EMU, "--disk", img, "--wire", sock]
        if trace:
            cmd.append("--wire-trace")
        self.err = open(os.path.join(workdir, "%s.err" % name), "wb")
        self.p = subprocess.Popen(cmd, cwd=EMUDIR, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=self.err)
        os.set_blocking(self.p.stdout.fileno(), False)

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            d = self.p.stdout.read(65536)
            if d:
                self.buf.extend(d)
                self.log.write(d)
                self.log.flush()
            else:
                time.sleep(0.05)

    def expect(self, pat, timeout, why=""):
        """Wait for pat in the output not yet matched.  Returns True/False.

        The search starts at a cursor that only ever advances past a match, NOT
        at "everything that arrives from now on": a single read() returns
        whatever the guest has sent, so the shell prompt that follows a
        program's last line of output usually arrives in the SAME chunk, and a
        from-now-on search waits out its whole timeout for a prompt it has
        already been given.
        """
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
            else:
                time.sleep(0.05)
        say("%s: TIMEOUT after %ds waiting for %r %s"
            % (self.name, timeout, pat.decode("latin1"), why))
        return False

    def send(self, s):
        self.p.stdin.write(s.encode("latin1") if isinstance(s, str) else s)
        self.p.stdin.flush()

    def line(self, s):
        self.send(s + "\r")

    def tail(self, n=1500):
        return bytes(self.buf[-n:]).decode("latin1")

    def since(self, mark):
        return bytes(self.buf[mark:]).decode("latin1")

    def mark(self):
        return len(self.buf)

    def stop(self):
        if self.p.poll() is None:
            self.p.kill()
            self.p.wait()
        self.log.close()
        self.err.close()


def say(s):
    sys.stdout.write(s + "\n")
    sys.stdout.flush()


def ensure_stack(g, addr, telnetd=False):
    """Leave the guest with a running inet daemon, an address, and SLIP on the
    wire -- doing by hand only what the boot did not do for itself.

    /etc/rc.net is supposed to have done all of this.  It is asked FIRST, by
    running ifconfig with no arguments: a daemon that answers means the boot
    configured the machine and there is nothing to start.  The check is not
    ceremony -- the images this harness boots today ship an /etc/rc.net whose
    comment lines are executed by a shell with no comment lexer, so it dies on
    its first line and the machine comes up with no network at all.  Written
    this way the harness works either way, and starting a second inet daemon on a
    machine that already has one is exactly what it must not do.
    """
    g.line("/etc/ifconfig")
    if not g.expect("# ", 120, "(ifconfig probe)"):
        return False
    if "cannot reach the inet daemon" not in g.tail(400):
        say("%s: the boot brought the network up; leaving it alone" % g.name)
        started = False
    else:
        say("%s: no inet daemon after boot -- starting the stack by hand" % g.name)
        g.line("/etc/mknod /dev/inet p")
        g.expect("# ", 60)
        g.line("/etc/inet &")
        if not g.expect("inet: ready", 900, "(inet startup)"):
            return False
        started = True

    g.line("/etc/ifconfig %s %s" % (addr, MASK))
    if not g.expect("# ", 300, "(ifconfig)"):
        return False
    # Setting an address prints nothing, and the ioctl returning 0 says the
    # daemon accepted the request, not that ip_port ended up holding it.  Read
    # it back -- and look past the shell's echo of the command, which contains
    # the address whatever happened.
    m = g.mark()
    g.line("/etc/ifconfig")
    g.expect("# ", 120, "(ifconfig readback)")
    back = g.since(m).split("\n", 1)[-1]
    if ("address " + addr) not in back:
        say("%s: the interface does not hold %s:\n%s" % (g.name, addr, back))
        return False

    if started:
        g.line("/etc/slip %s &" % SLIPLINE)
        if not g.expect("slip: ready", 300, "(slip attach)"):
            return False
        if telnetd:
            g.line("/etc/telnetd -m 2 &")
            g.expect("# ", 120)
    say("%s: %s on %s" % (g.name, addr, SLIPLINE))
    return True


def boot(g):
    """Single-user prompt -> multi-user -> root shell on the console."""
    if not g.expect("Hit Ctrl+D", 300, "(single-user shell)"):
        return False
    g.send("\x04")
    if not g.expect("login:", 600, "(multi-user getty)"):
        return False
    g.line("root")
    if not g.expect("# ", 300, "(root shell)"):
        return False
    say("%s: at a root shell" % g.name)
    return True


def run(cut, trace, dist, keep, quick):
    work = os.path.join(os.environ.get("TMPDIR", "/tmp"), "c900-twohost.%d" % os.getpid())
    os.makedirs(work)
    say("workdir %s" % work)
    src = dist_image(dist)
    if not src:
        return 2
    if not EMU or not os.path.exists(EMU):
        if EMU:
            say("no emulator at EMU=%s" % EMU)
        else:
            say("no emulator, and two of them are needed to run this harness.")
        say("  Clone https://github.com/MichalPleban/commodore-900-emulator")
        say("  and `make' it, to one of:")
        for d in EMU_SEARCH:
            say("    %s" % d)
        say("  -- or put its c900 on $PATH, or set EMU to the checkout")
        say("     or to its bin/c900.")
        return 2

    imgs = {}
    for n in ("A", "B"):
        imgs[n] = os.path.join(work, "%s.bin" % n)
        subprocess.call(["cp", "--reflink=auto", src, imgs[n]])

    sock = os.path.join(work, "wire.sock")
    wcmd = [sys.executable, os.path.join(HERE, "wire.py"), sock,
            "--log=%s" % os.path.join(work, "wire.log")]
    if cut:
        wcmd.append("--cut")
    if trace:
        wcmd.append("--trace")
    wire = subprocess.Popen(wcmd)
    for _ in range(100):
        if os.path.exists(sock):
            break
        time.sleep(0.1)

    guests, ok, why = {}, False, []
    try:
        for n in ("A", "B"):
            guests[n] = Guest(n, imgs[n], sock, work, trace)
        A, B = guests["A"], guests["B"]

        if not (boot(A) and boot(B)):
            why.append("a guest did not reach a root shell")
            return report(False, why, work, wire, guests, keep)

        # A nonce that exists only on B, so that seeing it on A's console is
        # proof of transport and not of the harness echoing itself.
        nonce = "N" + "".join(random.choice(string.ascii_uppercase + string.digits)
                              for _ in range(8))
        B.line("echo %s > /nonce" % nonce)
        B.expect("# ", 60)
        say("B: nonce %s written to /nonce" % nonce)

        # Both machines configure as 10.0.0.2 by default, so A takes the address
        # /etc/hosts already calls `peer'.
        if not ensure_stack(B, ADDR_B, telnetd=True):
            why.append("B's network did not come up")
        if not ensure_stack(A, ADDR_A):
            why.append("A's network did not come up")
        if why:
            return report(False, why, work, wire, guests, keep)

        # 1) ICMP.  The stack is started by rc.net in the background and the inet daemon
        # takes minutes to initialise on a 6 MHz machine, so ping is retried
        # rather than believed the first time.
        pings = 0
        for attempt in range(6):
            m = A.mark()
            A.line("/bin/ping -c 3 -w 5 %s" % ADDR_B)
            A.expect("packets transmitted", 180, "(ping summary)")
            A.expect("# ", 180)          # ping is slow to let go of its ip channel
            out = A.since(m)
            pings = out.count("bytes from")
            say("A: ping attempt %d -> %d replies" % (attempt + 1, pings))
            if pings:
                break
        if not pings:
            why.append("no ICMP replies from %s" % ADDR_B)

        if quick:
            return report(bool(pings) and not why, why, work, wire, guests, keep)

        # 2) + 3) telnet login and a command whose output only B can produce.
        m = A.mark()
        A.line("/bin/telnet %s" % ADDR_B)
        got_login = A.expect("login:", 240, "(telnetd on B)")
        if not got_login:
            why.append("no login prompt over telnet")
        else:
            A.line("root")
            A.expect("# ", 180, "(remote shell)")
            A.line("cat /nonce")
            A.expect(nonce, 120, "(the nonce, over TCP)")
            A.line("exit")
            A.pump(20)
        out = A.since(m)
        got_nonce = nonce in out
        if not got_nonce:
            why.append("the nonce written on B never arrived on A")
        say("A: telnet session ->\n%s" % out[-1200:])

        ok = bool(pings) and got_login and got_nonce
        return report(ok, why, work, wire, guests, keep)
    finally:
        for g in guests.values():
            g.stop()
        try:
            wire.wait(timeout=10)
        except subprocess.TimeoutExpired:
            wire.send_signal(signal.SIGINT)
            wire.wait(timeout=5)


def report(ok, why, work, wire, guests, keep):
    for g in guests.values():
        g.stop()
    try:
        wire.wait(timeout=10)
    except subprocess.TimeoutExpired:
        wire.kill()
    say("--- the wire ---")
    wlog = os.path.join(work, "wire.log")
    if os.path.exists(wlog):
        lines = open(wlog).read().splitlines()
        for l in lines[:6] + (["  ..."] if len(lines) > 12 else []) + lines[-6:]:
            say("  " + l)
    for f in why:
        say("!! " + f)
    say("=== two-host network: %s" % ("PASS" if ok else "FAIL"))
    if not keep:
        shutil.rmtree(work, ignore_errors=True)
    else:
        say("(kept %s)" % work)
    return 0 if ok else 1


def main(argv):
    opts = [a for a in argv[1:] if a.startswith("--")]
    dist = "coherent3-full-test"
    for o in opts:
        if o.startswith("--dist="):
            dist = o.split("=", 1)[1]
    cut = "--cut" in opts
    rc = run(cut, "--trace" in opts, dist, "--keep" in opts, "--quick" in opts)
    if cut:
        # The control passes when the test fails: with the wire cut there is no
        # network, so anything that still reported success was not measuring it.
        say("=== negative control: %s" % ("PASS (the cut wire fails the test)"
                                          if rc else "FAIL (the test passed with no wire!)"))
        return 0 if rc else 1
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
