"""sntpcheck.py -- does the C900 actually set its clock from the network?

    python3 sntpcheck.py [--deaf] [--dist NAME] [--keep] [--only CASE,...]

WHAT IS UNDER TEST.  net/sntp.c -- the machine's ONLY automatic clock source.
read_cmos() returns 0 (sys/z8001/src/mdstub.c) and the M58321 has no driver, so
this machine boots at the 1970 epoch every time, and every file mtime, every
make(1) decision, every find -newer and every mail Date: header downstream of
that is whatever the boot left behind.  Until this harness existed the client
had been compiled, staged as /etc/sntp and wired into /etc/rc.net, and had never
been seen to run.

HOW.  One emulator boots the shipped image to multi-user, so /etc/rc.net brings
up inet, the address and SLIP on /dev/tty51.  The far end of that serial line is
sntpwire.py, a scripted SNTP server on the host at 10.0.0.1 -- the address
rc.net's own NTPSERVER already names.  Every case is a command typed at the
guest's console, judged on THREE separate observations, never on one:

    what the guest printed
    what crossed the wire        (sntpwire.py's log: asked?  answered?)
    what the guest's clock says afterwards   (date -u, parsed back to a time_t)

THE TRAP THIS IS BUILT AROUND.  A machine whose clock is right after sntp ran
has three possible histories and only one of them is the thing under test:

    set from the network   a request went out, an answer came back, stime() took
    clock unchanged        sntp ran and declined, or failed, or never sent
    never asked            no packet at all -- a name that would not resolve, a
                           stack that was not up, a binary that is not installed

The first two are indistinguishable from a clock reading alone, which is why
every criterion below is a triple.  And the value the clock is moved TO is
generated per run -- a random second inside a wide window -- so it exists in no
file on the image, in nothing typed at the console, and in no other run.  A
guest that prints it can only have got it over the wire.

THE NEGATIVE CONTROLS.  Four, of increasing sharpness:

    --deaf         the whole run again with the server receiving and logging
                   but never answering.  Every case that judges an ANSWER must
                   then fail; the ones that judge only what the guest SENT are
                   marked deaf_ok and must still pass.
    case unknown   sntp is pointed at a name that resolves nowhere.  It must
                   fail AND put nothing on the wire AND leave the clock alone --
                   "never asked", told apart from "asked and got nothing".
    case nobody    sntp is pointed at an address inside the SLIP net where
                   nothing listens.  The request must APPEAR on the wire,
                   unanswered, and the clock must still not move.
    case queryonly sntp -q must ask, print the right date, and NOT set the
                   clock.  Reading the time and setting it are separate, and a
                   harness that cannot tell them apart is measuring neither.

A case that cannot fail proves nothing, and the deaf run is what enforces that
here: every case must either fall over under it or be MARKED deaf_ok with a
reason.  Two are so marked, `nobody' and `unknown', and both are marked because
every one of their criteria is about what the guest sent and what it did not do
-- which a silent server cannot change.  The rule caught two more when this was
written: `staleorg' and `nobody' both passed the first deaf run, `staleorg'
because its criterion ("no server answered") is printed just as readily by a
server that says nothing at all.  It now looks for the -v line that only a
DISCARDED reply produces.  (Unlike resolvcheck there is no source-coverage
assertion: sntp is one file, and the deaf run is the sharper question.)

GUEST TIME RUNS ABOUT 40x WALL CLOCK on the emulator (FINDINGS C-13), so no
criterion here is a wall-clock window: clock comparisons are made in GUEST
seconds, read out of the guest, with a slack that is a guest-second budget for
the commands typed in between.

Nothing is rebuilt and no image is modified: the guest boots a COPY, because the
emulator writes through to the disk it is given.
"""
import calendar
import os
import random
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
NET = os.path.normpath(os.path.join(HERE, "..", ".."))
OS = os.path.normpath(os.path.join(NET, ".."))
sys.path.insert(0, os.path.join(NET, "twohost"))
from twohost import Guest, boot, dist_image, ensure_stack, say  # noqa: E402

