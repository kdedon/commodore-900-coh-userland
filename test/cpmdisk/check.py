#!/usr/bin/env python3
# Copyright (c) 2026 Kevin Dedon.
# SPDX-License-Identifier: MIT
"""check.py - cpm(1) against CP/M drive images this program formats itself.

Usage: check.py <workdir> <cpm-binary>

A drive A: image is a sparse file of 2560 blocks of 4096 bytes, the first 4
the directory, all 0xE5 when empty.  A CP/M 3 drive adds a type-0x20 label
and an SFCB in every fourth slot.  This lays those down; cpm(1) does the
rest.

The directory is decoded here independently of cpm.c, from the format:

  - 32-byte entries, 512 of them in the first 4 blocks (16 KB); a type byte
    of 0xE5 is free, 0x00-0x0F a user's FCB, 0x10-0x1F an XFCB, 0x20 the
    label, 0x21 an SFCB for the three entries before it.
  - EX at 12, S2 at 14, RC at 15; logical extent = ((S2&0x3f)<<5)|(EX&0x1f);
    an entry's last record is extent*128 + RC.  AL is eight 16-bit
    little-endian block numbers of 4096 bytes.
  - a stamp is a little-endian day count from 1977-12-31, then BCD hour and
    BCD minute; an SFCB sub-record is create 4, update 4, password mode 1,
    reserved 1.
"""

import datetime
import os
import re
import shutil
import subprocess
import sys

ENTSIZE = 32
NENT = 512
DIRBYTES = NENT * ENTSIZE
BLS = 4096
DSM = 2559
DISKBYTES = (DSM + 1) * BLS
RECLEN = 128

T_FREE = 0xe5
T_XFCB = 0x10
T_LABEL = 0x20
T_SFCB = 0x21

DL_UPDATE = 0x20
DL_CREATE = 0x10
DL_EXISTS = 0x01

WORK = None
CPMBIN = None
failures = []
checks = 0


# ---- harness -----------------------------------------------------------------

def check(cond, what):
    global checks
    checks += 1
    if cond:
        print("ok    %s" % what)
    else:
        failures.append(what)
        print("FAIL  %s" % what)


def cpm(img, *args, **kw):
    """Run cpm(1) on <img>; (rc, output).  A failure is a FAIL unless the
    caller says it expects one."""
    argv = [CPMBIN, '-f', path(img)] + list(args)
    p = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = p.stdout.decode('utf-8', 'replace')
    if p.returncode != 0 and not kw.get('may_fail'):
        check(False, "cpm %s exits 0\n%s" % (' '.join(args), out))
    return p.returncode, out


def path(name):
    return os.path.join(WORK, name)


def image(name):
    with open(path(name), 'rb') as f:
        return f.read()


def copy(src, dst):
    shutil.copyfile(path(src), path(dst))


def hostfile(name, data):
    with open(path(name), 'wb') as f:
        f.write(data)
    return path(name)


def cmpdata(got, want):
    """Equal up to the record padding CP/M adds: NULs, or ^Z for text."""
    return (got[:len(want)] == want and len(got) - len(want) < RECLEN
            and set(got[len(want):]) <= {0, 0x1a})


# ---- formatting a drive ------------------------------------------------------

def mkdisk(name, label=None, stamped=False):
    """A formatted, empty drive of the C900 geometry, optionally carrying a
    CP/M 3 directory label and SFCB slots."""
    d = bytearray([T_FREE]) * DIRBYTES
    if label is not None:
        e = bytearray(32)
        e[0] = T_LABEL
        e[1:12] = ("%-8s%-3s" % (label, '')).encode()
        e[12] = DL_EXISTS | DL_CREATE | DL_UPDATE
        d[0:ENTSIZE] = e
    if stamped:
        for i in range(3, NENT, 4):
            s = bytearray(32)
            s[0] = T_SFCB
            d[i * ENTSIZE:(i + 1) * ENTSIZE] = s
    with open(path(name), 'wb') as f:
        f.write(d)
        f.truncate(DISKBYTES)
    return name


