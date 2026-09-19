# lprspool.cmd -- what does the printer spooler actually put on the paper?
#
#	hostbuild/emu-run.sh tests/cmd/lprspool.cmd
#
# This machine has no printer, so the bytes lpd(1) would send to one are
# captured instead: /dev/lp is replaced by an ORDINARY FILE, mode 666 because
# lpd runs setuid daemon, and each job's file is moved aside and read back.
# That makes every part of the spooler observable without hardware -- what lpr
# queues into /usr/spool/lpd, what lpd writes, and what it removes when it is
# done.  There is no lpq/lpstat/lprm in this generation of the spooler; the
# spool directory listing is the queue report.
#
# /usr is on the root filesystem of the test image, which the multi-user boot
# has mounted: /usr/lib/lpd is the daemon and /usr/spool/lpd is the queue.
#
# WHAT EACH CASE MUST SHOW.  Every case ends with `echo LPR-<n>-DONE'; a
# missing DONE line is a failure whatever else was printed.  Count the DONE
# lines at the START of a line -- the transcript echoes the commands too.
#
#   1  CONTROL.  /usr carries the daemon; the four programs exist.
#      Without it a run in which nothing could have printed would look calm.
#   2  A job with the daemon moved aside leaves its control file in the queue,
#      and `lpr -B' puts a bare `B' line in it.  The B line is what suppresses
#      the banner; a run that prints `Usage:' here is the OLD lpr, which has
#      no -B at all, and the queue stays empty.
#   3  CONTROL.  A plain job (no -B) prints the header line, the four banner
#      blocks in `#' fill, then the file.  P1 must contain `#'.
#   4  The banner rows end CR-LF.  This is the fix for the banner page not
#      printing on a head printer: od -c must show \r \n at the end of the
#      banner rows in P1, not \n alone.
#   5  `lpr -B' prints NO banner and NO header: P2 must contain no `#' and no
#      `cf' line -- just the file, then one formfeed.
#   6  The formfeed is TRAILING.  P2 must not BEGIN with \f, and must END with
#      one: od -c shows FIRSTLINE first and \f last.  (With a banner the two
#      generations emit the same stream, because the leading formfeed of the
#      first file fell where the formfeed after the banner now falls; the
#      banner-less job is where the change is visible.)
#   7  The queue is emptied by the daemon: it unlinks each control file as it
#      finishes with it and its own lock file on the way out, so after the jobs
#      `ls /usr/spool/lpd' lists nothing at all.
#   8  opr(1) is the spooler SELECTOR: it maps -lp/-vp onto /bin/lpr or
#      /bin/vpr and execs it, so an `opr' job must come out looking exactly
#      like case 3's -- header, banners, file.  P3 must contain `#'.
#
# For test/cmd/run.sh.  Every DONE line, and per case the line that carries its
# evidence.  P2 is judged exactly: its size, and the two lines of its od dump,
# which say at once that it begins with FIRSTLINE (no header, no `cf' line, no
# leading formfeed) and ends with a single \f.  The banner's CR-LF is a dump
# row of P1 holding both `#' and `\r \n' -- only P1's dump has a `#' in it.
# NOT covered line by line, and read from the transcript instead: that the
# queue listing in case 7 is EMPTY (an empty listing is the absence of a line
# between two prompts, and case 2 legitimately lists cf1), that bare `\n' ends
# no banner row, and that the non-zero `#' count belongs to P1 and to P3 each
# (the two counts print identically, so one required line cannot tell them
# apart; P2's zero is a line of its own).
#% expect ^LPR-1-DONE$
#% expect ^LPR-2-DONE$
#% expect ^LPR-3-DONE$
#% expect ^LPR-4-DONE$
#% expect ^LPR-5-DONE$
#% expect ^LPR-6-DONE$
#% expect ^LPR-7-DONE$
#% expect ^LPR-8-DONE$
#% expect ^== ALLDONE$
#% expect ^-.* /usr/lib/lpd$
#% expect ^-.* /bin/lpr$
#% expect ^-.* /bin/lpskip$
#% expect ^-.* /bin/opr$
#% expect ^cf[0-9]+$
#% expect ^B$
#% expect ^ +[1-9][0-9]* +/P1$
#% expect ^[1-9][0-9]*$
#% expect ^[0-9a-f]{8} .*#.*\\r \\n.*$
#% expect ^ +22 +/P2$
#% expect ^0$
#% expect ^00000000 F  I  R  S  T  L  I  N  E  \\r \\n L  A  S  T  L *$
#% expect ^00000010 I  N  E  \\r \\n \\f *$
#% expect ^ +[1-9][0-9]* +/P3$
#% reject ^Usage:
#% reject Segmentation violation
#% reject ^Panic:
echo == 1 show the four programs -- CONTROL
ls -l /usr/lib/lpd /bin/lpr /bin/lpskip /bin/opr
echo LPR-1-DONE
echo == 2 -B reaches the control file
echo FIRSTLINE > /f1
echo LASTLINE >> /f1
chmod 644 /f1
mv /usr/lib/lpd /usr/lib/lpd.hold
lpr -B /f1
echo == the queue, and the control file the job left in it
ls /usr/spool/lpd
cat /usr/spool/lpd/cf*
mv /usr/lib/lpd.hold /usr/lib/lpd
rm -f /usr/spool/lpd/cf* /usr/spool/lpd/df*
echo LPR-2-DONE
echo == 3 a plain job, banner and all -- CONTROL
rm -f /dev/lp
cp /dev/null /dev/lp
chmod 666 /dev/lp
lpr /f1
sleep 8
mv /dev/lp /P1
cp /dev/null /dev/lp
chmod 666 /dev/lp
wc -c /P1
grep -c '#' /P1
echo LPR-3-DONE
echo == 4 the banner rows end CR-LF
od -c /P1
echo LPR-4-DONE
echo == 5 -B suppresses the banner and the header
lpr -B /f1
sleep 8
mv /dev/lp /P2
cp /dev/null /dev/lp
chmod 666 /dev/lp
wc -c /P2
grep -c '#' /P2
cat /P2
echo LPR-5-DONE
echo == 6 the formfeed is trailing, not leading
od -c /P2
echo LPR-6-DONE
echo == 7 the daemon emptied the queue -- only dpid is left
ls /usr/spool/lpd
echo LPR-7-DONE
echo == 8 opr selects the lp spooler and execs it
opr /f1
sleep 8
mv /dev/lp /P3
cp /dev/null /dev/lp
chmod 666 /dev/lp
wc -c /P3
grep -c '#' /P3
echo LPR-8-DONE
echo == ALLDONE