ADDR = "10.0.0.2"                 # what rc.net gives the guest
PEER = "10.0.0.1"                 # this harness, and the SNTP server
NOBODY = "10.0.0.77"              # inside the SLIP net, nothing listening
NTP_EPOCH = 2208988800            # 1900 -> 1970, and it is > LONG_MAX

# The clock is parked here before every case that must not move it.  A date
# with no relation to anything: the machine boots at 1970 and the answers are
# in the 2020s, so a clock reading near 1985 can only be this.
WRONG = "8503140917"              # date -u yymmddhhmm: 1985-03-14 09:17 GMT
WRONG_T = calendar.timegm((1985, 3, 14, 9, 17, 0, 0, 0, 0))

# How far the guest's clock may legitimately have run past the answer by the
# time it is read back, in GUEST seconds.  Two console commands at 40x wall
# clock is tens of guest seconds; this is generous and still a thousandth of
# the distance to any wrong answer the arithmetic could produce.
SLACK = 3600


def rnd_ntp():
    """A per-run NTP transmit timestamp, and the time_t it must produce.

    Chosen inside 2021..2035 so that both halves are interesting: the NTP
    seconds field is well above LONG_MAX (it passed 2^31 in 1968), the offset
    subtracted from it is also above LONG_MAX, and the RESULT is a positive
    signed long.  The exact second is random, so the date this run sets the
    clock to appears nowhere on the image and in no previous run.
    """
    lo = calendar.timegm((2021, 1, 1, 0, 0, 0, 0, 0, 0))
    hi = calendar.timegm((2035, 12, 1, 0, 0, 0, 0, 0, 0))
    t = random.randint(lo, hi)
    return t + NTP_EPOCH, t


def rnd_era1():
    """An ERA-1 NTP timestamp: the counter has wrapped past 2036-02-07.

    sntp.c handles this with `t = secs + NTP_ERA1' where NTP_ERA1 is
    2^32 - 2208988800 = 2085978496.  The window is narrow and it is the
    interesting one: the result must still fit a signed 32-bit time_t, which
    runs out on 2038-01-19, so the NTP seconds may not exceed 61505151.  Below
    2036-02-07 the same field means 1900-something and must be REJECTED, which
    is the neighbouring case (`pre1970').
    """
    lo = calendar.timegm((2036, 3, 1, 0, 0, 0, 0, 0, 0))
    hi = calendar.timegm((2038, 1, 1, 0, 0, 0, 0, 0, 0))
    t = random.randint(lo, hi)
    return (t + NTP_EPOCH) & 0xFFFFFFFF, t


def cstr(t):
    """The guest's asctime() for a time_t, to the second.

    libc/gen/ctime.c writes the standard 24-character form and date(1) prints
    the first 24 characters of it, so this is an exact string the guest either
    produces or does not.
    """
    return time.asctime(time.gmtime(t))


def parse_date(out):
    """Parse `date -u' output back into a time_t, or None.

    The line is asctime()'s 24 characters plus " GMT" (cmd/date.c:70-77).  It
    is searched for rather than taken whole, because the shell's echo and the
    prompt share the chunk.
    """
    m = re.search(r"([A-Z][a-z]{2} [A-Z][a-z]{2} [ \d]\d "
                  r"\d\d:\d\d:\d\d \d{4}) +GMT", out)
    if not m:
        return None
    try:
        return calendar.timegm(time.strptime(m.group(1),
                                             "%a %b %d %H:%M:%S %Y"))
    except ValueError:
        return None


class Ctl(object):
    """sntpwire.py's control file: the answer, rewritten between cases."""

    def __init__(self, path):
        self.path = path
        self.write()

    def write(self, **kw):
        with open(self.path, "w") as f:
            for k, v in kw.items():
                f.write("%s %s\n" % (k, v))
        # The server re-reads this before every reply, so a rewrite takes
        # effect on the next request with no restart -- which matters because
        # the guest's SLIP line IS this server's socket.
        return self


