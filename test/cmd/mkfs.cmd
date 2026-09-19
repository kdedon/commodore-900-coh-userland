# mkfs.cmd -- does a filesystem MADE BY mkfs(1M) check clean, mount READ/WRITE
# and round-trip files byte for byte?
#
#	dd if=/dev/zero of=${TMPDIR:-/tmp}/mkfs-fd.img bs=512 count=2392 &&
#	FLOPPY=${TMPDIR:-/tmp}/mkfs-fd.img \
#	    hostbuild/emu-run.sh test/cmd/mkfs.cmd
#
# WHY THIS EXISTS.  mkfs is the only program that writes a filesystem from
# nothing, and a filesystem that is subtly wrong -- one block missing from the
# free list, one inode count that does not fit a short -- is written without a
# diagnostic and read back without one either.  Neither `mkfs ran' nor a source
# diff can settle that.  Only USING what it made can: the checkers walk the
# free list and the i-list, and the mount/write/umount/remount/cmp cycle makes
# the kernel allocate out of the free list mkfs built and hand the bytes back.
#
# WHICH DEVICE, and why it is safe.  THE GUEST COMES UP MULTI-USER: emu-run.sh
# boots to a login prompt and logs in as root, so /etc/rc has already run, and
# /etc/rc mounts every filesystem the media declares --
#
#	/etc/mount /dev/hd4 / -u
#	/etc/mount /dev/hd3 /tmp
#
# -- while rc.net and rc.local go on writing their logs into /tmp.  There is no
# spare partition to make a filesystem in.  The test image (test/image/build.sh)
# declares three slots and two filesystems: hd0 (boot), hd4 (/, with /usr on
# it) and hd3 (/tmp, plus the swap extent at 6512..10608 in the same slot).
# No other slot is declared -- the blocks past /tmp are an unallocated hole --
# and an undeclared slot is all-zero in the partition table kboot hands the
# kernel, so the driver refuses every block through it (/dev/hd1 included).
# Every partition that exists is either mounted or is swap.
#
# So the medium is one the RUN ATTACHES: a 2392-block floppy image handed to
# the emulator with FLOPPY=, reachable in the guest as /dev/fd1 (the route
# os/tests/fdmount uses in the distribution repository).  /etc/rc never mounts
# it -- case 0 prints the mount table to show that -- and no hard-disk
# partition is written at any point here.  2392 is fixed by NFBLK in wd(4) and
# FLOPPY_BLOCKS in the emulator, so that is the only size to make.
#
# THE MEDIUM MUST BE BLANK, which is what the `dd' above is for, and case 0
# proves it: the twelve bytes at offset 996 -- the volume and pack names in the
# super block -- must read as NULs BEFORE mkfs runs and as `gt'/`pk' after it in
# case 12.  A medium carrying a filesystem already would let every mount below
# pass without mkfs having written anything.  emu-run.sh does NOT copy the
# floppy (it copies only the disk), so the bytes mkfs wrote are in that file
# afterwards and the emulator's tools/disk.py --read returns them.
#
# WHAT A PASS LOOKS LIKE.  Twenty `== STATUS-<n> 0' lines, all zero -- a
# nonzero status or a missing STATUS line is a failure.  Count them at the
# START of a line: the transcript echoes the command that produced each one as
# well as its output.  In addition:
#
#   * STATUS-3/4/5 (icheck, dcheck, check on the fresh filesystem) must not be
#     accompanied by `missing =', `dups in free', `Bad ifree list', `Free
#     list/tfree counts differ' or `Bad freelist'.  Those lines are the
#     free-list defects this gate is for, and icheck prints them while still
#     printing `/dev/fd1:' and exiting -- so read the lines, not just the
#     status.  `free = <n>' and `bad=0' from icheck -v are the evidence the
#     walk happened rather than the program bailing out early.  The filesystem
#     is never mounted when a checker walks it: a checker run against a MOUNTED
#     filesystem reports blocks missing from the free list because the kernel
#     has allocated them since, which is a property of the mount and not of
#     mkfs (R7-123).
#   * STATUS-7 is a write into the freshly mounted filesystem, and case 6 must
#     NOT have printed `not cleanly unmounted, mounting read only'.  A freshly
#     made filesystem is FSCLEAN, and mount(2) forces one that is not read only
#     (sys/coh/fs2.c).
#   * STATUS-8..13 are the round trip: two files in, unmount, remount, compare
#     byte for byte with cmp(1).  cmp prints NOTHING when the files are
#     identical, so its STATUS line is what says it compared them at all.
#     /coherent is 208 blocks -- more than the 10 direct addresses and more
#     than one indirect block holds -- so that copy reaches the double
#     indirect, and it fits a 2392-block volume with room to spare.
#
# CASE 13 IS THE NEGATIVE CONTROL AND IT MUST GO RED BY DESIGN.  It writes a
# nonzero byte over s_dirty in the super block (block 1, offset 466: the field
# hostbuild/mkimage.py writes at the same offset) and mounts again.  mount MUST
# then print `not cleanly unmounted, mounting read only', the write into it
# MUST fail, and the `ls -l' of that file MUST say it is not there.  That is
# STATUS-7's read/write assertion proved able to fail; without it, case 7 would
# pass on a system that could not tell a clean super block from a dirty one and
# mkfs's s_dirty initialisation would be untested.  STATUS-18 covers the dd
# that wrote the byte -- 0 means the poke was written, nothing more.
#
# CASES 11 AND 12 ARE THE SECOND ASSERTION THAT CAN FAIL, and no hand-broken
# binary is needed for it.  The installer hands mkfs a PROTOTYPE FILE rather
# than a block count (dist/files/install/build, and the prototype
# hostbuild/mkimage.py generates), whose first line names the volume and the
# pack.  A token from gettoken() is not NUL-terminated until the NEXT call
# overwrites the separator, so a mkfs that copies the volume name with a plain
# `strncpy(S.s_fname, P.p_fname, 6)' takes the separator and whatever follows
# it with it.  Case 12 dumps those two fields: they must read `gt' and `pk'
# padded with NULs, and anything else there -- a space, a newline, the digits
# of the block count -- is that defect, on display in the super block that
# df(1) and mount(1) print.
#
# CASE 14 IS THE THIRD SUCH ASSERTION.  `-f' and `-p' name the volume and the
# pack on the command line, where there is no prototype file to carry them, and
# mkproto() has to synthesise the name line -- which it can only do if it also
# has a boot-file name to put first, so `-b' left off has to default to
# /dev/null.  The od(1) dump must again read `vol' and `pak'; a mkfs that drops
# the two options writes the `noname'/`nopack' defaults there instead, and its
# exit status is 0 either way.
#
# Case 1 makes a filesystem in a REGULAR FILE.  Beyond its status it asserts
# nothing here: it exists so the host can read /diff.fs back out with cohfs cat
# and compare it against one made by another mkfs under the same arguments.
#
# For test/cmd/run.sh, which makes the blank medium itself (`#% floppy'): the
# twenty zero statuses; the medium blank before; icheck's `free =' and `bad=0'
# and none of the free-list defect lines; the round-tripped file read back;
# case 13's read-only mount, refused write and missing file; the `gt'/`pk' and
# `vol'/`pak' dumps; and /dev/fd1 in neither mount table.  NOT covered line by
# line: that case 6 did NOT print the read-only message -- case 13 must print
# it, so it cannot be rejected outright.  STATUS-7 and the `written-by-the-gate'
# read-back are the evidence that the first mount was read/write.
#% floppy 2392
#% expect ^== STATUS-1 0$
#% expect ^== STATUS-2 0$
#% expect ^== STATUS-3 0$
#% expect ^== STATUS-4 0$
#% expect ^== STATUS-5 0$
#% expect ^== STATUS-6 0$
#% expect ^== STATUS-7 0$
#% expect ^== STATUS-8 0$
#% expect ^== STATUS-9 0$
#% expect ^== STATUS-10 0$
#% expect ^== STATUS-11 0$
#% expect ^== STATUS-12 0$
#% expect ^== STATUS-13 0$
#% expect ^== STATUS-14 0$
#% expect ^== STATUS-15 0$
#% expect ^== STATUS-16 0$
#% expect ^== STATUS-17 0$
#% expect ^== STATUS-18 0$
#% expect ^== STATUS-19 0$
#% expect ^== STATUS-20 0$
#% expect ^00000000 \\0 \\0 \\0 \\0 \\0 \\0 \\0 \\0 \\0 \\0 \\0 \\0$
#% expect ^free = [1-9][0-9]*$
#% expect ^bad=0 \(0 in I-list\)$
#% expect ^written-by-the-gate$
#% expect ^00000000 g  t  \\0 \\0 \\0 \\0 p  k  \\0 \\0 \\0 \\0$
#% expect ^mount: /dev/fd1: not cleanly unmounted, mounting read only.*$
#% expect ^Cannot create /mnt/ro$
#% expect ^/mnt/ro: no such file or directory$
#% expect ^00000000 v  o  l  \\0 \\0 \\0 p  a  k  \\0 \\0 \\0$
#% expect ^== ALLDONE$
#% reject missing =
#% reject dups in free
#% reject Bad ifree list
#% reject Free list/tfree counts differ
#% reject Bad freelist
#% reject ^bad=[1-9]
#% reject ^/dev/fd1 on /
#% reject Segmentation violation
#% reject ^Panic:
echo == 0 the mount table, and the medium this run writes
/etc/mount
df
echo == 0 the attached medium is blank -- these twelve bytes are the volume and pack names
dd if=/dev/fd1 bs=1 skip=996 count=12 | od -c
echo == 1 mkfs into a regular file, for the host-side differential
> /diff.fs
/etc/mkfs /diff.fs 400
echo == STATUS-1 $?
ls -l /diff.fs
echo == 2 mkfs the attached medium /dev/fd1, 2392 blocks
/etc/mkfs /dev/fd1 2392
echo == STATUS-2 $?
echo == 3 icheck the fresh filesystem -- the free-list assertion
/bin/icheck -v /dev/fd1
echo == STATUS-3 $?
echo == 4 dcheck the fresh filesystem
/bin/dcheck /dev/fd1
echo == STATUS-4 $?
echo == 5 check, which runs both
/bin/check /dev/fd1
echo == STATUS-5 $?
echo == 6 mount it -- no read-only message here
/etc/mount /dev/fd1 /mnt
echo == STATUS-6 $?
ls -la /mnt
echo == 7 it must be READ/WRITE
echo written-by-the-gate > /mnt/w
echo == STATUS-7 $?
cat /mnt/w
df /mnt
echo == 8 copy files in: a small one and a 208-block one
cp /etc/rc /mnt/rc
echo == STATUS-8 $?
cp /coherent /mnt/big
echo == STATUS-9 $?
sum /mnt/big
/etc/umount /dev/fd1
echo == STATUS-10 $?
echo == 9 remount and compare byte for byte
/etc/mount /dev/fd1 /mnt
echo == STATUS-11 $?
ls -l /mnt
cmp /mnt/rc /etc/rc
echo == STATUS-12 $?
cmp /mnt/big /coherent
echo == STATUS-13 $?
cat /mnt/w
sum /mnt/big
sum /coherent
echo == 10 the populated filesystem must still check clean
/etc/umount /dev/fd1
echo == STATUS-14 $?
/bin/icheck -v /dev/fd1
/bin/dcheck /dev/fd1
echo == 11 the PROTO FILE form, which is how the installer calls mkfs
echo /dev/null gt pk > /proto
echo 2392 400 >> /proto
echo d--755 0 1 >> /proto
echo '$' >> /proto
cat /proto
/etc/mkfs /dev/fd1 /proto
echo == STATUS-15 $?
/bin/icheck -v /dev/fd1
echo == STATUS-16 $?
/bin/dcheck /dev/fd1
echo == STATUS-17 $?
echo == 12 the volume name and pack name it wrote -- 'gt' and 'pk', nothing else
dd if=/dev/fd1 bs=1 skip=996 count=12 | od -c
echo == 13 NEGATIVE CONTROL: dirty the super block, mount must go read only
echo x | dd of=/dev/fd1 bs=1 seek=978 count=1
echo == STATUS-18 $?
/etc/mount /dev/fd1 /mnt
echo dirty-write-attempt > /mnt/ro
ls -l /mnt/ro
/etc/umount /dev/fd1
echo == 14 the -f and -p options must reach the super block
/etc/mkfs -f vol -p pak /dev/fd1 2392
echo == STATUS-19 $?
dd if=/dev/fd1 bs=1 skip=996 count=12 | od -c
/bin/icheck -v /dev/fd1
echo == STATUS-20 $?
echo == 15 leave the medium with a clean empty filesystem
/etc/mkfs /dev/fd1 2392
/bin/icheck /dev/fd1
echo == 16 the mount table again -- nothing here was ever mounted by /etc/rc
/etc/mount
echo == ALLDONE
