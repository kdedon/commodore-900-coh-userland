"""slipwire.py -- the host end of the C900's serial line, as a library.

SLIP framing (RFC 1055), IP/UDP/TCP checksums, packet construction and packet
description: everything a host-side peer on the guest's /dev/tty51 needs that
does not depend on how the bytes reach the line.  net/twohost's wire and the
resolver and SNTP peers under net/test build on it.

The host is 10.0.0.1 and the guest 10.0.0.2.  SLIP frames are delimited by
0xC0 and escape with 0xDB, so whatever carries the bytes must carry both values
intact in both directions.
"""
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
