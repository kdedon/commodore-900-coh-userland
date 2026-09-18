"""resolvcheck.py -- does the C900's DNS resolver actually resolve?

    python3 resolvcheck.py [--deaf] [--image PATH] [--keep] [--only CASE,...]

WHAT IS UNDER TEST.  net/resolv/ -- res_init, res_mkquery, res_comp,
res_send, res_query and gethnmadr -- and netdb.c's gethostbyname() falling
through /etc/hosts into it.  Until this harness existed none of that code had
ever been executed: it was ported, compiled into libsocket.a and shipped.

HOW.  One emulator boots the shipped image to multi-user, so /etc/rc.net brings
up inet, the address and SLIP on /dev/tty51.  The far end of that serial line is
not a second C900 but dnswire.py, a scripted nameserver on the host at 10.0.0.1
-- the address /etc/resolv.conf already names.  Every case is then a command
typed at the guest's console, judged on BOTH what the guest printed and what
crossed the wire.

THE TRAP THIS IS BUILT AROUND.  A name lookup here has three possible histories
and only two of them are success:

    answered by DNS        a query went out and an answer came back
    answered by /etc/hosts no query was ever sent, the file knew the name
    never asked at all     netdb.c's have_resolv() found no /etc/resolv.conf,
                           so the lookup failed without a packet

The first two are indistinguishable from the guest's output alone, which is why
every criterion below is a pair: what the guest printed AND whether dnswire.py
logged a query for that name.  The names this harness resolves are generated per
run and are in no file on the image -- so is the ADDRESS they resolve to.  A
guest that prints either can only have got it over the wire.

    dns<R>      an A record; nothing on the image has ever heard of it
    trunc<R>    answered over UDP with the truncation bit and NO records, and
                over TCP with an address that exists ONLY in the circuit reply
    silent<R>   never answered at all, so res_send's retry schedule runs
    nx<R>       denied, so "asked and told no" is told from "never asked"

THE NEGATIVE CONTROLS.  Three, of increasing sharpness:

    --deaf          the whole run again with the nameserver receiving and
                    logging but never answering.  Every positive criterion must
                    then fail; one that still passes was not measuring DNS.
    case noresolv   /etc/resolv.conf is moved aside mid-run and a DNS-only name
                    is looked up again.  It must fail AND put nothing on the
                    wire -- that is have_resolv() proved, not assumed -- and
                    then succeed again once the file is back.
    case hostsonly  a name that IS in /etc/hosts must resolve with NO query on
                    the wire, which is the fall-through order proved in the
                    direction that could otherwise hide a broken resolver.

A case that has never been seen to FAIL is reported as PROVING NOTHING and
fails the run, the rule test and net/test/hostcheck already use.

Nothing is rebuilt and no image is modified: the guest boots a COPY, because
the emulator writes through to the disk it is given.
"""
import atexit
import os
import random
import re
import shutil
import string
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
NET = os.path.normpath(os.path.join(HERE, "..", ".."))
OS = os.path.normpath(os.path.join(NET, ".."))
sys.path.insert(0, os.path.join(NET, "twohost"))
from twohost import Guest, boot, ensure_stack, say, test_image  # noqa: E402

ADDR = "10.0.0.2"                 # what rc.net gives the guest
PEER = "10.0.0.1"                 # this harness, and the nameserver
DOMAIN = "localnet"


def rnd(n=5):
    return "".join(random.choice(string.ascii_lowercase + string.digits)
                   for _ in range(n))


class Names(object):
    """The per-run names and addresses.  None of them is on the image.

    The addresses are inside 10.0.0.0/24 on purpose: rc.net configures that net
    with no default route, so an address outside it is unreachable and a ping
    would fail for a routing reason that has nothing to do with the resolver.
    """

    def __init__(self):
        tag = rnd()
        octs = random.sample(range(20, 250), 4)
        self.dns = "dns" + tag
        self.dnsaddr = "10.0.0.%d" % octs[0]
        self.dns2 = "dnsb" + tag             # for the have_resolv case
        self.dns2addr = "10.0.0.%d" % octs[1]
        self.trunc = "tc" + tag
        self.truncaddr = "10.0.0.%d" % octs[2]
        self.silent = "silent" + tag
        self.nx = "nx" + tag

    def fq(self, label):
        return "%s.%s" % (label, DOMAIN)

    def zone_args(self):
        return ["--zone=%s=%s" % (self.fq(self.dns), self.dnsaddr),
                "--zone=%s=%s" % (self.fq(self.dns2), self.dns2addr),
                "--trunc=%s=%s" % (self.fq(self.trunc), self.truncaddr),
                "--silent=%s" % self.silent,
                "--ptr=%s=%s" % (self.dnsaddr, self.fq(self.dns))]


