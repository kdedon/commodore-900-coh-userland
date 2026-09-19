# shfns.cmd -- the V4.0.6 shell: functions, and the features that came with them.
#
#	hostbuild/emu-run.sh tests/cmd/shfns.cmd
#
# /bin/sh is COHERENT's V4.0.6 Bourne shell.  Its headline feature over the
# 1985 shell is SHELL FUNCTIONS -- NFUNC/NRET in the grammar,
# def_shell_fn()/lookup_sh_fn()/sh_fn() in cmd/sh/exec3.c, and `return' -- and
# nothing else in this tree exercises them: no shipped script defines a
# function, so without this file the whole feature could be absent and every
# other test would still pass.
#
# THE OLD SHELL IS THE NEGATIVE CONTROL, and it is a strong one: the 1985
# grammar has no function production at all, so every case below that defines
# one answers `Syntax error in line 1' on it.  A transcript full of FN-* lines
# is therefore evidence about THIS shell and cannot be produced by the previous
# one.
#
# Each case runs in a CHILD sh reading a script, never at this prompt: a crash
# at the prompt would end the login session, while in a child it is
# confined and the driving shell reports it and carries on.  Every script ends
# by echoing AFTER-<n>; a MISSING AFTER-<n> is a failure, and the AFTER lines
# must be counted at the START of a line, because the transcript also echoes
# the `echo AFTER-<n> >> /f<n>' that wrote each script.
#
# THE VALUES ARE COMPUTED BY THE GUEST, not supplied by this file: FN-ARGS
# prints the arguments a function received, FN-RET prints an exit status that
# came back through `return', FN-DEPTH counts a recursion down through three
# nested calls of one function, and FN-COUNT prints the iteration a `return'
# broke a loop on.  A shell that merely accepted the syntax and ran nothing
# would print the markers and not the numbers.
#
# CASE 9 IS DOCUMENTED BEHAVIOUR, NOT A DEFECT.  clone() (cmd/sh/exec2.c) sets
# sh_fnp = NULL in the child, so a function is not inherited across a fork:
# `f' works, `( f )' and `f | cat' do not.  The case asserts the refusal, so a
# change in that direction shows up here rather than as a mystery later.
#
# For test/cmd/run.sh: the thirteen AFTER lines, every value the guest computes
# (above), case 9's refusal, the two files cases 11 and 12 wrote, case 14 at
# the prompt, and /etc/rc read to a zero status.  Rejected: the old shell's
# `Syntax error', the two lines only a wrong answer prints (the file run
# instead of the function in case 7, the loop running on past `return' in case
# 8), a /core and a `Segmentation violation'.
#
# Case 15 reads /etc/rc on a system rc has ALREADY brought up multi-user, so
# its mounts answer `busy' and it starts rc.net's daemons a second time.  What
# is asserted is only that this shell reads the file to a zero status; the
# mount table printed after it is evidence, not a condition.
#% expect ^AFTER-1$
#% expect ^AFTER-2$
#% expect ^AFTER-3$
#% expect ^AFTER-4$
#% expect ^AFTER-5$
#% expect ^AFTER-6$
#% expect ^AFTER-7$
#% expect ^AFTER-8$
#% expect ^AFTER-9$
#% expect ^AFTER-10$
#% expect ^AFTER-11$
#% expect ^AFTER-12$
#% expect ^AFTER-13$
#% expect ^CONTROL-RAN$
#% expect ^FN-HELLO world$
#% expect ^FN-RET=3$
#% expect ^FN-ARGS 2 alpha bravo$
#% expect ^FN-OUTER 0$
#% expect ^FN-DEPTH-3$
#% expect ^FN-DEPTH-2$
#% expect ^FN-DEPTH-1$
#% expect ^FN-FIRST$
#% expect ^FN-SECOND$
#% expect ^FN-BEAT-THE-FILE$
#% expect ^FN-COUNT=4$
#% expect ^FN-IN-PARENT$
#% expect ^Cannot find nine$
#% expect ^TEN-A$
#% expect ^TEN-B$
#% expect ^TEN-C$
#% expect ^TEN-AND$
#% expect ^TEN-OR$
#% expect ^DIRS-AT=/bin$
#% expect ^DIRS-BACK=/$
#% expect ^FN-AT-PROMPT$
#% expect ^HEREDOC-BODY$
#% expect ^GLOB-REDIR$
#% expect ^RC-STATUS=0$
#% expect ^/core: no such file or directory$
#% expect ^== ALLDONE$
#% reject Syntax error
#% reject ^FN-NOT-THE-FILE$
#% reject ^FN-LOOP-RAN-ON$
#% reject ^-.* /core$
#% reject Segmentation violation
#% reject ^Panic:
echo == 1 plain script -- CONTROL, no function
echo 'echo CONTROL-RAN' > /f1
echo 'echo AFTER-1' >> /f1
sh /f1
echo == 2 define a function and call it
echo 'greet() { echo FN-HELLO $1 ; }' > /f2
echo 'greet world' >> /f2
echo 'echo AFTER-2' >> /f2
sh /f2
echo == 3 return sets the exit status
echo 'fail3() { return 3 ; }' > /f3
echo 'fail3' >> /f3
echo 'echo FN-RET=$?' >> /f3
echo 'echo AFTER-3' >> /f3
sh /f3
echo == 4 positional parameters inside a function
echo 'args4() { echo FN-ARGS $# $1 $2 ; }' > /f4
echo 'args4 alpha bravo' >> /f4
echo 'echo FN-OUTER $#' >> /f4
echo 'echo AFTER-4' >> /f4
sh /f4
echo == 5 recursion -- a function that calls itself
# NO BACKSLASH APPEARS IN THIS FILE.  emu-run.sh feeds it through the
# emulator's --input, whose only escapes are \r \n \t and \\, so any other
# backslash is eaten before the guest sees it -- which silently rewrites the
# line into something that is still valid shell.  That is why the recursion is
# counted down with expr rather than multiplied up: `expr $1 * ...' would need
# the asterisk quoted and the inner substitution escaped.
echo 'down() {' > /f5
echo '	if test $1 -gt 0' >> /f5
echo '	then' >> /f5
echo '		echo FN-DEPTH-$1' >> /f5
echo '		down `expr $1 - 1`' >> /f5
echo '	fi' >> /f5
echo '}' >> /f5
echo 'down 3' >> /f5
echo 'echo AFTER-5' >> /f5
sh /f5
echo == 6 redefinition replaces the body
echo 'twice() { echo FN-FIRST ; }' > /f6
echo 'twice' >> /f6
echo 'twice() { echo FN-SECOND ; }' >> /f6
echo 'twice' >> /f6
echo 'echo AFTER-6' >> /f6
sh /f6
echo == 7 a function is looked up before a command of the same name
echo 'echo FN-NOT-THE-FILE > /f7cmd' > /f7
echo 'chmod 755 /f7cmd' >> /f7
echo 'PATH=/:$PATH' >> /f7
echo 'f7cmd() { echo FN-BEAT-THE-FILE ; }' >> /f7
echo 'f7cmd' >> /f7
echo 'echo AFTER-7' >> /f7
sh /f7
echo == 8 return leaves a loop and the function with it
echo 'loop8() {' > /f8
echo '	i=0' >> /f8
echo '	while test $i -lt 9' >> /f8
echo '	do' >> /f8
echo '		i=`expr $i + 1`' >> /f8
echo '		if test $i = 4' >> /f8
echo '		then' >> /f8
echo '			echo FN-COUNT=$i' >> /f8
echo '			return 0' >> /f8
echo '		fi' >> /f8
echo '	done' >> /f8
echo '	echo FN-LOOP-RAN-ON' >> /f8
echo '}' >> /f8
echo 'loop8' >> /f8
echo 'echo AFTER-8' >> /f8
sh /f8
echo == 9 a function is NOT inherited across a fork -- see the header
echo 'nine() { echo FN-IN-PARENT ; }' > /f9
echo 'nine' >> /f9
echo '( nine )' >> /f9
echo 'echo AFTER-9' >> /f9
sh /f9
# CASE 10 IS NOT A KEYWORD TEST.  Every word in this file is a keyword only
# where it is one -- `done' closes case 8's loop and nothing here hands a
# reserved word to a command as an ARGUMENT, which is a position with its own
# lexing rule and its own defect history.  That is gated by
# tests/shkeyword/run.sh, which needs no image.
echo == 10 several commands on one line, and the and/or list
echo 'echo TEN-A ; echo TEN-B ; echo TEN-C' > /f10
echo 'test 1 = 1 && echo TEN-AND' >> /f10
echo 'test 1 = 2 || echo TEN-OR' >> /f10
echo 'echo AFTER-10' >> /f10
sh /f10
echo == 11 a here-document that is not the last thing on the line
echo 'cat <<EOF > /o11' > /f11
echo 'HEREDOC-BODY' >> /f11
echo 'EOF' >> /f11
echo 'echo AFTER-11' >> /f11
sh /f11
echo == 12 a glob in a redirection target
echo 'echo GLOBBED > /o12target' > /f12
echo 'echo GLOB-REDIR > /o12tar*' >> /f12
echo 'echo AFTER-12' >> /f12
sh /f12
echo == 13 the directory stack -- pushd, dirs, popd
echo 'cd /' > /f13
echo 'pushd /bin' >> /f13
echo 'echo DIRS-AT=`pwd`' >> /f13
echo 'popd' >> /f13
echo 'echo DIRS-BACK=`pwd`' >> /f13
echo 'echo AFTER-13' >> /f13
sh /f13
echo == 14 a function defined and called at THIS prompt, in the driving shell
here14() { echo FN-AT-PROMPT ; }
here14
echo == what reached the disk
cat /o11
cat /o12target
echo == 15 /etc/rc, the boot script, read by this shell
sh /etc/rc
echo RC-STATUS=$?
echo == mounts rc made
/etc/mount
echo == cores -- there should be none
ls -l /core
echo == ALLDONE
