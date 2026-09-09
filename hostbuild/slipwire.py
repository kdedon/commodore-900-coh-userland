"""slipwire.py -- the host end of the C900's serial line, as a library.

Everything the SLIP tests share: SLIP framing, IP checksums and packet
description, the simulator transport, and the boot-and-bring-the-stack-up
sequence.  `slip-test.py' (ICMP echo) and `tcp-test.py' (a TCP echo server)
are each a host-side peer written against this.

The host is 10.0.0.1 and the guest 10.0.0.2, connected by SCC 0 channel A --
the guest's /dev/tty51.  A test here is host-initiated on purpose: the host
sends and waits for the guest to answer, so a pass exercises the whole chain
in both directions --

    host -> /serial/send -> SCC ch A -> al.c -> /dev/tty51 -> slip
         -> psip channel -> ip_arrived -> <protocol> -> ip_write -> psip
         -> slip -> /dev/tty51 -> SCC ch A -> /serial/recv -> host

This depends on the simulator's serial API being byte-exact: SLIP frames are
delimited by 0xC0 and escape with 0xDB, and both values must cross the API
intact in both directions (serial-bytes-test.py proves that).
"""
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

from simguard import SimGuard, sim_bin

HERE = os.path.dirname(os.path.abspath(__file__))
# The simulator: $SIM only, no default path.  See simguard.sim_bin and
# mk/simulator.sh for what c900sim is and what to use when you have none.
# Resolved AT USE, not here: serial-rx-probe.py imports this module only
# for workimg(), and a module-level refusal would stop a harness that was
# never going to start a simulator.
# Port 7801, NOT the default 7800: c900mcp manages its own simulator instance
# there, and a harness that grabs the default port -- or worse, pkills c900sim by
# name to force a cold boot -- takes that instance out from under the MCP server
# and makes it report a machine that is no longer the one it started.  Own the
# port, own the process, kill only the child spawned here.
S = os.environ.get("SIMHTTP", "http://localhost:7801")
SCC, CHAN = 0, 0                  # SCC 0 channel A == guest /dev/tty51
GUEST = "10.0.0.2"
PEER = "10.0.0.1"

END, ESC, ESC_END, ESC_ESC = 0xC0, 0xDB, 0xDC, 0xDD


# -- SLIP framing (RFC 1055) --------------------------------------------------
def slip_encode(pkt):
    out = [END]
    for b in pkt:
        if b == END:
            out += [ESC, ESC_END]
        elif b == ESC:
            out += [ESC, ESC_ESC]
        else:
            out.append(b)
    out.append(END)
    return out


class SlipDecoder:
    """Feed bytes, get whole packets out.  Keeps state across polls."""

    def __init__(self):
        self.buf, self.esc = [], False

    def feed(self, data):
        packets = []
        for b in data:
            if self.esc:
                self.buf.append(END if b == ESC_END else
                                ESC if b == ESC_ESC else b)
                self.esc = False
            elif b == ESC:
                self.esc = True
            elif b == END:
                if self.buf:
                    packets.append(bytes(self.buf))
                self.buf = []
            else:
                self.buf.append(b)
        return packets


# -- IP -----------------------------------------------------------------------
def cksum(b):
    s = 0
    for i in range(0, len(b) - 1, 2):
        s += (b[i] << 8) | b[i + 1]
    if len(b) % 2:
        s += b[-1] << 8
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return ~s & 0xFFFF


def ip4(s):
    return bytes(int(x) for x in s.split("."))


def ip_wrap(proto, payload, src=PEER, dst=GUEST, ident=0x1234):
    total = 20 + len(payload)
    ip = bytearray([0x45, 0]) + total.to_bytes(2, "big") \
        + ident.to_bytes(2, "big") + b"\x00\x00" + bytes([64, proto, 0, 0]) \
        + ip4(src) + ip4(dst)
    ip[10:12] = cksum(ip).to_bytes(2, "big")
    return bytes(ip + payload)


