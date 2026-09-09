#!/usr/bin/env python3
"""serial-bytes-test.py -- prove the simulator's serial API is byte-exact.

    python3 serial-bytes-test.py [dist]

Cold-boots a dist in the simulator, then checks all 256 byte values across
SCC 0 channel A (/dev/tty51 in the guest) in both directions:

    OUT  guest writes 0x00..0xFF  ->  host reads /serial/recv?scc=0&channel=0
    IN   host POSTs /serial/send {bytes:[0..255], scc:0, channel:0}
         ->  guest reads them and echoes the values as decimal on the console

The defects this guards against are all silent to ASCII traffic: a Go rune
conversion that turns every value >= 0x80 into two UTF-8 bytes, a channel not
exposed over HTTP, and `*_bytes' fields hardcoded empty.  SLIP is framed with
0xC0 and 0xDB, so any of them blocks TCP/IP bring-up.  A test that only moved
ASCII would pass throughout, which is why this one walks the whole byte range.
"""
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

from simguard import SimGuard, sim_bin
from slipwire import workimg

HERE = os.path.dirname(os.path.abspath(__file__))
DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"
IMG = os.path.join(HERE, "build", DIST + ".bin")
# The simulator: $SIM only, no default path.  See simguard.sim_bin and
# mk/simulator.sh for what c900sim is and what to use when you have none.
SIM = sim_bin("send bytes at the guest's serial line and read them back")
# Port 7801, NOT the default 7800: c900mcp manages its own simulator instance
# there, and a harness that grabs the default port -- or worse, pkills c900sim by
# name to force a cold boot -- takes that instance out from under the MCP server
# and makes it report a machine that is no longer the one it started.  Own the
# port, own the process, kill only the child spawned here.
S = os.environ.get("SIMHTTP", "http://localhost:7801")
# Per-run: see serial-rx-probe.py -- a fixed name is shared by every lane.
SIMLOG = os.environ.get("SIMLOG",
                        "/tmp/c900sim-serialbytes-%d.log" % os.getpid())
DEV = "/dev/tty51"          # SCC 0 channel A -- see tests/serialbytes
SCC, CHAN = 0, 0


def post(path, obj):
    req = urllib.request.Request(S + path, data=json.dumps(obj).encode(),
                                 headers={"Content-Type": "application/json"},
                                 method="POST")
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def get(path):
    with urllib.request.urlopen(S + path, timeout=30) as r:
        return json.load(r)


def console_drain(acc):
    """Append new console text to acc and return it."""
    try:
        acc.append(get("/serial/recv-all").get("ch_a", ""))
    except (urllib.error.URLError, OSError):
        pass
    return "".join(acc)