class Log(object):
    """sntpwire.py's log, read back and asked questions about."""

    def __init__(self, path):
        self.path = path

    def lines(self):
        try:
            with open(self.path) as f:
                return f.read().splitlines()
        except OSError:
            return []

    def requests(self):
        """(timestamp, srcport) for every SNTP request the guest actually sent."""
        out = []
        for l in self.lines():
            m = re.match(r"([\d.]+) REQUEST udp from \S+:(\d+)", l)
            if m:
                out.append((float(m.group(1)), int(m.group(2))))
        return out

    def notme(self):
        return [l for l in self.lines() if " NOTME udp" in l]

    def answers(self):
        return [l for l in self.lines() if l.split(" ", 1)[-1].startswith("ANSWER udp")]

    def has(self, needle):
        return any(needle in l for l in self.lines())

    def tail(self, n=40):
        return self.lines()[-n:]


class Case(object):
    def __init__(self, name, covers, why, deaf_ok=False):
        self.name, self.covers, self.why = name, covers, why
        self.deaf_ok = deaf_ok
        self.ok = None
        self.detail = []

    def note(self, s):
        self.detail.append(s)

    def check(self, what, ok):
        self.detail.append("      %-62s %s" % (what, "ok" if ok else "FAIL"))
        self.ok = ok if self.ok is None else (self.ok and ok)
        return ok


def cmd(g, line, timeout=240):
    """Type a command, wait for the prompt, return what it printed."""
    m = g.mark()
    g.line(line)
    if not g.expect("# ", timeout, "(%s)" % line):
        return g.since(m) + "\n[TIMEOUT: no prompt]"
    out = g.since(m)
    # Drop the shell's echo of the command: it contains the address that was
    # typed, and a criterion matching it would pass on the harness's own input.
    return out.split("\n", 1)[-1] if "\n" in out else ""


def clock(g):
    """The guest's clock, as a time_t, read with date -u."""
    return parse_date(cmd(g, "/bin/date -u", 180))


def park(g, c=None):
    """Leave the clock at a known-wrong value, and say whether that took.

    Every case that must NOT move the clock starts here, because "the clock did
    not change" is only evidence if the value it did not change FROM was put
    there deliberately.  A machine still sitting at the 1970 epoch would satisfy
    "unchanged" for the wrong reason.
    """
    cmd(g, "/bin/date -u %s" % WRONG, 240)
    t = clock(g)
    ok = t is not None and abs(t - WRONG_T) < SLACK
    if c:
        c.check("the clock was parked at %s" % cstr(WRONG_T), ok)
    return t


