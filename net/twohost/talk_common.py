"""talk_common.py -- the two-guest bring-up the talk and UDP harnesses share.

Boot both machines, log in, get a stack on each, and prove the wire carries ICMP
before anything else is asked of it.  Split out for the reason hunt_common.py
was: two harnesses that answer different questions about the same machines must
bring them up identically, or comparing their results proves nothing.
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import twohost as T                                           # noqa: E402


def readdress(g, addr, mask):
    """Change a RUNNING stack's interface address, and check it took.

    This is an ordinary `ifconfig' on a machine whose /etc/inet has been serving
    for minutes -- the case a person is in when they renumber a host, and the
    case that used to lose all UDP on this stack (FINDINGS T-51): udp and tcp
    each kept a copy of the address taken when their port started, so the
    pseudo-header checksum and the `is this datagram for me' test both went on
    using an address the machine no longer had.  Nothing here works around it
    any more -- there was a restack() in this file that killed the stack and
    started it again so the new inet would LEARN the address, and a workaround
    living in a harness is how a defect stays invisible to everybody else.
    inet/generic/udp.c and tcp.c now read the address live, and this function
    exists to keep asking them to.

    The readback is not ceremony: the ioctl returning 0 says the daemon accepted
    the request, not that ip_port ended up holding it.
    """
    g.line("/etc/ifconfig %s %s" % (addr, mask))
    if not g.expect("# ", 300, "(ifconfig %s)" % addr):
        return False
    m = g.mark()
    g.line("/etc/ifconfig")
    g.expect("# ", 120, "(ifconfig readback)")
    if ("address " + addr) not in g.since(m).split("\n", 1)[-1]:
        T.say("%s: the interface does not hold %s" % (g.name, addr))
        return False
    T.say("%s: interface readdressed to %s" % (g.name, addr))
    return True


def setup(tag, dist, cut, keep):
    """Two booted, logged-in, networked guests, or an exit status.

    Returns a dict on success: A, B, guests, work, wire, why (an empty list the
    caller appends its own failures to).
    """
    work = os.path.join(os.environ.get("TMPDIR", "/tmp"),
                        "c900-%s.%d" % (tag, os.getpid()))
    os.makedirs(work)
    T.say("workdir %s" % work)
    src = T.dist_image(dist)
    if not src:
        return 2
    if not T.EMU or not os.path.exists(T.EMU):
        T.say("no emulator, and two of them are needed to run this harness.")
        T.say("  see twohost.py for where it is looked for and what to clone")
        return 2

    imgs = {}
    for n in ("A", "B"):
        imgs[n] = os.path.join(work, "%s.bin" % n)
        subprocess.call(["cp", "--reflink=auto", src, imgs[n]])

    sock = os.path.join(work, "wire.sock")
    wcmd = [sys.executable, os.path.join(HERE, "wire.py"), sock,
            "--log=%s" % os.path.join(work, "wire.log")]
    if cut:
        wcmd.append("--cut")
    wire = subprocess.Popen(wcmd)
    for _ in range(100):
        if os.path.exists(sock):
            break
        time.sleep(0.1)

    guests, why = {}, []
    for n in ("A", "B"):
        guests[n] = T.Guest(n, imgs[n], sock, work, False)
    A, B = guests["A"], guests["B"]

    if not (T.boot(A) and T.boot(B)):
        why.append("a guest did not reach a root shell")
        return T.report(False, why, work, wire, guests, keep)

    if not T.ensure_stack(B, T.ADDR_B):
        why.append("B's network did not come up")
    # Both images ship IFADDR=10.0.0.2 and /etc/rc.net configures it at boot, so
    # A has to be renumbered on a stack that is already running.  That is a
    # plain ensure_stack -- see readdress() for what used to be needed instead.
    if not T.ensure_stack(A, T.ADDR_A):
        why.append("A's network did not come up")
    if why:
        return T.report(False, why, work, wire, guests, keep)

    # ICMP first.  A daemon that never answers and a wire that never carried a
    # packet look identical from a client's screen, and this separates them.
    pings = 0
    for attempt in range(6):
        m = A.mark()
        A.line("/bin/ping -c 3 -w 5 %s" % T.ADDR_B)
        A.expect("packets transmitted", 180, "(ping summary)")
        A.expect("# ", 180)
        pings = A.since(m).count("bytes from")
        T.say("A: ping attempt %d -> %d replies" % (attempt + 1, pings))
        if pings:
            break
    if not pings:
        why.append("no ICMP replies from %s -- the wire is not carrying" % T.ADDR_B)
        return T.report(False, why, work, wire, guests, keep)

    return {"A": A, "B": B, "guests": guests, "work": work, "wire": wire,
            "why": why}
