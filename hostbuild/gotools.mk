# gotools.mk -- resolve the c900oses/gotools tree, for makefiles.
# The same shape as toolchain.mk beside it, and for the same reason.
#
# Some host instruments are Go and import the PRIVATE c900/z8000 simulator
# checkouts: hostfsd, kdbg, and -- since 2026-08 -- loutdis, the l.out/n.out
# disassembler this tree uses to read section sizes and headers back out of a
# linked binary.  They used to live in commodore-900-toolchain/tools/go, but
# that repository is public and its gates must build without a simulator on the
# machine, so they moved to c900oses/gotools where the dependency is allowed.
# $(C900_TOOLCHAIN) therefore no longer answers "where is loutdis"; this does.
#
# They are Go, so the Go compiler is a dependency of this repository's HOST
# instruments and of nothing it ships; a tree that resolves is not enough, and
# `go: not found' out of a recipe two makefiles down names neither the tool
# that is missing nor what wanted it.
#
#   $(C900_GOTOOLS)  the gotools tree.  Unset, the first of $(C900_GT_SEARCH)
#                    with a Makefile in it wins, so a plain `make' works both
#                    when this repository is a sibling checkout and when it sits
#                    inside a `repos/' staging directory.  A single hard-coded
#                    relative path is silently wrong in whichever of the two it
#                    was not written for.
#
# Defines:
#   $(LOUTDIS)  the disassembler, BUILT ON DEMAND by the $(LOUTDIS) rule below:
#               it is an artifact of that tree's Makefile, not of this one.
#
# Rules that use it depend on $(LOUTDIS) and invoke $(LOUTDIS), so a missing
# gotools tree is one named error and not `loutdis: not found' from inside a
# recipe.
C900_GTDIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
# $(C900_GTDIR) is hostbuild.  Candidates, in order: this repository staged
# inside c900oses/repos/, then a c900oses checked out beside it, then one level
# further out either way.
C900_GT_SEARCH := $(abspath $(C900_GTDIR)/../../../gotools) \
		  $(abspath $(C900_GTDIR)/../../c900oses/gotools) \
		  $(abspath $(C900_GTDIR)/../../../c900oses/gotools)
C900_GOTOOLS ?= $(firstword $(patsubst %/Makefile,%,\
		 $(wildcard $(addsuffix /Makefile,$(C900_GT_SEARCH)))))
LOUTDIS := $(C900_GOTOOLS)/build/loutdis

# Built through gotools' own Makefile: it resolves the simulator by name and
# says which variable to set when it cannot find it, which a bare `go build'
# here could not do.  .PHONY, because that Makefile decides whether anything
# needs recompiling -- this one has no idea what loutdis is made of.
.PHONY: $(LOUTDIS)
$(LOUTDIS):
	@if [ ! -f "$(C900_GOTOOLS)/Makefile" ]; then \
		echo "no gotools tree at C900_GOTOOLS=$(C900_GOTOOLS): loutdis moved" >&2; \
		echo "out of commodore-900-toolchain (it needs the private simulator)." >&2; \
		echo "Clone c900oses to one of: $(C900_GT_SEARCH)" >&2; \
		echo "-- or set C900_GOTOOLS to the gotools directory." >&2; \
		exit 1; \
	fi
	@command -v go >/dev/null 2>&1 || { \
		echo "no go on the path: loutdis is a Go program, built from" >&2; \
		echo "$(C900_GOTOOLS) by its own Makefile.  Nothing this repository" >&2; \
		echo "SHIPS needs go; this host instrument does." >&2; \
		exit 2; \
	}
	@$(MAKE) -s -C $(C900_GOTOOLS) loutdis
# end of gotools.mk