# ---- the format, straight off the image --------------------------------------

def ent(img, i):
    return img[i * ENTSIZE:(i + 1) * ENTSIZE]


def name11(e):
    return bytes(b & 0x7f for b in e[1:12])


def prname(n):
    base, ext = n[:8].decode().rstrip(), n[8:].decode().rstrip()
    return base + ('.' + ext if ext else '')


def exttotal(e):
    return ((e[14] & 0x3f) << 5) | (e[12] & 0x1f)


def alloc(e):
    return [e[16 + 2 * j] | (e[17 + 2 * j] << 8) for j in range(8)]


def fcbs(img):
    return [i for i in range(NENT) if ent(img, i)[0] < 0x10]


def files(img):
    """{(user, name): {'size', 'blocks', 'nent', 'data', 'first'}}"""
    by = {}
    for i in fcbs(img):
        e = ent(img, i)
        by.setdefault((e[0], prname(name11(e))), []).append((exttotal(e), i))
    out = {}
    for key, lst in by.items():
        lst.sort()
        size, blocks, data = 0, 0, b''
        for x, i in lst:
            e = ent(img, i)
            size = max(size, (x * RECLEN + e[15]) * RECLEN)
            for b in alloc(e):
                if b:
                    blocks += 1
                    data += img[b * BLS:(b + 1) * BLS]
        out[key] = dict(size=size, blocks=blocks, nent=len(lst),
                        data=data[:size], first=lst[0][1])
    return out


def extensions(img):
    """Every entry that is neither a file nor free, as {index: bytes}."""
    return dict((i, ent(img, i)) for i in range(NENT)
                if ent(img, i)[0] >= 0x10 and ent(img, i)[0] != T_FREE)


def subrecord(img, i):
    """Entry i's SFCB sub-record, or None if its group carries no SFCB."""
    if i % 4 == 3 or ent(img, i | 3)[0] != T_SFCB:
        return None
    s = ent(img, i | 3)
    return s[1 + 10 * (i & 3):11 + 10 * (i & 3)]


def stampdate(s):
    days = s[0] | (s[1] << 8)
    if days == 0:
        return None
    d = datetime.date(1977, 12, 31) + datetime.timedelta(days=days)
    return "%s %02x:%02x" % (d.isoformat(), s[2], s[3])


# ---- cpm(1)'s listing --------------------------------------------------------

FILEROW = re.compile(r'^\s*(\d+) (\S+)\s+(\d+) bytes\s+(\d+) blocks\s+'
                     r'(\d+) entr\w+\s*(.*)$')
STAMP = re.compile(r'\d{4}-\d\d-\d\d \d\d:\d\d|-')


def listing(out):
    """{(user, name): (size, blocks, nent, [create, update] or [])}"""
    rows = {}
    for line in out.splitlines():
        m = FILEROW.match(line)
        if m:
            rows[(int(m.group(1)), m.group(2))] = (
                int(m.group(3)), int(m.group(4)), int(m.group(5)),
                STAMP.findall(m.group(6)))
    return rows


def agree(imgname, what):
    """cpm ls and the directory itself name the same files, sizes, block
    counts and entry counts."""
    img = image(imgname)
    _, out = cpm(imgname, 'ls')
    got = dict((k, v[:3]) for k, v in listing(out).items())
    want = dict((k, (f['size'], f['blocks'], f['nent']))
                for k, f in files(img).items())
    check(got == want and want != {},
          "%s: cpm ls lists exactly the directory's files, sizes, "
          "blocks and entries" % what)
    return out


# ---- a populated drive -------------------------------------------------------

# A spread of sizes: under a record, over a record, over a 16 KB logical
# extent (raw extent 1 on the first entry), and over a 32 KB entry.
SYSTEM = {
    'CPM.SYS': bytes(range(256)) * 300,          # 76800 B: three entries
    'CCP.COM': bytes(range(251)) * 70,           # 17570 B: two logical exts
    'PIP.COM': bytes(range(97)) * 40,            # 3880 B
    'READ.ME': b'a self-formatted drive\n' * 9,  # text
    'TINY.DAT': b'\x7f',
}


