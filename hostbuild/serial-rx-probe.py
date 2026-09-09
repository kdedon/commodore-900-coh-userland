#!/usr/bin/env python3
"""serial-rx-probe.py -- why does a character received on tty51 never reach read()?

    python3 serial-rx-probe.py [dist]

Boots a dist to single user, starts `serialbytes in /dev/tty51', injects bytes on
SCC 0 channel A, and then reads al.c's per-line interrupt counters (alc_rx,
alc_ttin, alc_sc, alc_es, alc_tx, alc_rxempty -- see sys/z8001/drv/al.c) straight
out of kernel memory.

The counters, rather than printf: the console is the OTHER line on the same chip,
so a printf in an interrupt handler both floods the line under test and changes
the timing of what is being measured -- with one printf per transmitted character
a 256-byte write started losing bytes.  They are also read after the fact, so
nothing is sampled at the wrong moment.

Together with the chip-side counters in GET /scc/0 (int_asserts, int_acks,
rr8_reads) this pins the character's last known location:

  chip rr8_reads[A] rises, alc_rx[1] does not  -> something other than alRxintr
                                                  drained the FIFO
  alc_rx[1] rises, alc_ttin[1] does not        -> handler ran, RR0 said no char
  alc_ttin[1] rises but read() still blocks    -> lost at or above ttin()
  nothing rises at all                         -> the interrupt never arrived
"""
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

from simguard import sim_bin
from slipwire import workimg

HERE = os.path.dirname(os.path.abspath(__file__))
DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"
IMG = os.path.join(HERE, "build", DIST + ".bin")
KERNEL = os.path.join(HERE, "kobj", "kernel.out")
# loutdis is NOT in the toolchain checkout: it decodes through the private
# z8000 simulator, so it lives with the other simulator-dependent Go
# instruments in c900oses/gotools.  Resolved by name -- $C900_GOTOOLS, else a
# search -- and built through that tree's Makefile on demand, which reports a
# missing simulator by naming the variable to set.
def _gotools():
    e = os.environ.get("C900_GOTOOLS")
    if e:
        return e
    for d in ("../../../gotools", "../../c900oses/gotools",
              "../../../c900oses/gotools"):
        c = os.path.normpath(os.path.join(HERE, d))
        if os.path.isfile(os.path.join(c, "Makefile")):
            return c
    sys.exit("no c900oses/gotools tree found near %s: loutdis moved out of "
             "commodore-900-toolchain (it needs the private simulator).  "
             "Set $C900_GOTOOLS to it." % HERE)

GOTOOLS = _gotools()
LOUTDIS = os.path.join(GOTOOLS, "build", "loutdis")
# The simulator: $SIM only, no default path.  See simguard.sim_bin and
# mk/simulator.sh for what c900sim is and what to use when you have none.
SIM = sim_bin("watch the SCC receiver at register level")
# Port 7801, not the default 7800: c900mcp manages its own simulator there.
S = os.environ.get("SIMHTTP", "http://localhost:7801")
# Per-run: a fixed name is shared by every lane, and a second run truncating
# it hands this one another lane's evidence.  Printed with any crash tail.
SIMLOG = os.environ.get("SIMLOG",
                        "/tmp/c900sim-rxprobe-%d.log" % os.getpid())
SCC, CHAN = 0, 0                  # SCC 0 channel A == guest /dev/tty51
LINE = 1                          # altty[] index of tty51

COUNTERS = ["alc_rx", "alc_ttin", "alc_sc", "alc_es", "alc_tx", "alc_rxempty"]


def post(path, obj):
    req = urllib.request.Request(S + path, data=json.dumps(obj).encode(),
                                 headers={"Content-Type": "application/json"},
                                 method="POST")
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def get(path):
    with urllib.request.urlopen(S + path, timeout=30) as r:
        return json.load(r)


def typeline(line):
    subprocess.run([sys.executable, os.path.join(HERE, "simtype.py"), S, line],
                   check=False)


# -- locating kernel variables ------------------------------------------------
def kernel_syms():
    """name -> (segment, offset) from the linked kernel's own symbol table.

    loutdis prints Coherent l.out addresses as seg<<24|offset.  Which segment
    holds the data is a property of the link -- text takes as many segments as
    it needs from 0x30 and data has the one after -- so read it off the symbol
    rather than naming it.  Trailing underscore is the compiler's symbol
    decoration.
    """
    if not os.access(LOUTDIS, os.X_OK):
        if subprocess.run(["make", "-s", "loutdis"], cwd=GOTOOLS).returncode:
            sys.exit("gotools could not build loutdis (see above)")
    out = subprocess.run([LOUTDIS, "-syms", KERNEL],
                         capture_output=True, text=True)
    syms = {}
    for ln in out.stdout.splitlines():
        f = ln.split()
        if len(f) >= 3 and f[2].endswith("_"):
            a = int(f[0], 16)
            syms[f[2][:-1]] = (a >> 24, a & 0xFFFF)
    return syms


