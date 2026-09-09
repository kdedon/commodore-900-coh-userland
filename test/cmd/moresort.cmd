# moresort.cmd -- does anything on this machine dump core because a file or a
# terminal capability is missing?
#
#	EMUWAIT=1200 hostbuild/emu-run.sh tests/cmd/moresort.cmd coherent3-full-gfx
#
# more(1) and sort(1) both died on an ordinary mistake: more on any terminal
# whose termcap entry addresses the cursor with `cm' and has no `ho' -- which
# includes vt100, the type /.profile sets for every serial console -- and sort
# on a filename that does not exist.  The host harnesses
# tests/moreterm/run.sh and tests/sortopen/run.sh drive the same two programs
# under the process runner and are mutation-proved; what they cannot see is the
# fault itself, because that runner has flat memory and no signals.  THIS file
# is where the machine answers: a real MMU, a real signal, a real /core.
#
# WHAT A PASS LOOKS LIKE.  Twenty-one markers, each at the start of a line,
# each with the status or the answer after the `='.  A pass is every one of
#	MORE-VT100=0 MORE-VT52=0 MORE-MGR=0 MORE-VT100N=0 MORE-VT100W=0
#	MORE-ANSI=0 MORE-HR=0 MORE-LR=0 MORE-DUMB=0 MORE-NOSUCH=0
#	SORT-MISSING=1 SORT-MIX=1 SORT-MERGE=1 SORT-GOOD=0
#	SORT-DASHFIRST=0 SORT-DASHLAST=0 SORT-DASHSAME=yes SORT-DASHSTDIN=yes
#	SORT-DASHALONE=0 SORT-DASHONLY=yes
#	CORE=no TMP=clean
# A MISSING marker is a failure exactly like a wrong one -- the program died
# before the shell could echo it -- and so is any `Segmentation violation'
# anywhere in the transcript.  Count the markers at the START of a line: the
# transcript echoes each command as well.
#
# THE TERMINAL TYPES ARE THE POINT, not the terminals.  The five in the first
# group have `cm' and no `ho' and the five in the second either have `ho' or
# have no `cm'; a run that tried only one type would report a pass with the
# defect standing.  Every entry named here is in the /etc/termcap this image
# ships.
#
# STDOUT MUST STAY ON THE CONSOLE.  more reads the termcap only when its output
# is a terminal, so redirecting it away is the one edit that turns this file
# into a test of nothing.  /pf is two lines for that reason -- short enough to
# print in full without pausing, so no case needs a keystroke.
#
# THE SORT-DASH GROUP IS ABOUT THE DATA, not the status.  `sort - file' dropped
# the standard input's records, exited 0 and printed the file's records in
# order, so a case that read only the status called it a pass.  These six
# redirect sort into a file and then ask the machine two questions about the
# file: whether it holds a word that could only have come from the standard
# input, and whether `- file' and `file -' produce the same bytes.
#
# Single user, so /usr (where more lives) and /tmp (where sort's scratch files
# go) are mounted here by hand; emu-run.sh says why they are not already.
/etc/mount /dev/hd3 /tmp
/etc/mount /dev/hd6 /usr
PATH=/bin:/usr/bin:/etc
export PATH
rm -f /core
echo one > /pf
echo two >> /pf
echo == MORE-CM-WITHOUT-HO
TERM=vt100 more /pf
echo MORE-VT100=$?
TERM=vt52 more /pf
echo MORE-VT52=$?
TERM=mgr more /pf
echo MORE-MGR=$?
TERM=vt100n more /pf
echo MORE-VT100N=$?
TERM=vt100w more /pf
echo MORE-VT100W=$?
echo == MORE-WITH-HO-OR-NO-CM
TERM=ansi more /pf
echo MORE-ANSI=$?
TERM=hr more /pf
echo MORE-HR=$?
TERM=lr more /pf
echo MORE-LR=$?
TERM=dumb more /pf
echo MORE-DUMB=$?
TERM=nosuchterm more /pf
echo MORE-NOSUCH=$?
echo == SORT-CANNOT-OPEN
sort /nosuchfile
echo SORT-MISSING=$?
sort /pf /nosuchfile
echo SORT-MIX=$?
sort -m /nosuchfile /pf
echo SORT-MERGE=$?
sort /pf
echo SORT-GOOD=$?
echo == SORT-DASH-OPERAND
echo yyy > /d1
echo qqq >> /d1
echo sss > /d2
echo ppp >> /d2
sort - /d2 < /d1 > /d3
echo SORT-DASHFIRST=$?
sort /d2 - < /d1 > /d4
echo SORT-DASHLAST=$?
if cmp /d3 /d4 > /dev/null ; then echo SORT-DASHSAME=yes ; else echo SORT-DASHSAME=no ; fi
if grep yyy /d3 > /dev/null ; then echo SORT-DASHSTDIN=yes ; else echo SORT-DASHSTDIN=no ; fi
sort - < /d1 > /d5
echo SORT-DASHALONE=$?
if grep yyy /d5 > /dev/null ; then echo SORT-DASHONLY=yes ; else echo SORT-DASHONLY=no ; fi
cat /d3
rm -f /d1 /d2 /d3 /d4 /d5
echo == WHAT-WAS-LEFT-BEHIND
if test -f /core ; then echo CORE=yes ; else echo CORE=no ; fi
# sort names its scratch files after its own pid, so the check is for the
# prefix rather than for a name: `ls' into a file first, because a grep with
# nothing to read would stop the run at a prompt that never comes.
ls /tmp > /tl
if grep sort /tl > /dev/null ; then echo TMP=dirty ; else echo TMP=clean ; fi
cat /tl
rm -f /tl
rm -f /pf
echo == ALLDONE
