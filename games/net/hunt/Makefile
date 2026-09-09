# Makefile -- hunt(6), the multi-player maze game.
#
# Two programs: huntd, the game driver, and hunt, the terminal client.  They
# share huntd/hunt.h, so the client compiles with -Ihuntd.
#
# Traditional make; builds natively on the C900 with cc/as/ar.  Cross-build on
# a host with make CC=ccz (see hostbuild/build-hunt.sh).
#
# WHY THESE DEFINES.  hunt/Makeconfig offers a per-vendor set; this is ours.
#
#	INTERNET	IP sockets rather than Unix-domain ones, which this
#			system does not have at all.
#	BROADCAST	the UDP discovery that finds a driver without being
#			told where one is -- bind() sets NWUO_EN_BROAD on
#			every datagram socket, so it works.
#	BSD_RELEASE=42	4.2 is the right vintage: >= 43 makes bsd.h turn on
#			SYSLOG_43, and there is no syslog here.
#	SIGNAL_TYPE=int	this system's signal handlers return int, as V7's do.
#	no LOG		which is what keeps syslog out; errors go to stderr
#			through the err/warn shims in huntd/compat.c.
#
# The game parameters are hunt's own defaults minus OTTO (the automatic player,
# which is client-side and unfinished in the original).
.SUFFIXES:
.SUFFIXES: .c .o

CC	= cc
AS	= as
CFLAGS	= -O
LFLAGS	= -i			# separated I/D

# Where the client looks for the driver when it has to start one itself.
HUNTD_PATH = /usr/games/lib/huntd

DEFS	= -DCOHERENT -DHUNTD=\"$(HUNTD_PATH)\" -DINTERNET -DBROADCAST -DBSD_RELEASE=42 \
	  -DSIGNAL_TYPE=int \
	  -DRANDOM -DREFLECT -DMONITOR -DOOZE -DFLY -DVOLCANO -DBOOTS
# base/lib/libcurses is the curses the client links against -- the in-tree
# 4.3BSD/sgtty one, whose header carries _tty_ch and the terminal capability
# strings (TI/VS/VE/TE) hunt drives directly.  include/curses.h is the 4.x
# terminfo header, which has neither.
INCS	= -Ihuntd -I$(OS)/base/lib/libcurses
OS	= ../../..

# faketalk.c is in: with the talk feature off it compiles to the empty stub
# driver.c calls, and leaving it out just makes faketalk() undefined.  The talk
# notification itself (ctl.c, ctl_transact.c, get_names.c) needs a talk daemon
# to notify, which this system has not got.
DOBJS	= huntd/driver.o huntd/answer.o huntd/draw.o huntd/execute.o \
	  huntd/expl.o huntd/extern.o huntd/makemaze.o huntd/pathname.o \
	  huntd/shots.o huntd/terminal.o huntd/faketalk.o huntd/compat.o
# pathname.o is shared: it holds Driver (where the client execs a driver from)
# and Test_port (the discovery port), which both programs use.
COBJS	= hunt/hunt.o hunt/connect.o hunt/playit.o huntd/pathname.o \
	  huntd/compat.o

# The binaries are built INSIDE their source directories, as the original
# does: a target named `huntd' at this level cannot be created, because
# huntd/ is a directory.
all:	huntd/huntd hunt/hunt

huntd/huntd:	$(DOBJS)
	$(CC) $(LFLAGS) -o huntd/huntd $(DOBJS) $(LIBS)

hunt/hunt:	$(COBJS)
	$(CC) $(LFLAGS) -o hunt/hunt $(COBJS) $(LIBS)

# INCS BEFORE CFLAGS: the curses header has to be found in
# base/lib/libcurses, not the terminfo one in include/ (see INCS above),
# and whichever -I comes first wins.
.c.o:
	$(CC) $(INCS) $(CFLAGS) $(DEFS) -c -o $@ $<

clean:
	rm -f huntd/*.o hunt/*.o huntd/huntd hunt/hunt