def find_data_base(syms):
    """Physical address of the kernel's data segment.

    Derived rather than assumed: alinit[] is a run of SCC port/value pairs whose
    contents are known exactly from al.c, and it is long enough (26 significant
    bytes) to be unique in 2 MB.  Searching for it and subtracting its symbol
    offset gives the segment base without depending on where the loader chose to
    put the kernel or on how the MMU is programmed.
    """
    seg, off = syms["alinit"]
    assert seg >= 0x31, "alinit is not in a kernel data segment (seg 0x%02X)" % seg
    # The first five port/value pairs: WR4,ALMODE WR3,RxPARAM WR5,TxPARAM
    # WR10,NRZ WR11,BRGINIT.  Stopping there is deliberate -- alload() patches
    # alinit[5] and alinit[6] with the baud-rate divisor before any of this runs,
    # so a signature that reached them would match the linked file and never the
    # running machine.
    pattern = [0x09, 0x4C, 0x07, 0xC0, 0x0B, 0x60, 0x15, 0x00, 0x17, 0x56]
    hexpat = "".join("%02x" % b for b in pattern)
    try:
        r = get("/memory/find?pattern=%s&start=0x0&end=0x400000" % hexpat)
    except urllib.error.HTTPError as e:
        print("  /memory/find unavailable (%s)" % e)
        return None
    if not r.get("found"):
        print("  alinit[] pattern not found in physical memory")
        return None
    first = r["addr"]
    if isinstance(first, str):
        first = int(first, 16)
    base = first - off
    print("  alinit[] at phys 0x%06X, symbol offset 0x%04X -> seg 0x%02X base "
          "0x%06X" % (first, off, seg, base))
    return base


def read_words(base, off, n):
    r = get("/memory/read?addr=0x%X&len=%d" % (base + off, n * 2))
    d = r["data"]
    return [(d[i] << 8) | d[i + 1] for i in range(0, len(d), 2)]  # big-endian


def counters(base, syms):
    """Each alc_* is an int[NMINOR]; NMINOR is 2."""
    out = {}
    for name in COUNTERS:
        if name in syms:
            out[name] = read_words(base, syms[name][1], 2)
    return out


def chip():
    try:
        return get("/scc/0")
    except (urllib.error.URLError, OSError):
        return {}


def main():
    if subprocess.run(["make", "-s", "dist", "DIST=" + DIST], cwd=HERE).returncode:
        sys.exit("make dist failed -- refusing to boot a stale image")

    syms = kernel_syms()
    missing = [c for c in COUNTERS if c not in syms]
    if missing:
        sys.exit("kernel has no %s.\n"
                 "al.c's counters are behind -DALDIAG and off by default -- they\n"
                 "cost ~134 bytes of text and the 0x30 code segment has little\n"
                 "room.  Add -DALDIAG to KDEFS in link-kernel.sh, rebuild, rerun."
                 % ", ".join(missing))
    print("kernel symbols: " + " ".join(
        "%s@0x%04X" % (c, syms[c][1]) for c in COUNTERS))

    # Keep the simulator's own output.  Discarding it meant that when the sim
    # exited mid-run -- which it does -- the harness saw only HTTP failures it
    # was written to ignore, and reported a guest that never booted.  A crash
    # must leave evidence.
    simlog = open(SIMLOG, "w")
    # One simulator on this host, globally, and none left behind by a harness
    # that died badly: the guard holds the lock, hands the simulator its fd,
    # kills it on any exit including a signal, and reclaims a port or a lock
    # left by a run that was SIGKILLed.  See simguard.py.
    with SimGuard(S.rsplit(":", 1)[-1], "rxprobe") as guard:
        sim = guard.spawn([SIM, "--http", S.replace("http://", ""),
                           "-display", "headless"],
                          stdout=simlog, stderr=subprocess.STDOUT)
        return probe(sim, simlog, syms)


