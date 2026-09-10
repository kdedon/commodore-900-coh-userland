#!/usr/bin/env python3
"""dns-test.py -- is the guest actually resolving names on the network?

    python3 dns-test.py [dist]

Cold-boots a dist, takes it to multi-user with a Ctrl-D, and then acts as the
NAMESERVER at the far end of the SLIP link -- 10.0.0.1, the address
/etc/resolv.conf names.  The guest is asked to look up a name that is in no
file it has, so the only way it can be answered is a DNS query crossing the
serial line and an answer coming back.

That is the difference between this test and telnetd-test.py.  Anything the
guest can find in /etc/hosts is found before netdb.c reaches the resolver at
all, so a lookup of a local name proves nothing about the DNS however well it
works.  The name here (`dnstest') is deliberately absent from /etc/hosts.

The responder answers whatever QNAME it is asked for, rather than matching a
name of its own.  Matching would put the resolver's search-list behaviour --
whether the query goes out as `dnstest' or `dnstest.localnet' -- between the
test and its result, so a working link would fail the test whenever the search
rules were not what the harness guessed.  Mirroring the question means the run
can only fail for a reason that matters, and the QNAME that arrived is printed
so the search behaviour is still visible.

Everything about the wire, the framing, the boot and the console is
slipwire.py; this file is the nameserver and the test.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "hostbuild"))

from slipwire import (Guest, SlipDecoder, Udp, echo_request,   # noqa: E402
                      ip4, is_echo_reply, send_packet, typeline,
                      udp_dgram, wire_drain)

DIST = sys.argv[1] if len(sys.argv) > 1 else "coherent3-full-test"

# The name asked for, and the address handed back.  The address is one nothing
# else in the system uses, so finding it on the guest's console cannot have
# come from anywhere but this responder.
QUERY_NAME = "dnstest"
ANSWER_ADDR = "10.0.0.9"

DNS_PORT = 53


# -- the nameserver -----------------------------------------------------------
def qname_end(msg, off):
    """Offset just past the QNAME starting at `off'.

    Uncompressed labels only: a QUESTION section is never compressed, and a
    pointer here would mean the query was malformed rather than that this
    parser is too simple."""
    while off < len(msg) and msg[off]:
        if msg[off] & 0xC0:
            return -1
        off += 1 + msg[off]
    return off + 1


def qname_text(msg, off):
    parts = []
    while off < len(msg) and msg[off]:
        n = msg[off]
        parts.append(msg[off + 1:off + 1 + n].decode("ascii", "replace"))
        off += 1 + n
    return ".".join(parts)


def dns_answer(query, addr):
    """An authoritative A-record reply to `query', or None if it is not a
    question this responder can answer.

    The question section is copied back verbatim and the answer names it with a
    compression pointer to offset 12, which is where every question section
    starts.  Returning the question unchanged is what lets the resolver match
    the reply to what it asked."""
    if len(query) < 17:
        return None, "short message (%d bytes)" % len(query)
    qdcount = (query[4] << 8) | query[5]
    if query[2] & 0x80 or qdcount != 1:
        return None, "not a single-question query"
    end = qname_end(query, 12)
    if end < 0 or end + 4 > len(query):
        return None, "malformed question"
    name = qname_text(query, 12)
    qtype = (query[end] << 8) | query[end + 1]
    qclass = (query[end + 2] << 8) | query[end + 3]
    if qtype != 1 or qclass != 1:
        return None, "%s: type %d class %d, not an Internet A record" \
                     % (name, qtype, qclass)

    hdr = bytearray(query[0:2])
    hdr += bytes([0x85, 0x80])          # QR AA RD, RA, RCODE 0
    hdr += b"\x00\x01\x00\x01\x00\x00\x00\x00"
    rr = b"\xC0\x0C" + b"\x00\x01\x00\x01" + b"\x00\x00\x0E\x10" \
        + b"\x00\x04" + ip4(addr)
    return bytes(hdr) + query[12:end + 4] + rr, name


def udp_reply(u, payload):
    """Back to the port the query came from, from port 53."""
    return udp_dgram(DNS_PORT, u.sport, payload)


def main():
    with Guest(DIST, "dns") as g:
        g.boot()
        g.multiuser()

        dec = SlipDecoder()
        wire_drain()

        # The boot must have finished bringing the stack up before a query can
        # cross the line, and rc.net runs in the background -- so wait for the
        # wire to work rather than for a fixed time.  A ping that comes back
        # proves inet, ifconfig and slip all did their job; without this the
        # run could report "no query ever arrived" for a machine that was
        # merely still starting.
        print("waiting for the boot to put the machine on the wire ...")
        up = False
        for seq in range(1, 13):
            send_packet(echo_request(seq))
            for _ in range(30):
                time.sleep(1)
                for pkt in dec.feed(wire_drain()):
                    if is_echo_reply(pkt, seq):
                        up = True
                if up:
                    break
            if up:
                break
        if not up:
            g.pump()
            print("--- guest console ---")
            print("\n".join(g.text().splitlines()[-16:]))
            print("=== DNS: FAIL (the boot never put the machine on the wire;"
                  " this is netboot-test.py's failure, not a DNS one)")
            return 1
        print("the guest answers ICMP: the stack and slip are up")

        # Log in on the console and ask for the name.
        typeline("root")
        mark = len(g.text())
        for _ in range(180):
            time.sleep(1)
            g.pump()
            if "toolchain" in g.text()[mark:]:
                break
        else:
            print("=== DNS: FAIL (no console login)")
            return 1
        time.sleep(5)

        mark = len(g.text())
        typeline("/bin/host %s" % QUERY_NAME)

        # Serve.  The console is pumped in the same loop: host(1) prints its
        # answer there, and a loop that only watched the wire would see a
        # perfectly good query-and-answer exchange and never learn whether the
        # program made anything of it.
        queries, answered, refused = [], 0, []
        for _ in range(600):
            time.sleep(1)
            g.pump()
            for pkt in dec.feed(wire_drain()):
                if len(pkt) < 28 or pkt[9] != 17:
                    continue
                u = Udp(pkt)
                if u.dport != DNS_PORT:
                    continue
                reply, why = dns_answer(u.data, ANSWER_ADDR)
                if reply is None:
                    refused.append(why)
                    continue
                queries.append("%s (from port %d)" % (why, u.sport))
                send_packet(udp_reply(u, reply))
                answered += 1
            if ANSWER_ADDR in g.text()[mark:]:
                break

        print("--- queries this responder answered ---")
        for q in queries:
            print("    " + q)
        for r in refused:
            print("    NOT ANSWERED: " + r)
        if not queries and not refused:
            print("    (none: no UDP datagram for port 53 ever crossed the"
                  " line)")

        g.pump()
        print("--- guest console ---")
        print("\n".join(g.text().splitlines()[-25:]))

        passed = answered > 0 and ANSWER_ADDR in g.text()[mark:]
        print("=== DNS resolution over SLIP: %s (%d quer%s answered)"
              % ("PASS" if passed else "FAIL", answered,
                 "y" if answered == 1 else "ies"))
        return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
