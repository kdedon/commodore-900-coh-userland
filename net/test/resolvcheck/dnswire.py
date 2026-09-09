"""dnswire.py -- a host-side nameserver on the emulator's serial wire.

    python3 dnswire.py <socket-path> --zone=NAME=A.B.C.D [...] [options]

WHAT IT IS.  The emulator's `--wire=PATH' attaches SCC channel A -- the guest's
/dev/tty51, the line /etc/rc.net gives to SLIP -- to an AF_UNIX stream socket.
twohost/wire.py puts a second emulator at the far end of that socket.  This puts
the HOST there instead, as a full IP peer: 10.0.0.1, answering ICMP echo, and
serving DNS on UDP port 53 and TCP port 53.

WHY THE HOST AND NOT A SECOND GUEST.  A second C900 would have to run a
nameserver, and there is none: BIND was never part of the Minix 2 userland this
port draws on, so no guest can answer a query at all.  Beyond availability, a
real server is the wrong instrument even if we had one -- three of the paths
under test are things a correct server never does on demand: stay silent so the
retry schedule can be counted, set the truncation bit so the TCP fallback is
taken, and deny a name at a chosen moment.  A scripted peer can be told to do
each of those, and can timestamp every query it refuses to answer.

WHAT IT LOGS, and why that is the point.  Every frame is written to the log with
a wall-clock timestamp and a decode: the queries the guest sent, the answers
sent back, and the queries deliberately not answered.  A resolver that silently
falls back to /etc/hosts is indistinguishable from one that works unless
something can say whether a query was ever put on the wire, and this log is that
something.  `QUERY' lines are what the guest ASKED; a test whose name never
appears in one was answered by the file, or not at all.

MODES.
    --zone NAME=A.B.C.D    answer NAME with that A record (repeatable)
    --ptr  A.B.C.D=NAME    answer NAME for that address's in-addr.arpa PTR
    --silent NAME          never answer this name at all, in any form (the
                           first label is matched, so the search-list variants
                           are silent too).  For the retry schedule.
    --trunc NAME=A.B.C.D   answer over UDP with the truncation bit set and NO
                           answer records, and over TCP with this address.  The
                           address therefore exists only in the circuit answer:
                           seeing it on the guest proves the TCP path ran.
    --deaf                 receive and log everything, answer nothing, ever.
                           The negative control: the guest sees a live line and
                           a dead server.
    --log FILE             where the log goes (default stderr).

Unknown names are answered NXDOMAIN rather than ignored, which is what a real
server does and what keeps the resolver's search list from costing a timeout
per element.

The SLIP framing, the IP/UDP/TCP builders and the packet description all come
from hostbuild/slipwire.py, so this decodes the wire the same way every other
serial harness in this tree does.
"""
import os
import random
import select
import socket
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "..",
                                                 "hostbuild")))
from slipwire import (SlipDecoder, slip_encode, cksum, ip_wrap, ip4,  # noqa: E402
                      udp_dgram, tcp_seg, Tcp, Udp, describe,
                      TCP_FIN, TCP_SYN, TCP_RST, TCP_PSH, TCP_ACK)

SELF = "10.0.0.1"                 # what this peer answers to

# DNS
T_A, T_PTR, T_CNAME = 1, 12, 5
C_IN = 1
NOERROR, FORMERR, SERVFAIL, NXDOMAIN, NOTIMP, REFUSED = 0, 1, 2, 3, 4, 5


# -- names --------------------------------------------------------------------
def encode_name(n):
    out = b""
    for lbl in n.rstrip(".").split("."):
        if not lbl:
            continue
        out += bytes([len(lbl)]) + lbl.encode("latin1")
    return out + b"\0"


def decode_name(msg, off):
    """Return (name, offset-after).  Follows compression pointers."""
    parts, jumped, after = [], False, off
    for _ in range(128):                      # a loop guard, not a limit
        if off >= len(msg):
            break
        n = msg[off]
        if n == 0:
            off += 1
            break
        if n & 0xC0 == 0xC0:
            if off + 1 >= len(msg):
                break
            ptr = ((n & 0x3F) << 8) | msg[off + 1]
            if not jumped:
                after = off + 2
            off, jumped = ptr, True
            continue
        parts.append(msg[off + 1:off + 1 + n].decode("latin1"))
        off += 1 + n
    return ".".join(parts), (after if jumped else off)


