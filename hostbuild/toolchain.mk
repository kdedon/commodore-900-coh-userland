# toolchain.mk -- resolve the Z8001 cross toolchain, for makefiles.
# The shell equivalent, and the long-form rationale, is toolchain.sh beside it.
#
# The compiler, assembler and linker are not in this repository: they live in
# `commodore-900-toolchain', which has three consumers -- COHERENT, CP/M-8000
# and kboot -- and belongs to none of them.  This tree consumes one of two
# shapes of it: a source CHECKOUT, or an unpacked RELEASE archive.
#
#   $(C900_TOOLCHAIN)  the toolchain.  Unset, mk/deps.sh searches -- deps/ for
#                      the pinned release, then a checkout beside this
#                      repository, then one inside a `repos/' staging
#                      directory.  Override it (on the command line or in the
#                      environment) to build against something else; a value
#                      that does not resolve is refused as itself, not quietly
#                      re-searched.  The search is in mk/deps.sh and nowhere
#                      else, so `make deps', this file and toolchain.sh cannot
#                      answer the same question three different ways.
#
# Defines:
#   $(TC)      the toolchain's host directory: ccz, the build-*.sh harnesses
#              (checkout only), and build/ (cc0/cc1/cc2-z8001, as-z8001,
#              ld-z8001, libc-z8001.a, curses, libm ...)
#   $(TCB)     the build directory to READ artifacts out of, for the makefiles
#              that only want the binaries: $(C900_TC_BUILD) when set,
#              $(TC)/build otherwise.  That is the toolchain's own name for the
#              directory as well, so one setting moves both where it emits and
#              where this tree reads; a lane whose compiler is its own sets it
#              and nothing else.  It is exported, so a toolchain script run from
#              a recipe here lands in the directory these makefiles read.  Which
#              was chosen is REPORTED, like the shape.
#   $(C900_TC_SHAPE)   `checkout' or `release X.Y.Z' -- WHICH of the two this
#              build resolved.  A release archive carries a host/ view of its
#              own bin/, so both shapes spell the parts identically and no
#              consumer learns a second layout; but a binary archive has no
#              build-*.sh and none of the libraries they build from this tree
#              (curses, libm, libmisc), so the two are not interchangeable and
#              the difference is REPORTED rather than left to be discovered.
#
# and exports $(COHERENT_OS), the reciprocal input: the toolchain builds itself
# from its own sources alone, but a few of its harnesses build OS artifacts WITH
# it (libc-z8001.a, libm, ccz's default include path) and take an OS tree as an
# input.  See the toolchain's host/coherent-os.sh.
C900_MKDIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
# $(C900_MKDIR) is hostbuild, so the resolver is one level up.
C900_DEPS := $(abspath $(C900_MKDIR)/../mk/deps.sh)
# Simply-expanded, and only when nothing named it: `?=' would make the variable
# recursive and re-run the search at every mention of it.
ifeq (,$(C900_TOOLCHAIN))
C900_TOOLCHAIN := $(shell C900_TOOLCHAIN= sh $(C900_DEPS) toolchain)
endif
C900_TC_SHAPE := $(shell C900_TOOLCHAIN='$(C900_TOOLCHAIN)' sh $(C900_DEPS) -k toolchain)
TC := $(C900_TOOLCHAIN)/host
TCB := $(if $(C900_TC_BUILD),$(abspath $(C900_TC_BUILD)),$(TC)/build)
# $(KINC): the KERNEL's header set, the make-side twin of toolchain.sh's $KINC
# and resolved through the same `kernel' edge.  The userland owns no machine
# layer: <sys/machine.h> is the toolchain's and, under Z8001 (which cc0-z8001
# predefines), it includes <sys/machz8001.h>, which exists only in the kernel
# repository -- as do <sys/timeout.h>, <sys/mouse.h>, <sys/con.h>, <sys/io.h>,
# <sys/mount.h> and <sys/mdata.h>.  EMPTY when no kernel resolves, and that is
# deliberate: only the few things that reach those headers need it, so a build
# of everything else must not be blocked by a missing kernel checkout.  What
# needs it refuses by name -- quoting `sh mk/deps.sh -n kernel' -- rather than
# compiling against whatever else happens to be reachable.
C900_KERNEL_DIR := $(shell C900_KERNEL='$(C900_KERNEL)' sh $(C900_DEPS) kernel 2>/dev/null)
KINC := $(or $(wildcard $(C900_KERNEL_DIR)/os/include),$(wildcard $(C900_KERNEL_DIR)/include))
# An empty shape is the "did not resolve" answer, whether nothing was found or
# $(C900_TOOLCHAIN) named something that is neither shape.
#
# $(C900_TC_OPTIONAL) suppresses the refusal.  A caller sets it when the work it
# is about to do compiles nothing -- packing an image of a system whose parts are
# already built -- because a hard requirement here would make the compiler a
# dependency of every operation in the tree, including ones that never invoke it.
# It suppresses only the DIAGNOSIS: $(TC) is still empty, so any rule that does
# reach for a compiler fails on the spot, loudly, at the command that wanted one.
ifeq (,$(C900_TC_SHAPE))
ifeq (,$(C900_TC_OPTIONAL))
# deps.sh writes the refusal -- the variable, whether its own value was the only
# thing tried, and every path -- straight to make's stderr; $(shell) captures
# stdout, which in this mode is empty.  $(error) then stops, because a refusal
# make prints and then carries on from is not a refusal.
$(shell C900_TOOLCHAIN='$(C900_TOOLCHAIN)' sh $(C900_DEPS) -n toolchain '$(C900_TOOLCHAIN)')
$(error no Z8001 toolchain: C900_TOOLCHAIN and the paths tried are listed above)
endif
endif
COHERENT_OS := $(abspath $(C900_MKDIR)/..)
# $(TCID): the compiler's source id, alone in a file, rewritten only when it
# changes.  Every target compiled with the toolchain names it as a prerequisite,
# which is what makes a changed compiler rebuild what came out of the old one
# instead of being a question somebody has to answer.
TCID := $(COHERENT_OS)/hostbuild/build/.tcid
# Once per build: this file is included by a dozen makefiles and by recursive
# makes under them, and the exported flag is what keeps that one line.  stderr,
# not $(info), so a target whose stdout is read by a script stays readable.
ifeq (,$(C900_TC_REPORTED))
ifneq (,$(C900_TC_SHAPE))
$(shell echo "toolchain: $(C900_TC_SHAPE) at C900_TOOLCHAIN=$(C900_TOOLCHAIN)" >&2)
# Beside it, and only when it is not the default: a non-default build directory
# changes WHICH compiler ran as completely as a different toolchain does.
ifneq (,$(C900_TC_BUILD))
$(shell echo "toolchain: build dir C900_TC_BUILD=$(TCB) (not the shared $(TC)/build)" >&2)
endif
# WHICH compiler, recorded where the build system can depend on it.  Done at
# parse time, before any rule is considered, so $(TCID) is current by the time
# make compares it against the targets that name it as a prerequisite.
#
# Not for a run that compiles nothing.  $(C900_TC_OPTIONAL) is the caller's
# statement that this one does not -- an info goal, or a dist built from a tree
# somebody else compiled.  Such a run has nothing to rebuild, so it has no use
# for the dependency and no reason to write a file for it.
ifeq (,$(C900_TC_OPTIONAL))
$(shell . $(C900_MKDIR)/provenance.sh; \
	prov_tc_record '$(TCB)' '$(COHERENT_OS)/hostbuild/build/.tcstamp' '$(TCID)' '$(C900_TC_SHAPE)')
endif
C900_TC_REPORTED := 1
endif
endif
export TCID
export C900_TOOLCHAIN COHERENT_OS C900_TC_SHAPE C900_TC_REPORTED C900_TC_BUILD
# end of toolchain.mk
