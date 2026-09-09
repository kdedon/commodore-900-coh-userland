"""selftest.py -- is the INSTRUMENT right, before an emulator run is spent?

    python3 selftest.py

sntpwire.py is asked the questions the guest will ask it, by a fake guest on the
host: a SLIP-framed IP/UDP/SNTP request goes in, the reply comes back out, and
it is decoded and checked against what the control file said.  No emulator, no
image, and it takes under a second.

WHY IT EXISTS.  A harness bug returns plausible data (the standing lesson:
"instrument failure looks like a result").  If sntpwire put the seconds in the
wrong half of the timestamp, every case in sntpcheck.py would fail in the same
way a genuinely broken client fails, and the first suspect would be the guest.
So the server is measured on its own first, against arithmetic computed here
independently of the code under test.

The rounding cases are the ones worth having: the whole reason sntp.c is hard is
that the numbers involved are above LONG_MAX, and Python has no such boundary --
which makes it a sound oracle and a poor imitation.  What is checked here is
therefore the WIRE ENCODING, byte by byte, not the conversion.
"""
import calendar
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "..",
                                                 "hostbuild")))
from slipwire import udp_dgram, Udp, slip_encode        # noqa: E402
from sntpwire import SntpWire, Control, SlipDecoder     # noqa: E402

NTP_EPOCH = 2208988800
GUEST, SELF = "10.0.0.2", "10.0.0.1"
FAILED = []


class FakeSock(object):
    def __init__(self):
        self.out = bytearray()

    def sendall(self, b):
        self.out.extend(b)


def check(what, ok):
    print("  %-58s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        FAILED.append(what)


def request(xmt_s, xmt_f, dst=SELF, sport=49152):
    body = (bytes([(0 << 6) | (4 << 3) | 3, 0, 6, 0xFA])
            + b"\0" * 12 + b"\0" * 16 + b"\0" * 8
            + xmt_s.to_bytes(4, "big") + xmt_f.to_bytes(4, "big"))
    assert len(body) == 48, len(body)
    return udp_dgram(sport, 123, body, src=GUEST, dst=dst)


def reply_of(w, sock):
    """Decode whatever the server put on the wire, as an SNTP body."""
    pkts = list(SlipDecoder().feed(bytes(sock.out)))
    sock.out = bytearray()
    if not pkts:
        return None
    return Udp(pkts[-1]).data


def server(tmp, **kw):
    path = os.path.join(tmp, "ctl")
    with open(path, "w") as f:
        for k, v in kw.items():
            f.write("%s %s\n" % (k, v))
    ctl = Control(path)
    ctl.reload()
    w = SntpWire("/dev/null", ctl, log=open(os.devnull, "w"))
    w.sock = FakeSock()
    return w, w.sock


def main():
    import tempfile
    tmp = tempfile.mkdtemp(prefix="sntpselftest.")
    t = calendar.timegm((2029, 4, 17, 5, 6, 7, 0, 0, 0))
    secs = t + NTP_EPOCH
    print("selftest: answering with NTP %u (time_t %d = %s)"
          % (secs, t, time.asctime(time.gmtime(t))))

    # -- the plain answer -----------------------------------------------------
    w, s = server(tmp, secs=secs, frac="0x40000000", stratum=2, refid="TEST")
    w.handle(request(0x11223344, 0x55667788))
    b = reply_of(w, s)
    check("a 48-byte reply comes back", b is not None and len(b) == 48)
    check("mode 4 (server), version 4",
          b[0] & 7 == 4 and (b[0] >> 3) & 7 == 4)
    check("stratum and refid are what the control file said",
          b[1] == 2 and b[12:16] == b"TEST")
    check("the transmit seconds are in bytes 40..43, big-endian",
          int.from_bytes(b[40:44], "big") == secs)
    check("the fraction is in bytes 44..47",
          int.from_bytes(b[44:48], "big") == 0x40000000)
    check("the originate field echoes the client's transmit timestamp",
          int.from_bytes(b[24:28], "big") == 0x11223344
          and int.from_bytes(b[28:32], "big") == 0x55667788)
    check("the seconds are ABOVE 2^31 (the whole reason this is hard)",
          secs > 0x7FFFFFFF)

    # -- the refusals the client must decline ---------------------------------
    w, s = server(tmp, secs=secs, li=3)
    b = reply_of(w, s) if w.handle(request(1, 1)) is None else None
    check("li 3 sets the alarm bits and nothing else", b[0] >> 6 == 3)

    w, s = server(tmp, secs=secs, stratum=0)
    w.handle(request(1, 1))
    check("stratum 0 is sent as stratum 0", reply_of(w, s)[1] == 0)

    w, s = server(tmp, secs=secs, org="garble")
    w.handle(request(0x11223344, 0x55667788))
    b = reply_of(w, s)
    check("org=garble does NOT echo the originate field",
          int.from_bytes(b[24:28], "big") != 0x11223344)

    w, s = server(tmp, secs=secs, trunc=20)
    w.handle(request(1, 1))
    check("trunc 20 puts 20 bytes on the wire", len(reply_of(w, s)) == 20)

    # -- silence, and the two ways it happens ---------------------------------
    w, s = server(tmp, secs=secs, deaf=1)
    w.handle(request(1, 1))
    check("deaf answers nothing at all", reply_of(w, s) is None)

    w, s = server(tmp, secs=secs, drop=1)
    w.handle(request(1, 1))
    check("drop 1 swallows the first request", reply_of(w, s) is None)
    w.handle(request(2, 2))
    check("and answers the second", reply_of(w, s) is not None)

    # -- the address discipline the `nobody' case depends on ------------------
    w, s = server(tmp, secs=secs)
    w.handle(request(1, 1, dst="10.0.0.77"))
    check("a request to another address is NOT answered",
          reply_of(w, s) is None)

    # -- ICMP, which is how the harness knows the line is alive ---------------
    w, s = server(tmp, secs=secs)
    import slipwire
    w.handle(slipwire.echo_request(1))
    check("an echo request is answered", len(s.out) > 0)

    print("")
    print("=== instrument: %s" % ("PASS" if not FAILED else
                                  "FAIL (%d)" % len(FAILED)))
    import shutil
    shutil.rmtree(tmp, ignore_errors=True)
    return 0 if not FAILED else 1


if __name__ == "__main__":
    sys.exit(main())
