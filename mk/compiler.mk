# compiler.mk -- select WHICH C compiler builds a system.  Include, do not run.
#
# Every tree that compiles Z8001 code names its flavour here rather than
# hard-coding a driver:
#
#	make ... COMPILER=<flavour>
#
# `make compiler-info' prints what the flavour resolved to and, if it did not
# resolve, why.  Resolved means every binary the flavour names exists and is
# runnable, no more.
#
# --- the flavours -----------------------------------------------------------
# mwc:       Built from commodore-900-toolchain (cc0/cc1/cc2-z8001, as-z8001, ld-z8001).
#
# --- the default ------------------------------------------------------------
# A caller sets C900_CC_DEFAULT before including this file; COMPILER= on the
# command line beats it.
C900_CC_DEFAULT ?= mwc
COMPILER ?= $(C900_CC_DEFAULT)

C900_MKDIR2 := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
C900_ROOT   := $(abspath $(C900_MKDIR2)/..)

# The toolchain: deps/ for a release, then a checkout beside this
# repository, then one inside a `repos/' directory beside it.  The list is
# mk/deps.sh's, not a fourth copy of it -- `make deps' places things where that
# file looks, so a search here that differed from it would be a lie about what
# `make deps' had just done.  $(C900_TC_SHAPE) is `checkout' or `release X.Y.Z',
# empty when nothing resolved.
ifeq (,$(C900_TOOLCHAIN))
C900_TOOLCHAIN := $(shell C900_TOOLCHAIN= sh $(C900_MKDIR2)/deps.sh toolchain)
endif
C900_TC_SHAPE := $(shell C900_TOOLCHAIN='$(C900_TOOLCHAIN)' sh $(C900_MKDIR2)/deps.sh -k toolchain)
# Where the toolchain's artifacts are READ from.  $(C900_TC_BUILD) when set,
# $(C900_TOOLCHAIN)/host/build otherwise -- the same rule and the same default
# as hostbuild/toolchain.mk's $(TCB), which documents it.  `make
# compiler-info' prints the as/ld paths in full, so which one answered here is
# reported rather than implied.
C900_TCB := $(if $(C900_TC_BUILD),$(abspath $(C900_TC_BUILD)),$(C900_TOOLCHAIN)/host/build)

# The emulator: `--exec' runs a linked l.out as a process against the host
# filesystem.  Resolved in emulator.mk so a shell gets the same answer.
include $(C900_MKDIR2)/emulator.mk

# ---------------------------------------------------------------------------
# Resolution.  Each flavour sets C900_CC/AS/LD/CPP/AR/RANLIB/YACC, or sets
# C900_CC_WHY to one sentence saying what is missing.  A flavour that cannot
# resolve is not an error here: `make compiler-info' must be able to report on
# it, and a target that never invokes a compiler must not be stopped by one.
# The refusal happens at the command that wanted the compiler.
# ---------------------------------------------------------------------------
C900_CC_WHY :=

ifeq ($(COMPILER),mwc)
  ifeq (,$(C900_TOOLCHAIN))
    C900_CC_WHY := no commodore-900-toolchain (set C900_TOOLCHAIN, or run `sh mk/deps.sh -n toolchain' for the paths tried)
  else
    C900_CC_TC  := $(C900_TOOLCHAIN)
    C900_CC     := $(C900_TOOLCHAIN)/host/ccz
    # cc0-z8001 itself is NOT a cpp: this pipeline folds preprocessing into the
    # compiler's first pass, so cc0 takes the positional `cc0 VARIANT IN OUT'
    # form and no cpp flags.  host/cppz is the driver that gives that mode the
    # `cpp [-P] [-D/-U/-I] IN [OUT]' spelling the assembly rules call.
    C900_CPP    := $(C900_TOOLCHAIN)/host/cppz
    C900_AS     := $(C900_TCB)/as-z8001
    C900_LD     := $(C900_TCB)/ld-z8001
    # The archiver has to be the toolchain's: a GNU `ar' writes a GNU archive,
    # and ld-z8001 scanning one says "bad header" and then reports every symbol
    # in it undefined.  host/arz wraps mkarz, which is built against ld's own
    # ar.h/canon.o, so the member layout is by construction what ld reads.
    # No ranlib: arz writes the archive whole, index included, and there is no
    # separate indexer to run.  yacc is a host tool.
    C900_AR     := $(C900_TOOLCHAIN)/host/arz
    C900_RANLIB := true
    C900_YACC   := yacc
    # ccz is a one-shot driver with its own flag set; it does not take the
    # GCC-style spellings.
    C900_CFLAGS :=
  endif
endif

ifeq (,$(C900_CC)$(C900_CC_WHY))
C900_CC_WHY := unknown COMPILER=$(COMPILER) (known: mwc)
endif

.PHONY: compiler-info
compiler-info:
	@echo "COMPILER   = $(COMPILER)"
ifeq (,$(C900_CC_WHY))
	@echo "status     = resolved"
	@echo "cc         = $(C900_CC)"
	@echo "cpp        = $(C900_CPP)"
	@echo "as         = $(C900_AS)"
	@echo "ld         = $(C900_LD)"
	@echo "ar         = $(C900_AR)"
	@echo "ranlib     = $(C900_RANLIB)"
	@echo "yacc       = $(C900_YACC)"
	@echo "cflags     = $(C900_CFLAGS)"
else
	@echo "status     = UNRESOLVED"
	@echo "reason     = $(C900_CC_WHY)"
endif
# end of compiler.mk