def populate(imgname, what=None):
    for name, data in sorted((what or SYSTEM).items()):
        cpm(imgname, 'write', hostfile(name.lower().replace('.', '_'), data),
            name)


# ---- cases -------------------------------------------------------------------

def t_drive(imgname, label, stamped):
    """cpm(1) fills a freshly formatted drive and reads it back unchanged."""
    mkdisk(imgname, label=label, stamped=stamped)
    _, out = cpm(imgname, 'ls')
    check(listing(out) == {}, "%s: a formatted drive lists no files"
          % imgname)
    check(re.search(r'^label\s+%s\s' % label, out, re.M) is not None,
          "%s: cpm ls shows the label %s of an empty drive"
          % (imgname, label))
    populate(imgname)

    before = image(imgname)
    out = agree(imgname, imgname)
    check(re.search(r'^label\s+%s\s' % label, out, re.M) is not None,
          "%s: cpm ls shows the label %s" % (imgname, label))
    if stamped:
        check(all(ent(before, i)[0] == T_SFCB for i in range(3, NENT, 4)),
              "%s: drive is stamped (every 4th entry an SFCB)" % imgname)
        check('128 SFCB entries' in out,
              "%s: cpm ls counts the 128 SFCBs" % imgname)
    else:
        check('SFCB' not in out,
              "%s: an unstamped drive reports no SFCBs" % imgname)

    bad = []
    for (user, name), f in sorted(files(before).items()):
        if user != 0:
            continue
        rc, _ = cpm(imgname, 'read', name, path('rd.out'), may_fail=True)
        with open(path('rd.out'), 'rb') as fh:
            got = fh.read()
        if rc != 0 or got != f['data'] or not cmpdata(got, SYSTEM[name]):
            bad.append(name)
    check(bad == [], "%s: cpm read returns every file's blocks exactly%s"
          % (imgname, '' if not bad else ' (not: %s)' % ' '.join(bad)))
    check(image(imgname) == before,
          "%s: ls and read leave the image byte-identical" % imgname)


def inject_oddities(imgname):
    """An XFCB and a type the format does not define, in non-SFCB slots."""
    with open(path(imgname), 'r+b') as f:
        d = bytearray(f.read(DIRBYTES))
        free = [i for i in range(NENT)
                if d[i * ENTSIZE] == T_FREE and i % 4 != 3]
        x = bytearray(32)
        x[0] = T_XFCB
        x[1:12] = b'HELLO   TXT'
        x[12] = 0x80
        x[16:24] = bytes(range(0x40, 0x48))
        d[free[0] * ENTSIZE:(free[0] + 1) * ENTSIZE] = x
        u = bytearray(32)
        u[0] = 0x30
        u[1:12] = b'FUTURE  ???'
        d[free[1] * ENTSIZE:(free[1] + 1) * ENTSIZE] = u
        f.seek(0)
        f.write(d)


NEW = {
    'BIG.DAT': bytes(range(256)) * 160,          # 40960 B: two entries
    'EDGE.DAT': bytes(range(256)) * 64,          # 16384 B: a whole extent
    'NOTE.TXT': b'written by cpm(1)\n' * 30,     # text: ^Z padded
    'SMALL.BIN': b'\x01\x02\x03',
}