class Log(object):
    """dnswire.py's log, read back and asked questions about."""

    def __init__(self, path):
        self.path = path

    def lines(self):
        try:
            with open(self.path) as f:
                return f.read().splitlines()
        except OSError:
            return []

    def queries(self, name=None, proto=None):
        """(timestamp, proto, name) for every query the guest actually sent."""
        out = []
        for l in self.lines():
            m = re.match(r"([\d.]+) QUERY (udp|tcp) +(\S+) type", l)
            if not m:
                continue
            if proto and m.group(2) != proto:
                continue
            if name and m.group(3).lower() != name.lower():
                continue
            out.append((float(m.group(1)), m.group(2), m.group(3)))
        return out

    def label_queries(self, label, proto=None):
        """Queries for a label in ANY of its forms.

        res_search tries the search list before the name as given, so a lookup
        of `silent.localnet' goes out as `silent.localnet.localnet' first.  A
        filter on the fully-qualified name alone therefore counts none of the
        attempts that were actually made, which read as a resolver that never
        retried.
        """
        return [q for q in self.queries(proto=proto)
                if q[2].split(".")[0].lower() == label.lower()]

    def has(self, needle):
        return any(needle in l for l in self.lines())

    def tail(self, n=40):
        return self.lines()[-n:]


class Case(object):
    """One question, its answer, and what it covers."""

    def __init__(self, name, covers, why, deaf_ok=False):
        self.name, self.covers, self.why = name, covers, why
        # A case whose criterion is what the guest SENT, not what it was told,
        # is unaffected by a deaf server and must still pass in that run.  Say
        # so here rather than letting the control quietly excuse it.
        self.deaf_ok = deaf_ok
        self.ok = None
        self.detail = []

    def note(self, s):
        self.detail.append(s)

    def check(self, what, ok):
        self.note("      %-58s %s" % (what, "ok" if ok else "FAIL"))
        self.ok = ok if self.ok is None else (self.ok and ok)
        return ok


def cmd(g, line, timeout=240):
    """Type a command, wait for the prompt, return what it printed."""
    m = g.mark()
    g.line(line)
    if not g.expect("# ", timeout, "(%s)" % line):
        return g.since(m) + "\n[TIMEOUT: no prompt]"
    out = g.since(m)
    # Drop the shell's echo of the command itself: it contains the name and the
    # address that were typed, and a criterion that matched it would pass on the
    # harness's own input rather than on the guest's answer.
    return out.split("\n", 1)[-1] if "\n" in out else ""


def asks_at_all(g, log, name):
    """Does a lookup of `name' put ANY query on the wire?

    The one question that has to be answered before any other case means
    anything.  A resolver that never sends fails every lookup for a reason that
    has nothing to do with DNS, and from the guest's output that is
    indistinguishable from a server that said no.
    """
    before = len(log.queries())
    cmd(g, "/bin/ping -c 1 -w 5 %s" % name, 240)
    return len(log.queries()) > before


