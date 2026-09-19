# mail.cmd -- does local mail reach a mailbox and come back out of it?
#
#	hostbuild/emu-run.sh test/cmd/mail.cmd
#
# The whole round trip a machine with no network has to be able to do: root
# logs in at the console of a MULTI-USER system, sends a letter to root, and
# mail(1) reads it back.  One account and one mailbox: mail(1) is its own local
# delivery agent (cmd/mail/send.c usend()), so nothing here needs a second
# user, a transport, or a daemon.
#
# IT RUNS IN MULTI-USER, as every emu-run.sh boot now does: the guest comes up
# to the console getty and emu-run.sh logs in as root.  The first two lines end
# that session and log in again at the getty's next prompt, so the session
# the letter is sent from is a second, fresh login on the same console -- a
# logout and a login in the middle of a running system, which init has to
# notice (cmd/init.c waits on the shell) and getty has to answer again.
# Ctrl-D would do the same thing and emu-run.sh cannot send one.
#
# GATE comes first because nothing after it prompts with `#' at the moment it is
# fed: getty is asking for a name, then mail is asking for a Subject.  Past the
# gate the emulator paces on the console falling quiet instead, which is why the
# letter can be TYPED here -- Subject prompt, body, `.' terminator -- exactly as
# a person types it.
#
# For test/cmd/run.sh.  The login on the console out of utmp; the empty spool
# before, the notification -m asks for, and the letter as the DELIVERY AGENT
# wrote it -- the envelope and the headers, which no line here types.  The
# Subject and the body are not required on their own: the console echoes both
# as they are typed, so the typing alone would satisfy them.  NOT covered line
# by line: that T_READ's `mail -p' prints the letter as well as T_BOX's cat --
# the two print the same lines, so one required line cannot tell them apart.
#% expect ^T_READ$
#% expect ^root +console .*$
#% expect ^d.* /usr/spool/mail$
#% expect ^No mailbox '/usr/spool/mail/root'\.$
#% expect ^root: you have mail\.$
#% expect ^From root .* GMT$
#% expect ^Message-Id: <.*>$
#% expect ^From: c900!root \(System Administrator\)$
#% expect ^To: +root$
#% reject Segmentation violation
#% reject ^Panic:
#% wait 1200
#
# root has no password (dist/files/etc/passwd), so the name is the whole login.
GATE
exit
root
# The evidence that this is a login session on the console: who(1) reads it
# out of /etc/utmp, which login wrote.  And the spool directory as the running
# system sees it.
echo T_LOGIN
who am i
/bin/ls -ld /usr/spool/mail
# No mailbox yet.  This is the negative control for the two assertions at the
# bottom: they read a mailbox, and a mailbox that was already there would let
# them pass without anything being delivered.
echo T_EMPTY
mail -p
# The letter.  -m so that mail also notifies the recipient at the terminal
# /etc/utmp says they are on -- root, at this console.  The line after the
# command is the answer to mail's `Subject: ' prompt, which is why it carries no
# `Subject:' of its own; the line after that is the body, and `.' ends it.
echo T_SEND
mail -m root
c900 round trip
c900mailbody
.
# The mailbox itself: the envelope line and the headers are the delivery
# agent's, and no line of this file types them.
echo T_BOX
/bin/cat /usr/spool/mail/root
# And back out through the reader.  -p prints every letter and exits, which is
# the non-interactive half of the same code the `? ' prompt drives.
echo T_READ
mail -p
