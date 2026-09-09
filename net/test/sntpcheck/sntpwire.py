"""sntpwire.py -- a host-side SNTP server on the emulator's serial wire.

    python3 sntpwire.py <socket-path> [--ctl=FILE] [--log=FILE] [--deaf]

WHAT IT IS.  The same shape as resolvcheck/dnswire.py, with a different payload:
the emulator's `--wire=PATH' attaches SCC channel A (the guest's /dev/tty51, the
line /etc/rc.net gives to SLIP) to an AF_UNIX stream socket, and this puts the
HOST at the far end of it as a full IP peer -- 10.0.0.1, answering ICMP echo and
serving SNTP on UDP port 123.

WHY A SCRIPTED SERVER AND NOT A REAL ONE.  Two reasons, and the second is the
whole test.

  1. The ANSWER has to be known.  Against pool.ntp.org the only available check
     is that the year looks plausible -- which passes just as well with the
     seconds half misread by a fortnight, or with the 1900->1970 offset applied
     twice and then luckily re-wrapped.  Here the transmit timestamp is a number
     this harness CHOSE, generated per run, so the guest's clock can be compared
     against it to the second.

  2. Half the code under test is refusals.  sntp.c declines a reply five ways --
     leap-indicator alarm, stratum 0 (kiss-o'-death), a zero transmit timestamp,
     a timestamp before 1970, and an originate field that does not echo what was
     sent -- and a correct server never does any of them.  Each is one line of
     control file here.

THE CONTROL FILE is re-read before every reply, so the answer can be changed
between cases WITHOUT restarting: the guest's SLIP line is this AF_UNIX
connection, and dropping it would take the guest's network down mid-run.
Format is `key value' per line, `#' comments:

    secs   <u32>     NTP seconds to send as the transmit timestamp (decimal)
    frac   <u32>     the fraction half            (default 0x40000000, .25 s)
    stratum <n>      default 1
    li     <n>       leap indicator; 3 is the ALARM, "not synchronised"
    mode   <n>       reply mode; 4 is server, anything else must be refused
    refid  <4 chars> default LOCL
    org    echo|zero|garble   what to put in the originate field.  `echo' is
                     correct; the others must make the client discard the reply
                     as an answer to somebody else's question
    trunc  <n>       send only the first n bytes of the 48 (0 = whole packet)
    deaf   0|1       receive and log, answer nothing.  The negative control.
    drop   <n>       ignore the next n requests, then answer normally -- for
                     the retry path

WHAT IT LOGS, and why that is the point.  A clock that is right after sntp ran
proves nothing on its own: it could have been right before, or set by something
else, or the program could have exited without sending a packet.  Every request
is logged with its mode and its transmit timestamp, and every reply with the
timestamp actually put on the wire, so "the guest asked", "the guest was
answered" and "the guest's clock changed" are three separate observations.

The SLIP framing, the IP/UDP builders and the packet description come from
hostbuild/slipwire.py, the same as every other serial harness here.
"""
import os
import select
import socket
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "..",
                                                 "hostbuild")))
from slipwire import (SlipDecoder, slip_encode, cksum, ip_wrap,  # noqa: E402
                      udp_dgram, Udp, describe)

SELF = "10.0.0.1"                 # what this peer answers to
NTP_PORT = 123
NTP_EPOCH = 2208988800            # 1900-01-01 .. 1970-01-01, > LONG_MAX


def u32(v):
    return (int(v) & 0xFFFFFFFF).to_bytes(4, "big")