def rr(name_ptr, rtype, rdata, ttl=60):
    return (name_ptr + rtype.to_bytes(2, "big") + C_IN.to_bytes(2, "big")
            + ttl.to_bytes(4, "big") + len(rdata).to_bytes(2, "big") + rdata)


class Question(object):
    def __init__(self, msg):
        self.id = msg[0:2]
        self.flag1, self.flag2 = msg[2], msg[3]
        self.qdcount = (msg[4] << 8) | msg[5]
        self.name, off = decode_name(msg, 12)
        self.qtype = int.from_bytes(msg[off:off + 2], "big")
        self.qclass = int.from_bytes(msg[off + 2:off + 4], "big")
        self.qsec = msg[12:off + 4]
        self.rd = bool(self.flag1 & 0x01)


def dns_reply(q, rcode, answers=(), tc=False):
    """Build a response to `q'.  `answers' are already-encoded RRs."""
    flag1 = 0x80 | 0x04                                              # QR|AA
    if q.rd:
        flag1 |= 0x01
    if tc:
        flag1 |= 0x02
    flag2 = 0x80 | (rcode & 0x0F)                                    # RA|RCODE
    hdr = q.id + bytes([flag1, flag2]) + (1).to_bytes(2, "big") \
        + len(answers).to_bytes(2, "big") + b"\0\0\0\0"
    return hdr + q.qsec + b"".join(answers)


# -- the zone -----------------------------------------------------------------
class Zone(object):
    def __init__(self):
        self.a = {}                  # lowercase fqdn -> dotted quad
        self.ptr = {}                # dotted quad    -> fqdn
        self.silent = set()          # first labels never answered
        self.trunc = {}              # lowercase fqdn -> dotted quad (TCP only)
        self.deaf = False

    def is_silent(self, name):
        return name.split(".")[0].lower() in self.silent

    def answer(self, q, over_tcp):
        """(rcode, [rr...], truncate?) for this question, or None for silence."""
        name = q.name.lower()
        if self.deaf or self.is_silent(name):
            return None
        ptrn = b"\xc0\x0c"                     # the question's own name
        if q.qclass != C_IN:
            return (REFUSED, [], False)
        if q.qtype == T_A:
            if name in self.trunc:
                if not over_tcp:
                    # Truncated, and DELIBERATELY carrying no answer: the
                    # address exists only in the circuit reply, so a guest that
                    # prints it cannot have got it from this datagram.
                    return (NOERROR, [], True)
                return (NOERROR, [rr(ptrn, T_A, ip4(self.trunc[name]))], False)
            if name in self.a:
                return (NOERROR, [rr(ptrn, T_A, ip4(self.a[name]))], False)
            return (NXDOMAIN, [], False)
        if q.qtype == T_PTR:
            if name.endswith(".in-addr.arpa"):
                quad = ".".join(reversed(name[:-len(".in-addr.arpa")].split(".")))
                if quad in self.ptr:
                    return (NOERROR,
                            [rr(ptrn, T_PTR, encode_name(self.ptr[quad]))],
                            False)
            return (NXDOMAIN, [], False)
        # A type this zone does not serve: NOERROR with no records is "the name
        # exists, that type does not", which is a different answer from NXDOMAIN
        # and is what res_query turns into NO_DATA.
        return (NOERROR if (name in self.a or name in self.trunc)
                else NXDOMAIN, [], False)


