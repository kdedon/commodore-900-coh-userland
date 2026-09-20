# tarcycle.cmd -- does tar(1) CREATE an archive a reader can get the bytes back
# out of, and can it read one a host wrote?
#
#	hostbuild/emu-run.sh test/cmd/tarcycle.cmd
#
# tar is the interchange program on this machine: everything that arrives from
# or leaves for another system arrives as a .tar, so BOTH directions are under
# test here and neither one alone is the gate.
#
# WHAT A PASS LOOKS LIKE.  Eleven markers, each at the start of a line, each
# with the status after the `='.  A pass is
#	TARC=0 TART=0 TARX=0
#	CMP-SMALL=0 CMP-MULTI=0 CMP-BIG=0 CMP-EMPTY=0 CMP-SUB=0
#	HOSTLIST=0 HOSTX=0 CMP-HOST=0
# and no /core listed at the end.  A MISSING marker is a failure exactly like a
# non-zero one -- tar dumped core before it could echo, which is the defect
# this file was written for -- and so is any `Segmentation violation'.  Count
# the markers at the START of a line: the transcript echoes each command too.
#
# THE cmp CASES ARE THE GATE, NOT THE LISTING.  `tar cvf' printing an `a name'
# line for every member and `tar tvf' listing them proves only that tar reached
# the end of its own argument walk; a listing of members whose data blocks are
# short, mis-padded or written at the wrong offset looks identical.  Only the
# byte comparison of an EXTRACTED file against the original says the archive
# carried the file.  Every member is therefore compared, and each comparison is
# a size that exercises a different part of the block machinery:
#	SMALL	6 bytes -- one block, 506 bytes of padding
#	MULTI	/bin/echo, 3686 bytes -- eight blocks, last one partial
#	BIG	/bin/cmp, 11378 bytes -- more than MAXBLK*512, so the reader
#		must refill its 20-block buffer and then handle a SHORT final
#		record.  A reader that treats a short read as end of archive
#		loses the tail of this file, or the whole archive.
#	EMPTY	0 bytes -- a header with no data block at all
#	SUB	a file inside a subdirectory, so the archive carries directory
#		members and the extractor has to create the path
#
# THE HOST ARCHIVE IS LOAD-BEARING AND IT IS NOT WRITTEN BY THIS tar.
# /h330.tar is assembled below byte for byte from what `tar --format=gnu -b1'
# wrote on the host: 2048 bytes -- one header block, one data block and the two
# zero blocks that end an archive.  Every NUL is written as `~' and tr(1) puts
# them back, and since all but five of the thirty-two 64-byte pieces are
# nothing but NUL, those come from cat'ing one 64-NUL file rather than from
# being typed.  It is encoded rather than shipped as a file because this
# repository holds no binaries and the guest has no uudecode.  /h330.tar must
# come out 2048 bytes; a shorter one means a piece was typed short and the
# cases after it are then testing the wrong bytes.
#
# What it proves is that a POSIX/ustar/GNU header -- which carries `ustar' at
# offset 257, where a v7 header has NUL padding, and whose checksum covers it
# -- is read rather than refused, and that no spurious `bad checksum' is
# reported.  A tar that only round-trips its own archives passes every case
# above it and is still useless on a machine that receives files from a host.
#
# `bad checksum' ANYWHERE in the transcript is a failure, and it is not covered
# by any marker -- grep for it, no line of this file contains the phrase: it goes to stderr while the listing is buffered, so it surfaces
# next to whichever line happens to be flushing.  It is what a header read at
# the wrong offset says, and reading a data block as a header is exactly what
# happens when a member's type byte is misjudged -- HOSTLIST and HOSTX both
# exited 0 while doing that, which is why CMP-HOST is the case that decides.
#
# The archive holds one file, h330f, whose contents are the sixteen bytes
# `host wrote this!' with no newline; /expect is that same string written by the
# guest, so CMP-HOST compares extracted bytes against locally produced ones.
#
#% needs runtime archive
#% expect ^TARC=0$
#% expect ^TART=0$
#% expect ^TARX=0$
#% expect ^CMP-SMALL=0$
#% expect ^CMP-MULTI=0$
#% expect ^CMP-BIG=0$
#% expect ^CMP-EMPTY=0$
#% expect ^CMP-SUB=0$
#% expect ^HOSTLIST=0$
#% expect ^HOSTX=0$
#% expect ^CMP-HOST=0$
#% expect ^-.* 2048 .*/h330\.tar$
#% expect ^== ALLDONE$
#% expect ^/core: no such file or directory$
#% reject ^-.* /core$
#% reject bad checksum
#% reject Segmentation violation
#% reject ^Panic:
echo == build the tree to archive
mkdir /t1
mkdir /t1/sub
echo alpha > /t1/a
cp /bin/echo /t1/multi
cp /bin/cmp /t1/big
cat /dev/null > /t1/empty
echo charlie > /t1/sub/c
ls -l /t1
echo == create
cd /
tar cvf /t1.tar t1
echo TARC=$?
ls -l /t1.tar
echo == list
tar tvf /t1.tar
echo TART=$?
echo == extract somewhere else
mkdir /x
cd /x
tar xvf /t1.tar
echo TARX=$?
ls -l /x/t1
cmp /t1/a /x/t1/a
echo CMP-SMALL=$?
cmp /t1/multi /x/t1/multi
echo CMP-MULTI=$?
cmp /t1/big /x/t1/big
echo CMP-BIG=$?
cmp /t1/empty /x/t1/empty
echo CMP-EMPTY=$?
cmp /t1/sub/c /x/t1/sub/c
echo CMP-SUB=$?
echo == the host-written GNU archive, assembled from its own bytes
echo == /z64 is 64 NUL bytes, written as ~ and translated by tr at the end
echo -n '~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~' > /z64
cat /z64 /z64 /z64 /z64 /z64 /z64 /z64 /z64 > /z512
echo == the four pieces of the header block that are not NUL
echo -n 'h330f~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~' > /a0
echo -n '~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~0000664~0000000~0000001~0000' > /a1
echo -n '0000020~15233233400~010570~ 0~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~' > /a2
echo -n '~ustar  ~root~~~~~~~~~~~~~~~~~~~~~~~~~~~~daemon~~~~~~~~~~~~~~~~~' > /a4
echo == and the one piece of the data block
echo -n 'host wrote this!~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~' > /a8
echo == header block, data block, then the two zero blocks that end an archive
cat /a0 /a1 /a2 /z64 /a4 /z64 /z64 /z64 > /h330.txt
cat /a8 /z64 /z64 /z64 /z64 /z64 /z64 /z64 >> /h330.txt
cat /z512 /z512 >> /h330.txt
tr '~' '\\000' < /h330.txt > /h330.tar
echo == /h330.tar must be 2048 bytes
ls -l /h330.tar
echo == read it -- it must be listed and extracted, not refused
cd /
tar tvf /h330.tar
echo HOSTLIST=$?
mkdir /hx
cd /hx
tar xvf /h330.tar
echo HOSTX=$?
echo -n 'host wrote this!' > /expect
cmp /expect /hx/h330f
echo CMP-HOST=$?
echo == cores -- /core must NOT be listed
ls -l /core
echo == ALLDONE
