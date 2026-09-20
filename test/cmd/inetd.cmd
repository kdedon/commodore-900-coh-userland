# inetd.cmd -- the switchboard, every path it has, with both ends on this
# machine.
#
#	hostbuild/emu-run.sh test/cmd/inetd.cmd
#
# No serial line, no slip, no peer: the stack routes a connection to its own
# address internally, so a whole accept/fork/exec/relay round trip runs under
# the emulator.
#
# THE GUEST IS MULTI USER, and everything rc.net starts is already running when
# the first line below is typed: /dev/inet, the inet daemon, the interface
# address, syslogd, the mounted /usr, and a switchboard on /etc/inetd.conf
# holding echo, daytime, telnet, finger, shell, smtp and ntalk.  So this file
# starts none of them.  What it does is STOP the one daemon it has to replace:
# two inetds asking for one set of ports leave the second holding none of them,
# and the first connection then never completes.  Everything else rc.net brought
# up is left alone and used.
#
# THE ASSERTIONS ARE ON THE GUEST, in /bin/A, /bin/Q and /bin/N, and every one of
# them prints `ok <tag>' or `FAIL <tag>' on the console.  A judgement that lives
# in a reader's head is not one, and a transcript that only shows output cannot
# say whether the output was right.
#
# T_SIZE is reported, not judged.
#% needs net testing base
#% expect ^ok T_DROPRACE$
#% expect ^ok T_DROPUP$
#% expect ^ok T_DROPPED$
#% expect ^ok T_DROPRSH$
#% expect ^ok T_DROPINT$
#% expect ^ok T_DROPDAY$
#% expect ^ok T_DROPGONE$
#% expect ^ok T_DROPCOUNT$
#% expect ^ok T_REPECHO$
#% expect ^ok T_REPDAY$
#% expect ^ok T_REPTELNET$
#% expect ^ok T_REPSMTP$
#% expect ^ok T_REPFINGER$
#% expect ^ok T_REPSHELL$
#% expect ^ok T_REPTALK$
#% expect ^ok T_REPALL$
#% expect ^ok T_READY$
#% expect ^ok T_NOTINSTMSG$
#% expect ^ok T_INTERNAL$
#% expect ^ok T_EXEC$
#% expect ^ok T_DOWN$
#% expect ^ok T_DOWNCLEAN$
#% expect ^ok T_ENV$
#% expect ^ok T_ENVPORT$
#% expect ^ok T_NEG$
#% expect ^ok T_FINGERD$
#% expect ^ok T_REMSHD$
#% expect ^ok T_REMSHDU$
#% expect ^ok T_REMSHDUID$
#% expect ^ok T_TELNETD$
#% expect ^ok T_SMTPD$
#% expect ^ok T_SMTPMAIL$
#% expect ^ok T_SMTPBODY$
#% expect ^ok T_NOTINST$
#% expect ^ok T_NTALK$
#% expect ^ok T_NTALKOK$
#% expect ^ok T_TALKKID$
#% expect ^ok T_TALKCHILD$
#% expect ^ok T_TALKGONE$
#% expect ^ok T_TALKREAPED$
#% expect ^ok T_NOTALKD$
#% expect ^ok T_SYSLOG$
#% expect ^ok T_REFUSED$
#% expect ^ok T_NOSPIN$
#% expect ^ok T_NOSPINBOUND$
#% expect ^ok T_CEILING$
#% expect ^ok T_CEILINGOK$
#% expect ^ok T_LEAK$
#% expect ^ok T_LEAKOK$
#% expect ^ok T_LASTREADY$
#% expect ^ok T_LASTGONE$
#% expect ^ok T_LASTREBOUND$
#% expect ^ok T_LASTNOTDEAD$
#% expect ^ok T_LASTALIVE$
#% expect ^ok T_LASTANSWERED$
#% expect ^ok T_LASTAGAIN$
#% expect ^ok T_BACKHELD$
#% expect ^ok T_BACKREFUSED$
#% expect ^ok T_BACKBOUND$
#% expect ^ok T_HELDTAKEN$
#% expect ^ok T_HELDTRIES$
#% expect ^ok T_HELDSPENT$
#% expect ^ok T_HELDSTOP$
#% expect ^ok T_HELDGONE$
#% reject ^FAIL T_
#% reject Segmentation violation
#% reject ^Panic:
#
# Commentary is in `#' lines, which are not typed: the script is near the
# emulator's 8192-byte input limit.
#
# Stop on the mark alone: park counts host loops and would take the
# ten-minute hold below for a hang.
#% stop mark
#
# FOUR PATHS, AND NONE OF THEM COVERS ANOTHER:
#
#   port 7     an INTERNAL service.  The forked child answers on the socket
#              itself -- no pipe, no exec, no second process.
#   port 7777  an EXTERNAL service, /bin/echo, which proves exec(2) happened and
#              that argv came from the conf file (the reply is the ARGUMENT
#              `hello', which is also exactly what echoclient sent, so the
#              round trip is byte-exact and echoclient can score it).  It says
#              nothing about the peer-to-program direction: /bin/echo never
#              reads its standard input.
#   port 7778  the OTHER direction.  /bin/sh reads the connection as commands,
#              so the reply can only exist if the peer's bytes reached the
#              program's standard input.  The command is /bin/hello, written
#              below to echo the word back -- so this too is byte-exact.
#              /bin/hello ALSO records $INETD_REMADDR and $INETD_LOCPORT, which
#              is how the address hand-over (the environment variables a relayed
#              service reads instead of getpeername) is checked: those values
#              cannot appear unless inetd put them there.
#   port 7779  configured nowhere.  `connect failed' is the required answer --
#              without it, a client that somehow succeeded against anything at
#              all would look like a working switchboard.
#
# THEN THE THREE REAL DAEMONS, each converted to answer on standard input and
# standard output so that it can be served from here at all (task #277).  These
# are what the switchboard exists for; /bin/echo and /bin/sh above only prove
# the mechanism.
#
#   port 79    fingerd, END TO END WITH ITS OWN CLIENT.  `finger root@10.0.0.2'
#              is the whole path in one command: the client opens the real
#              service port, inetd accepts and relays, fingerd reads the query
#              off its standard input, runs finger(1) on a pipe, and the answer
#              comes back up.  The assertion is the GECOS field out of
#              /etc/passwd, which only finger(1) running on the server can have
#              produced -- nothing in this file types it.  (The client hardwires
#              port 79, TCPPORT_FINGER, so this service cannot be moved to a
#              test port the way /bin/echo could.)
#
#   port 514   remshd, TWICE, and the two answers are opposite.
#
#              `-l root' MUST BE REFUSED and the caller must be told why.  The
#              daemon asks for no password -- it authenticates on the caller's
#              address -- and root's /etc/passwd field is empty so that the
#              machine's own keyboard can never be locked out, so a command run
#              here as uid 0 would be a root shell for whoever the address
#              database names.  The refusal is before the .rhosts check, so no
#              file can change the answer; T_SYSLOG below is the other half,
#              because a service under the switchboard reports on standard error
#              and that is a pipe to syslog(3).
#
#              `-l guest' MUST BE SERVED, which is what says the port was closed
#              to root and not simply broken.  It also tests the ADDRESS
#              HAND-OVER WHERE IT MATTERS: remshd authenticates on the peer's
#              address and on its port being a reserved one, and under inetd it
#              can ask the descriptor for neither -- both come out of
#              INETD_REMADDR and INETD_REMPORT.  So a session served at all is
#              one whose address parsed correctly and matched a .rhosts.  The
#              command prints $INETD_REMADDR from the environment it inherited,
#              so the transcript shows the value that authenticated it, and it
#              writes the file in /tmp whose OWNER is then listed: that owner is
#              the uid the daemon's child set, and it is guest.
#
#   telnetd    NOT over the network here, and the reason is worth stating: a
#              telnet login is a getty, a login(1) and a shell, which is twenty
#              minutes of guest time (see test/cmd/rsh.cmd) and needs an
#              interactive client to drive.  What IS tested is the part that is
#              new -- telnetd started with the switchboard's variables in its
#              environment and its connection on a pipe.  Running it by hand
#              with those variables set, standard input on /dev/null and standard
#              output in a file, it must take the inetd path: allocate a pty,
#              write its five telnet options ON STANDARD OUTPUT rather than on a
#              socket it never opened, see end of file at once, retire the utmp
#              records and exit.  The file is then FIFTEEN BYTES -- five options
#              of three -- and that is a computed number no other path produces.
#              A telnetd that missed the environment would instead look up the
#              telnet service and block in a passive open for ever, and this
#              test would time out with an empty file.
#
#   ntalk      A DATAGRAM SERVICE, answered INSIDE the switchboard.  There is no
#              accept() and no program: on the first datagram inetd forks a child
#              without exec and gives it the socket, and that child holds the
#              invitation table.  /bin/talkping asks for something that can only
#              come out of that table -- an id minted while a DIFFERENT datagram
#              was being answered -- which is the assertion no per-datagram
#              service can pass.  Its own first step (a lookup before anything was
#              left) is the control that says the answers are not constant.
#
#              Then the LIFECYCLE, in two `ps' samples, which is where the memory
#              claim is settled on the machine rather than computed off the host:
#              right after the call there are TWO inetds (the parent and the child
#              holding talk's state), and after the invitation has been deleted
#              and the child's idle timer has run out there is ONE.  Nothing is
#              resident between calls, and /etc/talkd does not exist.
#
#   A SERVICE'S DIAGNOSTICS GO TO syslog AND NOT TO THE PEER.  /bin/hello, the
#              program behind port 7778, reads a file that is not there, so cat
#              writes a complaint on its standard error.  The same run therefore
#              asserts both halves: T_DOWN is byte-exact, so the complaint did NOT
#              reach the connection, and /usr/adm/syslog holds it, so it went
#              somewhere an administrator reads.  Those are opposite assertions
#              about the same bytes -- with stderr on the old shared pipe, T_DOWN
#              fails and the log is empty.
#
# Then /bin/chanmax measures how many connections one process can hold, which is
# the ceiling this design lives under and the number that decides how many
# services may be configured.
#
# The conf file is written here rather than taken from /etc: the shipped one has
# no external service in it (running a program off a port is not something to
# ship enabled), and a test that edits no shipped file runs against any dist.
# /bin/hello and the two files under / are on a per-run COPY of the image.
#
# SEVEN services, which is MAXSERV exactly (inetd.c), so the `inetd: ready' count
# below is the number to check and there is no room for an eighth.
#
#   port 25    smtpd, MAIL END TO END AND OVER THE NETWORK.  smtpsend(1) opens
#              port 25 on this machine, inetd accepts and relays, smtpd reads the
#              SMTP conversation off its standard input and appends the message
#              to /usr/spool/mail/root.  The assertion is the delivered mailbox:
#              the `From ' envelope line and the \1\1 separator around the
#              message are written by the SERVER's deliver(), and the mailbox
#              does not exist at all on a fresh image -- so a file with those in
#              it cannot exist unless the whole path ran.  This is the path the
#              shipped inetd.conf's smtp line serves.
#
#   port 7780  A SERVICE WHOSE PROGRAM IS NOT INSTALLED.  inetd must report it
#              and NOT bind the port: that skip is what makes an enabled
#              inetd.conf line conditional on the dist list that installs the
#              daemon, which is how the smtp line above ships enabled on a
#              machine with no mail transport.  Two assertions, because either
#              alone would pass on a broken switchboard: the message naming the
#              program, and `connect failed' on the port.
#
# AND WHAT EVERY ONE OF THOSE SERVICES DOES WHEN ITS CONNECTION GOES AWAY.
# That is asked FIRST, before this file replaces anything, and it is asked of
# the switchboard rc.net started on the SHIPPED /etc/inetd.conf -- the table a
# machine actually offers.  /bin/dropclient opens the real ports, holds the
# connections while a `ps' is taken, and then exits with every socket still
# open: no close(2), nothing said to the stack, which is what a client that
# crashed leaves behind.  inetd's relay answers that by closing the program's
# standard input, so what is under test is what each program does on a
# zero-length read -- telnetd, fingerd and smtpd all end their session on it,
# remshd never gets that far because it refuses a caller on an unprotected port
# and says so, and the two internal services are answered inside inetd itself.
# The batch is dropped twice over: once after four minutes, and once as soon as
# the connections are open, which is the harder case -- the end of file is then
# already waiting when the service starts.
#
# ntalk is the exception, and it is named where it is met: a datagram service
# has no connection to drop, and its child is meant to outlive the datagram
# that started it because the invitation table is state between datagrams.  Its
# bound is its own idle timer, asserted at the far end of this file as
# T_TALKGONE.
#
# THE THREE JUDGES.  A names the evidence and judges it, Q judges without
# repeating evidence A has already printed, and N is the same judgement
# inverted, for the assertions that are about something NOT being there.  Each
# takes a tag, a file and a pattern, and each prints one line.
echo '/bin/cat $2' > /bin/A
echo '/bin/Q "$1" "$2" "$3"' >> /bin/A
echo 'if /bin/grep "$3" "$2" >/dev/null' > /bin/Q
echo 'then echo "ok $1"; else echo "FAIL $1"; fi' >> /bin/Q
echo 'if /bin/grep "$3" "$2" >/dev/null' > /bin/N
echo 'then echo "FAIL $1"; else echo "ok $1"; fi' >> /bin/N
/bin/chmod 755 /bin/A /bin/Q /bin/N
# What the boot configured and mounted, before anything changes.
/etc/ifconfig
/etc/mount
# WHAT A DROPPED CONNECTION LEAVES BEHIND, on rc.net's shipped switchboard.
# /bin/dropclient opens real ports through inetd, holds them while ps runs,
# then _exit(2)s with every socket open.  A service that ignores the end of
# file on its stdin is a process left per drop.
# Two held at once, within inetd's -m 4; these two wait for the caller to
# speak first, so ps can catch them.
# The sample taken while held proves the connections were served at all.
/bin/ps -ax > /td0.out
# A drop right after connect: telnetd forks its login as the carrier goes,
# so it must hang the child up by name or leave it asleep in open() forever.
/bin/dropclient 10.0.0.2 0 23 > /tdrace.out 2>&1
/bin/A T_DROPRACE /tdrace.out 'port 23: connected'
/bin/dropclient 10.0.0.2 600 79 25 > /tdrop.log 2>&1 &
sleep 20
/bin/ps -ax > /td1.out
/bin/cat /td1.out
/bin/awk '$3~/fingerd|smtpd/{n++}END{print n+0}' /td1.out > /tdn1.out
/bin/A T_DROPUP /tdn1.out '^2$'
# Typing the line above takes minutes of guest time, so the hold must
# outlast it.  Then time for both daemons to see end of file.
sleep 600
/bin/A T_DROPPED /tdrop.log 'dropping 2 connection'
# remshd refuses this unreserved caller port and exits on its own; the
# refusal read back shows it ran and was gone before the drop.
/bin/dropclient -r 10.0.0.2 0 514 > /tdrsh.out 2>&1
/bin/A T_DROPRSH /tdrsh.out 'unprotected port'
# The internal services, dropped the same way.  echo's reply also shows the
# switchboard still serving.  daytime closes first, so its reply is the test.
/bin/dropclient -w hello -r 10.0.0.2 0 7 13 > /tdint.out 2>&1
/bin/A T_DROPINT /tdint.out 'port 7: answered 7'
/bin/Q T_DROPDAY /tdint.out 'port 13: answered'
sleep 30
/bin/ps -ax > /td2.out
/bin/cat /td2.out
/bin/awk '$3~/telnetd|fingerd|remshd|smtpd/{n++}END{print n+0}' /td2.out > /tdn2.out
/bin/A T_DROPGONE /tdn2.out '^0$'
# The whole process count, before and after, compared as files: a stranded
# getty or uncollected relay shows too.  cmp is silent when equal.
/bin/wc -l < /td0.out > /tdc0.out
/bin/wc -l < /td2.out > /tdc1.out
/bin/cmp /tdc0.out /tdc1.out > /tdcmp.out 2>&1
/bin/cat /tdc0.out /tdc1.out
/bin/N T_DROPCOUNT /tdcmp.out .
# DOES EVERY SERVICE ANSWER THE SECOND CALLER, AND THE TENTH?  When accept()
# has no room for a replacement listener it drops the connection, and the
# port goes silent with nothing left running to see.  Only a second caller
# notices.  /bin/repeatclient calls each port ten times, one at a time, and
# asks each service for real output.  A lost listener scores 1/10.
/bin/repeatclient -n 10 10.0.0.2 7:hello 13: 23: 25: 79:root 514: u518 > /trep.out 2>&1
/bin/A T_REPECHO /trep.out 'port 7: 10/10'
/bin/Q T_REPDAY /trep.out 'port 13: 10/10'
/bin/Q T_REPTELNET /trep.out 'port 23: 10/10'
/bin/Q T_REPSMTP /trep.out 'port 25: 10/10'
/bin/Q T_REPFINGER /trep.out 'port 79: 10/10'
/bin/Q T_REPSHELL /trep.out 'port 514: 10/10'
/bin/Q T_REPTALK /trep.out 'port 518: 10/10'
/bin/Q T_REPALL /trep.out 'every port answered every round'
# ntalk's child outlives its datagram by design: it holds the invitation
# table and retires when idle (T_TALKGONE below).
# Stop rc.net's inetd: the conf below reuses its ports, and a second daemon
# would bind none.  The pid goes through awk to a kill line rather than a
# backquote.
/bin/ps -ax > /tps.out
/bin/awk '$3 == "/etc/inetd" { print "/bin/kill", $2 }' /tps.out > /tkill.sh
/bin/cat /tkill.sh
/bin/sh /tkill.sh
sleep 5
echo 'echo $INETD_REMADDR $INETD_LOCPORT > /tinetd.env' > /bin/hello
# A missing file, so cat writes to stderr: that must reach the log and not
# the peer.
echo /bin/cat /tnosuchfile >> /bin/hello
echo echo hello >> /bin/hello
/bin/chmod 755 /bin/hello
# What remshd's session runs.  It prints the address inetd handed the daemon,
# which is the value the daemon authenticated on -- written here, OUTSIDE any
# T_ segment, so the transcript's copy of it can only have come off the wire.
echo 'echo $INETD_REMADDR' > /bin/renv
/bin/chmod 755 /bin/renv
# What the GUEST session runs.  It writes in /tmp, which is 777 on the root
# filesystem, so the file's owner is the uid remshd's child set -- the evidence
# that the command ran as the account named on the command line and not as the
# daemon.  A .rhosts for that account is what lets it in at all: none is
# shipped, for anybody, so a run must make one.
echo 'echo $INETD_REMADDR >/tmp/g' > /bin/rg
/bin/chmod 755 /bin/rg
/bin/mkdir /usr/guest
# THE CANONICAL NAME, not the `c900' alias /etc/hosts also carries: iruserok()
# compares the entry against gethostbyaddr()'s answer and then against its
# aliases, and the local host database reports none (net/netdb.c _host_answer
# sets h_aliases empty), so an alias here matches nothing.  A dotted quad
# matches without asking the database at all.
echo c900.localnet root > /usr/guest/.rhosts
# 644, and the chmod is the point: a shell redirect makes the file 666 under
# this session's umask, and iruserok() ignores a .rhosts that group or other can
# write -- a file the world can append to grants the account to the world.
/bin/chmod 644 /usr/guest/.rhosts
echo echo stream tcp nowait root internal > /tinetd.conf
echo 7777 stream tcp nowait root /bin/echo echo hello >> /tinetd.conf
echo 7778 stream tcp nowait root /bin/sh sh >> /tinetd.conf
echo finger stream tcp nowait root /etc/fingerd fingerd >> /tinetd.conf
echo shell stream tcp nowait root /etc/remshd remshd >> /tinetd.conf
echo smtp stream tcp nowait root /etc/smtpd smtpd >> /tinetd.conf
echo ntalk dgram udp wait root internal >> /tinetd.conf
echo 7780 stream tcp nowait root /etc/nosuchd nosuchd >> /tinetd.conf
/bin/cat /tinetd.conf
# Under -d the daemon reports what it opened and refused on stderr; kept
# for the assertions below.
/etc/inetd -d /tinetd.conf > /tinetd.log 2>&1 &
sleep 10
/bin/A T_READY /tinetd.log 'ready, 7 services'
/bin/Q T_NOTINSTMSG /tinetd.log 'is not installed'
/bin/echoclient 10.0.0.2 7 > /t.out 2>&1
/bin/A T_INTERNAL /t.out 'echoclient: PASS'
sleep 2
/bin/echoclient 10.0.0.2 7777 > /t.out 2>&1
/bin/A T_EXEC /t.out 'echoclient: PASS'
sleep 2
/bin/echoclient 10.0.0.2 7778 > /t.out 2>&1
/bin/A T_DOWN /t.out 'echoclient: PASS'
/bin/N T_DOWNCLEAN /t.out tnosuchfile
sleep 5
/bin/A T_ENV /tinetd.env 10.0.0.2
/bin/Q T_ENVPORT /tinetd.env 7778
/bin/echoclient 10.0.0.2 7779 > /t.out 2>&1
/bin/A T_NEG /t.out 'connect failed'
sleep 2
/bin/finger root@10.0.0.2 > /t.out 2>&1
/bin/A T_FINGERD /t.out 'System Administrator'
sleep 5
/bin/remsh -l root 10.0.0.2 /bin/renv > /t.out 2>&1
/bin/A T_REMSHD /t.out 'root may not run commands over the network'
sleep 5
/bin/remsh -l guest 10.0.0.2 /bin/rg > /t.out 2>&1
sleep 5
# /bin/rg writes the address to a file, answering both what authenticated
# the session and which uid ran it.
/bin/A T_REMSHDU /tmp/g 10.0.0.2
/bin/ls -l /tmp/g > /tls.out 2>&1
/bin/A T_REMSHDUID /tls.out guest
# telnetd on a pipe, with the switchboard's variables set by hand.  No network
# and no inetd in this one: it is the DAEMON's half of the contract under test.
INETD_LOCADDR=10.0.0.2 INETD_LOCPORT=23 INETD_REMADDR=10.0.0.2 INETD_REMPORT=1023 /etc/telnetd < /dev/null > /ttelnetd.out
/bin/wc -c < /ttelnetd.out > /twc.out
/bin/A T_TELNETD /twc.out '[1-9]'
# smtpd's inetd mode, the same way: the conversation on standard input, the
# answers on standard output.  Not over the network because its inetd.conf line
# ships COMMENTED (smtpd comes from a different dist list), so there is no
# service to connect to on a stock image -- but the mode itself is code that has
# to work, and the banner it prints is computed from /etc/hostname by the server,
# not typed here.
echo QUIT > /tsmtp.in
INETD_LOCADDR=10.0.0.2 INETD_LOCPORT=25 INETD_REMADDR=10.0.0.2 INETD_REMPORT=1024 /etc/smtpd < /tsmtp.in > /t.out 2>&1
/bin/A T_SMTPD /t.out '^220 '
# And the same daemon over the network, through the switchboard, with the real
# client: this is the path the shipped conf file's smtp line serves.  The mailbox
# is shown
# rather than grepped so the transcript carries the evidence -- the envelope line
# is the server's and nothing here types it.
# The mailbox is on /usr.
/bin/ls -ld /usr/spool/mail
echo Subject: switchboard > /tmail.in
echo '' >> /tmail.in
echo the message body >> /tmail.in
/bin/smtpsend -f root root@10.0.0.2 < /tmail.in
sleep 5
/bin/A T_SMTPMAIL /usr/spool/mail/root '^From '
/bin/Q T_SMTPBODY /usr/spool/mail/root 'the message body'
# The service inetd must NOT have bound, because its program is not installed.
/bin/echoclient 10.0.0.2 7780 > /t.out 2>&1
/bin/A T_NOTINST /t.out 'connect failed'
sleep 2
# THE DATAGRAM SERVICE.  talkping speaks ntalk: lookup before, invitation,
# the other party's lookup getting that id, announcement, delete, and a bad
# version.  The announcement is reported, not scored: it depends on utmp and
# mesg.
/bin/talkping 10.0.0.2 guest root > /t.out 2>&1
/bin/A T_NTALK /t.out 'lookup-after: PASS'
/bin/N T_NTALKOK /t.out FAIL
# THE CHILD THAT HOLDS THE TABLE.  talkping -k leaves its invitation, so the
# child stays.  Typing a ps outlasts the child's life, so the daemon's own
# log (fork and reap) is asserted; ps is only shown.
/bin/talkping -k 10.0.0.2 guest root > /t.out 2>&1
/bin/ps -axl > /tps.out
/bin/A T_TALKKID /t.out 'keeping the invitation'
/bin/cat /tps.out
/bin/Q T_TALKCHILD /tinetd.log 'has the socket'
# Once the invitation expires (MAX_LIFE 60, then a TALKIDLE 30 poll), one
# inetd: nothing stays resident between calls.
sleep 100
/bin/ps -axl > /tps.out
/bin/grep -c inetd /tps.out > /tc.out
/bin/A T_TALKGONE /tc.out '^1$'
/bin/Q T_TALKREAPED /tinetd.log 'has finished'
# No separate talkd is installed.
/bin/ls /etc > /tetc.out
/bin/N T_NOTALKD /tetc.out '^talkd$'
# cat's complaint from /bin/hello: T_DOWN showed it missed the peer, this
# finds it in syslog.
/bin/grep 7778 /usr/adm/syslog > /t.out
/bin/A T_SYSLOG /t.out tnosuchfile
sleep 2
# remshd's refusal of root, filed by the relay under the service name.
/bin/grep refused /usr/adm/syslog > /t.out
/bin/A T_REFUSED /t.out 'refused a session as root'
# The resident cost that moving these three under the switchboard reclaims,
# measured by the system's own instrument rather than computed off the host.
# NOT an assertion -- it is a number to report, and there is no threshold worth
# failing a build over -- but it belongs in the transcript, because the reason
# for the whole exercise is this figure and nothing else here shows it.
echo T_SIZE
/bin/size /etc/telnetd /etc/fingerd /etc/remshd
# THE SETUP-FAILURE BOUND.  A standalone daemon whose transport cannot be opened
# must report the failure a bounded number of times and then EXIT: its
# pre-forked child dies on the open, and a parent that replaced it unconditionally
# would fork at the speed of the machine and write a line per turn -- which is
# the state a first boot or a failed rc.net leaves, so it has to be a test and
# not a reading of the code.  TCP_DEVICE is the daemon's own environment
# variable, so this needs no network and no missing node: it is the code path a
# missing /dev/tcp takes.  What must be seen is FIVE messages and then the
# daemon gone -- the count is the bound, and the prompt coming back at all is
# the other half of it.
TCP_DEVICE=/dev/nosuchtcp /etc/smtpd -m 1 > /t.out 2>&1
/bin/A T_NOSPIN /t.out nosuchtcp
/bin/Q T_NOSPINBOUND /t.out 'in 5 tries, exiting'
/bin/chanmax 10 3 > /t.out 2>&1
/bin/A T_CEILING /t.out 'chanmax: closed'
/bin/N T_CEILINGOK /t.out FAIL
sleep 5
# The other half of the ceiling question, on the same booted stack.  libsocket
# holds a socket's state on the heap while the socket is open, so a closed socket
# has to give the block back: chanmax above says how many the machine will give
# at once, and this says that taking them repeatedly does not grow the process.
# Twelve rounds of three -- a leak would move the break by 20 KB, which nothing
# else in this file would notice.
/bin/sockcycle 12 3 > /t.out 2>&1
/bin/A T_LEAK /t.out 'heap flat'
/bin/N T_LEAKOK /t.out FAIL
sleep 5
# THE LAST SERVICE OF ALL, AND THE ARM THAT HAS NOTHING TO POLL.
#
# Everything above is a switchboard that still has somewhere to listen: when one
# service loses its listening socket the others are still in the poll set, and
# the ten-second idle turn is what paces the rebind.  With ONE service there is
# no descriptor left to poll and no turn to be paced by, so that arm keeps a
# pace of its own: it sleeps REBINDWAIT and spends one rebind() turn, bounded
# at REBINDMAX like every other rebind.  A daemon that reported "no service is
# listening; nothing left to do" and stopped there would turn one accept() that
# could not open a replacement channel into a machine with no network services
# at all until somebody noticed.
#
# THE LOSS IS MADE, not waited for.  accept() opens a replacement listening
# channel before it hands the connection over (libsocket.c), and only when there
# is no room for one -- and none for the second try acceptlost() makes after
# giving the connection back -- does the socket stop being a socket.  That takes
# fewer than two free descriptors, and descriptors are per process: there is no
# way to take one from a running program, so the shortage has to be INHERITED.
# /bin/fdhog fills its own table, leaves exactly as many free as it is asked to,
# and execs the daemon into what is left.  Three is the number: one channel
# costs three descriptors while it is being opened and keeps two, so a daemon
# given three binds exactly one listening socket and is then one short of ever
# opening a second.
# ONE SERVICE, AND THE DAEMON THAT MUST OUTLIVE LOSING IT.
/bin/ps -ax > /tps.out
/bin/awk '$3 == "/etc/inetd" { print "/bin/kill", $2 }' /tps.out > /tkill.sh
/bin/sh /tkill.sh
sleep 5
echo echo stream tcp nowait root internal > /tlast.conf
/bin/fdhog -f 3 /etc/inetd -d /tlast.conf > /tlast.log 2>&1 &
sleep 10
/bin/A T_LASTREADY /tlast.log 'ready, 1 service'
# One caller: accept drops it (ECONNRESET) and the listener is gone (EBADF).
/bin/repeatclient -n 1 10.0.0.2 7:hello > /tlost.out 2>&1
sleep 30
/bin/cat /tlast.log
/bin/A T_LASTGONE /tlast.log 'listening socket is'
# Two listening lines: startup, and the rebind.
/bin/grep -c 'listening on tcp port 7' /tlast.log > /tbound.out
/bin/A T_LASTREBOUND /tbound.out '[2-9]'
/bin/N T_LASTNOTDEAD /tlast.log 'nothing left to do'
/bin/ps -ax > /tlps.out
/bin/A T_LASTALIVE /tlps.out /etc/inetd
# A second caller is accepted: the port really is back.
/bin/repeatclient -n 1 10.0.0.2 7:hello > /tagain.out 2>&1
/bin/N T_LASTANSWERED /tagain.out 'connect failed'
# It loses the listener again, since the descriptor shortage is inherited;
# a third listening line shows it rebound once more.
sleep 30
/bin/cat /tlast.log
/bin/grep -c 'listening on tcp port 7' /tlast.log > /tbound2.out
/bin/A T_LASTAGAIN /tbound2.out '[3-9]'
# The same loss with /bin/portholder holding the port by an EXCL /dev/tcp
# claim (SHARED sockets could coexist).  It lets go after twenty seconds,
# within inetd's five tries.
/bin/ps -ax > /tps.out
/bin/awk '$3 == "/etc/inetd" { print "/bin/kill", $2 }' /tps.out > /tkill.sh
/bin/sh /tkill.sh
sleep 5
/bin/fdhog -f 3 /etc/inetd -d /tlast.conf > /tback.log 2>&1 &
sleep 10
/bin/portholder -w 3000 -t 20 7 > /tphb.out 2>&1 &
sleep 5
/bin/repeatclient -n 1 10.0.0.2 7:hello > /tb1.out 2>&1
sleep 60
/bin/cat /tback.log
/bin/cat /tphb.out
/bin/A T_BACKHELD /tphb.out 'port 7 taken'
/bin/Q T_BACKREFUSED /tback.log 'listen: errno'
/bin/grep -c 'listening on tcp port 7' /tback.log > /tbc.out
/bin/A T_BACKBOUND /tbc.out '[2-9]'
# With the port kept: five tries ten seconds apart, one line saying they
# are spent, then the daemon stops.
/bin/ps -ax > /tps.out
/bin/awk '$3 == "/etc/inetd" { print "/bin/kill", $2 }' /tps.out > /tkill.sh
/bin/sh /tkill.sh
sleep 5
/bin/fdhog -f 3 /etc/inetd -d /tlast.conf > /theld.log 2>&1 &
sleep 10
/bin/portholder -w 3000 -t 3000 7 > /tphh.out 2>&1 &
sleep 5
/bin/repeatclient -n 1 10.0.0.2 7:hello > /th1.out 2>&1
sleep 120
/bin/cat /theld.log
/bin/A T_HELDTAKEN /tphh.out 'port 7 taken'
/bin/grep -c 'listen: errno' /theld.log > /ttries.out
/bin/A T_HELDTRIES /ttries.out '^5$'
/bin/Q T_HELDSPENT /theld.log 'cannot be bound again after 5 tries'
/bin/Q T_HELDSTOP /theld.log 'nothing left to do'
/bin/ps -ax > /thps.out
/bin/cat /thps.out
/bin/N T_HELDGONE /thps.out /etc/inetd