def echo_request(seq, payload=b"c900slip"):
    """An ICMP Echo Request from the host to the guest."""
    icmp = bytearray([8, 0, 0, 0]) + bytes([0x43, 0x21]) \
        + seq.to_bytes(2, "big") + payload
    icmp[2:4] = cksum(icmp).to_bytes(2, "big")
    return ip_wrap(1, bytes(icmp))


def is_echo_reply(pkt, seq):
    if len(pkt) < 28 or pkt[9] != 1:
        return False
    hl = (pkt[0] & 0x0F) * 4
    ic = pkt[hl:]
    return (cksum(pkt[:hl]) == 0 and cksum(ic) == 0 and ic[0] == 0
            and ((ic[6] << 8) | ic[7]) == seq
            and bytes(pkt[12:16]) == ip4(GUEST))


# -- UDP ----------------------------------------------------------------------
def udp_pseudo(src, dst, seg):
    return ip4(src) + ip4(dst) + bytes([0, 17]) + len(seg).to_bytes(2, "big")


def udp_cksum_ok(pkt):
    """UDP's checksum is OPTIONAL: an all-zero field means `not computed', and
    it must not be read as a wrong one."""
    hl = (pkt[0] & 0x0F) * 4
    seg = pkt[hl:]
    if seg[6:8] == b"\x00\x00":
        return True
    src = ".".join(str(b) for b in pkt[12:16])
    dst = ".".join(str(b) for b in pkt[16:20])
    return cksum(udp_pseudo(src, dst, seg) + seg) == 0


def udp_dgram(sport, dport, payload, src=PEER, dst=GUEST):
    seg = bytearray(sport.to_bytes(2, "big") + dport.to_bytes(2, "big")
                    + (8 + len(payload)).to_bytes(2, "big") + b"\x00\x00") \
        + payload
    c = cksum(udp_pseudo(src, dst, seg) + seg)
    # Zero means "no checksum", so a computed zero is transmitted as all-ones.
    seg[6:8] = (c if c else 0xFFFF).to_bytes(2, "big")
    return ip_wrap(17, bytes(seg), src=src, dst=dst)


class Udp(object):
    """A parsed UDP datagram carried in an IP packet."""

    def __init__(self, pkt):
        hl = (pkt[0] & 0x0F) * 4
        u = pkt[hl:]
        self.sport = (u[0] << 8) | u[1]
        self.dport = (u[2] << 8) | u[3]
        self.length = (u[4] << 8) | u[5]
        # Trust the header's length over the captured tail: a frame that lost
        # bytes on the wire must read as short, not as a datagram with junk on
        # the end.
        self.data = bytes(u[8:self.length]) if self.length >= 8 else b""
        self.truncated = len(u) < self.length
        self.cksum_ok = udp_cksum_ok(pkt)


TCP_FIN, TCP_SYN, TCP_RST, TCP_PSH, TCP_ACK = 1, 2, 4, 8, 16


def tcp_flagstr(f):
    names = [(TCP_FIN, "FIN"), (TCP_SYN, "SYN"), (TCP_RST, "RST"),
             (TCP_PSH, "PSH"), (TCP_ACK, "ACK")]
    return "|".join(n for bit, n in names if f & bit) or "-"


def describe(pkt):
    """One line naming what a packet is, and whether its checksums hold."""
    if len(pkt) < 20:
        return "runt (%d bytes)" % len(pkt)
    hl = (pkt[0] & 0x0F) * 4
    src = ".".join(str(b) for b in pkt[12:16])
    dst = ".".join(str(b) for b in pkt[16:20])
    d = "%s -> %s proto %d" % (src, dst, pkt[9])
    if cksum(pkt[:hl]) != 0:
        return d + " BAD-IP-CKSUM"
    if pkt[9] == 1 and len(pkt) >= hl + 8:
        ic = pkt[hl:]
        d += " icmp type %d seq %d" % (ic[0], (ic[6] << 8) | ic[7])
        if cksum(ic) != 0:
            d += " BAD-ICMP-CKSUM"
    elif pkt[9] == 6 and len(pkt) >= hl + 20:
        t = pkt[hl:]
        doff = (t[12] >> 4) * 4
        d += " tcp %d->%d %s seq %u ack %u len %d" % (
            (t[0] << 8) | t[1], (t[2] << 8) | t[3], tcp_flagstr(t[13]),
            int.from_bytes(t[4:8], "big"), int.from_bytes(t[8:12], "big"),
            len(t) - doff)
        if tcp_cksum_ok(pkt):
            pass
        else:
            d += " BAD-TCP-CKSUM"
    elif pkt[9] == 17 and len(pkt) >= hl + 8:
        u = Udp(pkt)
        d += " udp %d->%d len %d" % (u.sport, u.dport, len(u.data))
        if u.truncated:
            d += " TRUNCATED"
        if not u.cksum_ok:
            d += " BAD-UDP-CKSUM"
    return d