class Control(object):
    """The scripted answer, re-read from disk before every reply."""

    DEFAULTS = {
        "secs": str(NTP_EPOCH + 1785002880),   # a 2026 date, overridden per run
        "frac": "0x40000000",
        "stratum": "1",
        "li": "0",
        "mode": "4",
        "refid": "LOCL",
        "org": "echo",
        "trunc": "0",
        "deaf": "0",
        "drop": "0",
    }

    def __init__(self, path):
        self.path = path
        self.d = dict(self.DEFAULTS)
        self.text = None
        self.seen = 0                 # requests received under THIS control

    def reload(self):
        if not self.path:
            return
        try:
            with open(self.path) as f:
                text = f.read()
        except OSError:
            return
        # `drop N' means "swallow the next N requests", which is only useful if
        # it counts from the moment the case asked for it.  A counter running
        # since the server started would already be past N by the time any
        # later case set it, and the drop would silently never happen -- a
        # retry case that reported one attempt and looked like a client that
        # does not retry.
        if text != self.text:
            self.text, self.seen = text, 0
        d = dict(self.DEFAULTS)
        for line in text.splitlines():
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            k, _, v = line.partition(" ")
            d[k.strip()] = v.strip()
        self.d = d

    def num(self, k):
        v = self.d.get(k, self.DEFAULTS[k])
        return int(v, 0)

    def str_(self, k):
        return self.d.get(k, self.DEFAULTS[k])


class Msg(object):
    """The 48-byte RFC 4330 packet, read off the wire."""

    def __init__(self, b):
        if len(b) < 48:
            raise ValueError("short SNTP packet, %d bytes" % len(b))
        self.li = (b[0] >> 6) & 3
        self.vn = (b[0] >> 3) & 7
        self.mode = b[0] & 7
        self.stratum = b[1]
        self.xmt_s = int.from_bytes(b[40:44], "big")
        self.xmt_f = int.from_bytes(b[44:48], "big")


