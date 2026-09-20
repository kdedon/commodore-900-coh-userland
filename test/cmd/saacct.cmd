# saacct.cmd -- sa(1) against an accounting file the KERNEL wrote.
#
#	hostbuild/emu-run.sh test/cmd/saacct.cmd
#
# acct(2) appends one record per process exit (sys2.c uacct turns it on,
# proc.c/fs2.c setacct writes the record), so the file sa reads is the
# kernel's own format.  /usr/adm/acct is the raw file, /usr/adm/savacct and
# /usr/adm/usracct the summaries.
#
# accton(8) does not create the file and the kernel refuses anything that is
# not a plain regular file, so it is created first.
#
# WHAT EACH CASE MUST SHOW.  Every case ends with `echo SA-<n>-DONE'; a
# missing DONE line is a failure whatever else was printed.
#
#   1  CONTROL.  /usr/adm exists, accton turns accounting ON and
#      says nothing.  A run in which accounting was never on would otherwise
#      look like a clean sa with nothing to report.
#   2  wc is run EXACTLY SEVEN TIMES and cat exactly twice.  After accounting
#      is turned off the file must be non-empty, and its size must be a whole
#      multiple of one record.
#   3  sa's default report must list `wc' with a #CALL of 7 and `cat' with 2.
#      Those two numbers are computed here, not read out of sa.
#   4  the sorting and summarising flags: -n sorts by call count, -b by time
#      per call, -r reverses, -l splits user and system time into two columns,
#      -j gives seconds per call.  Each must print the header and the same
#      command set; -l must print `USER  SYS' where the others print `CPU'.
#   5  -c and -t are the percentage columns, and -t is where percent() divides
#      by a total that can be zero: a command that finishes inside one clock
#      second has an elapsed time of 0 seconds, so its divisor is 0 x HZ.  Such
#      a row must read 100.0 in the CPU/REAL column.  A row reading some
#      hundreds of percent is the failure -- a divide by zero on this target
#      does not trap, it yields the dividend, so without the `total == 0' guard
#      in percent() the column silently prints the CPU time as a percentage.
#      Any row whose REAL is non-zero (a command slow enough to cross a second
#      boundary) is the control: it reads the same either way.
#   6  -u prints one line per record, user name and command name, and must
#      print at least the seven wc lines.  -m summarises by USER and must
#      print root's line with the #PROC count for the whole session.
#   7  -s merges the raw file into the summaries and truncates it: savacct and
#      usracct must appear, and the raw file must be zero bytes afterwards.
#   8  sa on the now-empty raw file must still report wc with 7 calls, because
#      that count now comes out of savacct: the merge round-tripped through
#      the summary file.  -c and -t must survive here too.
#   9  sa on a file that does not exist must say so and exit non-zero, rather
#      than reporting an empty system.
#
# No sa row may exceed 100.0% (case 5's failure).  Left to the transcript:
# the raw file being whole records, the count of -u's wc lines, and case 9's
# exit status.
#% needs runtime
#% expect ^SA-1-DONE$
#% expect ^SA-2-DONE$
#% expect ^SA-3-DONE$
#% expect ^SA-4-DONE$
#% expect ^SA-5-DONE$
#% expect ^SA-6-DONE$
#% expect ^SA-7-DONE$
#% expect ^SA-8-DONE$
#% expect ^SA-9-DONE$
#% expect ^== ALLDONE$
#% expect ^d.* /usr/adm$
#% expect ^ *[1-9][0-9]* +/usr/adm/acct$
#% expect ^wc +7 +[0-9]+ +[0-9]+$
#% expect ^cat +2 +[0-9]+ +[0-9]+$
#% expect ^ +#CALL +CPU +REAL$
#% expect ^ +#CALL +USER +SYS +REAL$
#% expect ^ +#CALL +CPU +REAL +CPU % *$
#% expect ^ +#CALL +CPU +REAL +CPU/REAL %$
#% expect ^wc +7 +[0-9]+ +[0-9]+ +[0-9]+\.[0-9] *$
#% expect ^ +#CALL +USER +SYS +REAL +CPU % +CPU/REAL %$
#% expect ^wc +7 +[0-9]+ +[0-9]+ +[0-9]+ +[0-9]+\.[0-9] +[0-9]+\.[0-9] *$
#% expect ^root +wc$
#% expect ^root +[1-9][0-9]* +[0-9]+ +[0-9]+$
#% expect ^-.* 0 .* /usr/adm/acct$
#% expect ^-.* [1-9][0-9]* .* /usr/adm/savacct$
#% expect ^-.* [1-9][0-9]* .* /usr/adm/usracct$
#% expect ^Cannot open raw accounting file `/nosuchacct'$
#% reject ^[a-z]+ +[0-9]+ .*( |^)(100\.[1-9]|10[1-9]\.[0-9]|1[1-9][0-9]\.[0-9]|[2-9][0-9][0-9]\.[0-9]|[0-9]{4,}\.[0-9])( |$)
#% reject Segmentation violation
#% reject ^Panic:
echo == 1 create the raw file, turn accounting on -- CONTROL
ls -ld /usr/adm
rm -f /usr/adm/acct /usr/adm/savacct /usr/adm/usracct
cp /dev/null /usr/adm/acct
/etc/accton /usr/adm/acct
echo SA-1-DONE
echo == 2 seven wc and two cat, then accounting off
for i in 1 2 3 4 5 6 7 ; do wc -c /etc/motd ; done
cat /etc/motd
cat /etc/motd
/etc/accton
wc -c /usr/adm/acct
echo SA-2-DONE
echo == 3 the default report -- wc must show 7 calls, cat 2
sa
echo SA-3-DONE
echo == 4 sorting and summarising flags
sa -n
sa -b
sa -nr
sa -l
sa -j
echo SA-4-DONE
echo == 5 the percentage columns, where percent divides by the total
sa -c
sa -t
sa -clt
echo SA-5-DONE
echo == 6 per-record and per-user views
sa -u
sa -m
echo SA-6-DONE
echo == 7 -s merges into the summaries and truncates the raw file
sa -s
ls -l /usr/adm/acct /usr/adm/savacct /usr/adm/usracct
echo SA-7-DONE
echo == 8 the empty raw file -- the other zero divisor
sa
sa -c
sa -t
echo SA-8-DONE
echo == 9 a raw file that is not there
sa /nosuchacct
echo SA-9-DONE
echo == ALLDONE