def tcp_pseudo(src, dst, seg):
    return ip4(src) + ip4(dst) + bytes([0, 6]) + len(seg).to_bytes(2, "big")


def tcp_cksum_ok(pkt):
    hl = (pkt[0] & 0x0F) * 4
    seg = pkt[hl:]
    src = ".".join(str(b) for b in pkt[12:16])
    dst = ".".join(str(b) for b in pkt[16:20])
    return cksum(tcp_pseudo(src, dst, seg) + seg) == 0


def tcp_seg(sport, dport, seq, ack, flags, payload=b"", window=4096,
            src=PEER, dst=GUEST):
    seg = bytearray(sport.to_bytes(2, "big") + dport.to_bytes(2, "big")
                    + (seq & 0xFFFFFFFF).to_bytes(4, "big")
                    + (ack & 0xFFFFFFFF).to_bytes(4, "big")
                    + bytes([0x50, flags]) + window.to_bytes(2, "big")
                    + b"\x00\x00\x00\x00") + payload
    seg[16:18] = cksum(tcp_pseudo(src, dst, seg) + seg).to_bytes(2, "big")
    return ip_wrap(6, bytes(seg), src=src, dst=dst)


class Tcp(object):
    """A parsed TCP segment carried in an IP packet."""

    def __init__(self, pkt):
        hl = (pkt[0] & 0x0F) * 4
        t = pkt[hl:]
        self.sport = (t[0] << 8) | t[1]
        self.dport = (t[2] << 8) | t[3]
        self.seq = int.from_bytes(t[4:8], "big")
        self.ack = int.from_bytes(t[8:12], "big")
        self.flags = t[13]
        self.window = (t[14] << 8) | t[15]
        self.data = bytes(t[(t[12] >> 4) * 4:])
        self.cksum_ok = tcp_cksum_ok(pkt)


# -- simulator transport ------------------------------------------------------
def post(path, obj):
    req = urllib.request.Request(S + path, data=json.dumps(obj).encode(),
                                 headers={"Content-Type": "application/json"},
                                 method="POST")
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def get(path):
    with urllib.request.urlopen(S + path, timeout=30) as r:
        return json.load(r)


# Every byte the guest put on the wire, in order, for when a frame arrives
# malformed and the question is whether it left that way.
WIRE_LOG = []

# Bytes fetched from the wire but not yet handed to a decoder.
#
# Sending is SLOW here -- one byte per POST, each waiting on the line to drain,
# so a 44-byte frame takes seconds -- and the guest transmits during that whole
# window.  A harness that only reads the wire between sends loses whatever
# arrived while it was busy, and the loss shows up as frames that are short by a
# few bytes: `len -4', or a packet too small to hold the TCP header it claims.
# Every fetch therefore queues here, wire_send() fetches as it goes, and tests
# take bytes with wire_drain() rather than from a single call's return value.
RX_PENDING = []


def wire_recv():
    d = get("/serial/recv?scc=%d&channel=%d" % (SCC, CHAN))["data"]
    if d:
        WIRE_LOG.extend(d)
        RX_PENDING.extend(d)
    return d


def wire_drain():
    """Fetch whatever is waiting, and return everything queued since the last
    call, in order.

    It FETCHES as well as drains: callers replaced wire_recv() with this, and a
    drain that only emptied the queue meant nothing ever pulled from the
    simulator outside a send -- so a run saw no frames at all."""
    wire_recv()
    d = list(RX_PENDING)
    del RX_PENDING[:]
    return d


