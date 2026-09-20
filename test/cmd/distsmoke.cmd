# distsmoke.cmd -- does a packed image come up and work at all?
#
#	hostbuild/emu-run.sh test/cmd/distsmoke.cmd
#
# The smallest question worth asking of an image, and the one a green BUILD
# cannot answer: root gets a multi-user console shell, the root filesystem
# mounts read-write, and a file written through the buffer cache reads back.
#
# SMOKE=<content> is echoed from the file, so it appears only if the write,
# sync and read all happened.  run.sh --selftest reuses this transcript.
#% needs base
#% expect ^SMOKE=c900smokeok$
#% expect ^LSBIN=0$
#% reject Segmentation violation
#% reject ^Panic:
/bin/echo c900smokeok > /smoke.txt
/bin/echo SMOKE=`/bin/cat /smoke.txt`
/bin/rm /smoke.txt
/bin/ls /bin > /dev/null
/bin/echo LSBIN=$?
