"""selftest.py -- is dnswire.py itself right?

    python3 selftest.py

A fake guest, on the host, that speaks SLIP/IP down the same AF_UNIX socket an
emulator would attach to, and checks that the nameserver at the far end answers
correctly: ICMP echo, an A record, NXDOMAIN, a name it must stay silent about,
the truncation bit, a PTR, and a whole TCP handshake carrying a length-prefixed
query and its answer.

WHY IT EXISTS.  Every criterion in resolvcheck.py is a statement about the
guest, and every one of them is only as good as the peer the guest was talking
to.  A nameserver whose answers are malformed makes a working resolver look
broken -- and that reads as a bug in the port, which is the most expensive kind
of wrong answer this tree produces.  This asks about the instrument, needs no
emulator and no image, and takes a second.
"""
import os
import socket
import subprocess
import sys
import shutil
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "..",
                                                 "hostbuild")))
sys.path.insert(0, HERE)
from slipwire import (slip_encode, SlipDecoder, udp_dgram, tcp_seg,
                      Tcp, Udp, echo_request)
from dnswire import encode_name, decode_name, Question

WORK = tempfile.mkdtemp(prefix="dnswire-selftest.", dir="/tmp")  # short: AF_UNIX
SOCK = os.path.join(WORK, "t.sock")
LOG = os.path.join(WORK, "t.log")

srv = subprocess.Popen([sys.executable, os.path.join(HERE, "dnswire.py"), SOCK,
                        "--log=" + LOG,
                        "--zone=dnsx.localnet=10.0.0.77",
                        "--trunc=tcx.localnet=10.0.0.88",
                        "--silent=silentx",
                        "--ptr=10.0.0.77=dnsx.localnet"])
for _ in range(100):
    if os.path.exists(SOCK):
        break
    time.sleep(0.1)
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(SOCK)
dec = SlipDecoder()


def send(pkt):
    s.sendall(bytes(slip_encode(pkt)))


def get(timeout=3):
    s.settimeout(timeout)
    out = []
    end = time.time() + timeout
    while time.time() < end:
        try:
            d = s.recv(4096)
        except socket.timeout:
            break
        if not d:
            break
        out += dec.feed(d)
        if out:
            time.sleep(0.2)
            try:
                s.settimeout(0.2)
                d = s.recv(4096)
                out += dec.feed(d)
            except socket.timeout:
                pass
            break
    return out


def query(name, qtype=1, qid=b"\x12\x34", rd=True):
    return (qid + bytes([0x01 if rd else 0, 0]) + b"\x00\x01\x00\x00\x00\x00\x00\x00"
            + encode_name(name) + qtype.to_bytes(2, "big") + b"\x00\x01")


fails = 0


def ck(what, ok):
    global fails
    print("  %-50s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        fails += 1


# ICMP
send(echo_request(7))
p = get()
ck("icmp echo reply", any(len(x) >= 28 and x[9] == 1 and x[20] == 0 for x in p))

# A record
send(udp_dgram(1234, 53, query("dnsx.localnet")))
p = get()
ok = False
for x in p:
    if x[9] == 17:
        u = Udp(x)
        m = u.data
        q = Question(m)
        an = (m[6] << 8) | m[7]
        ok = q.name == "dnsx.localnet" and an == 1 and m[-4:] == bytes([10, 0, 0, 77])
ck("A record answered 10.0.0.77", ok)

# NXDOMAIN
send(udp_dgram(1235, 53, query("nope.localnet")))
p = get()
ok = any(x[9] == 17 and (Udp(x).data[3] & 0x0F) == 3 for x in p)
ck("unknown name -> NXDOMAIN", ok)

# silent
send(udp_dgram(1236, 53, query("silentx.localnet")))
p = get(1.5)
ck("silent name -> nothing at all", not p)

# truncated over UDP
send(udp_dgram(1237, 53, query("tcx.localnet")))
p = get()
ok = False
for x in p:
    if x[9] == 17:
        m = Udp(x).data
        ok = bool(m[2] & 0x02) and ((m[6] << 8) | m[7]) == 0
ck("truncated name -> TC bit, no records", ok)

# PTR
send(udp_dgram(1238, 53, query("77.0.0.10.in-addr.arpa", qtype=12)))
p = get()
ok = False
for x in p:
    if x[9] == 17:
        m = Udp(x).data
        if ((m[6] << 8) | m[7]) == 1:
            q = Question(m)
            off = 12 + len(encode_name(q.name)) + 4
            nm, _ = decode_name(m, off + 12)
            ok = nm == "dnsx.localnet"
ck("PTR answered dnsx.localnet", ok)

# TCP: handshake, length-prefixed query, length-prefixed answer
sp, seq = 4000, 1000
send(tcp_seg(sp, 53, seq, 0, 2, src="10.0.0.2", dst="10.0.0.1"))
p = get()
sa = None
for x in p:
    if x[9] == 6:
        t = Tcp(x)
        if t.flags & 2 and t.flags & 16:
            sa = t
ck("tcp SYN -> SYN|ACK", sa is not None)
if sa:
    seq += 1
    ack = (sa.seq + 1) & 0xFFFFFFFF
    send(tcp_seg(sp, 53, seq, ack, 16, src="10.0.0.2", dst="10.0.0.1"))
    m = query("tcx.localnet")
    send(tcp_seg(sp, 53, seq, ack, 16 | 8, len(m).to_bytes(2, "big") + m,
                 src="10.0.0.2", dst="10.0.0.1"))
    p = get()
    data = b""
    for x in p:
        if x[9] == 6:
            data += Tcp(x).data
    ok = False
    if len(data) > 2:
        n = (data[0] << 8) | data[1]
        msg = data[2:2 + n]
        ok = ((msg[6] << 8) | msg[7]) == 1 and msg[-4:] == bytes([10, 0, 0, 88])
    ck("tcp query -> full answer 10.0.0.88", ok)

s.close()
srv.terminate()
srv.wait()
if fails:
    print("--- the log the cases were judged against ---")
    print(open(LOG).read())
print("=== dnswire selftest: %s" % ("PASS" if not fails else "FAIL"))
shutil.rmtree(WORK, ignore_errors=True)
sys.exit(1 if fails else 0)