def wire_hex(limit=64):
    """The first bytes the guest sent, raw.  A SLIP frame must open with C0;
    if the first payload byte after it is not 0x45 the frame left the guest
    already wrong, and if it is, the loss happened later."""
    b = WIRE_LOG[:limit]
    return " ".join("%02X" % x for x in b)


# Bytes per POST.  A SLIP frame is hundreds of bytes and the SCC's receive FIFO
# is three deep, so a frame delivered as one burst at 9600 baud overruns: the
# guest has about three character times to service each interrupt and a 6 MHz
# Z8001 running ttin() does not make that.  Measured with serial-bytes-test.py --
# eight-byte bursts lost slightly over half of a 255-byte range.  Real hardware
# has the same limit and answers it with flow control; here the host IS the peer,
# so the host paces, one byte at a time and on the wire's own state (wire_idle).
WIRE_CHUNK = 1


def wire_idle(timeout=2.0):
    """Wait until the wire is empty: nothing queued for the shifter and nothing
    left in the receive FIFO.  Pacing by wall clock does not pace the LINE --
    the shifter clocks queued bytes onto RxD at the programmed baud rate, so
    posting faster than that in simulated time only builds a backlog which is
    then emitted back-to-back, which is what overruns the FIFO."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            ch = get("/scc/0")["channels"][CHAN]
        except (urllib.error.URLError, OSError, KeyError):
            return
        if ch["rx_queued"] == 0 and ch["rx_fifo_len"] == 0:
            return
        time.sleep(0.02)


WIRE_FIELDS = ("rx_enabled", "rx_int_mode", "tx_enabled", "rx_char_avail",
               "rx_fifo_len", "rx_queued", "http_pending", "WR1", "WR3", "WR5")


def wire_state():
    """What the SCC itself says about the guest's end of the line.

    `rx_enabled' is WR3 D0 and `rx_int_mode' is WR1 D4:D3 -- BOTH must be set
    for the guest to receive at all, and al.c writes WR1 in exactly three
    places.  Bytes piling up in rx_queued with the receiver disabled is a
    different fault from bytes that were never sent, and from outside the two
    are indistinguishable."""
    try:
        ch = get("/scc/%d" % SCC)["channels"][CHAN]
    except (urllib.error.URLError, OSError, KeyError) as e:
        return {"error": str(e)}
    return dict((k, ch.get(k)) for k in WIRE_FIELDS)


def wire_report(label):
    print("    wire %-18s %s" % (label, wire_state()))


# slip's counter block (coh_slip.c `struct slipstat'): an 8-byte magic followed
# by eight big-endian longs.
SLIPSTAT_MAGIC = "SLIPSTAT"
SLIPSTAT_FIELDS = ("wakes", "rx", "in", "injected", "pumps", "replies",
                   "out", "writes", "kidreads", "kidrx", "kidwr",
                   "wpipe", "wchan", "len", "pack0", "o", "sl0", "wrc")


def read_slipstat(limit=8):
    """Read slip's counters straight out of the machine's memory.

    Needs nothing from the guest -- no console, no `sync', no readable disk --
    so it still reports when the guest has stopped responding, which is the
    case actually worth measuring.  Returns a list, because the magic also
    appears in any disk block of the slip binary sitting in the buffer cache;
    the live copy is the one whose counters move."""
    found, addr, out = [], 0, []
    pattern = SLIPSTAT_MAGIC.encode().hex()
    for _ in range(limit):
        try:
            r = get("/memory/find?start=0x%X&end=0x1000000&pattern=%s"
                    % (addr, pattern))
        except (urllib.error.URLError, OSError) as e:
            return [{"error": str(e)}]
        if not r.get("found"):
            break
        found.append(r["addr"])
        addr = r["addr"] + 1
    for a in found:
        try:
            raw = get("/memory/read?addr=0x%X&len=%d"
                      % (a + 8, 4 * len(SLIPSTAT_FIELDS)))
        except (urllib.error.URLError, OSError) as e:
            out.append({"addr": "0x%08X" % a, "error": str(e)})
            continue
        b = _membytes(raw)
        if len(b) < 4 * len(SLIPSTAT_FIELDS):
            out.append({"addr": "0x%08X" % a, "error": "short read"})
            continue
        rec = {"addr": "0x%08X" % a}
        for i, name in enumerate(SLIPSTAT_FIELDS):
            v = int.from_bytes(b[i * 4:i * 4 + 4], "big")
            # The byte-capture fields are only readable as bytes.
            rec[name] = ("%08X" % v) if name in ("pack0", "sl0") else v
        out.append(rec)
    return out


def _membytes(r):
    """/memory/read answers with whichever of these the build provides."""
    for key in ("bytes", "data"):
        v = r.get(key)
        if isinstance(v, list):
            return bytes(v)
        if isinstance(v, str):
            try:
                return bytes.fromhex(v)
            except ValueError:
                pass
    for key in ("hex", "text"):
        v = r.get(key)
        if isinstance(v, str):
            try:
                return bytes.fromhex(v.replace(" ", ""))
            except ValueError:
                pass
    return b""


def slipstat_report(label):
    for rec in read_slipstat():
        print("    slipstat %-14s %s" % (label, rec))


def wire_send(byte_list):
    for i in range(0, len(byte_list), WIRE_CHUNK):
        post("/serial/send", {"bytes": byte_list[i:i + WIRE_CHUNK],
                              "scc": SCC, "channel": CHAN})
        wire_idle()
        wire_recv()		# keep the guest's side from backing up


def send_packet(pkt):
    wire_send(slip_encode(pkt))


def typeline(line):
    subprocess.run([sys.executable, os.path.join(HERE, "simtype.py"), S, line],
                   check=False)


def console_bytes(byte_list, delay=0.12):
    """Raw bytes to the CONSOLE, which is SCC 0 channel B -- a different
    channel from the SLIP wire (channel A), so the two never collide.  For
    characters simtype.py cannot carry, chiefly the Ctrl-D that takes init from
    single-user to multi-user."""
    for b in byte_list:
        post("/serial/send", {"scc": 0, "channel": 1, "bytes": [b]})
        time.sleep(delay)


# -- the guest ----------------------------------------------------------------
#
# Each step is (command, marker-to-wait-for or None).  Waiting on a marker
# rather than sleeping matters most for slip itself: until it has the line open,
# alclose() has left the channel's receive interrupts disabled, so frames sent
# early are assembled into the three-deep FIFO and announced to nobody -- and a
# SLIP frame missing its leading bytes is simply gone.
def stack_setup(control=True):
    steps = [
        ("/etc/mknod /dev/inet p", None),
        ("/etc/inet &", "inet: ready"),
        ("/etc/ifconfig %s 255.255.255.0" % GUEST, None),
        ("/etc/ifconfig", "ip0: address %s" % GUEST),
    ]
    if control:
        # The control runs BEFORE slip attaches, on purpose.  Run after, it is
        # confounded: slip holds an outstanding READ on the same psip interface,
        # so the stack's replies go to slip's queue rather than psipping's and a
        # 0/2 here would say nothing about whether the stack works.
        steps.append(("/bin/psipping 2 echo", "psipping: echo 2/2 correct"))
    steps.append(("/etc/slip /dev/tty51 &", "slip: ready"))
    return steps


def build(dist):
    """Build the image rather than boot whatever build/ happens to hold: a
    failed `make dist' leaves the previous one behind, so "no output from the
    new code" can just mean the new code was never in the image."""
    if subprocess.run(["make", "-s", "dist", "DIST=" + dist],
                      cwd=HERE).returncode:
        sys.exit("make dist DIST=%s failed -- refusing to boot a stale image"
                 % dist)
    img = os.path.join(HERE, "build", dist + ".bin")
    if not os.path.exists(img):
        sys.exit("make dist succeeded but %s is missing" % img)
    return img


