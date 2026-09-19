# inetd.cmd -- the switchboard, every path it has, with both ends on this
# machine.
#
#	EMUWAIT=3600 hostbuild/emu-run.sh test/cmd/inetd.cmd
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
# For test/cmd/run.sh, that is the whole pass condition: an `ok' line for every
# tag this file judges, and no `FAIL' line.  T_SIZE is a number to report and
# judges nothing.
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
#% wait 3600
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
#              minutes of guest time (see tests/cmd/rsh.cmd) and needs an
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
: 'WHAT THE BOOT ALREADY DID, on the record before anything is changed: the'
: 'interface rc.net configured, and the filesystems it mounted.  /usr is one'
: 'of them, which is where the log and the mailbox this file reads live.'
/etc/ifconfig
/etc/mount
: 'WHAT A DROPPED CONNECTION LEAVES BEHIND, ON THE SHIPPED SWITCHBOARD.'
: 'This runs FIRST and against rc.net s own inetd on /etc/inetd.conf, before'
: 'the conf file below replaces it: the question is about the services the'
: 'machine ships, so the shipped table is the one to ask it of.'
: 'A daemon started BY HAND with /dev/null where its connection should be'
: 'has no peer whose going away could tell it to exit, so its surviving'
: 'proves nothing about the case that matters.  /bin/dropclient makes that'
: 'case: it opens the real ports through inetd, holds the connections while'
: 'a ps is taken, and then _exit(2)s with every socket still open -- no'
: 'close(2), no shutdown, nothing said to the stack.  inetd s relay sees the'
: 'connection go and closes the program s standard input; a program that'
: 'does not act on end of file there is one process left per dropped'
: 'connection, and a 16-bit process table cannot absorb that.'
: 'TWO HELD AT ONCE, and never more than -m: inetd serves -m connections at'
: 'a time and -m is 4 (MAXKIDS, net/inetd.c), so a batch that held more'
: 'would be measuring the child ceiling instead of the drop.  These two are'
: 'the services that wait for the caller to speak first and can therefore be'
: 'caught in a ps.  shell answers and exits before any sample could see it,'
: 'and telnet is dropped above; both are taken on their own.'
: 'THE PROCESS COUNT IS TAKEN BEFORE AND AFTER, and a sample is taken WHILE'
: 'the connections are held.  The middle one is what stops the assertion'
: 'being vacuous: nothing is left behind by a connection that was never'
: 'served either, and this is what tells the two apart.'
/bin/ps -ax > /td0.out
: 'THE DROP THAT ARRIVES FIRST, and it comes first here because it is the'
: 'harder case: a connection held for minutes finds every service well'
: 'inside its own read loop when the peer goes.  This one is dropped as soon'
: 'as it is open, so the end of file is already waiting when the service'
: 'starts -- which for telnetd'
: 'means the fork of its login and the closing of its pty happen in the same'
: 'instant, and a child still on its way into the slave open() is asleep'
: 'waiting for a carrier that has already been dropped.  Nothing on a line'
: 'nobody holds can wake it, so it would stay for the life of the machine:'
: 'telnetd hangs its login child up by name for that reason and does not'
: 'leave it to the carrier (net/telnetd/main.c).'
/bin/dropclient 10.0.0.2 0 23 > /tdrace.out 2>&1
/bin/A T_DROPRACE /tdrace.out 'port 23: connected'
/bin/dropclient 10.0.0.2 600 79 25 > /tdrop.log 2>&1 &
sleep 20
/bin/ps -ax > /td1.out
/bin/cat /td1.out
/bin/awk '$3~/fingerd|smtpd/{n++}END{print n+0}' /td1.out > /tdn1.out
/bin/A T_DROPUP /tdn1.out '^2$'
: 'TEN MINUTES IS THE HOLD, and the reason is the console rather than the'
: 'daemons: this line is typed one character at a time and paced against the'
: 'SCC receive FIFO, so the ps above is minutes of guest time after the'
: 'background job started, and a hold that merely looked generous expired'
: 'before the sample was taken.  Then the drop, and long enough after it for'
: 'two daemons to read end of file and for inetd to collect two relays.'
sleep 600
/bin/A T_DROPPED /tdrop.log 'dropping 2 connection'
: 'shell (514) ENDS ITS OWN SESSION and needs no end of file to do it:'
: 'remshd authenticates on the caller s port being a reserved one, this'
: 'caller s is not, and the refusal goes to the CALLER because a service'
: 'under the switchboard cannot reach anyone else.  So the answer read back'
: 'here is both halves at once -- the daemon ran, and it was already gone'
: 'before the connection was dropped.  That is why the count above is three.'
/bin/dropclient -r 10.0.0.2 0 514 > /tdrsh.out 2>&1
/bin/A T_DROPRSH /tdrsh.out 'unprotected port'
: 'THE INTERNAL SERVICES, dropped the same way.  echo answers only what it'
: 'is sent, so the batch writes a line and reads the reply back: seven'
: 'bytes, hello and CRLF.  That reply is also what says the switchboard is'
: 'still serving after the batch -- a switchboard that had died would leave'
: 'nothing behind either.  daytime answers without being asked and closes'
: 'the connection itself, so nothing here can drop it first; the reply is'
: 'the whole service.'
/bin/dropclient -w hello -r 10.0.0.2 0 7 13 > /tdint.out 2>&1
/bin/A T_DROPINT /tdint.out 'port 7: answered 7'
/bin/Q T_DROPDAY /tdint.out 'port 13: answered'
sleep 30
/bin/ps -ax > /td2.out
/bin/cat /td2.out
/bin/awk '$3~/telnetd|fingerd|remshd|smtpd/{n++}END{print n+0}' /td2.out > /tdn2.out
/bin/A T_DROPGONE /tdn2.out '^0$'
: 'And the whole table, not just those four names: the process count before'
: 'the batch and after it, compared as files so that no arithmetic is needed'
: 'and no line can be missed -- a stranded getty or an uncollected relay'
: 'moves this number as surely as a daemon does.  cmp prints nothing when'
: 'they are equal, so the inverted judge is the one that reads it.'
/bin/wc -l < /td0.out > /tdc0.out
/bin/wc -l < /td2.out > /tdc1.out
/bin/cmp /tdc0.out /tdc1.out > /tdcmp.out 2>&1
/bin/cat /tdc0.out /tdc1.out
/bin/N T_DROPCOUNT /tdcmp.out .
: 'DOES EVERY SERVICE ANSWER THE SECOND CALLER, AND THE TENTH?  Still on the'
: 'shipped switchboard, and after the six connections above were dropped on it'
: 'uncontrolled, which is what makes this the harder place to ask.'
: 'A SWITCHBOARD CAN STOP OFFERING A SERVICE WITHOUT ANYTHING GOING WRONG THAT'
: 'A READER COULD SEE.  accept() opens a replacement listening channel before'
: 'it hands the connection over, and when there is no room for one the'
: 'connection is what is given up (libsocket.c acceptlost) -- because a caller'
: 'handed the LISTENING descriptor back cannot tell it from a connection, and'
: 'closes its own listener when it is done with the caller.  Nothing is left'
: 'running when that happens, so the drop assertions above cannot see it: the'
: 'only thing that can is a SECOND caller on the same port.'
: '/bin/repeatclient is that caller, ten times over: one connection at a time,'
: 'each one closed properly before the next is opened, so nothing here can fail'
: 'for want of a descriptor, a process or a pty.  Each port is asked for what'
: 'that service owes a caller -- echo is written to first, finger is given a'
: 'query, and the four that speak first are only read -- because a port that'
: 'accepts and says nothing would pass a test that merely connected.  ntalk is'
: 'asked in the only language it has, a talk(1) rendezvous request, since a'
: 'datagram service has no connection to make twice.'
: 'THE SCORE IS THE WHOLE COUNT AND NOT THE LAST ROUND: a service that loses'
: 'its listener answers once and refuses for ever after, so 1/10 and 10/10 are'
: 'the two outcomes and the fraction is what tells them apart.'
/bin/repeatclient -n 10 10.0.0.2 7:hello 13: 23: 25: 79:root 514: u518 > /trep.out 2>&1
/bin/A T_REPECHO /trep.out 'port 7: 10/10'
/bin/Q T_REPDAY /trep.out 'port 13: 10/10'
/bin/Q T_REPTELNET /trep.out 'port 23: 10/10'
/bin/Q T_REPSMTP /trep.out 'port 25: 10/10'
/bin/Q T_REPFINGER /trep.out 'port 79: 10/10'
/bin/Q T_REPSHELL /trep.out 'port 514: 10/10'
/bin/Q T_REPTALK /trep.out 'port 518: 10/10'
/bin/Q T_REPALL /trep.out 'every port answered every round'
: 'ntalk IS THE EXCEPTION, and it is not a defect.  It is a datagram'
: 'service: there is no connection to drop, and its child is meant to'
: 'outlive the datagram that started it because the invitation table is'
: 'state BETWEEN datagrams -- one party leaves a call and the other looks it'
: 'up seconds later.  It retires once it holds no invitation and has been'
: 'idle, which is measured further down this file as T_TALKGONE, not here:'
: 'what bounds that child is its own timers and not a peer.'
: 'AND THE SWITCHBOARD IT STARTED, stopped.  The conf file below configures'
: 'echo, finger, shell, smtp and ntalk over again plus three ports of its'
: 'own, and rc.net s inetd holds the first five: a second daemon binds none'
: 'of them and the run stalls on its first connection.  Killing it gives'
: 'every listening socket back to the stack and collects the ntalk child'
: 'that holds port 518, so this file s inetd is then the only switchboard on'
: 'the machine and what is measured is still ONE switchboard serving every'
: 'kind of path it has.'
: 'The pid comes out of ps(1) -- TTY, PID, then the command -- through awk'
: 'writing a kill line that sh runs, because this console is typed to'
: 'through a pacing emulator and a backquote round trip is not worth the'
: 'risk.'
/bin/ps -ax > /tps.out
/bin/awk '$3 == "/etc/inetd" { print "/bin/kill", $2 }' /tps.out > /tkill.sh
/bin/cat /tkill.sh
/bin/sh /tkill.sh
sleep 5
echo 'echo $INETD_REMADDR $INETD_LOCPORT > /tinetd.env' > /bin/hello
: 'A file that is not there, so cat complains on standard error.  That is'
: 'the diagnostic the two opposite assertions below are about: it must not'
: 'be in what the peer receives, and it must be in the log.'
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
: 'The daemon reports on its standard error under -d, and that is where the'
: 'count of services it opened and the name of the one it refused to open'
: 'both appear -- so it is kept rather than watched, and read as a file by'
: 'the two assertions below.'
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
: 'The address is in the file the session wrote, not on the client s'
: 'standard output: /bin/rg redirects it there so that the same run'
: 'answers both questions, what authenticated the session and which uid'
: 'ran it.'
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
: 'The mailbox lives on the /usr filesystem, which rc mounted before this'
: 'file was typed a line of.'
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
: 'THE DATAGRAM SERVICE.  talkping speaks the real ntalk protocol on the'
: 'real port through the real stack: a lookup before anything was left'
: '(NOT_HERE), an invitation (SUCCESS with an id), the OTHER party looking'
: 'it up and getting THAT id -- which is the state between datagrams and the'
: 'thing no per-datagram service can produce -- the announcement, the'
: 'invitation given back, and a version the service must refuse.  The'
: 'announcement is REPORTED and not scored (talkping passes want -1 for it):'
: 'its answer turns on utmp and on mesg, and this guest is multi user with'
: 'root logged in on the console, so it is not the constant a single-user'
: 'machine made it look like.  The terminal it writes on is asserted by'
: 'net/twohost (make -C net/twohost talk), which has two machines and two'
: 'logins to do it with.'
/bin/talkping 10.0.0.2 guest root > /t.out 2>&1
/bin/A T_NTALK /t.out 'lookup-after: PASS'
/bin/N T_NTALKOK /t.out FAIL
: 'THE CHILD THAT HOLDS THE TABLE.  Two inetds: the parent, and the child a'
: 'datagram forked.  talkping -k leaves its invitation with the service'
: 'instead of giving it back, which is what makes this sample possible at'
: 'all: a child holding nothing retires on its next idle poll, and the run'
: 'above deleted its invitation -- so after it, the honest answer to ps is'
: 'already ONE.  The state is the thing that keeps the process alive, so the'
: 'test has to leave some.'
: 'AND THE SAMPLING COMMAND IS AS SHORT AS IT CAN BE, WHICH IS PART OF THE'
: 'MEASUREMENT.  This console is typed one character at a time and paced'
: 'against the SCC receive FIFO, so a command line is minutes of guest time:'
: 'a 48-character ps pipeline here took ninety seconds to be typed, by which'
: 'time the invitation had expired (60) and the child had gone -- the service'
: 'behaving exactly as documented, and the test asking after the fact.  A'
: 'AND ps CANNOT SEE THE CHILD, so it is not what the child is asserted'
: 'with.  This console is typed one character at a time and paced against'
: 'the SCC receive FIFO, so ONE short command line is more than the ninety'
: 'seconds the child lives after its last datagram (MAX_LIFE 60 from the'
: 'invitation, then the TALKIDLE 30 poll that retires it): a ps taken on'
: 'the very next line still counts ONE inetd, and an assertion on that'
: 'count could only ever fail.  The daemon says it itself instead, in the'
: 'log kept above -- the fork that hands the socket over, and the reaping'
: 'that binds a fresh one -- which is the same claim measured by the'
: 'process that makes it.  The ps output stays in the transcript as the'
: 'reading it is, and the count is asserted only where it is decidable,'
: 'after the timers have run (T_TALKGONE).'
/bin/talkping -k 10.0.0.2 guest root > /t.out 2>&1
/bin/ps -axl > /tps.out
/bin/A T_TALKKID /t.out 'keeping the invitation'
/bin/cat /tps.out
/bin/Q T_TALKCHILD /tinetd.log 'has the socket'
: 'And once that invitation has expired: ONE inetd, so nothing at all is'
: 'resident between calls -- which is the whole reason the daemon moved in'
: 'here.  The wait covers both timers: an entry lives MAX_LIFE (60) seconds'
: 'from the last request that touched it, and the child then goes on its'
: 'next idle poll, TALKIDLE (30) apart (talkserv.c).'
sleep 100
/bin/ps -axl > /tps.out
/bin/grep -c inetd /tps.out > /tc.out
/bin/A T_TALKGONE /tc.out '^1$'
/bin/Q T_TALKREAPED /tinetd.log 'has finished'
: 'And there is no separate daemon left to install.'
/bin/ls /etc > /tetc.out
/bin/N T_NOTALKD /tetc.out '^talkd$'
: 'THE DIAGNOSTIC.  /bin/hello read a file that is not there on every'
: 'connection to 7778, so cat complained on its standard error.  T_DOWN'
: 'above is byte-exact, which says the complaint did not reach the peer;'
: 'this says where it went instead.  The tag is the service name inetd'
: 'files it under.'
/bin/grep 7778 /usr/adm/syslog > /t.out
/bin/A T_SYSLOG /t.out tnosuchfile
sleep 2
: 'AND WHERE A REFUSAL IS VISIBLE.  A service under the switchboard cannot'
: 'report to the caller except as protocol, so remshd says on its standard'
: 'error that it refused a session as root -- and that is the pipe the relay'
: 'files under the service name.  This is the line an administrator reads.'
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
: 'ONE SERVICE, AND THE DAEMON THAT HAS TO OUTLIVE LOSING IT.'
/bin/ps -ax > /tps.out
/bin/awk '$3 == "/etc/inetd" { print "/bin/kill", $2 }' /tps.out > /tkill.sh
/bin/sh /tkill.sh
sleep 5
echo echo stream tcp nowait root internal > /tlast.conf
/bin/fdhog -f 3 /etc/inetd -d /tlast.conf > /tlast.log 2>&1 &
sleep 10
/bin/A T_LASTREADY /tlast.log 'ready, 1 service'
: 'One caller, which is all it takes: the accept finds no room for the'
: 'replacement listener, gives the CONNECTION up (errno 46, ECONNRESET) and'
: 'then finds its own descriptor is no longer a socket at all (errno 9,'
: 'EBADF) -- which is a switchboard with nothing left to listen on.'
/bin/repeatclient -n 1 10.0.0.2 7:hello > /tlost.out 2>&1
sleep 30
/bin/cat /tlast.log
/bin/A T_LASTGONE /tlast.log 'listening socket is'
: 'And the port is bound again: TWO listening lines, the one at startup and'
: 'the one the arm under test produced.  Counted rather than matched,'
: 'because the first line is there on any build.'
/bin/grep -c 'listening on tcp port 7' /tlast.log > /tbound.out
/bin/A T_LASTREBOUND /tbound.out '[2-9]'
/bin/N T_LASTNOTDEAD /tlast.log 'nothing left to do'
/bin/ps -ax > /tlps.out
/bin/A T_LASTALIVE /tlps.out /etc/inetd
: 'AND THE PORT IS ANSWERED AGAIN.  A second caller, after the arm has had'
: 'its turn: what says the socket is really back is that the connection is'
: 'ACCEPTED at all -- a port nobody is listening on refuses, which is what'
: 'the same caller gets from a daemon that has stopped.'
/bin/repeatclient -n 1 10.0.0.2 7:hello > /tagain.out 2>&1
/bin/N T_LASTANSWERED /tagain.out 'connect failed'
: 'It costs the listener a second time, and that is the machine and not the'
: 'daemon: one descriptor short is a state a program cannot leave, since the'
: 'shortage is inherited and nothing can give a running process a descriptor'
: 'back.  With one more the same daemon on the same socket answers hello.'
: 'So the assertion is a THIRD listening line: an accept that cost the'
: 'listener is an accept that happened, and the port was bound again after'
: 'it as well.'
sleep 30
/bin/cat /tlast.log
/bin/grep -c 'listening on tcp port 7' /tlast.log > /tbound2.out
/bin/A T_LASTAGAIN /tbound2.out '[3-9]'
: 'AND IT TAKES THE PORT BACK OUT OF CONTENTION.  The same loss with'
: '/bin/portholder waiting for the port: an EXCL claim through /dev/tcp,'
: 'which the stack will not put beside another descriptor on that port and'
: 'will not let one be put beside -- so it cannot be taken while inetd is'
: 'listening, and inetd cannot have the port while it stands.  Nothing built'
: 'on socket() would do: libsocket configures every socket SHARED, and two'
: 'SHARED descriptors may name one port.  It gives the port back after'
: 'twenty seconds, which is inside the five tries the daemon has.'
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
: 'AND IT STOPS ASKING WHEN THE PORT IS NOT COMING BACK.  The same again,'
: 'with the holder keeping the port: five tries ten seconds apart, one line'
: 'each, one line saying they are spent, and then the daemon stops.  That'
: 'is the decision this arm was always making, made after asking rather'
: 'than instead of asking -- an unbounded retry here would write a line'
: 'every turn for as long as the machine was up and bury the one that said'
: 'what happened.'
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