def run_cases(g, n, log, ctl, deaf, only):
    cases = []
    ntp_s, want_t = n

    def sel(cse):
        if only and cse.name not in only:
            return None
        cases.append(cse)
        return cse

    # ------------------------------------------------------- the boot itself
    # rc.net already runs /etc/sntp -t 5 -r 2 $NTPSERVER, AFTER the slip block.
    # This is the case that says whether the shipped machine sets its own clock
    # unattended, which is the only form that matters in service, and it runs
    # FIRST because it is the only one whose evidence a later case would erase.
    #
    # Its output is NOT on the console: /etc/rc runs `sh /etc/rc.net
    # >/tmp/rc.net.log 2>&1 &' (dist/files/etc/rc.in), in the BACKGROUND, so
    # the machine reaches a login prompt while the network is still coming up
    # and nothing rc.net says is ever printed.  Read the log, and wait for it,
    # because "the file does not mention sntp yet" and "rc.net never got there"
    # look identical if it is read once.
    c = sel(Case("boot", ["rc.net", "sntp.c", "stime(2)"],
                 "/etc/rc.net sets the clock at boot, unattended"))
    if c:
        say("--- case boot")
        rcnet = ""
        for _ in range(12):
            rcnet = cmd(g, "/bin/cat /tmp/rc.net.log", 240)
            if "sntp" in rcnet or "setting the clock" in rcnet:
                break
        c.check("rc.net reached the sntp line",
                "setting the clock" in rcnet)
        c.check("a request went out during the boot", bool(log.requests()))
        c.check("the boot reports a step", "clock stepped" in rcnet)
        after = clock(g)
        c.check("and the machine's clock now reads %s, which only the wire"
                " carried" % cstr(want_t),
                after is not None and 0 <= after - want_t <= SLACK)
        c.note("      | " + rcnet.strip().replace("\n", "\n      | "))

    # ------------------------------------------------- the whole point: set it
    c = sel(Case("setclock", ["sntp.c", "stime(2)"],
                 "sntp moves the clock to a value only the wire carried"))
    if c:
        say("--- case setclock")
        before = park(c=c, g=g)
        ctl.write(secs=ntp_s, frac="0x40000000", stratum=3, refid="WIRE")
        nreq = len(log.requests())
        out = cmd(g, "/etc/sntp -v %s" % PEER, 600)
        after = clock(g)
        c.check("a request left the machine",
                len(log.requests()) > nreq)
        c.check("sntp printed the wire's date, %s" % cstr(want_t),
                cstr(want_t) in out)
        c.check("sntp reported a step", "clock stepped" in out)
        c.check("the clock MOVED off %s" % cstr(WRONG_T),
                after is not None and before is not None
                and abs(after - before) > SLACK)
        c.check("and it now reads %s (+0..%ds of guest time)"
                % (cstr(want_t), SLACK),
                after is not None and 0 <= after - want_t <= SLACK)
        c.note("      wire said %d, clock reads %s (delta %s)"
               % (want_t, after,
                  "n/a" if after is None else after - want_t))
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    # --------------------------------------------------- reading is not setting
    c = sel(Case("queryonly", ["sntp.c"],
                 "sntp -q asks and prints, and does not touch the clock"))
    if c:
        say("--- case queryonly")
        park(g, c)
        ctl.write(secs=ntp_s, frac="0x40000000")
        nreq = len(log.requests())
        out = cmd(g, "/etc/sntp -q %s" % PEER, 600)
        after = clock(g)
        c.check("it asked", len(log.requests()) > nreq)
        c.check("it printed %s" % cstr(want_t), cstr(want_t) in out)
        c.check("it said the clock was not set", "not set" in out)
        c.check("and the clock is still %s" % cstr(WRONG_T),
                after is not None and abs(after - WRONG_T) < SLACK)
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    # ---------------------------------------------------------- the rounding
    c = sel(Case("rounding", ["sntp.c"],
                 "a fraction over half a second rounds up, not down"))
    if c:
        say("--- case rounding")
        ctl.write(secs=ntp_s, frac="0xC0000000")
        out = cmd(g, "/etc/sntp -q %s" % PEER, 600)
        c.check("0.75 s made it %s, one second on" % cstr(want_t + 1),
                cstr(want_t + 1) in out)
        ctl.write(secs=ntp_s, frac="0x40000000")
        out2 = cmd(g, "/etc/sntp -q %s" % PEER, 600)
        c.check("0.25 s left it at %s" % cstr(want_t), cstr(want_t) in out2)
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    # ------------------------------------------------------- the 2036 rollover
    c = sel(Case("era1", ["sntp.c"],
                 "an NTP timestamp past the 2036 wrap converts, not rejected"))
    if c:
        say("--- case era1")
        era_s, era_t = rnd_era1()
        ctl.write(secs=era_s, frac="0x40000000")
        out = cmd(g, "/etc/sntp -q %s" % PEER, 600)
        c.check("NTP 0x%08x (era 1) read as %s" % (era_s, cstr(era_t)),
                cstr(era_t) in out)
        c.note("      the same 32 bits mean 1902 in era 0; the era-1 branch is"
               " the only reading that fits a signed time_t")
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    # ------------------------------------------- five refusals, none may set
    for name, kw, want, why in (
        ("unsync", dict(secs=ntp_s, li=3), "not synchronised",
         "leap-indicator ALARM is refused"),
        ("kod", dict(secs=ntp_s, stratum=0), "unusable stratum",
         "a kiss-o'-death packet is refused"),
        ("zerots", dict(secs=0), "zero timestamp",
         "a zero transmit timestamp is refused"),
        ("pre1970", dict(secs=NTP_EPOCH - 1), "before 1970",
         "a timestamp one second before 1970 is refused"),
        # The criterion here is the -v line, NOT "no server answered": a deaf
        # server produces that too, so the weaker wording passed the negative
        # control and was therefore measuring nothing.  "reply to a different
        # request" can only be printed when a reply ARRIVED and was discarded.
        ("staleorg", dict(secs=ntp_s, org="garble"),
         "reply to a different request",
         "a reply that does not echo the originate field is discarded"),
    ):
        c = sel(Case(name, ["sntp.c"], why))
        if not c:
            continue
        say("--- case %s" % name)
        park(g, c)
        ctl.write(**kw)
        nreq = len(log.requests())
        out = cmd(g, "/etc/sntp -v -r 1 -t 5 %s" % PEER, 600)
        after = clock(g)
        c.check("it asked", len(log.requests()) > nreq)
        c.check("it said why (%r)" % want, want in out)
        c.check("it did not report a step", "clock stepped" not in out)
        c.check("and the clock is still %s" % cstr(WRONG_T),
                after is not None and abs(after - WRONG_T) < SLACK)
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    # ------------------------------------------------------------- the runt
    c = sel(Case("runt", ["sntp.c"],
                 "a short packet is a different protocol, not a small answer"))
    if c:
        say("--- case runt")
        park(g, c)
        ctl.write(secs=ntp_s, trunc=20)
        out = cmd(g, "/etc/sntp -v -r 1 -t 5 %s" % PEER, 600)
        after = clock(g)
        c.check("it said runt", "runt reply" in out)
        c.check("and the clock is still %s" % cstr(WRONG_T),
                after is not None and abs(after - WRONG_T) < SLACK)
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    # ------------------------------------------------------------- the retry
    c = sel(Case("retry", ["sntp.c"],
                 "an unanswered attempt is retried, then succeeds",
                 deaf_ok=False))
    if c:
        say("--- case retry")
        park(g, c)
        ctl.write(secs=ntp_s, drop=1)
        nreq = len(log.requests())
        out = cmd(g, "/etc/sntp -v -t 5 -r 3 %s" % PEER, 900)
        after = clock(g)
        got = len(log.requests()) - nreq
        c.note("      %d request(s) crossed the wire; the first was dropped"
               % got)
        c.check("it sent more than one", got >= 2)
        c.check("it reported the silence", "no reply (try 1" in out)
        c.check("and the second attempt set the clock",
                after is not None and 0 <= after - want_t <= SLACK)
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    # --------------------------------------------- NEGATIVE: asked nobody home
    # deaf_ok, and for the same reason as `unknown': every one of its criteria
    # is about what the guest SENT and what it did NOT do, so a server that
    # stays quiet cannot change any of them.  Saying so here is the honest
    # form; letting it "pass" the deaf run unremarked would have been a case
    # that can never fail.
    c = sel(Case("nobody", ["sntp.c"],
                 "an address with no server: asked, unanswered, clock unmoved",
                 deaf_ok=True))
    if c:
        say("--- case nobody")
        park(g, c)
        ctl.write(secs=ntp_s)
        nnot = len(log.notme())
        out = cmd(g, "/etc/sntp -v -t 5 -r 2 %s" % NOBODY, 900)
        after = clock(g)
        c.check("the request DID leave the machine", len(log.notme()) > nnot)
        c.check("nothing answered", "no server answered" in out)
        c.check("and the clock is still %s" % cstr(WRONG_T),
                after is not None and abs(after - WRONG_T) < SLACK)
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    # ------------------------------------------------- NEGATIVE: never asked
    c = sel(Case("unknown", ["sntp.c"],
                 "a name that resolves nowhere: no packet at all",
                 deaf_ok=True))
    if c:
        say("--- case unknown")
        park(g, c)
        nreq = len(log.requests()) + len(log.notme())
        out = cmd(g, "/etc/sntp -t 5 -r 1 nosuch%s" % rndtag(), 600)
        after = clock(g)
        c.check("it said unknown host", "unknown host" in out)
        c.check("and NOTHING was put on the wire",
                len(log.requests()) + len(log.notme()) == nreq)
        c.check("and the clock is still %s" % cstr(WRONG_T),
                after is not None and abs(after - WRONG_T) < SLACK)
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    return cases