# -- a very small TCP responder ----------------------------------------------
class TcpConn(object):
    """One passive connection, enough to carry a DNS message each way.

    No retransmission and no window management: the peer is the host, the link
    is a socket, and nothing here is lost.  What it must get right is the
    handshake, the sequence numbers and the FIN, because the guest's stack is
    the real one and will not talk to a peer that gets those wrong.
    """

    def __init__(self, wire, zone, key, iss=None):
        self.w, self.zone, self.key = wire, zone, key
        self.gaddr, self.gport, self.lport = key
        self.snd = iss if iss is not None else random.randint(1, 0x7FFFFFFF)
        self.rcv = 0
        self.state = "LISTEN"
        self.inbuf = b""
        self.replied = False

    def send(self, flags, payload=b""):
        self.w.send_ip(tcp_seg(self.lport, self.gport, self.snd, self.rcv,
                               flags, payload, src=SELF, dst=self.gaddr))
        self.snd = (self.snd + len(payload)
                    + (1 if flags & (TCP_SYN | TCP_FIN) else 0)) & 0xFFFFFFFF

    def feed(self, t):
        if t.flags & TCP_RST:
            self.state = "CLOSED"
            return
        if t.flags & TCP_SYN and self.state == "LISTEN":
            self.rcv = (t.seq + 1) & 0xFFFFFFFF
            self.state = "SYN_RCVD"
            self.send(TCP_SYN | TCP_ACK)
            return
        if self.state == "SYN_RCVD" and t.flags & TCP_ACK:
            self.state = "ESTAB"
        if t.data:
            if t.seq == self.rcv:
                self.inbuf += t.data
                self.rcv = (self.rcv + len(t.data)) & 0xFFFFFFFF
            self.send(TCP_ACK)
            self.serve()
        if t.flags & TCP_FIN and self.state in ("ESTAB", "FINWAIT"):
            self.rcv = (self.rcv + 1) & 0xFFFFFFFF
            self.send(TCP_ACK | TCP_FIN)
            self.state = "CLOSED"

    def serve(self):
        """A DNS message over a circuit is preceded by its 16-bit length."""
        while len(self.inbuf) >= 2:
            n = (self.inbuf[0] << 8) | self.inbuf[1]
            if len(self.inbuf) < 2 + n:
                return
            msg, self.inbuf = self.inbuf[2:2 + n], self.inbuf[2 + n:]
            try:
                q = Question(msg)
            except (IndexError, ValueError) as e:
                self.w.say("TCP  malformed query: %s" % e)
                return
            self.w.say("QUERY tcp  %s type %d from %s:%d"
                       % (q.name, q.qtype, self.gaddr, self.gport))
            a = self.zone.answer(q, over_tcp=True)
            if a is None:
                self.w.say("SILENT tcp %s (this name is never answered)" % q.name)
                return
            rcode, answers, _ = a
            reply = dns_reply(q, rcode, answers)
            self.w.say("ANSWER tcp %s rcode %d records %d"
                       % (q.name, rcode, len(answers)))
            self.send(TCP_ACK | TCP_PSH,
                      len(reply).to_bytes(2, "big") + reply)
            self.replied = True


