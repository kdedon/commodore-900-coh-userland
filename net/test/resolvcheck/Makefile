# resolvcheck -- run the DNS resolver against a real nameserver.
#
#   make test        boot one guest on the emulator with dnswire.py at the far
#                    end of its serial line, and put every resolver path
#                    through it.  PASS/FAIL, exit status.
#   make negative    the same run with the nameserver DEAF: it receives and
#                    logs but never answers.  Every case that judges an ANSWER
#                    must then fail; the two that judge what the guest SENT
#                    (nsparse, retry) must still pass, and are marked so.
#   make check       both, in the order that makes the result mean something.
#   make selftest    dnswire.py alone, against a fake guest on the host.  No
#                    emulator, no image -- it answers "is the instrument
#                    right?" before a run is spent asking about the guest.
#
# Variables:
#   IMAGE=...          the image to boot (a copy), the test image by default
#   EMU=...            the emulator binary.
#
# Nothing here rebuilds an image; the harness needs one that already exists.
# A run takes well under a minute.

PY   ?= python3
IMAGE ?=
RUN   = $(PY) resolvcheck.py $(if $(IMAGE),--image=$(IMAGE))

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
	@sed -n '2,22p' Makefile