def workimg(img, tag):
    """Path of the working COPY to insert, leaving the build artifact alone.

    The simulator writes through to whatever file it is given and the guest
    mounts its root read-write, so booting the artifact itself both ships the
    session's leftovers inside it and makes each run start from whatever the
    last one left.  hostbuild/workimg.sh owns the copy (and PERSIST=1, which
    turns it off); this is the same call the shell harnesses make.
    """
    r = subprocess.run([os.path.join(HERE, "workimg.sh"), img, str(tag)],
                       stdout=subprocess.PIPE)
    if r.returncode:
        sys.exit("workimg.sh failed for %s" % img)
    return r.stdout.decode().strip()


class Guest(object):
    """A booted simulator, its console, and the commands typed at it."""

    def __init__(self, dist, tag):
        # One working copy per (image, port): the port is what this harness
        # already owns exclusively, so two harnesses can never share a copy.
        self.img = workimg(build(dist), S.rsplit(":", 1)[-1])
        # Per-run, not per-tag: two lanes running different harnesses still
        # collide on a fixed name, and the log is the only evidence left when
        # the simulator dies mid-run -- reading another lane's copy of it is
        # worse than having none.
        self.simlog = os.environ.get(
            "SIMLOG", "/tmp/c900sim-%s-%d.log" % (tag, os.getpid()))
        self.console = []
        self.sim = None
        self._log = None
        # One simulator on this host, globally, and none left behind by a
        # harness that died badly.  See simguard.py.
        self.guard = SimGuard(S.rsplit(":", 1)[-1], tag)

    def __enter__(self):
        # The guard takes the global lock first, then reclaims this port if a
        # previous run left something listening on it: a second simulator
        # cannot bind, exits at once, and the run would report "no
        # single-user prompt", reading as a broken image.
        self.guard.__enter__()
        # Keep the simulator's own output.  Discarding it meant that when the
        # sim exited mid-run -- which it does -- the harness saw only HTTP
        # failures it was written to ignore, and reported a guest that never
        # booted.  A crash must leave evidence.
        self._log = open(self.simlog, "w")
        self.sim = self.guard.spawn(
            [sim_bin("drive a SLIP link over the guest's serial line"),
             "--http", S.replace("http://", ""), "-display", "headless"],
            stdout=self._log, stderr=subprocess.STDOUT)
        return self

    def __exit__(self, *exc):
        # Report an exit the harness did not ask for: a sim that died mid-run is
        # the difference between "the guest never booted" and "the machine went
        # away", and those were indistinguishable while its output was discarded.
        if self.sim.poll() is not None:
            print("NOTE: simulator exited on its own (status %s); see %s"
                  % (self.sim.poll(), self.simlog))
            try:
                with open(self.simlog) as f:
                    tail = f.read()[-600:]
                if tail.strip():
                    print("--- simulator output (tail) ---")
                    print(tail)
            except OSError:
                pass
        # The guard kills the simulator and drops the global lock; doing it
        # here as well would only race with it.
        self.guard.__exit__(*exc)
        self._log.close()
        return False

    def pump(self):
        try:
            self.console.append(get("/serial/recv-all").get("ch_a", ""))
        except (urllib.error.URLError, OSError):
            pass

    def text(self):
        return "".join(self.console)

    def boot(self, timeout=420):
        time.sleep(3)
        post("/disk/hd/0/insert", {"path": self.img})
        post("/exec/run", {})
        t = 0
        while t < timeout:
            time.sleep(3)
            t += 3
            self.pump()
            if "# " in self.text():
                break
        else:
            sys.exit("FAIL: no single-user prompt\n%s" % self.text()[-400:])
        print("booted to single user in %ds" % t)
        time.sleep(2)

    def multiuser(self, timeout=300):
        """Ctrl-D at the single-user prompt: init runs /etc/rc and brings the
        machine up.  Returns when getty offers a login."""
        console_bytes([4])
        for _ in range(timeout):
            time.sleep(1)
            self.pump()
            if "login:" in self.text():
                print("multi-user: login prompt")
                return True
        print("--- guest console ---")
        print("\n".join(self.text().splitlines()[-20:]))
        sys.exit("FAIL: no login prompt after Ctrl-D")

    def run(self, line, marker=None, timeout=240, pause=3, retry=False):
        """Type a command; if a marker is given, wait for it to appear.

        `retry' starts the command once more if the marker never comes.  the inet daemon
        reaches its ready line about three runs in four and otherwise stops
        partway through initialising, printing its progress letters and then
        nothing; a second attempt has always got there.  Losing a twenty-minute
        run to that is not worth it, and the retry is visible in the output so
        it can never be mistaken for a first-attempt pass."""
        mark = len(self.text())
        typeline(line)
        if marker is None:
            time.sleep(pause)
            self.pump()
            return True
        # 240 s, not 60: the inet daemon is a 143 KB binary that reads its config and
        # initialises ten layers, and the simulator is far slower than the
        # instruction-level emulator where the same startup takes seconds.  A
        # short timeout here reported "inet never became ready" for a daemon
        # that was merely still starting.
        for _ in range(timeout):
            time.sleep(1)
            self.pump()
            if marker in self.text()[mark:]:
                print("ok: %s" % marker)
                return True
        if retry:
            print("NOTE: %r did not appear; the last thing it printed was %r."
                  " Retrying once."
                  % (marker, self.text()[mark:].strip().splitlines()[-1:]))
            # Kill the stalled one first.  Starting a second while the first
            # still holds /dev/inet leaves two daemons contending for the
            # rendezvous, which is a worse state than the stall.  `sh' echoes
            # the background pid, so take it from the console.
            for tok in reversed(self.text()[mark:].split()):
                if tok.isdigit():
                    self.run("kill -9 %s" % tok)
                    break
            return self.run(line, marker, timeout=timeout, pause=pause,
                            retry=False)
        print("--- guest console ---")
        print("\n".join(self.text().splitlines()[-20:]))
        sys.exit("FAIL: %r never appeared after %r" % (marker, line))

    def setup(self, steps):
        for line, marker in steps:
            self.run(line, marker, retry=("/etc/inet " in line))
        print("--- guest console ---")
        print("\n".join(self.text().splitlines()[-14:]))

    def alive(self, timeout=25):
        """Does the console shell still answer?

        A guest whose kernel is wedged and a daemon that is merely stuck look
        identical from the wire, and they need completely different fixes.  This
        also gates read_file(): if the shell cannot run `echo', it cannot run
        `sync' either, and a file the guest wrote will not be on the image no
        matter how the read is done -- reporting that as "no such file" blames
        the wrong thing."""
        mark = len(self.text())
        typeline("echo __ALIVE__")
        for _ in range(timeout):
            time.sleep(1)
            self.pump()
            if "__ALIVE__" in self.text()[mark:]:
                print("    console: alive")
                return True
        print("    console: NO ANSWER -- the guest is wedged, not just the daemon")
        return False

    def rcnetlog(self, lines=40):
        """rc.net's own log, the guest's /tmp/rc.net.log.

        init runs rc on /dev/null, so this is the only place the boot-time
        daemons can say anything -- inet's ready line, ifconfig's error, slip
        refusing the line.  /tmp is its own filesystem (hd3 at block 31008,
        media/hd21.media) and not the root, so the partition is named here and
        the path inside it drops the mount point."""
        return self.read_file("/rc.net.log", part="31008", lines=lines)

    def read_file(self, path, part="136", lines=24):
        """Read a file the guest wrote, off the DISK rather than the console.

        Reading a trace back with `cat' can truncate it mid-line; the
        fsread.py path has no console timing in it at all: sync in the
        guest, commit the sim's COW sectors, read the image."""
        if not self.alive():
            return ["(console wedged: `sync' cannot run, so nothing this guest"
                    " wrote has reached the image -- the file's absence says"
                    " nothing about whether it was written)"]
        self.run("sync")
        time.sleep(4)
        try:
            post("/disk/sync", {})
        except (urllib.error.URLError, OSError) as e:
            print("disk sync failed: %s" % e)
        r = subprocess.run([sys.executable, os.path.join(HERE, "fsread.py"),
                            self.img, "cat", path, "--part", part],
                           capture_output=True, text=True)
        return (r.stdout or r.stderr).splitlines()[-lines:]