# -- the peer -----------------------------------------------------------------
class DnsWire(object):
    def __init__(self, path, zone, log=sys.stderr):
        self.path, self.zone, self.log = path, zone, log
        self.dec = SlipDecoder()
        self.sock = None
        self.conns = {}
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
        self.say("dnswire: listening on %s as %s%s"
                 % (self.path, SELF, "  (DEAF)" if self.zone.deaf else ""))
        self.sock, _ = srv.accept()
        srv.close()
        self.say("dnswire: guest attached")
        try:
            while True:
                r, _, _ = select.select([self.sock], [], [], 1.0)
                if not r:
                    continue
                data = self.sock.recv(4096)
                if not data:
                    self.say("dnswire: guest closed the line")
                    return
                for pkt in self.dec.feed(data):
                    self.frames_in += 1
                    self.handle(pkt)
        finally:
            self.say("dnswire: %d frames in, %d out"
                     % (self.frames_in, self.frames_out))

    def handle(self, pkt):
        self.say("RX   %s" % describe(pkt))
        if len(pkt) < 20 or cksum(pkt[:(pkt[0] & 0x0F) * 4]) != 0:
            self.say("     dropped: not an IP packet this peer can read")
            return
        src = ".".join(str(b) for b in pkt[12:16])
        dst = ".".join(str(b) for b in pkt[16:20])
        hl = (pkt[0] & 0x0F) * 4
        # A deaf server still DECODES.  Refusing to look at the packet as well
        # as to answer it would put the queries the guest sent out of the log,
        # and those are the evidence for the two things that hold whether or
        # not anybody answers: that a query was sent at all, and how often it
        # was repeated.  Deafness is a property of the replies.
        if pkt[9] == 1:
            self.icmp(pkt, hl, src, dst)
        elif pkt[9] == 17:
            self.udp(pkt, src, dst)
        elif pkt[9] == 6:
            self.tcp(pkt, src, dst)

    def icmp(self, pkt, hl, src, dst):
        ic = bytearray(pkt[hl:])
        if not ic or ic[0] != 8:
            return
        if self.zone.deaf:
            self.say("DEAF: echo request from %s not answered" % src)
            return
        ic[0] = 0                                    # echo reply
        ic[2:4] = b"\0\0"
        ic[2:4] = cksum(bytes(ic)).to_bytes(2, "big")
        # Answer FROM the address that was asked for.  The guest may be pinging
        # a name this peer serves, whose address is not this peer's own.
        self.send_ip(ip_wrap(1, bytes(ic), src=dst, dst=src))
        self.say("ICMP echo reply to %s as %s" % (src, dst))

    def udp(self, pkt, src, dst):
        u = Udp(pkt)
        if u.dport != 53:
            self.say("     udp port %d: not DNS, ignored" % u.dport)
            return
        try:
            q = Question(u.data)
        except (IndexError, ValueError) as e:
            self.say("     malformed query: %s" % e)
            return
        self.say("QUERY udp  %s type %d from %s:%d id %s"
                 % (q.name, q.qtype, src, u.sport, q.id.hex()))
        a = self.zone.answer(q, over_tcp=False)
        if a is None:
            self.say("SILENT udp %s (%s)"
                     % (q.name, "DEAF: this server answers nothing"
                        if self.zone.deaf else "this name is never answered"))
            return
        rcode, answers, tc = a
        reply = dns_reply(q, rcode, answers, tc=tc)
        self.say("ANSWER udp %s rcode %d records %d%s"
                 % (q.name, rcode, len(answers), " TRUNCATED" if tc else ""))
        self.send_ip(udp_dgram(53, u.sport, reply, src=dst, dst=src))

    def tcp(self, pkt, src, dst):
        t = Tcp(pkt)
        if t.dport != 53:
            # Refuse anything else, rather than leaving the guest to time out.
            self.send_ip(tcp_seg(t.dport, t.sport, 0,
                                 (t.seq + len(t.data) + 1) & 0xFFFFFFFF,
                                 TCP_RST | TCP_ACK, src=dst, dst=src))
            return
        if self.zone.deaf:
            self.say("DEAF: tcp %d->%d not answered" % (t.sport, t.dport))
            return
        key = (src, t.sport, t.dport)
        c = self.conns.get(key)
        if c is None:
            c = self.conns[key] = TcpConn(self, self.zone, key)
        c.feed(t)
        if c.state == "CLOSED":
            del self.conns[key]


def main(argv):
    zone = Zone()
    path, logf = None, sys.stderr
    for a in argv[1:]:
        if a.startswith("--zone="):
            n, v = a[7:].split("=", 1)
            zone.a[n.lower()] = v
        elif a.startswith("--ptr="):
            n, v = a[6:].split("=", 1)
            zone.ptr[n] = v
        elif a.startswith("--silent="):
            zone.silent.add(a[9:].split(".")[0].lower())
        elif a.startswith("--trunc="):
            n, v = a[8:].split("=", 1)
            zone.trunc[n.lower()] = v
        elif a == "--deaf":
            zone.deaf = True
        elif a.startswith("--log="):
            logf = open(a[6:], "w")
        elif a.startswith("--"):
            sys.stderr.write("unknown option %s\n" % a)
            return 2
        else:
            path = a
    if path is None:
        sys.stderr.write(__doc__)
        return 2
    w = DnsWire(path, zone, log=logf)
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
