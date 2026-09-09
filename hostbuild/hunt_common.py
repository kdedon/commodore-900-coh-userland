"""hunt_common.py -- what the hunt harnesses share.

A byte-exact console, a readable dump of what a curses program drew, and the
stack setup a hunt driver and client need.  Split out so `disco-test.py' -- ten
minutes, no game -- and `hunt-test.py' -- half an hour, a played game -- cannot
drift apart in how they bring the machine up.  If they did, comparing them
would prove nothing, and comparing them is the point.
"""
import time

from slipwire import (GUEST, Guest, console_bytes, get)


class Player(Guest):
    """A guest whose console is read BYTE-exact.

    Guest.pump() takes `ch_a', which is a JSON string and has been through a
    rune decode; hunt's output is termcap escape sequences and cursor
    addressing, where a byte above 0x7f is data and not a character.
    `ch_a_bytes' is the same channel without that loss, so the screen can be
    dumped as it was actually sent.
    """

    def __init__(self, dist, tag):
        Guest.__init__(self, dist, tag)
        self.raw = bytearray()

    def pump(self):
        try:
            b = bytes(get("/serial/recv-all").get("ch_a_bytes") or [])
        except Exception:
            return
        if b:
            self.raw.extend(b)
            self.console.append(b.decode("latin-1"))

    def keys(self, s, delay=0.15):
        """Type raw characters at the game -- no CR appended, no line
        discipline: in cbreak mode each one is a command by itself."""
        console_bytes([ord(c) for c in s], delay=delay)

    def done(self, mark, timeout=1800, echo=True):
        """Wait for the SHELL PROMPT, i.e. for the command to have finished.

        A fixed wall-clock window is the wrong clock: a guest that waits 30
        of ITS seconds takes about 25 times as many of ours on the simulator,
        so a window sized in wall time either gives up on a program still
        running or reports silence as failure.  The prompt is the guest's own
        statement that it is finished, and it costs nothing to wait for.

        Returns True if the prompt came back, False if the deadline passed --
        and a False here means the command really is stuck, which is a result
        rather than a harness artefact.

        `echo' prints each complete line as it arrives.  These runs last tens
        of minutes, and a harness that shows nothing until the end cannot be
        watched: a command that got three lines out before stalling looks
        exactly like one that never started, and which of those it was has
        been the whole question more than once.
        """
        shown = mark
        for _ in range(timeout):
            time.sleep(1)
            self.pump()
            if echo:
                text = self.text()
                cut = text.rfind("\n")
                if cut + 1 > shown:
                    for line in text[shown:cut].splitlines():
                        if line.strip():
                            print("    | %s" % line.rstrip())
                    shown = cut + 1
            if "# " in self.text()[mark:]:
                return True
        return False

    def expect(self, marker, timeout=180, mark=None):
        """Wait for `marker' to appear after `mark' (default: from now)."""
        if mark is None:
            mark = len(self.text())
        for _ in range(timeout):
            time.sleep(1)
            self.pump()
            if marker in self.text()[mark:]:
                print("ok: %r" % marker)
                return True
        print("--- console since the mark ---")
        print(visible(self.text()[mark:])[-600:])
        return False


def visible(s):
    """Escape sequences with the escapes shown, so a screen dump stays readable
    and a missing sequence is visible as a missing sequence."""
    out = []
    for c in s:
        n = ord(c)
        if c in "\n\t" or 32 <= n < 127:
            out.append(c)
        elif c == "\r":
            out.append("\n")
        elif n == 27:
            out.append("<ESC>")
        else:
            out.append("<%02x>" % n)
    return "".join(out)


def net_setup():
    """Enough stack for a client and a driver on the same machine.

    No slip: hunt never leaves the box.  psipping stays as the control -- if it
    fails, the fault is the stack and nothing hunt does afterwards is worth
    reading."""
    return [
        # /usr is its own partition and only /etc/rc mounts it, so in single
        # user there is no /usr/games at all -- and a missing hunt looks
        # exactly like a hunt that will not run.
        # The device differs by media: /usr is hd6 on the 42 MB Coherent-only
        # disk (hd42-coh) and hd1 on the 21 MB one.  Both nodes exist on both
        # disks, so the wrong one answers `badly formed file system' rather
        # than `no such device'; try each and let the wrong one fail.
        ("/etc/mount /dev/hd6 /usr", None),
        ("/etc/mount /dev/hd1 /usr", None),
        ("/etc/mknod /dev/inet p", None),
        ("/etc/inet &", "inet: ready"),
        ("/etc/ifconfig %s 255.255.255.0" % GUEST, None),
        ("/etc/ifconfig", "ip0: address %s" % GUEST),
        ("/bin/psipping 2 echo", "psipping: echo 2/2 correct"),
    ]


