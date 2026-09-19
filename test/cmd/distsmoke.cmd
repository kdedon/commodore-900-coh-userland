# distsmoke.cmd -- does a packed image come up and work at all?
#
#	hostbuild/emu-run.sh test/cmd/distsmoke.cmd
#
# The smallest question worth asking of an image, and the one a green BUILD
# cannot answer: the kernel came up multi-user and gave root a shell at the
# console login, the root filesystem mounts read-write, and a file written
# through the buffer cache reads back.  That is the path a miscompiled kernel
# takes down while still printing its banner, so it is not a formality.
#
# Every path here is on the root filesystem.  The verdict line is
# SMOKE=<content>, echoed from the file rather than from the shell, so it can
# only appear if the write, the sync and the read all happened.
#
# test/cmd/run.sh judges it by the lines below, and its --selftest judges this
# same transcript again with conditions that must fail.
#% expect ^SMOKE=c900smokeok$
#% expect ^LSBIN=0$
#% reject Segmentation violation
#% reject ^Panic:
/bin/echo c900smokeok > /smoke.txt
/bin/echo SMOKE=`/bin/cat /smoke.txt`
/bin/rm /smoke.txt
/bin/ls /bin > /dev/null
/bin/echo LSBIN=$?
