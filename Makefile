# COHERENT 3.x userland for the Commodore 900 (Z8001).
#
# The build is `make -C hostbuild' and stays there: this file exists so the
# dependency edges DEPS names can be placed from the repository root, which is
# where DEPS, mk/deps.sh and mk/deps-fetch.sh live and where every refusal in
# the tree tells a human to stand.
#
#   make deps             place every dependency edge listed in DEPS
#   make deps DEP=<name>  place just that one (toolchain, emu, kernel)

SHELL	= /bin/sh
.DELETE_ON_ERROR:
MAKEFLAGS += --no-builtin-rules --no-builtin-variables
.SUFFIXES:
HERE	:= $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

.DEFAULT_GOAL := help
.PHONY: deps help

help:
	@printf '%s\n' \
	  'make deps                place every dependency edge listed in DEPS' \
	  'make deps DEP=<name>     place just that one: toolchain, emu, kernel' \
	  '' \
	  'The build is elsewhere:' \
	  'make -C hostbuild help   the userland build and its targets'

# --- dependencies ----------------------------------------------------------
# DEP=<name> places just that edge; no DEP places every edge in DEPS.  The
# fetcher asks mk/deps.sh whether an edge already resolves and leaves it alone
# if it does, so this is safe to run in a tree that is already set up.
deps:
	sh $(HERE)/mk/deps-fetch.sh $(DEP)
