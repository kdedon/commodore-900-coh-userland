"""wire.py -- the host-side bridge that joins two C900 emulators' serial lines.

    python3 wire.py <socket-path> [--trace] [--cut] [--log FILE]

The emulator's `--wire=PATH' attaches SCC channel A -- the guest's /dev/tty51,
which is the line /etc/rc.net gives to SLIP -- to an AF_UNIX stream socket.  This
program is what those two sockets meet in: it listens, accepts exactly two
connections, and copies bytes from each to the other.  Two emulators started
against one socket path are then two machines on one point-to-point serial link.

WHY A RELAY AND NOT A socketpair.  Nothing about the link needs a process in the
middle; what needs one is the evidence.  Everything on this wire is SLIP, and a
run that produces no traffic looks exactly like a run whose guests never started
their stacks unless something was watching the wire itself.  So the relay counts
bytes and frames per direction, decodes each SLIP frame as IP, and can dump the
raw bytes -- and its summary is what the harness quotes when a test fails.

--cut is the negative control: both ends attach and the guests see an idle line,
but nothing crosses.  A test that still passes with --cut is not testing the
network, and this is how that is proved rather than assumed.

The SLIP framing and packet description come from hostbuild/slipwire.py, so the
decode here is the same one the simulator-side SLIP harnesses use.
"""
import os
import select
import socket
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "hostbuild")))
from slipwire import SlipDecoder, describe          # noqa: E402


class Wire:
    def __init__(self, path, trace=False, cut=False, log=sys.stderr):
        self.path, self.trace, self.cut, self.log = path, trace, cut, log
        self.bytes = [0, 0]         # [A->B, B->A]
        self.frames = [0, 0]
        self.dec = [SlipDecoder(), SlipDecoder()]

    def say(self, s):
        self.log.write(s + "\n")
        self.log.flush()

    def run(self):
        if os.path.exists(self.path):
            os.unlink(self.path)
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        srv.bind(self.path)
        srv.listen(2)
        self.say("wire: listening on %s%s" % (self.path, " (CUT)" if self.cut else ""))

        ends = []
        while len(ends) < 2:
            c, _ = srv.accept()
            c.setblocking(False)
            ends.append(c)
            self.say("wire: end %d attached" % (len(ends) - 1))
        srv.close()

        try:
            while True:
                r, _, _ = select.select(ends, [], [], 1.0)
                for i, s in enumerate(ends):
                    if s not in r:
                        continue
                    data = s.recv(4096)
                    if not data:
                        self.say("wire: end %d closed" % i)
                        return
                    self.account(i, data)
                    if not self.cut:
                        try:
                            ends[1 - i].sendall(data)
                        except OSError as e:
                            self.say("wire: end %d send failed: %s" % (1 - i, e))
                            return
        finally:
            self.summary()

    def account(self, i, data):
        self.bytes[i] += len(data)
        if self.trace:
            self.say("wire %s raw %s" % ("AB"[i], data.hex()))
        for pkt in self.dec[i].feed(data):
            self.frames[i] += 1
            self.say("wire %s %s" % ("A->B" if i == 0 else "B->A", describe(pkt)))

    def summary(self):
        self.say("wire: A->B %d bytes %d frames | B->A %d bytes %d frames%s"
                 % (self.bytes[0], self.frames[0], self.bytes[1], self.frames[1],
                    "  (CUT: nothing was relayed)" if self.cut else ""))


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    opts = [a for a in argv[1:] if a.startswith("--")]
    if len(args) != 1:
        sys.stderr.write(__doc__)
        return 2
    logf = sys.stderr
    for o in opts:
        if o.startswith("--log="):
            logf = open(o[6:], "w")
    w = Wire(args[0], trace="--trace" in opts, cut="--cut" in opts, log=logf)
    try:
        w.run()
    except KeyboardInterrupt:
        w.summary()
    finally:
        try:
            os.unlink(w.path)
        except OSError:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
