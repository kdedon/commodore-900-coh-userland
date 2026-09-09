#!/usr/bin/env python3
"""inject.py [--mode OCTAL] <image> <partition-start-block> <hostfile> <guest-path> ...

Put host files inside one Coherent partition of a disk image, in place: the
contents of a guest path that exists are replaced, keeping its mode, and a
path that does not exist is created in its (existing) parent directory, mode
0644 unless --mode says otherwise.  With --mode, an existing path's mode is
set too.
Same filesystem knowledge as the distribution repository's inject-kernel.py --
this one takes a partition offset and walks directories, so it can reach
/drv/notty as well as /coherent.  Old blocks are leaked, which is fine for a
scratch boot image.

This is the writer for a packed COHERENT filesystem that the harnesses here
use: tests/privsep reaches it at ../rawalign/inject.py to stage privids over a
packed image, and tests/stackhw to put its probe on one.  The alignment test
this directory is named for (rawalign.c, run.sh) needs a relinked kernel, so
it lives in commodore-900-coh-kernel3's own test/rawalign; this file has no
kernel dependency of its own.
"""
import sys

BSIZE = 512
INODE_SIZE = 64


def u16(b, o): return b[o] | b[o + 1] << 8
def u32(b, o): return (b[o] | b[o + 1] << 8) << 16 | (b[o + 2] | b[o + 3] << 8)
def p16(v): return bytes((v & 0xFF, (v >> 8) & 0xFF))


def p32(v):
    hi, lo = (v >> 16) & 0xFFFF, v & 0xFFFF
    return bytes((hi & 0xFF, hi >> 8, lo & 0xFF, lo >> 8))


def l3get(b, o): return b[o] << 16 | b[o + 1] | b[o + 2] << 8
def l3put(v): return bytes(((v >> 16) & 0xFF, v & 0xFF, (v >> 8) & 0xFF))