def probe(sim, simlog, syms):
    console = []

    def pump():
        try:
            console.append(get("/serial/recv-all").get("ch_a", ""))
        except (urllib.error.URLError, OSError):
            pass

    def wait_for(needle, since, secs):
        """Poll the console until `needle' appears after offset `since'."""
        for _ in range(int(secs * 2)):
            time.sleep(0.5)
            pump()
            if needle in "".join(console)[since:]:
                return True
        return False

    try:
        time.sleep(3)
        post("/disk/hd/0/insert",
             {"path": workimg(IMG, S.rsplit(":", 1)[-1])})
        post("/exec/run", {})

        t = 0
        while t < 420:
            time.sleep(3)
            t += 3
            pump()
            if "# " in "".join(console):
                break
        else:
            sys.exit("FAIL: no single-user prompt\n%s" % "".join(console)[-600:])
        print("booted to single user in %ds" % t)
        time.sleep(2)

        base = find_data_base(syms)
        if base is None:
            sys.exit("cannot locate kernel data segment; no counters readable")

        print("counters at rest: %s" % counters(base, syms))

        # Three cases in one boot.  The first run showed 4 bytes injected
        # back-to-back producing exactly 3 characters at ttin() and a single Rx
        # interrupt, so what matters now is whether ANY byte gets through and
        # whether the losses depend on inter-byte spacing.
        cases = [
            ("one byte", 1, [0x41], 0.0),
            ("four back-to-back", 4, [0x41, 0x42, 0x43, 0x44], 0.0),
            ("four spaced 50ms", 4, [0x51, 0x52, 0x53, 0x54], 0.05),
        ]
        results = []
        for n, (name, want, data, gap) in enumerate(cases):
            # FOREGROUND, and synchronised on the guest's own marker.  Sleeping a
            # fixed few seconds instead raced the open, and the race is not
            # harmless: alclose() sets WR1 = 0, so between readers this channel's
            # receive interrupts are disabled, and bytes injected then are
            # assembled into the FIFO and announced to nobody.  That produced
            # "one byte: 0 delivered" followed by the byte turning up on the NEXT
            # open, which reads as the driver losing characters.
            mark = len("".join(console))
            typeline("/bin/serialbytes in /dev/tty51 %d" % want)
            if not wait_for("serialbytes: ready", mark, 60):
                print("--- %s: reader never became ready; skipped" % name)
                continue
            before = counters(base, syms)
            cb = chip()

            if gap:
                for b in data:
                    post("/serial/send", {"bytes": [b], "scc": SCC,
                                          "channel": CHAN})
                    time.sleep(gap)
            else:
                post("/serial/send", {"bytes": data, "scc": SCC,
                                      "channel": CHAN})
            peak = {}
            for _ in range(16):
                time.sleep(0.5)
                c = chip()
                if c.get("rx_fifo_len", 0) or c.get("rx_char_avail"):
                    peak = c
            time.sleep(2)
            after = counters(base, syms)
            ca = chip()

            d = {k: after[k][LINE] - before[k][LINE] for k in after}
            print("--- %s: injected %d" % (name, len(data)))
            print("    line-1 deltas: %s" % d)
            print("    chip: asserts %s->%s acks %s->%s rr8 %s->%s ius=%s" % (
                cb.get("int_asserts"), ca.get("int_asserts"),
                cb.get("int_acks"), ca.get("int_acks"),
                cb.get("rr8_reads"), ca.get("rr8_reads"), ca.get("ius_mask")))
            if peak:
                print("    in flight: fifo=%s avail=%s queued=%s pending=%s" % (
                    peak.get("rx_fifo_len"), peak.get("rx_char_avail"),
                    peak.get("rx_queued"), peak.get("int_pending")))
            results.append((name, len(data), d))

            # The reader prints its own verdict on the console once satisfied.
            if not wait_for("serialbytes: read", mark, 60):
                print("    reader still blocked (never printed its count)")

        text = "".join(console)
        print("--- console ---")
        print("\n".join(text.splitlines()[-30:]))
        print("--- summary ---")
        for name, sent, d in results:
            print("  %-20s sent %d  rx-ints %d  chars-to-ttin %d" % (
                name, sent, d.get("alc_rx", 0), d.get("alc_ttin", 0)))
        return 0
    finally:
        # Report an exit the harness did not ask for: a sim that died mid-run is
        # the difference between "the guest never booted" and "the machine went
        # away", and those were indistinguishable while its output was discarded.
        if sim.poll() is not None:
            print("NOTE: simulator exited on its own (status %s); see %s"
                  % (sim.poll(), SIMLOG))
            try:
                with open(SIMLOG) as f:
                    tail = f.read()[-600:]
                if tail.strip():
                    print("--- simulator output (tail) ---")
                    print(tail)
            except OSError:
                pass
        # Stopping the simulator is the guard's job, not this one's.
        simlog.close()


if __name__ == "__main__":
    sys.exit(main())