def setup_resolvconf(g, n, log, c):
    """Leave the guest able to reach its nameserver, and say what that took.

    THE SHIPPED /etc/resolv.conf DOES NOT PARSE.  res_init() hands the rest of
    a `nameserver' line straight to inet_aton() with the newline fgets() left on
    it, and this libsocket's inet_addr() is strict about trailing characters
    (libsocket.c: it returns INADDR_NONE unless the string ends after the last
    digit, which is deliberate -- a lax one made "no.such.host" parse as
    0.0.0.0).  So the line is discarded, _res.nscount stays 0, and res_init
    falls back to its 127.0.0.1 default: every query is sent to a loopback
    address where nothing listens, nothing reaches the wire, and the lookup
    fails after the full retry schedule.

    The source fix is in net/resolv/res_init.c (terminate the value at the
    first white space).  It cannot be got onto an already-built image from
    here, so this rewrites the file with dd(1) so that the nameserver line is
    the file's last and carries NO trailing newline -- which is the one shape
    the shipped binary parses.  The point is not the workaround: it is that
    with the newline gone, and nothing else changed, the whole resolver works.
    An image built from the fixed source will pass the first probe and this
    will do nothing.
    """
    if asks_at_all(g, log, n.fq(n.nx)):
        c.check("the shipped /etc/resolv.conf is parsed", True)
        return False
    c.check("the shipped /etc/resolv.conf is parsed (nameserver line lost to"
            " the trailing newline)", False)
    say("    rewriting /etc/resolv.conf without its trailing newline")
    cmd(g, "/bin/echo nameserver %s > /rc.tmp" % PEER, 120)
    cmd(g, "/bin/dd if=/rc.tmp of=/etc/resolv.conf bs=1 count=%d"
        % len("nameserver %s" % PEER), 180)
    c.check("with the newline gone, the guest sends a query",
            asks_at_all(g, log, n.fq(n.nx)))
    return True


