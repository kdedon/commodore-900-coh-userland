# sntpcheck -- does the C900 set its clock from the network?
#
#   make test        boot one guest on the emulator with sntpwire.py at the far
#                    end of its serial line, park the clock at a known-wrong
#                    value, and prove /etc/sntp moves it to a value generated
#                    per run that exists nowhere on the image.  PASS/FAIL, exit
#                    status.
#   make negative    the same run with the SNTP server DEAF: it receives and
#                    logs but never answers.  Every case that judges an ANSWER
#                    must then fail; the one that judges only what the guest
#                    SENT (unknown) must still pass, and is marked so.
#   make check       both, in the order that makes the result mean something.
#   make selftest    sntpwire.py alone, against a fake guest on the host.  No
#                    emulator, no image -- it answers "is the instrument right?"
#                    before a run is spent asking about the guest.
#
# Variables:
#   IMAGE=...          the image to boot (a copy), the test image by default
#   EMU=...            the emulator binary.
#   ONLY=name,name     run only these cases.
#
# Nothing here rebuilds an image; the harness needs one that already exists.

PY   ?= python3
IMAGE ?=
ONLY ?=
RUN   = $(PY) sntpcheck.py $(if $(IMAGE),--image=$(IMAGE)) $(if $(ONLY),--only=$(ONLY))

.PHONY: test negative check selftest help

test:
	$(RUN)

negative:
	$(RUN) --deaf

check:
	$(MAKE) test
	$(MAKE) negative

selftest:
	$(PY) selftest.py

help:
	@sed -n '2,25p' Makefile