class FS:
    def __init__(self, d, base):
        self.d = d
        self.base = base * BSIZE
        sb = self.blk(1)
        self.isize = u16(sb, 0)
        self.fsize = u32(sb, 2)

    def blk(self, n):
        o = self.base + n * BSIZE
        return self.d[o:o + BSIZE]

    def wblk(self, n, data):
        o = self.base + n * BSIZE
        self.d[o:o + len(data)] = data

    def inode_off(self, ino):
        return self.base + 2 * BSIZE + (ino - 1) * INODE_SIZE

    def alloc_block(self):
        sb = bytearray(self.blk(1))
        nfree = u16(sb, 6)
        if nfree == 0:
            raise RuntimeError("free list exhausted")
        nfree -= 1
        bno = u32(sb, 8 + 4 * nfree)
        if nfree == 0:
            if bno == 0:
                raise RuntimeError("out of disk space")
            nxt = self.blk(bno)
            nfree = u16(nxt, 0)
            sb[8:8 + 4 * 64] = nxt[2:2 + 4 * 64]
        sb[6:8] = p16(nfree)
        self.wblk(1, sb)
        if bno == 0 or bno >= self.fsize:
            raise RuntimeError("bad free block %d" % bno)
        self.wblk(bno, bytes(BSIZE))
        return bno

    def dirents(self, ino):
        ioff = self.inode_off(ino)
        size = u32(self.d, ioff + 8)
        addrs = [l3get(self.d, ioff + 12 + 3 * i) for i in range(13)]
        pos = 0
        for a in addrs[:10]:
            if a == 0 or pos >= size:
                break
            blk = self.blk(a)
            for e in range(0, BSIZE, 16):
                if pos + e >= size:
                    break
                n = u16(blk, e)
                nm = bytes(blk[e + 2:e + 16]).split(b'\0')[0].decode('latin1')
                if n:
                    yield n, nm
            pos += BSIZE

    def lookup(self, path):
        ino = 2
        for part in path.strip('/').split('/'):
            for n, nm in self.dirents(ino):
                if nm == part:
                    ino = n
                    break
            else:
                raise RuntimeError("%s: no %s" % (path, part))
        return ino

    def alloc_inode(self):
        """A free inode, from the superblock cache or by scanning the table."""
        sb = bytearray(self.blk(1))
        ninode = u16(sb, 264)
        if ninode:
            ninode -= 1
            ino = u16(sb, 266 + 2 * ninode)
            sb[264:266] = p16(ninode)
        else:
            ino = 0
            for i in range(1, (self.isize - 2) * 8 + 1):
                if u16(self.d, self.inode_off(i)) == 0:
                    ino = i
                    break
            if ino == 0:
                raise RuntimeError("no free inode in the partition")
        tinode = u16(sb, 478)
        if tinode:
            sb[478:480] = p16(tinode - 1)
        self.wblk(1, sb)
        return ino

    def link(self, dino, name, ino):
        """Append a 16-byte directory entry, growing the directory a block
        at a time.  A directory this deep in is a handful of blocks, so only
        the ten direct addresses are used."""
        nm = name.encode('latin1')
        if len(nm) > 14:
            raise RuntimeError("%s: name is longer than 14 bytes" % name)
        ioff = self.inode_off(dino)
        size = u32(self.d, ioff + 8)
        slot, off = size // BSIZE, size % BSIZE
        if off == 0:
            if slot >= 10:
                raise RuntimeError("directory needs an indirect block")
            b = self.alloc_block()
            self.d[ioff + 12 + 3 * slot:ioff + 15 + 3 * slot] = l3put(b)
        else:
            b = l3get(self.d, ioff + 12 + 3 * slot)
        o = self.base + b * BSIZE + off
        self.d[o:o + 16] = p16(ino) + nm + bytes(14 - len(nm))
        self.d[ioff + 8:ioff + 12] = p32(size + 16)

    def create(self, path, mode):
        """A new empty regular file at `path', whose parent must exist."""
        head, _, name = path.rstrip('/').rpartition('/')
        dino = self.lookup(head or '/')
        ino = self.alloc_inode()
        ioff = self.inode_off(ino)
        self.d[ioff:ioff + INODE_SIZE] = bytes(INODE_SIZE)
        self.d[ioff:ioff + 2] = p16(0o100000 | (mode & 0o7777))
        self.d[ioff + 2:ioff + 4] = p16(1)
        self.link(dino, name, ino)
        return ino

    def put(self, path, data, mode):
        """Replace the contents of `path', creating it if it is not there.
        `mode' is None unless --mode was given: a replace leaves the mode a
        packed image already gave the file (privsep's subject is exactly those
        bits), and a create with no --mode makes a plain 0644 file."""
        try:
            ino = self.lookup(path)
        except RuntimeError:
            ino = self.create(path, 0o644 if mode is None else mode)
        else:
            if mode is not None:
                ioff = self.inode_off(ino)
                self.d[ioff] = mode & 0xFF
                self.d[ioff + 1] = (self.d[ioff + 1] & 0xF0) | ((mode >> 8) & 0x0F)
        self.replace(path, data, ino)

    def replace(self, path, data, ino=None):
        if ino is None:
            ino = self.lookup(path)
        ioff = self.inode_off(ino)
        old = u32(self.d, ioff + 8)
        nblk = (len(data) + BSIZE - 1) // BSIZE
        blocks = [self.alloc_block() for _ in range(nblk)]
        for i, b in enumerate(blocks):
            self.wblk(b, data[i * BSIZE:(i + 1) * BSIZE])
        addrs = [0] * 13
        addrs[:min(nblk, 10)] = blocks[:10]
        rest = blocks[10:]
        if rest:
            ind = self.alloc_block()
            addrs[10] = ind
            ib = bytearray(BSIZE)
            for i, b in enumerate(rest[:128]):
                ib[4 * i:4 * i + 4] = p32(b)
            self.wblk(ind, ib)
            rest = rest[128:]
        if rest:
            dbl = self.alloc_block()
            addrs[11] = dbl
            db = bytearray(BSIZE)
            for j in range(0, len(rest), 128):
                l1 = self.alloc_block()
                db[4 * (j // 128):4 * (j // 128) + 4] = p32(l1)
                ib = bytearray(BSIZE)
                for i, b in enumerate(rest[j:j + 128]):
                    ib[4 * i:4 * i + 4] = p32(b)
                self.wblk(l1, ib)
            self.wblk(dbl, db)
        for i in range(13):
            self.d[ioff + 12 + 3 * i:ioff + 15 + 3 * i] = l3put(addrs[i])
        self.d[ioff + 8:ioff + 12] = p32(len(data))
        print("  %s: inode %d, %d -> %d bytes (%d blocks)"
              % (path, ino, old, len(data), nblk))


def main():
    args = sys.argv[1:]
    mode = None
    if args and args[0] == '--mode':
        mode = int(args[1], 8)
        args = args[2:]
    img, base = args[0], int(args[1])
    d = bytearray(open(img, 'rb').read())
    fs = FS(d, base)
    args = args[2:]
    for i in range(0, len(args), 2):
        fs.put(args[i + 1], open(args[i], 'rb').read(), mode)
    open(img, 'wb').write(fs.d)
    print("written:", img)


main()