def rndtag(n=5):
    import string
    return "".join(random.choice(string.ascii_lowercase) for _ in range(n))


def report(cases, deaf, log, work, keep):
    say("")
    say("=== %s ===" % ("NEGATIVE CONTROL (deaf SNTP server)" if deaf
                        else "SNTP cases"))
    bad = 0
    for c in cases:
        want_ok = (not deaf) or c.deaf_ok
        got = bool(c.ok)
        verdict = "ok" if got == want_ok else "FAIL"
        if got != want_ok:
            bad += 1
        # In the deaf run "ok" means "behaved as the control requires", which
        # for most cases means the case ITSELF failed.  Print both, or the
        # control's output reads as though nothing changed.
        note = ("  [%s]" % ("case still passed" if got else "case fell over")
                if deaf else "")
        say("  %-4s %-10s%s %s" % (verdict, c.name, note, c.why))
        for d in c.detail:
            say(d)

    say("")
    say("--- the wire (last lines) ---")
    for l in log.tail(25):
        say("  " + l)

    if deaf:
        wrong = [c.name for c in cases
                 if bool(c.ok) != ((not deaf) or c.deaf_ok)]
        say("")
        say("With the server deaf, %d of %d cases did not behave as the control"
            " requires%s."
            % (bad, len(cases), (": " + ", ".join(wrong)) if wrong else ""))
        say("(`unknown' is marked deaf_ok: it judges only whether a packet was"
            " SENT, which no server can change by staying quiet.)")

    say("")
    say("=== sntp: %s" % ("PASS" if bad == 0 else "FAIL (%d)" % bad))
    if keep:
        say("(kept %s)" % work)
    else:
        shutil.rmtree(work, ignore_errors=True)
    return 0 if bad == 0 else 1


