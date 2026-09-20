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
# The first two lines log out and back in at the console getty, so the letter
# is sent from a fresh login that init and getty had to handle.
#
# GATE comes first: getty and mail's Subject prompt don't end in `#'.  Past
# it the emulator paces on console quiet, so the letter is typed as a person
# would.
#
# The Subject and body aren't required alone, since the console echoes them;
# the envelope and headers the delivery agent wrote are.
#% needs runtime base
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
#
# root has no password (dist/files/etc/passwd), so the name is the whole login.
GATE
exit
root
# who(1) shows the console login from /etc/utmp; then the spool directory.
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