def run_cases(g, n, log, deaf, only):
    cases = []

    def sel(c):
        if only and c.name not in only:
            return None
        cases.append(c)
        return c

    # --------------------------------------------------- can it ask at all?
    c = sel(Case("nsparse", ["res_init.c"],
                 "res_init reads a nameserver out of /etc/resolv.conf",
                 deaf_ok=True))
    if c:
        say("--- case nsparse")
        setup_resolvconf(g, n, log, c)

    # ---------------------------------------------------------------- link
    c = sel(Case("hostsonly", ["netdb.c"],
                 "a name in /etc/hosts resolves with NO query on the wire"))
    if c:
        say("--- case hostsonly")
        before = len(log.queries())
        out = cmd(g, "/bin/ping -c 1 -w 5 peer", 180)
        c.check("ping peer reaches 10.0.0.1", "bytes from 10.0.0.1" in out)
        c.check("no DNS query was sent for it",
                len(log.queries()) == before)
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    # ------------------------------------------------------- A record, via
    # ------------------------------------------------------- gethostbyname
    c = sel(Case("dnsonly", ["netdb.c", "res_init.c", "res_query.c",
                             "res_mkquery.c", "res_send.c", "res_comp.c",
                             "gethnmadr.c"],
                 "gethostbyname falls through /etc/hosts to the network"))
    if c:
        say("--- case dnsonly")
        out = cmd(g, "/bin/ping -c 1 -w 5 %s" % n.dns, 240)
        c.check("ping %s answered from %s" % (n.dns, n.dnsaddr),
                ("bytes from %s" % n.dnsaddr) in out)
        c.check("the guest ASKED for %s" % n.fq(n.dns),
                bool(log.queries(n.fq(n.dns), "udp")))
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    # --------------------------------------------------------------- host(1)
    c = sel(Case("host-a", ["res_query.c", "res_comp.c", "res_send.c"],
                 "host(1) prints the A record it was told"))
    if c:
        say("--- case host-a")
        out = cmd(g, "/bin/host %s" % n.fq(n.dns), 240)
        c.check("host printed %s" % n.dnsaddr, n.dnsaddr in out)
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    c = sel(Case("host-ptr", ["res_query.c", "res_comp.c"],
                 "a reverse lookup expands a compressed PTR name"))
    if c:
        say("--- case host-ptr")
        out = cmd(g, "/bin/host %s" % n.dnsaddr, 240)
        c.check("host printed %s" % n.fq(n.dns), n.dns in out)
        c.check("the guest asked in-addr.arpa",
                any("in-addr.arpa" in q[2] for q in log.queries()))
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    # ------------------------------------------------------------- NXDOMAIN
    c = sel(Case("nxdomain", ["res_query.c"],
                 "a denial is told from silence and from never asking"))
    if c:
        say("--- case nxdomain")
        out = cmd(g, "/bin/host %s" % n.fq(n.nx), 240)
        c.check("host did not invent an address",
                not re.search(r"\b10\.0\.0\.\d+\b", out))
        c.check("the guest asked, and was denied",
                bool(log.queries(n.fq(n.nx), "udp"))
                and log.has("ANSWER udp %s rcode 3" % n.fq(n.nx)))
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    # ------------------------------------------------------ have_resolv trap
    c = sel(Case("noresolv", ["netdb.c"],
                 "no /etc/resolv.conf means no query, not a silent fallback"))
    if c:
        say("--- case noresolv")
        cmd(g, "/bin/mv /etc/resolv.conf /etc/resolv.off", 120)
        before = len(log.queries())
        out = cmd(g, "/bin/ping -c 1 -w 5 %s" % n.dns2, 180)
        c.check("the lookup failed", "unknown host" in out)
        c.check("and NOTHING was put on the wire",
                len(log.queries()) == before)
        c.note("      | " + out.strip().replace("\n", "\n      | "))
        cmd(g, "/bin/mv /etc/resolv.off /etc/resolv.conf", 120)
        out = cmd(g, "/bin/ping -c 1 -w 5 %s" % n.dns2, 240)
        c.check("with the file back, %s resolves to %s"
                % (n.dns2, n.dns2addr),
                ("bytes from %s" % n.dns2addr) in out)
        c.check("and the query appears",
                bool(log.queries(n.fq(n.dns2), "udp")))
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    # ------------------------------------------------------------ truncation
    c = sel(Case("truncated", ["res_send.c"],
                 "a truncated datagram answer is re-asked over TCP"))
    if c:
        say("--- case truncated")
        out = cmd(g, "/bin/host %s" % n.fq(n.trunc), 300)
        c.check("the datagram answer carried the truncation bit",
                log.has("ANSWER udp %s rcode 0 records 0 TRUNCATED"
                        % n.fq(n.trunc)))
        c.check("the guest re-asked over a circuit",
                bool(log.queries(n.fq(n.trunc), "tcp")))
        # The address is in the TCP answer and nowhere else -- not in the
        # datagram, not in any file, not in anything typed here.
        c.check("host printed %s, which only the circuit carried"
                % n.truncaddr, n.truncaddr in out)
        c.note("      | " + out.strip().replace("\n", "\n      | "))

    # -------------------------------------------------------- retry schedule
    c = sel(Case("retry", ["res_send.c"],
                 "four rounds with a doubling timeout, then a clean failure"))
    if c:
        say("--- case retry (this one waits out the whole schedule)")
        t0 = time.time()
        out = cmd(g, "/bin/host %s" % n.fq(n.silent), 900)
        # Attempts at ONE question, which is what a retry is.  res_search puts
        # the search-list form on the wire before the name as given, so the raw
        # count over a whole lookup mixes two questions together.
        qs = log.label_queries(n.silent, "udp")
        counts = {}
        for q in qs:
            counts[q[2].lower()] = counts.get(q[2].lower(), 0) + 1
        worst = max(counts, key=counts.get) if counts else ""
        most = counts.get(worst, 0)
        qs = [q for q in qs if q[2].lower() == worst]
        gaps = [round(qs[i + 1][0] - qs[i][0], 1) for i in range(len(qs) - 1)]
        c.note("      %d attempts at %s in %.0fs, gaps %s"
               % (most, worst or "(nothing)", time.time() - t0, gaps))
        c.check("it retried (4 attempts, _res.retry)", most >= 4)
        c.check("the timeout doubles between attempts",
                len(gaps) >= 3 and all(gaps[i + 1] > gaps[i] * 1.4
                                       for i in range(min(2, len(gaps) - 1))))
        c.check("and it gave up rather than hanging",
                "[TIMEOUT" not in out)
        c.note("      | " + out.strip().replace("\n", "\n      | "))
        # THE CONTROL for the count above.  Four attempts is only evidence
        # about the retry loop if a lookup that IS answered produces a
        # different number -- a harness that would report four whatever
        # happened has measured nothing.  Same command, same server, same
        # everything but the answer.
        #
        # NOT host(1)'s -w, which looks like the control to use (it sets
        # _res.retry to 1) and is a trap: -w is `wait forever until reply', and
        # its outer loop re-asks a name that never answers until it is killed.
        # A control must be able to finish.
        #
        # Counted PER QUESTION, not per lookup.  host(1) asks name.domain
        # first and the name as given second, so an answered lookup puts two
        # datagrams on the wire -- but they are two different questions, and
        # only a repeat of the SAME question is a retry.
        say("    control: the same command for a name that IS answered")
        before = len(log.queries(n.fq(n.dns), "udp"))
        cmd(g, "/bin/host %s" % n.fq(n.dns), 300)
        again = len(log.queries(n.fq(n.dns), "udp")) - before
        c.note("      the answered question was asked %d time(s); the silent"
               " one %d" % (again, most))
        c.check("an answered question is asked once, not four times",
                again == 1)

    return cases