def t_edits():
    """write, overwrite and rm on a stamped, populated drive A: keep the
    other files, the extension entries and CP/M's encodings."""
    copy('cpma.img', 'edit.img')
    inject_oddities('edit.img')
    img0 = image('edit.img')
    ext0 = extensions(img0)
    orig = files(img0)
    victim = sorted(k for k in orig if k[0] == 0)[0]
    over = sorted(k for k in orig if k[0] == 0)[-1]

    for name, data in sorted(NEW.items()):
        cpm('edit.img', 'write', hostfile(name.lower(), data), name)
    cpm('edit.img', 'write', hostfile('over.bin', b'replaced\n'), over[1])
    cpm('edit.img', 'rm', victim[1])
    img = image('edit.img')
    now = files(img)

    for name, data in sorted(NEW.items()):
        f = now.get((0, name))
        check(f is not None and cmpdata(f['data'], data),
              "write %s: its blocks on disk hold what was written" % name)
        rc, _ = cpm('edit.img', 'read', name, path('rd.out'))
        with open(path('rd.out'), 'rb') as fh:
            check(cmpdata(fh.read(), data),
                  "read %s: returns what was written" % name)
    check(now[(0, 'BIG.DAT')]['nent'] == 2,
          "a 40960-byte file takes two directory entries")
    e = ent(img, now[(0, 'EDGE.DAT')]['first'])
    check(exttotal(e) == 0 and e[15] == 0x80,
          "a file ending on a 16 KB boundary is EX=0 RC=0x80, "
          "the encoding the BDOS writes")
    check(now.get(over) is not None
          and cmpdata(now[over]['data'], b'replaced\n')
          and len([i for i in fcbs(img)
                   if prname(name11(ent(img, i))) == over[1]]) == 1,
          "write over an existing name replaces it, leaving one file")

    check(victim not in now and ent(img, orig[victim]['first'])[0] == T_FREE,
          "rm %s: its FCB is free" % victim[1])
    check(subrecord(img, orig[victim]['first']) == b'\0' * 10,
          "rm %s: its SFCB sub-record is zero" % victim[1])
    kept = [k for k in orig if k not in (victim, over)]
    check(all(now[k]['data'] == orig[k]['data'] for k in kept),
          "every other file still reads back byte-identical")

    ext = extensions(img)
    check(set(ext) == set(ext0),
          "the same set of extension entries survives write/rm")
    check(all(ext[i] == ext0[i] for i in ext0 if ext0[i][0] != T_SFCB),
          "label, XFCB and unknown-type entry are byte-identical")
    check(all(i % 4 != 3 for i in fcbs(img)),
          "no file entry lands in an SFCB slot")

    today = datetime.date.today()
    near = [(today + datetime.timedelta(days=n)).isoformat()
            for n in (-1, 0, 1)]
    _, out = cpm('edit.img', 'ls')
    rows = listing(out)
    for name in sorted(NEW):
        s = subrecord(img, now[(0, name)]['first'])
        c, u = (stampdate(s[0:4]), stampdate(s[4:8])) if s else (None, None)
        check(c is not None and c[:10] in near and u is not None
              and u[:10] in near,
              "write %s: stamped create and update with today's date" % name)
        check(rows.get((0, name), (0, 0, 0, []))[3] == [c or '-', u or '-'],
              "ls %s: shows the stamps the SFCB holds" % name)
    # BIG.DAT is over 16 KB, so its first entry carries raw extent 1 with
    # EXM=1: ls must find its stamps by entry, not by a raw extent of 0.
    e = ent(img, now[(0, 'BIG.DAT')]['first'])
    check(e[12] & 0x1f == 1,
          "the first entry of a 40960-byte file has raw extent 1")

    # A stamp must not outlive its file: the next file in that slot would
    # inherit it.
    cpm('edit.img', 'write', hostfile('gone.txt', b'gone\n'), 'GONE.TXT')
    i = files(image('edit.img'))[(0, 'GONE.TXT')]['first']
    stamped = subrecord(image('edit.img'), i) != b'\0' * 10
    cpm('edit.img', 'rm', 'GONE.TXT')
    img = image('edit.img')
    check(stamped and ent(img, i)[0] == T_FREE
          and subrecord(img, i) == b'\0' * 10 and ent(img, i | 3)[0] == T_SFCB,
          "rm of a stamped file frees its FCB, clears its sub-record, "
          "keeps the SFCB")

    out = agree('edit.img', 'after write/rm')
    check(re.search(r'^password\s+0 HELLO\.TXT', out, re.M) is not None,
          "cpm ls reports the XFCB without interpreting it")
    check('unknown' in out and 'type 0x30' in out,
          "cpm ls reports the unknown-type entry as preserved")


