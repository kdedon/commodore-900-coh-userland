# twohost -- two C900 emulators on one serial link, and a login across it.
#
#   make test        boot two guests, bring both stacks up, ping, then telnet
#                    from one to the other and log in.  PASS/FAIL, exit status.
#   make negative    the same run with the wire CUT.  It passes when the test
#                    FAILS: a test that still succeeds with no wire between the
#                    machines was never measuring the network.
#   make check       both of the above, in the order that makes the result mean
#                    something.  This is the gate.
#   make trace       `test' with every byte on the wire hexdumped, for a failure
#                    that needs to be seen at the link layer.
#   make smoke       ping only (no telnet), for a quick answer about the link.
#   make udp         a datagram between the two guests (sntp against sntpsrv).
#                    The UDP path had never been tested between two C900s.
#   make peerfirst   ONE guest and a host TCP peer: the three-byte exchange talk
#                    does, with the peer speaking FIRST.  make peerfirst-listen
#                    for talk's interrupted ring, make peerfirst-negative for the
#                    mutation case, make peerfirst-check for all three.  Minutes,
#                    not the twenty `talk' costs -- run it BEFORE `talk'.
#   make talk        talk(1) between the two machines, through the ntalk service
#                    /etc/inetd answers at both ends.  make talk-negative for the
#                    cut-wire control.
#   make staleaddr   the mutation case for `udp': put the read-once interface
#                    address back into the stack and require `udp' to refuse the
#                    result.  Builds two images, so it is the slowest thing here
#                    and must not run beside another image build.
#
# Variables:
#   IMAGE=...        the image to boot (a copy per guest), the test image by
#                    default
#   EMU=...          a commodore-900-emulator checkout, or its bin/c900.  Unset,
#                    a checkout beside this repository is used; see
#                    mk/emulator.sh for the search and for what to clone.
#
# Neither guest's image is modified, and nothing here rebuilds an image except
# `staleaddr', which has to: the defect it re-creates is in the stack's source.
# Every other target needs an image that already exists.  A run takes roughly ten minutes: two
# multi-user boots plus the inet daemon, which initialises ten protocol layers on a 6 MHz
# machine.

PY   ?= python3
IMAGE ?=
IMG = $(if $(IMAGE),--image=$(IMAGE))
HARNESS = $(PY) twohost.py $(IMG)

.PHONY: test negative check trace smoke talk talk-negative udp staleaddr help \
	peerfirst peerfirst-listen peerfirst-negative peerfirst-check

test:
	$(HARNESS)

negative:
	$(HARNESS) --cut

check:
	$(MAKE) test
	$(MAKE) negative

trace:
	$(HARNESS) --trace --keep

smoke:
	$(HARNESS) --quick

# talk -- two people talking to each other across the wire, which is the only
# way talk(1) and the ntalk service can be exercised at all: a call is a
# rendezvous through a daemon at BOTH ends, and the announcement needs a logged-in
# user with a terminal.  See talk-test.py.
talk:
	$(PY) talk-test.py $(IMG)

talk-negative:
	$(PY) talk-test.py $(IMG) --cut

# udp -- does a datagram cross the wire between two guests?  Every other UDP
# test in the tree is loopback on one machine or has the host's Python at the
# far end, so when the talk daemon first reported no answer there was nothing to
# ask.
# Run this BEFORE suspecting a UDP daemon.
udp:
	$(PY) udp-two-test.py $(IMG)

# staleaddr -- `udp' seen to fail.  Its reconfigure-then-send assertion watches
# for a stack that keeps a copy of the interface address, which is a property of
# the source and cannot be produced from the harness side, so the gate puts the
# defect back and requires the refusal.  See the script.
staleaddr:
	sh staleaddr-gate.sh

# peerfirst -- ONE guest and a host TCP peer, for the delivery question talk(1)
# asks: a three-byte write followed by a three-byte read, with the PEER speaking
# first.  Every other client here writes before its peer answers, so a segment
# that arrives before the program asks for it was exercised nowhere.  One boot,
# so minutes rather than the twenty `talk' costs -- run this first.
peerfirst:
	$(PY) peerfirst-test.py $(IMG)

# The same with the guest LISTENING and its first listen ended by its own alarm,
# which is talk's ring (proto.c TalkInit) and the only interrupted network
# operation anywhere in this tree.
peerfirst-listen:
	$(PY) peerfirst-test.py $(IMG) --listen

# The mutation case, and it needs no rebuild: the same payload with PSH cleared
# must NOT be delivered to a short read, so this passes when the test fails.  A
# delivery assertion that cannot fail proves nothing (#101).
peerfirst-negative:
	$(PY) peerfirst-test.py $(IMG) --no-psh

peerfirst-check:
	$(MAKE) peerfirst
	$(MAKE) peerfirst-listen
	$(MAKE) peerfirst-negative

help:
	@sed -n '2,36p' Makefile