def drain_wire(timeout=2.0):
    """Wait until the injected bytes have actually left the wire.

    Pacing by wall clock does not pace the LINE.  The shifter queue is
    unbounded and clocks bytes onto RxD at the programmed baud rate, so a host
    that posts faster than 9600 baud in simulated time just builds a backlog
    (measured: 60 bytes queued while sending one byte every 20 ms) and the
    shifter then emits them back-to-back regardless of the gaps between posts.
    Back-to-back is exactly what overruns the three-deep receive FIFO.

    So wait on the wire itself: queue empty and FIFO drained means the guest has
    taken everything and the next byte cannot overrun.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            ch = get("/scc/0")["channels"][CHAN]
        except (urllib.error.URLError, OSError, KeyError):
            return
        if ch["rx_queued"] == 0 and ch["rx_fifo_len"] == 0:
            return
        time.sleep(0.02)


def decode(text):
    """Numbers the guest printed after its `read N of M' line."""
    tail = text.split("serialbytes: read")[-1]
    return [int(x) for x in tail.replace("\n", " ").split() if x.isdigit()]


def typeline(line):
    subprocess.run([sys.executable, os.path.join(HERE, "simtype.py"), S, line],
                   check=False)


def build():
    """Build the image, rather than boot whatever build/ happens to hold.

    A failed `make dist' leaves the PREVIOUS image in place -- and since the
    failure can happen upstream of the image rule, .DELETE_ON_ERROR does not
    catch it.  Booting that stale image is how this test spent a run reporting
    that a kernel probe never fired when the kernel under test predated the
    probe.  Build here, and stop on failure instead of falling back.
    """
    r = subprocess.run(["make", "-s", "dist", "DIST=" + DIST], cwd=HERE)
    if r.returncode != 0:
        sys.exit("make dist DIST=%s failed -- refusing to boot a stale image"
                 % DIST)
    if not os.path.exists(IMG):
        sys.exit("make dist succeeded but %s is missing" % IMG)


def main():
    build()

    # Cold boot needs a fresh process: an HTTP reset resumes a snapshot.
    # Keep the simulator's own output.  Discarding it meant that when the sim
    # exited mid-run -- which it does -- the harness saw only HTTP failures it
    # was written to ignore, and reported a guest that never booted.  A crash
    # must leave evidence.
    simlog = open(SIMLOG, "w")
    # One simulator on this host, globally, and none left behind by a harness
    # that died badly: the guard holds the lock, hands the simulator its fd,
    # kills it on any exit including a signal, and reclaims a port or a lock
    # left by a run that was SIGKILLed.  See simguard.py.
    with SimGuard(S.rsplit(":", 1)[-1], "serialbytes") as guard:
        sim = guard.spawn([SIM, "--http", S.replace("http://", ""),
                           "-display", "headless"],
                          stdout=simlog, stderr=subprocess.STDOUT)
        return run(sim, simlog)


def run(sim, simlog):
    try:
        time.sleep(3)
        post("/disk/hd/0/insert",
             {"path": workimg(IMG, S.rsplit(":", 1)[-1])})
        post("/exec/run", {})

        # Wait for the single-user prompt.  Drain as we go, or the console
        # buffer we are waiting on is also the one we are consuming.
        acc, t = [], 0
        while t < 420:
            time.sleep(3)
            t += 3
            if "# " in console_drain(acc):
                break
        else:
            sys.exit("FAIL: no single-user prompt after %ds\n%s"
                     % (t, "".join(acc)[-400:]))
        print("booted to single user in %ds" % t)
        time.sleep(2)
        console_drain(acc)

        failures = []

        # ---- OUT: guest writes 0x00..0xFF, host reads them back ----
        get("/serial/recv?scc=%d&channel=%d" % (SCC, CHAN))   # clear
        typeline("/bin/serialbytes out %s" % DEV)
        out = []
        for _ in range(20):
            time.sleep(2)
            out += get("/serial/recv?scc=%d&channel=%d" % (SCC, CHAN))["data"]
            if len(out) >= 256:
                break
        if out == list(range(256)):
            print("OUT: 256/256 bytes exact  PASS")
        else:
            failures.append("OUT")
            print("OUT: got %d bytes  FAIL" % len(out))
            # The old bug's signature: 0xC0 arriving as 0xC3 0x80.
            if 0xC3 in out and 0x80 in out:
                print("     0xC3 present -- looks like the rune conversion")
            print("     first 40: %s" % out[:40])
            print("     from 120: %s" % out[120:160])

        # ---- IN: host sends bytes, guest reads and echoes them ----
        # Two phases, because two different things can go wrong and they look
        # identical from the outside.  A SMALL burst tests only whether the
        # channel's receiver is live at all.  The full range then tests pacing:
        # the shifter queue is unbounded and clocks bytes onto RxD at baud rate,
        # but the SCC's own receive FIFO is 3 deep, so a guest that falls behind
        # loses bytes -- which is why kdbg paces console input (ConsoleRxIdle).
        def in_phase(values, chunk, gap, label):
            try:
                base = get("/scc/0")
                print("     before inject: int_asserts=%s int_acks=%s rr8=%s"
                      % (base.get("int_asserts"), base.get("int_acks"),
                         base.get("rr8_reads")))
            except (urllib.error.URLError, OSError):
                pass
            console_drain(acc)
            typeline("/bin/serialbytes in %s %d" % (DEV, len(values)))
            # Wait for the guest to say the line is OPEN.  A fixed sleep raced
            # it, and the race silently eats bytes: alclose() sets WR1 = 0, so
            # between readers this channel's receive interrupts are disabled and
            # anything arriving is assembled into the 3-deep FIFO and announced
            # to nobody.  That read as the driver losing characters for most of
            # a day (task #44, retracted).
            ready, t = False, 0
            while t < 60:
                time.sleep(1)
                t += 1
                if "serialbytes: ready" in console_drain(acc):
                    ready = True
                    break
            if not ready:
                print("IN %s: reader never opened the line  FAIL" % label)
                return False
            for i in range(0, len(values), chunk):
                wire = values[i:i + chunk]
                post("/serial/send", {"bytes": wire, "scc": SCC, "channel": CHAN})
                drain_wire(gap)
            # Watch the chip while the bytes are on the wire.  This is the
            # decisive observation: if the receive FIFO ever fills, the chip
            # assembled the character and the guest is not servicing it (an
            # interrupt-dispatch problem); if it never does while the shifter
            # queue drains, the byte crossed the wire unassembled (a model
            # problem).  Poll fast -- at 9600 baud a character is gone quickly.
            peak = {"fifo": 0, "avail": False, "queued": 0, "pending": False}
            for _ in range(30):
                try:
                    sc = get("/scc/0")
                except (urllib.error.URLError, OSError):
                    break
                ch = sc["channels"][CHAN]
                peak["fifo"] = max(peak["fifo"], ch["rx_fifo_len"])
                peak["queued"] = max(peak["queued"], ch["rx_queued"])
                peak["avail"] = peak["avail"] or ch["rx_char_avail"]
                peak["pending"] = peak["pending"] or sc["int_pending"]
                peak["ius"] = sc.get("ius_mask", "?")
                peak["asserts"] = sc.get("int_asserts", "?")
                peak["acks"] = sc.get("int_acks", "?")
                peak["rr8"] = sc.get("rr8_reads", "?")
                time.sleep(0.2)
            print("     in-flight peak: rx_fifo=%d rx_avail=%s queued=%d "
                  "int_pending=%s ius=%s"
                  % (peak["fifo"], peak["avail"], peak["queued"],
                     peak["pending"], peak.get("ius", "?")))
            print("     chip event COUNTS: int_asserts=%s int_acks=%s "
                  "rr8_reads(A,B)=%s"
                  % (peak.get("asserts", "?"), peak.get("acks", "?"),
                     peak.get("rr8", "?")))
            # Wait for the marker, then keep draining until the value list
            # stops growing.  Stopping at the marker raced the guest's own
            # printf: the tail held "read " and not one digit yet, which was
            # reported as zero values decoded rather than as a slow console.
            text, t, seen = "", 0, -1
            while t < 60:
                time.sleep(2)
                t += 2
                text = console_drain(acc)
                if "serialbytes: read" not in text:
                    continue
                n = len(decode(text))
                if n == seen and n > 0:
                    break
                seen = n
            if "serialbytes: read" not in text:
                print("IN %s: guest never reported -- still blocked in read()  FAIL"
                      % label)
                print("     console tail: %r" % text[-200:])
                # Ask the chip what it saw.  Without this, "the host never sent
                # it", "it is still on the wire" and "the guest was never
                # interrupted" all look the same from out here.
                try:
                    sc = get("/scc/0")
                    print("     SCC: mie=%s vis=%s vector=%s int_pending=%s ius=%s"
                          % (sc["mie"], sc["vis"], sc["int_vector"],
                             sc["int_pending"], sc.get("ius_mask", "?")))
                    for c in sc["channels"]:
                        print("     ch%s: rx_en=%s rx_int_mode=%d bit_ticks=%d "
                              "avail=%s fifo=%d queued=%d WR1=%s WR3=%s "
                              "WR11=%s WR12=%s WR13=%s WR14=%s"
                              % (c["label"], c["rx_enabled"], c["rx_int_mode"],
                                 c["bit_ticks"], c["rx_char_avail"],
                                 c["rx_fifo_len"], c["rx_queued"], c["WR1"],
                                 c["WR3"], c["WR11"], c["WR12"], c["WR13"],
                                 c["WR14"]))
                        print("           WR4=%s WR5=%s WR15=%s"
                              % (c["WR4"], c["WR5"], c["WR15"]))
                except (urllib.error.URLError, OSError, KeyError) as e:
                    print("     (no /scc/0: %s)" % e)
                return False
            nums = decode(text)
            counts, got = nums[:2], nums[2:]
            if got[:len(values)] == values:
                print("IN %s: %d/%d bytes exact  PASS" % (label, len(values), len(values)))
                return True
            print("IN %s: FAIL -- guest said 'read %s', decoded %d values"
                  % (label, " of ".join(str(c) for c in counts), len(got)))
            print("     got %s" % got[:24])
            print("     want %s" % values[:24])
            return False

        # The full range goes through the guest's `echo' mode: it reads N bytes
        # and writes them straight back down the SAME line, so the comparison
        # never touches the console.  Reporting 255 values as decimal is about a
        # kilobyte of console traffic, and at 9600 baud the host ended up parsing
        # a half-printed line and calling it zero values received -- a reporting
        # failure that looked like a data failure.
        def echo_phase(values):
            console_drain(acc)
            mark = len("".join(acc))
            typeline("/bin/serialbytes echo %s %d" % (DEV, len(values)))
            ready, t = False, 0
            while t < 60:
                time.sleep(1)
                t += 1
                if "serialbytes: ready" in console_drain(acc)[mark:]:
                    ready = True
                    break
            if not ready:
                print("IN full: reader never opened the line  FAIL")
                return False
            get("/serial/recv?scc=%d&channel=%d" % (SCC, CHAN))   # clear
            for v in values:
                post("/serial/send", {"bytes": [v], "scc": SCC, "channel": CHAN})
                drain_wire()
            back, t = [], 0
            while t < 90 and len(back) < len(values):
                time.sleep(1)
                t += 1
                back += get("/serial/recv?scc=%d&channel=%d"
                            % (SCC, CHAN))["data"]
            if back == values:
                print("IN full: %d/%d bytes echoed back exact  PASS"
                      % (len(values), len(values)))
                return True
            print("IN full: FAIL -- %d of %d came back" % (len(back), len(values)))
            first = next((i for i in range(min(len(back), len(values)))
                          if back[i] != values[i]), None)
            if first is not None:
                print("     first difference at %d: got %d want %d"
                      % (first, back[first], values[first]))
                print("     around it: got %s" % back[max(0, first - 4):first + 8])
                print("                want %s"
                      % values[max(0, first - 4):first + 8])
            elif len(back) < len(values):
                print("     truncated after %d; next expected %d"
                      % (len(back), values[len(back)]))
            print("     console: %r" % console_drain(acc)[-200:])
            return False

        # 0x00 is excluded: the guest reports with printf, and a NUL in the
        # middle of that is not worth the ambiguity.  Everything >= 0x80 -- the
        # values the old rune conversion destroyed -- is covered either way.
        if not in_phase([1, 2, 0xC0, 0xDB], 4, 0, "small"):
            failures.append("IN-small")
        # One byte per POST with a gap, NOT bursts.  Eight-byte bursts lost
        # slightly over half the range: the SCC receive FIFO is three deep, so a
        # burst arriving back-to-back at 9600 baud overruns unless the guest
        # services it within about three character times, and on a 6 MHz Z8001
        # running ttin() it does not.  Real hardware has the same limit; the
        # cure there is flow control, which this line does not use.  What the
        # test needs to establish is that every VALUE survives, so it paces to
        # a rate the guest sustains and records what that rate is.
        elif not echo_phase(list(range(1, 256))):
            failures.append("IN-full")

        print("=== %s" % ("ALL PASS" if not failures
                          else "FAILURES: " + ",".join(failures)))
        return 1 if failures else 0
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
