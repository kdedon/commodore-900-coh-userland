# smtpdata.cmd -- what smtpd does with a DATA transfer that ends without its
# terminating `.'.
#
#	hostbuild/emu-run.sh test/cmd/smtpdata.cmd
#
# RFC 821 accepts a message on the end-of-mail-data line and on nothing else.
# A transfer the sender abandoned in the middle of the body was never accepted,
# so it must not reach a mailbox looking like a letter somebody finished.
#
# THE CONNECTION IS A PIPE, and that is the shipped path rather than a
# simplification.  A connection on this system is a pair of FIFOs plus framing
# state that no exec(2) survives, so /etc/inetd runs a service on a pipe and
# relays; the switchboard's INETD_* variables in the environment are how smtpd
# knows it is in that mode (mail/smtpd/smtpd.c).  What the daemon is told when
# the peer goes away is a zero-length read -- inetd's relay closes the program's
# standard input the instant the peer's half is gone (net/inetd.c relay()) --
# and end of file on a file is the same read(2) result at the same place in
# netline().  So a conversation fed from a file that STOPS mid-body is exactly
# the case, and it needs no network, no switchboard and no second host.
#
# THE FINISHED MESSAGE GOES FIRST, and the order is the whole design.  It puts a
# real letter in the mailbox, so the assertions about the interrupted one that
# follows are read against a file that EXISTS and has content in it; asked the
# other way round they would be asking about a mailbox that was never created,
# which is a question a daemon that delivers nothing at all also passes.
#
# The guest is MULTI USER: /usr is mounted, which is where
# /usr/spool/mail/root lives, and root is a real account with a real passwd
# entry for getpwnam() to find.
#
# THE THREE JUDGES, the same ones test/cmd/inetd.cmd uses.  A names the
# evidence and judges it, Q judges without reprinting evidence A has already
# shown, N is the same judgement inverted for what must NOT be there.
echo '/bin/cat $2' > /bin/A
echo '/bin/Q "$1" "$2" "$3"' >> /bin/A
echo 'if /bin/grep "$3" "$2" >/dev/null' > /bin/Q
echo 'then echo "ok $1"; else echo "FAIL $1"; fi' >> /bin/Q
echo 'if /bin/grep "$3" "$2" >/dev/null' > /bin/N
echo 'then echo "FAIL $1"; else echo "ok $1"; fi' >> /bin/N
/bin/chmod 755 /bin/A /bin/Q /bin/N
: 'The mailbox as the running system has it, and then emptied: what is'
: 'asserted below is what this file put in it and nothing else.'
/bin/ls -ld /usr/spool/mail
/bin/rm -f /usr/spool/mail/root
/bin/rm -f /tmp/smtp*
: 'A FINISHED MESSAGE, over the whole of the conversation: greeting, sender,'
: 'recipient, body, the dot that ends it, and QUIT.'
echo 'HELO sender' > /twhole.in
echo 'MAIL FROM:<root>' >> /twhole.in
echo 'RCPT TO:<root>' >> /twhole.in
echo DATA >> /twhole.in
echo 'Subject: finished' >> /twhole.in
echo '' >> /twhole.in
echo WHOLEBODY >> /twhole.in
echo . >> /twhole.in
echo QUIT >> /twhole.in
INETD_LOCADDR=10.0.0.2 INETD_LOCPORT=25 INETD_REMADDR=10.0.0.2 INETD_REMPORT=1024 /etc/smtpd < /twhole.in > /twhole.out 2>&1
/bin/A T_ACCEPTED /twhole.out '250 accepted'
: 'And it is in the mailbox, printed whole: the envelope line is the'
: 'daemon s and nothing here typed it.'
/bin/A T_DELIVERED /usr/spool/mail/root WHOLEBODY
/bin/Q T_ENVELOPE /usr/spool/mail/root '^From '
/bin/grep -c '^From ' /usr/spool/mail/root > /tfrom1.out
/bin/A T_ONELETTER /tfrom1.out '^1$'
: 'The mailbox is kept as it stands, to be compared against afterwards.'
/bin/cp /usr/spool/mail/root /tbox1
: 'THE INTERRUPTED TRANSFER.  The same conversation as far as the middle of'
: 'the body, and then nothing: no dot, no QUIT, no close -- the file ends,'
: 'which is the read(2) a peer that went away produces.'
echo 'HELO sender' > /ttrunc.in
echo 'MAIL FROM:<root>' >> /ttrunc.in
echo 'RCPT TO:<root>' >> /ttrunc.in
echo DATA >> /ttrunc.in
echo 'Subject: interrupted' >> /ttrunc.in
echo '' >> /ttrunc.in
echo HALFWRITTENBODY >> /ttrunc.in
INETD_LOCADDR=10.0.0.2 INETD_LOCPORT=25 INETD_REMADDR=10.0.0.2 INETD_REMPORT=1024 /etc/smtpd < /ttrunc.in > /ttrunc.out 2>&1
: 'The daemon did reach the body: it asked for one.  Without this the'
: 'assertions under it would also pass on a daemon that refused DATA'
: 'outright, which is not what is being asked.'
/bin/A T_DATASTARTED /ttrunc.out '^354 '
: 'And it did not tell the sender the message was accepted, because it'
: 'never was.'
/bin/N T_NOTACCEPTED /ttrunc.out '250 accepted'
: 'THE MAILBOX AFTER IT.  Not a line of that transfer is in it, there is'
: 'still exactly one letter, and it is the same bytes as before -- which is'
: 'the assertion a mark inside a delivered fragment would also fail.'
/bin/N T_NOTDELIVERED /usr/spool/mail/root HALFWRITTENBODY
/bin/grep -c '^From ' /usr/spool/mail/root > /tfrom2.out
/bin/A T_STILLONELETTER /tfrom2.out '^1$'
/bin/cmp /tbox1 /usr/spool/mail/root > /tcmp.out 2>&1
/bin/N T_UNCHANGED /tcmp.out .
: 'And the spool file it wrote the body into is gone rather than left in'
: '/tmp for the next thing to trip over.'
/bin/ls /tmp > /ttmp.out 2>&1
/bin/N T_NOSPOOLLEFT /ttmp.out '^smtp'
