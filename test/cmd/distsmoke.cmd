# distsmoke.cmd -- does a packed dist image come up and work at all?
#
#	make -C os/hostbuild smoke-test DIST=<dist>
#
# The smallest question worth asking of an image, and the one a green BUILD
# cannot answer: the kernel reached the single-user shell, the root filesystem
# mounts read-write, and a file written through the buffer cache reads back.
# That is the path a miscompiled kernel takes down while still printing its
# banner, so it is not a formality.
#
# Single user, so /usr and /tmp are NOT mounted (emu-run.sh says why): every
# path here is on the root filesystem.  The verdict line is SMOKE=<content>,
# echoed from the file rather than from the shell, so it can only appear if the
# write, the sync and the read all happened.
/bin/echo c900smokeok > /smoke.txt
/bin/echo SMOKE=`/bin/cat /smoke.txt`
/bin/rm /smoke.txt
/bin/ls /bin > /dev/null
/bin/echo LSBIN=$?