def report(cases, deaf, log, work, keep):
    say("")
    say("=== %s ===" % ("NEGATIVE CONTROL (deaf nameserver)" if deaf
                        else "resolver cases"))
    bad, seen_fail = 0, set()
    for c in cases:
        want_ok = (not deaf) or c.deaf_ok
        got = bool(c.ok)
        if not got:
            seen_fail.update(c.covers)
        verdict = "ok" if got == want_ok else "FAIL"
        if got != want_ok:
            bad += 1
        say("  %-4s %-12s %s" % (verdict, c.name, c.why))
        for d in c.detail:
            say(d)
    say("")
    say("--- the wire (last lines) ---")
    for l in log.tail(25):
        say("  " + l)

    if deaf:
        say("")
        wrong = [c.name for c in cases
                 if bool(c.ok) != ((not deaf) or c.deaf_ok)]
        say("With the nameserver deaf, %d of %d cases did not behave as the"
            " control requires%s."
            % (bad, len(cases), (": " + ", ".join(wrong)) if wrong else ""))
        say("(nsparse is marked deaf_ok: it judges only whether a query was"
            " SENT, which no server can change by staying quiet.)")
    else:
        # Coverage: every resolver source a case claims must have been named,
        # and the deaf run is what shows each of them can fail.
        covered = set()
        for c in cases:
            covered.update(c.covers)
        srcs = set(f for f in os.listdir(os.path.join(NET, "resolv"))
                   if f.endswith(".c"))
        srcs.add("netdb.c")
        missing = sorted(srcs - covered)
        if missing:
            say("PROVES NOTHING: no case names %s" % ", ".join(missing))
            bad += len(missing)

    say("")
    say("=== resolver: %s" % ("PASS" if bad == 0 else "FAIL (%d)" % bad))
    if keep:
        say("(kept %s)" % work)
    else:
        shutil.rmtree(work, ignore_errors=True)
    return 0 if bad == 0 else 1


def main(argv):
    opts = [a for a in argv[1:] if a.startswith("--")]
    image = None
    only = None
    for o in opts:
        if o.startswith("--image="):
            image = o.split("=", 1)[1]
        if o.startswith("--only="):
            only = set(o.split("=", 1)[1].split(","))
    deaf = "--deaf" in opts
    keep = "--keep" in opts

    work = os.path.join(os.environ.get("TMPDIR", "/tmp"),
                        "c900-resolv.%d" % os.getpid())
    os.makedirs(work)
    say("workdir %s" % work)
    src = test_image(image)
    if not src:
        return 2
    img = os.path.join(work, "guest.bin")
    subprocess.call(["cp", "--reflink=auto", src, img])

    n = Names()
    say("this run resolves %s -> %s, %s -> %s (circuit only), %s silent, %s denied"
        % (n.fq(n.dns), n.dnsaddr, n.fq(n.trunc), n.truncaddr,
           n.fq(n.silent), n.fq(n.nx)))

    # Not in the workdir: an AF_UNIX path is capped at about 108 bytes.
    sock = "/tmp/c900dns.%d.sock" % os.getpid()
    atexit.register(lambda: os.path.lexists(sock) and os.unlink(sock))
    logpath = os.path.join(work, "wire.log")
    wcmd = [sys.executable, os.path.join(HERE, "dnswire.py"), sock,
            "--log=%s" % logpath] + n.zone_args()
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
        cases = run_cases(g, n, log, deaf, only)
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