class SntpWire(object):
    def __init__(self, path, ctl, log=sys.stderr, deaf=False):
        self.path, self.ctl, self.log = path, ctl, log
        self.deaf = deaf
        self.dec = SlipDecoder()
        self.sock = None
        self.frames_in = self.frames_out = 0

    def say(self, s):
        self.log.write("%.3f %s\n" % (time.time(), s))
        self.log.flush()

    def send_ip(self, pkt):
        self.frames_out += 1
        try:
            self.sock.sendall(bytes(slip_encode(pkt)))
        except OSError as e:
            self.say("send failed: %s" % e)

    def run(self):
        if os.path.exists(self.path):
            os.unlink(self.path)
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        srv.bind(self.path)
        srv.listen(1)
        self.say("sntpwire: listening on %s as %s%s"
                 % (self.path, SELF, "  (DEAF)" if self.deaf else ""))
        self.sock, _ = srv.accept()
        srv.close()
        self.say("sntpwire: guest attached")
        try:
            while True:
                r, _, _ = select.select([self.sock], [], [], 1.0)
                if not r:
                    continue
                data = self.sock.recv(4096)
                if not data:
                    self.say("sntpwire: guest closed the line")
                    return
                for pkt in self.dec.feed(data):
                    self.frames_in += 1
                    self.handle(pkt)
        finally:
            self.say("sntpwire: %d frames in, %d out"
                     % (self.frames_in, self.frames_out))

    def handle(self, pkt):
        self.say("RX   %s" % describe(pkt))
        if len(pkt) < 20 or cksum(pkt[:(pkt[0] & 0x0F) * 4]) != 0:
            self.say("     dropped: not an IP packet this peer can read")
            return
        src = ".".join(str(b) for b in pkt[12:16])
        dst = ".".join(str(b) for b in pkt[16:20])
        hl = (pkt[0] & 0x0F) * 4
        # A deaf server still DECODES: the queries the guest sent are the
        # evidence for the one thing that holds whether or not anybody answers,
        # namely that it asked at all.  Deafness is a property of the replies.
        if pkt[9] == 1:
            self.icmp(pkt, hl, src, dst)
        elif pkt[9] == 17:
            self.udp(pkt, src, dst)

    def icmp(self, pkt, hl, src, dst):
        ic = bytearray(pkt[hl:])
        if not ic or ic[0] != 8:
            return
        if self.deaf:
            self.say("DEAF: echo request from %s not answered" % src)
            return
        ic[0] = 0
        ic[2:4] = b"\0\0"
        ic[2:4] = cksum(bytes(ic)).to_bytes(2, "big")
        self.send_ip(ip_wrap(1, bytes(ic), src=dst, dst=src))
        self.say("ICMP echo reply to %s as %s" % (src, dst))

    def udp(self, pkt, src, dst):
        u = Udp(pkt)
        if u.dport != NTP_PORT:
            self.say("     udp port %d: not SNTP, ignored" % u.dport)
            return
        # Only this peer's own address is served.  An SNTP request sent to any
        # other address inside the SLIP net therefore leaves the guest, is
        # logged here, and is never answered -- which is the "asked, nobody
        # home" state, distinct from "never asked".
        if dst != SELF:
            self.say("NOTME udp request to %s from %s:%d (not answered)"
                     % (dst, src, u.sport))
            return
        try:
            m = Msg(u.data)
        except ValueError as e:
            self.say("     malformed request: %s" % e)
            return
        self.say("REQUEST udp from %s:%d mode %d version %d xmt %08x.%08x"
                 % (src, u.sport, m.mode, m.vn, m.xmt_s, m.xmt_f))

        self.ctl.reload()
        self.ctl.seen += 1
        if self.deaf or self.ctl.num("deaf"):
            self.say("SILENT udp (DEAF: this server answers nothing)")
            return
        drop = self.ctl.num("drop")
        if drop and self.ctl.seen <= drop:
            self.say("SILENT udp (drop %d of %d)" % (self.ctl.seen, drop))
            return

        secs = self.ctl.num("secs") & 0xFFFFFFFF
        frac = self.ctl.num("frac") & 0xFFFFFFFF
        li = self.ctl.num("li") & 3
        mode = self.ctl.num("mode") & 7
        stratum = self.ctl.num("stratum") & 0xFF
        refid = (self.ctl.str_("refid") + "\0\0\0\0")[:4].encode("latin1")

        org = self.ctl.str_("org")
        if org == "zero":
            org_s, org_f = 0, 0
        elif org == "garble":
            org_s, org_f = (m.xmt_s ^ 0x5A5A5A5A), (m.xmt_f ^ 0xFFFF)
        else:
            org_s, org_f = m.xmt_s, m.xmt_f

        out = (bytes([(li << 6) | (4 << 3) | mode, stratum, 6, 0xFA])
               + u32(0) + u32(0) + refid
               + u32(secs) + u32(0)          # reference
               + u32(org_s) + u32(org_f)     # originate: the client's transmit
               + u32(secs) + u32(0)          # receive
               + u32(secs) + u32(frac))      # transmit -- what the client uses
        assert len(out) == 48
        trunc = self.ctl.num("trunc")
        if trunc:
            out = out[:trunc]

        self.say("ANSWER udp to %s:%d xmt %08x.%08x li %d mode %d stratum %d"
                 " org %s%s"
                 % (src, u.sport, secs, frac, li, mode, stratum, org,
                    (" TRUNCATED to %d bytes" % trunc) if trunc else ""))
        self.send_ip(udp_dgram(NTP_PORT, u.sport, out, src=dst, dst=src))


def main(argv):
    path, ctlpath, logf, deaf = None, None, sys.stderr, False
    for a in argv[1:]:
        if a.startswith("--ctl="):
            ctlpath = a[6:]
        elif a.startswith("--log="):
            logf = open(a[6:], "w")
        elif a == "--deaf":
            deaf = True
        elif a.startswith("--"):
            sys.stderr.write("unknown option %s\n" % a)
            return 2
        else:
            path = a
    if path is None:
        sys.stderr.write(__doc__)
        return 2
    ctl = Control(ctlpath)
    ctl.reload()
    w = SntpWire(path, ctl, log=logf, deaf=deaf)
    try:
        w.run()
    except KeyboardInterrupt:
        pass
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
