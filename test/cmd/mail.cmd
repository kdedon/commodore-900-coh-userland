# mail.cmd -- does local mail reach a mailbox and come back out of it?
#
#	make -C os/hostbuild mail-test DIST=<dist>
#
# The whole round trip a machine with no network has to be able to do: root
# logs in at the console of a MULTI-USER system, sends a letter to root, and
# mail(1) reads it back.  One account and one mailbox: mail(1) is its own local
# delivery agent (cmd/mail/send.c usend()), so nothing here needs a second
# user, a transport, or a daemon.
#
# IT RUNS IN MULTI-USER, and that is the point of the first two lines.  The
# mailbox lives at /usr/spool/mail/<user>, which is on the /usr filesystem, and
# only /etc/rc mounts /usr -- so `exit' hands the single-user shell back to
# init, init runs rc, and the console getty then offers the login this test
# actually uses.  Ctrl-D would do the same thing and emu-run.sh cannot send one;
# a shell that exits is the same event to init (cmd/init.c waits on it).
#
# GATE comes first because nothing after it prompts with `#' at the moment it is
# fed: rc is running, then getty is asking for a name, then mail is asking for a
# Subject.  Past the gate the emulator paces on the console falling quiet
# instead, which is why the letter can be TYPED here -- Subject prompt, body,
# `.' terminator -- exactly as a person types it.
#
# root has no password (dist/files/etc/passwd), so the name is the whole login.
GATE
exit
root
# The evidence that this is a login session on the console and not the
# single-user shell: who(1) reads it out of /etc/utmp, which login wrote.  And
# the spool directory as the running system sees it, /usr having been mounted by
# rc rather than by anything typed here.
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
