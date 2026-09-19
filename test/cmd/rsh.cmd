# rsh.cmd -- does /usr/bin/rsh, the RESTRICTED shell, actually restrict?
#
#	EMUWAIT=2400 hostbuild/emu-run.sh tests/cmd/rsh.cmd
#
# rsh is the same binary as /bin/sh, restricting itself when argv[0] says
# `rsh' (cmd/sh/main.c); this file is the gate that the restrictions hold.
#
# WHAT MAKES THIS A TEST AND NOT A DEMONSTRATION.  Every restriction is
# exercised TWICE, by two accounts that differ in exactly one field of
# /etc/passwd -- their shell.  rman gets /usr/bin/rsh, oman gets /bin/sh, and
# they share one home directory and therefore one .profile, so they start from
# an identical environment.  A line that rman is refused and oman is not is
# evidence that the SHELL is what refused it.  Watching rman alone fail would
# prove nothing: a missing command and a refused one look the same.
#
# READING THE TRANSCRIPT.  Each account prints MARK-START-<user> and
# MARK-END-<user> around its attempts.  Between them, expect:
#
#   oman (/bin/sh)          rman (/usr/bin/rsh)
#   AT-ROOT                 Restricted: cd        + Can't find ./probe
#   (ls lists /rsafe)       Restricted: PATH=/bin + Can't find ls
#   (no output)             Restricted: SHELL=/bin/evil
#   (creates out-oman)      Restricted: > /rhome/out-oman
#   SLASH-RAN-oman          Restricted: /rsafe/echo
#   ENVASSIGN-RAN-oman      Restricted: PATH=/bin
#   non-existent group      Restricted: newgrp
#
# newgrp is an escape rsh must refuse, and it is the one line of this file
# whose two halves do not pair up.  `newgrp' is a BUILTIN: s_login runs
# /bin/newgrp by absolute pathname without consulting PATH, so no confined PATH
# can keep it away, and /bin/newgrp ends by exec'ing /bin/sh with argv[0] `-',
# which is an unrestricted LOGIN shell.  rman's `Restricted: newgrp' is the
# shell refusing the builtin outright, before any of that, and it is direct
# evidence on its own.
#
# THE CONTROL DOES NOT DEMONSTRATE THE ESCAPE.  oman answers `non-existent
# group' and gets no shell: this /bin/newgrp reports a bad group name and
# exits rather than exec'ing a shell on the failure path.  So the pairing
# proves the refusal and not what is being refused.  Reaching the exec would
# take a group oman is actually a member of, which needs a second /etc/group
# entry this file does not make.
#
# `Can't find' after a refusal is the point of the pairing: cd was refused, so
# ./probe is not where the shell is looking, and PATH was refused, so ls is not
# on it.  The refusal is what caused the second failure.
#
# THE PROFILE ORDERING.  The restrictions must begin only AFTER .profile runs,
# or the administrator cannot build the environment the user is confined to.
# /rhome/.profile does BOTH of the things rman is later refused -- it sets PATH
# and it cds -- and prints PROFILE-CD-AND-PATH-OK.  That line appearing for
# rman, with no `Restricted:' before it, is the ordering test.  Its absence, or
# a refusal in front of it, means the flag is being set too early and the
# feature is useless.
#
# THE ESCAPE.  eman is the third account and it is meant to succeed: its
# profile leaves /bin on the PATH, so `sh' is a command name with no `/' in it
# that rsh will happily run, and the sh it starts is not restricted.  ESCAPED
# in the transcript is the documented limitation working exactly as documented
# (man rsh, "sh itself must not be on that PATH") -- it is NOT a regression.
# If ESCAPED ever stops appearing, the man page is the thing that is now wrong.
#
# EMUWAIT IS NOT OPTIONAL.  emu-run.sh kills the emulator after EMUWAIT seconds,
# 300 by default, and this file is three logins long -- around twenty minutes of
# guest time.  At the default the run ENDS PARTWAY THROUGH and still exits 0,
# leaving a transcript that simply stops: the restricted account's section is
# then MISSING rather than failing, which reads like a test that never covered
# it.  The only sign is emu-run's own "marker never appeared" line at the
# bottom.  Confirm __EMU_DONE__ is in the transcript before believing anything
# this test appears to say -- including a pass.
#
# FOR test/cmd/run.sh, the table above line by line: each account's markers,
# oman's six results, rman's refusals and the `Can't find' each one causes,
# and eman's ESCAPED.  The lines rman must NOT produce are rejected by name:
# the three that only a command it was refused could print, the file its
# refused redirect would have made (read back at the end), and a shell out of
# newgrp.  `Restricted: PATH=/rsafe' is the ordering test's failure: it is
# what the profile's own PATH line says when the flag is set before .profile
# runs.  Which account printed PROFILE-CD-AND-PATH-OK is not something one
# required line can tell -- oman prints it too -- so that it is rman's is read
# from the transcript.
#% expect ^MARK-START-oman$
#% expect ^MARK-END-oman$
#% expect ^MARK-START-rman$
#% expect ^MARK-END-rman$
#% expect ^MARK-START-eman$
#% expect ^MARK-END-eman$
#% expect ^PROFILE-CD-AND-PATH-OK$
#% expect ^AT-ROOT$
#% expect ^ls-safe$
#% expect ^SLASH-RAN-oman$
#% expect ^ENVASSIGN-RAN-oman$
#% expect ^non-existent group$
#% expect ^REDIR-oman$
#% expect ^Restricted: cd$
#% expect ^Cannot find \./probe$
#% expect ^Restricted: PATH=/bin$
#% expect ^Cannot find ls$
#% expect ^Restricted: SHELL=/bin/evil$
#% expect ^Restricted: > /rhome/out-rman$
#% expect ^Restricted: /rsafe/echo$
#% expect ^Restricted: newgrp$
#% expect ^ESCAPED$
#% reject ^SLASH-RAN-rman$
#% reject ^ENVASSIGN-RAN-rman$
#% reject ^REDIR-rman$
#% reject ^NEWGRP-GAVE-A-SHELL$
#% reject ^Restricted: PATH=/rsafe$
#% reject Segmentation violation
#% reject ^Panic:
#% wait 2400
#
# The confined command directory a restricted account is supposed to have: one
# command, and not a shell.
mkdir /rsafe
cp /bin/echo /rsafe/echo
cp /bin/ls /rsafe/ls-safe
# `. ./probe' finds this only from /, so sourcing it reports the cwd.
echo echo AT-ROOT > /probe
# One home directory, one profile, shared by the restricted and the ordinary
# account so that the shell is the only difference between them.
mkdir /rhome
chmod 777 /rhome
#
# PS1 IS NOT DECORATION.  emu-run.sh feeds one line and then waits for a `#'
# prompt before feeding the next; that pacing keeps input off the floor, and
# a login shell prompting with the default `$ ' never releases it.  Releasing
# the gate instead (GATE) hands the whole rest of the file to the tty at
# once, which overruns its clists.  Giving these shells a `>' prompt keeps
# every line paced -- emu-run.sh's gate accepts `#' or `>'.  It is exported
# so the sh that eman starts inherits it and stays paced too.
#
# It is `>' and not `#' so the transcript says WHICH SHELL read each line:
# with both prompts `#', a failed login leaves the lines being typed at the
# root shell, and root doing what rsh was supposed to refuse reads exactly
# like rsh failing to refuse it.
echo "PS1='> '" > /rhome/.profile
echo export PS1 >> /rhome/.profile
echo PATH=/rsafe >> /rhome/.profile
echo cd / >> /rhome/.profile
echo echo PROFILE-CD-AND-PATH-OK >> /rhome/.profile
echo cd /rhome >> /rhome/.profile
# The third account, whose profile leaves the escape open on purpose.
mkdir /ehome
chmod 777 /ehome
echo "PS1='> '" > /ehome/.profile
echo export PS1 >> /ehome/.profile
echo PATH=/rsafe:/bin >> /ehome/.profile
cat /rhome/.profile
echo rman::100:1:restricted:/rhome:/usr/bin/rsh >> /etc/passwd
echo oman::101:1:ordinary:/rhome:/bin/sh >> /etc/passwd
echo eman::102:1:escapes:/ehome:/usr/bin/rsh >> /etc/passwd
# Each login is run in a SUBSHELL: login(1) execs over the process it is given,
# so a bare `login' would end the root shell and with it the rest of this file.
# login(1) locks the tty and leaves the lock behind when the session ends;
# a later login then fails with "cannot lock terminal" and its lines are
# read by the root shell instead.  Clear the lock first.
#
# The GLOB is load-bearing.  The lock is not named after the tty: lock.c's
# gen_res_name() builds the name from the device's MAJOR.MINOR, so it is
# LCK..<maj>.<min> and not LCK..console.  Removing the name the error message
# prints deletes nothing, silently, and the run looks the same as before.
rm -f /usr/spool/uucp/LCK..*
echo ==== ordinary shell, the control ====
( login oman )
echo MARK-START-oman
cd /
. ./probe
PATH=/bin
ls /rsafe
SHELL=/bin/evil
echo REDIR-oman > /rhome/out-oman
/rsafe/echo SLASH-RAN-oman
PATH=/bin echo ENVASSIGN-RAN-oman
( echo echo NEWGRP-GAVE-A-SHELL | newgrp nosuchgroup )
echo MARK-END-oman
exit
rm -f /usr/spool/uucp/LCK..*
echo ==== restricted shell, the same profile ====
( login rman )
echo MARK-START-rman
cd /
. ./probe
PATH=/bin
ls /rsafe
SHELL=/bin/evil
echo REDIR-rman > /rhome/out-rman
/rsafe/echo SLASH-RAN-rman
PATH=/bin echo ENVASSIGN-RAN-rman
newgrp nosuchgroup
echo MARK-END-rman
exit
rm -f /usr/spool/uucp/LCK..*
echo ==== restricted shell with sh left on the PATH ====
( login eman )
echo MARK-START-eman
sh
cd /
. ./probe
echo ESCAPED
exit
echo MARK-END-eman
exit
echo ==== what reached the disk ====
cat /rhome/out-oman
cat /rhome/out-rman