def t_full():
    """A stamped directory holds 384 files, and a write past that is refused
    with nothing changed."""
    mkdisk('full.img', label='C900A', stamped=True)
    img = image('full.img')
    slots = len([i for i in range(NENT)
                 if ent(img, i)[0] == T_FREE and i % 4 != 3])
    used = set(b for i in fcbs(img) for b in alloc(ent(img, i)) if b)
    blocks = 2560 - 4 - len(used)
    check(blocks > slots, "setup: blocks outrun directory slots (%d > %d)"
          % (blocks, slots))
    tiny = hostfile('tiny.bin', b'x')
    n = 0
    while n <= slots:
        rc, out = cpm('full.img', 'write', tiny, 'F%03d.BIN' % n,
                      may_fail=True)
        if rc != 0:
            break
        n += 1
    check(n == slots, "cpm write fills exactly the %d free file slots (%d)"
          % (slots, n))
    img = image('full.img')
    check(all(ent(img, i)[0] == T_SFCB for i in range(3, NENT, 4)),
          "every SFCB is still an SFCB on a full directory")
    check(len(fcbs(img)) + 1 == 384,
          "file entries plus the label are 384")
    rc, _ = cpm('full.img', 'write', tiny, 'OVER.BIN', may_fail=True)
    check(rc != 0 and image('full.img') == img,
          "a write to a full directory fails and changes nothing")


def t_legacy():
    """With no label and no SFCBs, drive A: is a CP/M 2.2 directory and
    cpm(1) keeps it one."""
    mkdisk('legacy.img')
    populate('legacy.img')
    img0 = image('legacy.img')
    check(extensions(img0) == {} and files(img0) != {},
          "a plain 2.2 drive fills up with no extension entries")
    victim = sorted(k for k in files(img0) if k[0] == 0)[0]
    cpm('legacy.img', 'write', hostfile('big.dat', NEW['BIG.DAT']), 'BIG.DAT')
    cpm('legacy.img', 'rm', victim[1])
    img = image('legacy.img')
    check(extensions(img) == {},
          "write/rm on an unstamped drive invent no extension entries")
    _, out = cpm('legacy.img', 'ls')
    check('label' not in out and 'stamps' not in out
          and all(r[3] == [] for r in listing(out).values()),
          "cpm ls on an unstamped drive shows no label and no stamps")
    now = files(img)
    check((0, 'BIG.DAT') in now and victim not in now
          and cmpdata(now[(0, 'BIG.DAT')]['data'], NEW['BIG.DAT']),
          "write/rm took effect on the unstamped drive")
    agree('legacy.img', 'unstamped, after write/rm')


def main():
    global WORK, CPMBIN
    if len(sys.argv) != 3:
        sys.exit("usage: check.py <workdir> <cpm-binary>")
    WORK, CPMBIN = sys.argv[1], os.path.abspath(sys.argv[2])

    cases = [
        (t_drive, ('cpma.img', 'C900A', True)),
        (t_drive, ('cpmb.img', 'C900B', False)),
        (t_edits, ()),
        (t_full, ()),
        (t_legacy, ()),
    ]
    for t, args in cases:
        print("\n-- %s%s: %s" % (t.__name__, args[:1] and ' ' + args[0],
                                t.__doc__.splitlines()[0]))
        t(*args)

    print("\n%d checks, %d failures" % (checks, len(failures)))
    if failures:
        for f in failures:
            print("  FAILED: %s" % f.splitlines()[0])
        print("cpmdisk: FAIL")
        sys.exit(1)
    print("cpmdisk: PASS")


if __name__ == '__main__':
    main()
