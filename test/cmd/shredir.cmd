# shredir.cmd -- does a COMPOUND command survive a redirection?
#
#	EMUWAIT=1800 hostbuild/emu-run.sh tests/cmd/shredir.cmd
#
# Nothing in /etc/rc, /etc/rc.net or any shipped profile redirects a compound
# command, so only this gate exercises it.  The suspect path: `( ... ) > f'
# arrives at comscom() (cmd/sh/exec1.c) as one NCOMS with an NCTRL node, an
# NIORS node and NO argument words; the FIORS bit routes it through inline()
# (cmd/sh/exec3.c), which hashes nargv[0] -- NULL, there being no command
# name.  A plain `( ... )' with no redirect never sets FIORS and never
# reaches inline().
#
# Each construct runs in a CHILD sh reading a script, never at this prompt: a
# crash at the prompt would end the login session, while in a child the
# crash is confined and the driving shell reports it and carries on.
#
# Every script ends with `echo AFTER-<n>'.  A MISSING `AFTER-<n>' is the
# failure, and it is the only reliable signal -- the case's own output may well
# have been redirected into a file, so seeing nothing from a case proves
# nothing on its own.  Sixteen cases, sixteen AFTER lines; count them, and
# count them at the START of a line, because the transcript also echoes the
# `echo AFTER-<n> >> /v<n>' that WROTE each script.
#
# THE CONTROLS ARE LOAD-BEARING.  Cases 1, 13 and 14 -- a plain subshell, a
# subshell in a pipe, and a simple command with a redirect -- must pass;
# without them a shell that had merely stopped running compound commands at
# all would pass this file.
#
# CASE 7 IS NOT A CONTROL: any redirection word at all sets FIORS, `<'
# included, so an INPUT redirect reaches inline() by exactly the same route.
#
# CASE 11 IS A DIFFERENT ANSWER.  `case x in x) echo matched ;; esac > /o11'
# is refused with `Syntax error in line 1'.  That is the grammar, not this
# defect: sh.y ends the case statement at `esac' and does not accept a
# redirection after it.  A `Syntax error' here is a PASS for this file, and a
# `Segmentation violation' would not be.
#
# The tail of the run reads back the files the redirections were supposed to
# create: a case can `pass' its AFTER line and still have written nothing,
# and the redirect is the thing under test.  Case 10's condition is test(1)
# because this system has no true(1) -- only the `:' built-in, and `Can't
# find true' would leave /o10 empty behind a passing AFTER line.
#
# For test/cmd/run.sh: the sixteen AFTER lines; what the read-back must show --
# `inside' from the subshells and the group, `a' and `b' from the for loop,
# `loop', `yes', `simple' -- with no `cat:' complaint about a file a redirect
# never made and no `pre' left in the file case 3 had to overwrite; and no
# `Segmentation violation' or /core.  Case 11's `Syntax error' is allowed.
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
#% expect ^AFTER-14$
#% expect ^AFTER-15$
#% expect ^AFTER-16$
#% expect ^inside$
#% expect ^data$
#% expect ^a$
#% expect ^b$
#% expect ^loop$
#% expect ^yes$
#% expect ^simple$
#% expect ^== ALLDONE$
#% reject ^cat: /o
#% reject ^pre$
#% expect ^/core: no such file or directory$
#% reject ^-.* /core$
#% reject Segmentation violation
#% reject ^Panic:
#% wait 1800
echo == 1 plain subshell, no redirect -- CONTROL
echo '( echo inside )' > /v1
echo 'echo AFTER-1' >> /v1
sh /v1
echo == 2 subshell, redirect to a NEW file
echo '( echo inside ) > /o2' > /v2
echo 'echo AFTER-2' >> /v2
sh /v2
echo == 3 subshell, redirect over an EXISTING file
echo pre > /o3
echo '( echo inside ) > /o3' > /v3
echo 'echo AFTER-3' >> /v3
sh /v3
echo == 4 brace group, redirect
echo '{ echo inside ; } > /o4' > /v4
echo 'echo AFTER-4' >> /v4
sh /v4
echo == 5 subshell, APPEND
echo '( echo inside ) >> /o5' > /v5
echo 'echo AFTER-5' >> /v5
sh /v5
echo == 6 subshell, stderr redirect
echo '( echo inside ) 2> /o6' > /v6
echo 'echo AFTER-6' >> /v6
sh /v6
echo == 7 subshell, INPUT redirect
echo data > /i7
echo '( cat ) < /i7' > /v7
echo 'echo AFTER-7' >> /v7
sh /v7
echo == 8 for loop, redirect
echo 'for i in a b ; do echo $i ; done > /o8' > /v8
echo 'echo AFTER-8' >> /v8
sh /v8
echo == 9 while loop, redirect
echo 'n=1' > /v9
echo 'while test $n = 1 ; do echo loop ; n=2 ; done > /o9' >> /v9
echo 'echo AFTER-9' >> /v9
sh /v9
echo == 10 if, redirect
echo 'if test 1 = 1 ; then echo yes ; fi > /o10' > /v10
echo 'echo AFTER-10' >> /v10
sh /v10
echo == 11 case, redirect -- SYNTAX ERROR, see the header
echo 'case x in x) echo matched ;; esac > /o11' > /v11
echo 'echo AFTER-11' >> /v11
sh /v11
echo == 12 nested subshells, redirect on the outer
echo '( ( echo inside ) ) > /o12' > /v12
echo 'echo AFTER-12' >> /v12
sh /v12
echo == 13 subshell in a PIPE, no redirect -- CONTROL
echo '( echo inside ) | cat' > /v13
echo 'echo AFTER-13' >> /v13
sh /v13
echo == 14 simple command with a redirect -- CONTROL
echo 'echo simple > /o14' > /v14
echo 'echo AFTER-14' >> /v14
sh /v14
echo == 15 EMPTY subshell with a redirect
echo '( ) > /o15' > /v15
echo 'echo AFTER-15' >> /v15
sh /v15
echo == 16 assignment in front of a redirected subshell
echo 'X=1 ( echo inside ) > /o16' > /v16
echo 'echo AFTER-16' >> /v16
sh /v16
echo == what reached the disk -- each should print inside/its own word
cat /o2
cat /o3
cat /o4
cat /o5
cat /o8
cat /o9
cat /o10
cat /o12
cat /o14
echo == cores -- there should be none
ls -l /core
echo == ALLDONE