def main(argv):
    opts = [a for a in argv[1:] if a.startswith("--")]
    dist, only = "coherent3-full-test", None
    for o in opts:
        if o.startswith("--dist="):
            dist = o.split("=", 1)[1]
        if o.startswith("--only="):
            only = set(o.split("=", 1)[1].split(","))
    deaf = "--deaf" in opts
    keep = "--keep" in opts

    work = os.path.join(os.environ.get("TMPDIR", "/tmp"),
                        "c900-sntp.%d" % os.getpid())
    os.makedirs(work)
    say("workdir %s" % work)
    src = dist_image(dist)
    if not src:
        return 2
    img = os.path.join(work, "guest.bin")
    subprocess.call(["cp", "--reflink=auto", src, img])

    ntp_s, want_t = rnd_ntp()
    say("this run's answer: NTP 0x%08X (%u) -> time_t %d = %s"
        % (ntp_s, ntp_s, want_t, cstr(want_t)))
    say("nothing on the image has ever named that second")

    sock = os.path.join(work, "sntp.sock")
    logpath = os.path.join(work, "wire.log")
    ctlpath = os.path.join(work, "answer.ctl")
    ctl = Ctl(ctlpath)
    ctl.write(secs=ntp_s, frac="0x40000000")
    wcmd = [sys.executable, os.path.join(HERE, "sntpwire.py"), sock,
            "--ctl=%s" % ctlpath, "--log=%s" % logpath]
    if deaf:
        wcmd.append("--deaf")
    server = subprocess.Popen(wcmd)
    for _ in range(100):
        if os.path.exists(sock):
            break
        time.sleep(0.1)
    log = Log(logpath)

    g = None
    try:
        g = Guest("guest", img, sock, work)
        if not boot(g):
            say("FAIL: the guest never reached a root shell")
            return 2
        if not ensure_stack(g, ADDR):
            say("FAIL: the guest's network did not come up")
            return 2
        cases = run_cases(g, (ntp_s, want_t), log, ctl, deaf, only)
    finally:
        if g:
            g.stop()
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()

    return report(cases, deaf, log, work, keep)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
