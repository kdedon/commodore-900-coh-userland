# Makefile -- build entry point for the userland host build.
#
#   make userland              the full cmd/games/libs sweep
#   make toolchain             the Z8001 cross toolchain (a separate repository)
#   make libc                  libc-z8001.a + crt0.o, built from this tree's sources
#   make man                   /usr/man, from the pages every component tracks
#   make packages              export every component as bin/src/man archives
#   make publish               build the sweep and stamp it, for the dist repo
#   make check-publish-fresh   assert publish ran after everything else, not eyeball it
#   make clean-work            drop the harnesses' working copies
#
# This repository builds programs; it does not link a kernel and does not
# pack an image.  The kernel and image-packing tools live in separate
# repositories (commodore-900-coh-kernel3 and commodore-900-dist).
# This tree contains every program, its data files and its man pages.
#
# The recipes are the sweep scripts, which carry stateful host-port patching
# that does not belong in a make rule.

SHELL	= /bin/sh
PYTHON	?= python3
# A recipe that dies mid-write must not leave a partial image looking finished.
.DELETE_ON_ERROR:
# Cancel the builtin rules: every staged file is a Z8001 guest binary, and a
# missing one must not match `%: %.c' and be "built" by the host compiler.
MAKEFLAGS += --no-builtin-rules --no-builtin-variables
.SUFFIXES:
# Grouped targets (`&:') need GNU make 4.3; an older make parses them as a
# rule for a target named `&' and silently builds nothing.
ifeq (,$(filter grouped-target,$(.FEATURES)))
$(error GNU make 4.3 or newer is required (grouped targets); this is $(MAKE_VERSION))
endif
HERE	:= $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
OS	:= $(abspath $(HERE)/..)
# The toolchain is a separate repository; toolchain.mk defines $(TC) and
# exports $(COHERENT_OS).  C900_TC_OPTIONAL keeps a missing checkout non-fatal
# when the goal only asks the tree a question and builds nothing.  INFO_GOALS
# must be named before the include: make expands an $(error) while it parses,
# so gating after the fact is too late.
INFO_GOALS = help check-curses-header
C900_TC_OPTIONAL := $(if $(filter $(INFO_GOALS),$(MAKECMDGOALS)),1,)
include $(HERE)/toolchain.mk
# B is fixed here: the recipes compute "$HERE/build/..." internally.
# ld-z8001 cannot create files whose paths exceed ~96 characters.
B	:= $(HERE)/build
# What the compiler was actually invoked on, appended by every harness that
# compiles (ccz, and the four here that drive the passes themselves).
# Exported from here rather than defaulted in the scripts so that a build run by
# hand records nothing until it is asked to.  Append-only across builds; delete
# it to forget what the tree no longer compiles.
C900_BUILD_LOG := $(B)/compiled-sources.log
export C900_BUILD_LOG
# WHICH PROGRAM each of those sources was compiled FOR, recorded by the same
# harnesses (host/buildlog.sh's c900_buildmap).  A separate file and not a
# column of the log above: that log's reader takes every line as a bare path,
# and the two records answer different questions -- one is "was this source
# judged", which is about the tree, the other is "what is this program made of",
# which is about a shipped binary and is what a licence obligation asks.
# publish-userland.sh folds it into build/.ulsrcmap, which is the published
# form; this is the raw record and, like the log, is append-only.
C900_BUILD_MAP := $(B)/compiled-programs.log
export C900_BUILD_MAP
# A source list built by find(1) over a path that does not exist is EMPTY, not
# an error, so a renamed directory silently costs a stamp its dependencies and
# every edit under it stops triggering a rebuild.  Every find list below names
# its directories through this: a missing one stops the build here.
define dirs-exist
$(foreach d,$(2),$(if $(wildcard $(d)),,$(error $(1): no such directory: $(d))))
endef

MAN	= $(B)/man
# Sweeps with no per-file rules get a stamp; the sweep is the unit that
# succeeds or fails.
TCSTAMP	= $(B)/.stamp-toolchain
ULSTAMP	= $(B)/.stamp-userland
NETSTAMP = $(B)/.stamp-net
HUNTSTAMP = $(B)/.stamp-hunt
NETCMDSTAMP = $(B)/.stamp-netcmds
SCREENSTAMP = $(B)/.stamp-screen
TESTSTAMP = $(B)/.stamp-tests
LIBCSTAMP = $(B)/.stamp-libc
# archive and comms are package-per-directory trees with per-package -D
# switches and their own file selection, so the cmd/ sweep cannot build them.
ARCSTAMP = $(B)/.stamp-archive
COMMSTAMP = $(B)/.stamp-comms
# MGR's three staged binaries; see the `mgr' rule for why they are files.
MGRBIN	= $(B)/mgr/bin/mgr $(B)/mgr/bin/mgrlogin $(B)/mgr/bin/hrlight
# The client programs, staged by dist/lists/mgr-clients.list; the list of
# names lives in mgr/Makefile.c900's CLIENTS and is asked for, not repeated.
# MAKE_RESTARTS is cleared for the query: after regenerating build/mk/<dist>.d
# make re-execs with MAKE_RESTARTS set, which turns -w back on in every
# sub-make despite --no-print-directory, and the "Leaving directory" line
# would be parsed as client names.
MGRCLIENTS := $(shell MAKE_RESTARTS= $(MAKE) -s --no-print-directory -C $(OS)/mgr -f Makefile.c900 echo-clients 2>/dev/null)
MGRCLBIN = $(MGRCLIENTS:%=$(B)/mgr/bin/cl/%)

.PHONY: FORCE all userland net net-hostcheck hunt netcmds curses screen tests man toolchain libc \
	archive comms mgr hr publish check-publish-fresh clean-stage clean-work help \
	check-curses-header

# The full sweep: every program this repository ships, plus its man pages and
# libraries.  Does not link a kernel and does not pack an image -- see the
# header comment for where those moved.
SWEEP = userland net hunt netcmds screen archive comms tests mgr hr man

# `publish' is the sweep's last step, and the sweep is its prerequisite.
# It writes build/.ulstamp, which is how commodore-900-dist knows which
# userland it is packing and whether that userland is still current with the
# sources beside it -- a claim about build/, made by reading the sources.  Made without building first, that claim can be false the moment it
# is written: a tree whose sources moved after the last sweep gets a stamp
# saying the binaries match them, and the packer that trusts it stages
# yesterday's programs under today's name.  So publishing BUILDS what it
# publishes; a tree already swept re-stamps and compiles nothing.
all: publish

publish: $(SWEEP)
	sh $(HERE)/publish-userland.sh

# The failure mode this guards: `all' exits 0 (every prerequisite built) yet
# `publish' itself never ran or ran against a STALE build/.ulstamp, which
# looks identical to a finished build to anything that only checks the exit
# code.  A publish that ran after everything else this build touched leaves
# .ulstamp not older than any of it; run this right after `make all' to check
# that, rather than eyeball a log.
#
# build/.tcstamp is excluded: toolchain.mk's identity record (prov_tc_record)
# rewrites it every time this Makefile is PARSED, whether or not any target
# rebuilds anything, so it is newer than .ulstamp after nothing but a second
# `make' invocation and would fail this check for a reason that has nothing
# to do with publish.  build/.tcid is excluded for the same reason: it is
# compared and, when unchanged, left alone -- but the comparison itself runs
# at every parse.
.PHONY: check-publish-fresh
check-publish-fresh:
	@test -f "$(B)/.ulstamp" || \
	    { echo "check-publish-fresh: no $(B)/.ulstamp -- publish never ran"; exit 1; }
	@newer=$$(find "$(B)" -type f ! -name .ulstamp ! -name .tcstamp ! -name .tcid -newer "$(B)/.ulstamp" | head -1); \
	if [ -n "$$newer" ]; then \
	    echo "check-publish-fresh: $$newer is newer than $(B)/.ulstamp"; \
	    echo "check-publish-fresh: publish did not run after this build finished"; \
	    exit 1; \
	fi
	@echo "check-publish-fresh: ok -- $(B)/.ulstamp is not older than anything else under $(B)"
help:
	@sed -n '2,18p' $(HERE)/Makefile

FORCE:

# --- the parts ----------------------------------------------------------------
userland: $(ULSTAMP)
# The sources are prerequisites, not just the toolchain that compiles them.
# *.y counts: sh, rsh, awk, find and test are built from their grammar, which
# build-yacc-cmd.sh regenerates.
#
# ULDIRS is EVERY tree build-all-userland.sh compiles out of, and the list is
# derived from the scripts it runs rather than from what the sweep is called:
# a directory left out of it is a directory where an edit rebuilds NOTHING and
# invalidates no stamp, and the sweep then reports success while staging the
# (ps; pr and tr) that build-extra-userland.sh compiles under the OLD name
# because a file of the SAME name already exists, and is built, in cmd/ --
# see build-userland.sh's own ps/pr/tr skip cases for which copy ships.
# games/ is the last step of the sweep.
#   base/cmd       build-userland.sh, build-cmd.sh, build-extra-userland.sh and
#                  the yacc/rogue/elvis scripts
#   base/lib       libcurses/libterm/ndir/regexp, linked by the above
#   editors        build-editors.sh (me, pico)
#   base/cmd       build-less.sh, build-patch.sh, build-rcs.sh
#   archive        build-gzip.sh
#   games          build-games.sh + build-curses-games.sh
# include is NOT listed, and no longer exists: every header it held was
# a stale duplicate of one the toolchain or the kernel owns, and shadowed it.
# The headers the sweep compiles against now come from those two repositories
# alone -- which means this list no longer sees them change, and a header-only
# edit there will not by itself invalidate $(ULSTAMP).  The toolchain half is
# still covered, by $(TCSTAMP); the kernel's is not, so a change to $KINC needs
# `make clean-userland' (or a touch of any source below) to be picked up.
ULDIRS = $(OS)/base/cmd $(OS)/base/lib $(OS)/editors $(OS)/archive/gzip \
	 $(OS)/games
$(call dirs-exist,ULDIRS,$(ULDIRS))
ULSRC := $(shell find $(ULDIRS) \
	   \( -name '*.c' -o -name '*.h' -o -name '*.s' -o -name '*.y' \))
# The sweep's non-C inputs: the game data files build-games.sh stages into
# build/root (fortunes, quiz.db, the .m4 and .msg tables) and the list that
# decides which games are built with curses rather than without.  They decide
# what is staged just as a .c does, and the scripts' own `-nt' guards only run
# once the stamp is out of date.
ULSRC += $(wildcard $(OS)/games/bsd/CURSES.list) \
	 $(shell find $(OS)/games/lib -type f 2>/dev/null)
# Where build-hunt.sh finds libcurses/libterm: the OS's own archives, staged
# in the toolchain's build directory beside libc-z8001.a, where ccz looks.
CURSESDIR := $(TCB)/curses

# The sweep's recipe: build-all-userland.sh and every script it runs.  Each of
# them decides the compiler's command line for the programs it links -- which
# memory model, which -D switches, which libraries -- so an edit to one
# produces a different binary just as an edit to a .c does, and the source map
# names the script that built a program as that program's recipe.  The entry
# point alone is not the recipe: a script left out of this list is a script
# where an edit rebuilds NOTHING and invalidates no stamp, while the report
# reading the map says every program it builds is behind it.  The other
# sweeps' scripts are prerequisites of their own stamps.
ULSCRIPTS = $(addprefix $(HERE)/,build-all-userland.sh build-userland.sh \
	      build-cmd.sh build-hyacc.sh build-awk.sh build-nawk.sh \
	      build-yacc-cmd.sh build-bc.sh build-elvis.sh build-editors.sh \
	      build-less.sh build-gzip.sh build-patch.sh build-rcs.sh \
	      build-rogue.sh build-extra-userland.sh build-games.sh \
	      build-curses-games.sh)

$(ULSTAMP): $(TCSTAMP) $(LIBCSTAMP) $(ULSCRIPTS) $(ULSRC) \
	      $(CURSESDIR)/libcurses.a $(CURSESDIR)/libterm.a
	sh $(HERE)/build-all-userland.sh
	@mkdir -p $(dir $@) && touch $@

# The networking stack and the standalone guest test programs, staged by
# dist/lists/net.list.
net: $(NETSTAMP)
# find, not wildcard: the ioctl headers live at net/include/net/gen/*.h,
# four levels down.
NETSRC := $(shell find $(OS)/net \( -name '*.c' -o -name '*.s' -o -name '*.h' \) \
	    2>/dev/null)
$(NETSTAMP): $(TCSTAMP) $(LIBCSTAMP) $(HERE)/build-net.sh $(NETSRC)
	sh $(HERE)/build-net.sh
	@mkdir -p $(dir $@) && touch $@

# hunt(6): needs both net (libsocket) and curses (the client draws the maze),
# which is why it is its own target rather than part of the games sweep.

CURSESSRC := $(shell find $(OS)/base/lib/libcurses $(OS)/base/lib/libterm \
	       \( -name '*.c' -o -name '*.h' \) 2>/dev/null)
curses: $(CURSESDIR)/libcurses.a
# Grouped, like the drivers: one script builds both archives.
$(CURSESDIR)/libcurses.a $(CURSESDIR)/libterm.a &: \
		$(TCSTAMP) $(LIBCSTAMP) $(HERE)/build-curses.sh $(CURSESSRC)
	sh $(HERE)/build-curses.sh

hunt: $(HUNTSTAMP)
# The two libraries it links are prerequisites, not just the stamps that
# happen to build them.
$(HUNTSTAMP): $(NETSTAMP) $(ULSTAMP) $(HERE)/build-hunt.sh \
	      $(OS)/net/libsocket.a $(CURSESDIR)/libcurses.a $(LIBCSTAMP) \
	      $(wildcard $(OS)/games/net/hunt/*/*.c $(OS)/games/net/hunt/*/*.h)
	sh $(HERE)/build-hunt.sh
	@mkdir -p $(dir $@) && touch $@

# The net clients whose source is a directory are outside the userland sweep.
# libsocket.a is a prerequisite: they reach the stack entirely through that
# library's /dev/tcp shim.  Every directory the script compiles is named, so an
# edit to any one of them invalidates the stamp; the SMTP pair is the mail
# component's and is built here because it links the same library.
NETCMDDIRS = $(addprefix $(OS)/net/,telnet telnetd ftp host rlogin finger \
	       talk ping pr_routes add_route netstat fingerd remsh remshd) \
	     $(OS)/mail/smtpd $(OS)/mail/smtpsend
netcmds: $(NETCMDSTAMP)
$(NETCMDSTAMP): $(NETSTAMP) $(LIBCSTAMP) $(HERE)/build-netcmds.sh \
		$(OS)/net/libsocket.a \
		$(wildcard $(addsuffix /*.c,$(NETCMDDIRS)) \
			   $(addsuffix /*.h,$(NETCMDDIRS)))
	sh $(HERE)/build-netcmds.sh
	@mkdir -p $(dir $@) && touch $@

# screen: nine objects, the large model, and libterm rather than libsocket.
screen: $(SCREENSTAMP)
$(SCREENSTAMP): $(LIBCSTAMP) $(HERE)/build-screen.sh \
		$(CURSESDIR)/libterm.a \
		$(wildcard $(OS)/net/screen/*.c $(OS)/net/screen/*.h)
	sh $(HERE)/build-screen.sh
	@mkdir -p $(dir $@) && touch $@

# The archivers and the serial transfer programs: one script each, because
# each package needs its own -D switches and its own file list.
archive: $(ARCSTAMP)
$(ARCSTAMP): $(LIBCSTAMP) $(HERE)/build-archive.sh \
		$(wildcard $(OS)/archive/*/*.c $(OS)/archive/*/*.h $(OS)/archive/*/*.i)
	sh $(HERE)/build-archive.sh
	@mkdir -p $(dir $@) && touch $@

comms: $(COMMSTAMP)
# Everything this step compiles is under comms: ckermit uses open(2)/read(2)
# to read directories, as COHERENT's own ls(1) does.
$(COMMSTAMP): $(LIBCSTAMP) $(HERE)/build-comms.sh \
		$(wildcard $(OS)/comms/*/*.c $(OS)/comms/*/*.h $(OS)/comms/*/*.w)
	sh $(HERE)/build-comms.sh
	@mkdir -p $(dir $@) && touch $@

tests: $(TESTSTAMP)
$(TESTSTAMP): $(TCSTAMP) $(LIBCSTAMP) $(HERE)/build-tests.sh $(wildcard $(OS)/test/*/*.c)
	sh $(HERE)/build-tests.sh
	@mkdir -p $(dir $@) && touch $@

# The multiboot loader (kboot) and CP/M-8000 are built by the distribution
# repository, not here.

# MGR, the window manager, built by its own Makefile.  BUILD stays short:
# ld-z8001 cannot create an output file whose path runs past ~96 characters.
# The three binaries are named as files because dist/lists/mgr.list stages
# them; the link rules relink unconditionally, so the sub-make is what makes
# them current and FORCE is what gets it run.
# maze(6) links net/libsocket.a, so the stack must exist before the clients
# are linked; Makefile.c900 names the archive as a prerequisite with no rule
# of its own, so a wrong order fails loudly.
mgr: net $(MGRBIN) $(MGRCLBIN)
# Grouped: as independent targets `make -j dists' runs the sub-make
# concurrently into the same output.
# C900_BUILD_RECIPE is set here and not by Makefile.c900 sourcing toolchain.sh
# (which is what gives every sweep script its own name in the source map): MGR is
# built by a make, not by a script, and the recipe that decides which of its two
# thousand files are which of its fifty programs is that Makefile.  Without it
# the map names a client's sources and not what compiles them.
$(MGRBIN) $(MGRCLBIN) &: FORCE
	C900_BUILD_RECIPE=$(OS)/mgr/Makefile.c900 \
	$(MAKE) -C $(OS)/mgr -f Makefile.c900 BUILD=$(B)/mgr mgr hrlight clients

# The hi-res window system, built by the component's own Makefile: the loadable
# driver, the screen and desktop managers, the client library, the clients, the
# generated fonts and the icons.  BUILD is this tree's build directory, so all
# of it stages where dist/lists/hr.list and hr-clients.list look for it.
#
# It is the one sweep that needs the kernel edge.  /drv/hr is kernel code: it
# compiles against the kernel's header set and `ld -k' binds it to one kernel
# image, whose absolute symbol addresses it carries, so it is relinked whenever
# the kernel is and a driver older than the kernel beside it is not loadable.
# That Makefile refuses with the paths it wanted when the export is absent.
# FORCE because the sub-make is what knows which of its parts are current.
# C900_BUILD_RECIPE for the same reason MGR's is set above: hr/Makefile is what
# links the window system's objects into its forty programs, and it records
# those links itself (hr/Makefile, buildmap).
hr: $(LIBCSTAMP) FORCE
	C900_BUILD_RECIPE=$(OS)/hr/Makefile \
	$(MAKE) -C $(OS)/hr BUILD=$(B) $(B)/root/drv/hr hrgui
	@mkdir -p $(B)/drv
	cp $(B)/root/drv/hr $(B)/drv/hr
	chmod 755 $(B)/drv/hr

man: $(MAN)
# Every page the manual is assembled from is tracked: the articles for what
# this repository builds sit in the component that builds it, the locally
# written pages in man, and the rest come across the toolchain and kernel
# edges.  All of them are prerequisites, so an edited or new page rebuilds
# /usr/man.
MANSRC := $(shell find $(OS)/man $(addsuffix /man,$(addprefix $(OS)/,base net \
	editors mail archive comms games hr mgr test)) \
	-type f 2>/dev/null)
$(MAN): $(HERE)/extract-man.sh $(MANSRC)
	sh $(HERE)/extract-man.sh

toolchain: $(TCSTAMP) $(LIBCSTAMP)

# An unpacked RELEASE is already built, and by construction: there are no
# sources to compile, no build-*.sh to run, and the libc in it was built by the
# release workflow against a named COHERENT commit (sec. 13.2/13.4).  So the two
# stamps stand for what the archive carries rather than for a build, and they
# depend on the artifacts themselves -- a truncated or half-unpacked archive
# still fails here, at the stamp, and not four hundred lines later at a link.
#
# The libc question a release DOES raise is the one the stamp cannot answer:
# whether it was built from the sources in THIS tree.  That is what the release
# stamp records and what the shape line reports, and it is the price of a pin.
ifneq (,$(filter release,$(C900_TC_SHAPE)))
$(TCSTAMP): $(TCB)/z8001/cc0-z8001 $(TCB)/as-z8001 $(TCB)/ld-z8001
	@mkdir -p $(dir $@) && touch $@
$(LIBCSTAMP): $(TCB)/libc-z8001/libc-z8001.a $(TCB)/libc-z8001/crt0.o
	@mkdir -p $(dir $@) && touch $@
else
# Sources are found rather than globbed: the machine backends sit a level
# deeper than the passes (cc/n2/z8001/*.c).
TCSRC := $(shell find $(C900_TOOLCHAIN)/src \
	   \( -name '*.c' -o -name '*.h' -o -name '*.t' \) 2>/dev/null)
# Delegated to the toolchain's own Makefile: which scripts make a toolchain is
# the toolchain's business.
$(TCSTAMP): $(wildcard $(TC)/build-*.sh) $(TCSRC)
	$(MAKE) -C $(C900_TOOLCHAIN)
	@mkdir -p $(dir $@) && touch $@

# libc + crt0, split out from the toolchain stamp because they are built from
# sources under $(OS).
libc: $(LIBCSTAMP)
# The headers libc compiles against belong to the toolchain, and $(TCSTAMP)
# already carries them.  So do the SOURCES: build-libc-z8001.sh compiles
# $(C900_TOOLCHAIN)/src/{libc,csu}, and this tree's own copies of those two --
# which were the i386 4.2.12 originals, not the ported ones -- were archaeology
# that no build read.  They were removed on 2026-08-21, so the dependency list
# names where the sources actually are; naming ours made an edit to the real
# libc leave this stamp up to date.
LIBCDIRS = $(C900_TOOLCHAIN)/src/libc $(C900_TOOLCHAIN)/src/csu
$(call dirs-exist,LIBCDIRS,$(LIBCDIRS))
LIBCSRC := $(shell find $(LIBCDIRS) \
	     \( -name '*.c' -o -name '*.s' -o -name '*.h' \))
# A missing emulator must cost a test, not the build.  The toolchain's script
# builds crt0.o and libc-z8001.a and then smoke-tests them under the emulator.
# If the script fails but left artifacts newer than $@.try, the build has what
# it came for: the stamp is written and the failure repeated, loudly, as the
# test result it is.  Otherwise the failure was in the build and stays fatal.
# $@.try is the freshness mark: without it a previous run's archive in the
# toolchain's build directory would pass the check.
LIBCOUT = $(TCB)/libc-z8001
$(LIBCSTAMP): $(TCSTAMP) $(TC)/build-libc-z8001.sh $(LIBCSRC)
	@mkdir -p $(dir $@) && touch $@.try
	@sh $(TC)/build-libc-z8001.sh; st=$$?; \
	 if [ $$st -ne 0 ]; then \
		if [ -f $(LIBCOUT)/libc-z8001.a ] && [ $(LIBCOUT)/libc-z8001.a -nt $@.try ] && \
		   [ -f $(LIBCOUT)/crt0.o ] && [ $(LIBCOUT)/crt0.o -nt $@.try ]; then \
			echo "libc: BUILT, but build-libc-z8001.sh exited $$st -- its smoke" >&2; \
			echo "      test did not run or did not pass (see above; the usual" >&2; \
			echo "      cause is no emulator: set C900_EMU, or clone" >&2; \
			echo "      https://github.com/MichalPleban/commodore-900-emulator)." >&2; \
			echo "      crt0.o + libc-z8001.a are present and this build continues" >&2; \
			echo "      UNTESTED." >&2; \
		else \
			echo "libc: build-libc-z8001.sh exited $$st and produced no fresh" >&2; \
			echo "      libc-z8001.a/crt0.o in $(LIBCOUT) -- this is a build failure," >&2; \
			echo "      not a missing test." >&2; \
			rm -f $@.try; exit $$st; \
		fi; \
	 fi; \
	 rm -f $@.try; touch $@
endif

# --- packages: the file groups this repository exports -------------------------
# A component is a dist/lists/*.list carrying a `package' line, and each one
# exports up to four archives: its runtime files, the complete corresponding
# source for every program in them, the manual pages for those programs, and its
# compile-time interface.  This is where they are cut, because this is where the
# facts they need are: which binaries were built, which sources each was
# compiled from (hostbuild/build/.ulsrcmap) and which manual pages this tree
# tracks for them.
#
#   make packages        cut every component-kind that can be cut, and
#                        collect the release's licence texts beside them
#   make package PKG=net cut one -- `net', `net-src', `net-man', `net-dev'
#   make package-list    the components, their kinds, and their gaps
#
# The archives land in $(PKGOUT), and the licence texts they are under land in
# $(B)/licences, once each, beside them rather than inside each archive: a
# source package already carries every program's own COPYING next to the code it
# governs.  The distribution repository consumes both from there.  Their format is declared in dist/packages/component-{bin,src,man,dev}
# .pkg and judged by commodore-900-dist's os/dist/check-package.sh.
PKGOUT ?= $(B)/packages
COMPONENT = $(PYTHON) $(OS)/hostbuild/component.py

.PHONY: packages package package-list
package:
	@test -n "$(PKG)" || { echo "usage: make package PKG=<component>[-src|-man|-dev]"; exit 2; }
	sh $(OS)/dist/pack-component.sh -o $(PKGOUT) $(PKG)

# Every kind that CAN be cut.  A component with no manual page, no mapped source
# or no compile-time interface refuses, by name and with the reason; that is the
# state of the tree and not a build failure, so the refusal is reported and the
# sweep goes on.  What would be a failure is a kind that could be cut and was
# not, which is why the counts are printed at the end rather than an exit status
# being the whole of the answer.
packages:
	@mkdir -p $(PKGOUT); ok=0; skip=0; 	$(COMPONENT) components | awk '$$2 ~ /^bin/ {print $$1, $$2}' | 	while read -r n kinds; do 		for k in $$(echo "$$kinds" | tr ',' ' '); do 			if sh $(OS)/dist/pack-component.sh -o $(PKGOUT) "$$n" "$$k" 			   2>$(PKGOUT)/.why; then ok=$$((ok+1)); 			else skip=$$((skip+1)); 			   echo "-- $$n-$$k not cut: $$(head -2 $(PKGOUT)/.why | tr '\n' ' ')"; 			fi; 		done; 		echo "$$ok $$skip" > $(PKGOUT)/.count; 	done; rm -f $(PKGOUT)/.why; 	read -r ok skip < $(PKGOUT)/.count; rm -f $(PKGOUT)/.count; 	echo "== packages: $$ok cut, $$skip refused, in $(PKGOUT)"; 	ls -1 $(PKGOUT)/*.tar.gz 2>/dev/null | wc -l | 	    xargs -I{} echo "== {} archive(s) present"
	@$(PYTHON) $(OS)/dist/licences.py --collect $(B)/licences

package-list:
	@$(COMPONENT) components

# --- verification -------------------------------------------------------------
# Image boot tests live in the distribution repository.
# What follows are host-only checks that need no image, no emulator, no simulator.

# The net tests' own negative controls: build each with the host cc against
# stand-ins that can be told to misbehave, and check that every test fails
# against the defect it exists to catch as well as passing against a working
# system.  Host-only and fast; needs no image, emulator or simulator.
.PHONY: net-hostcheck
net-hostcheck:
	$(MAKE) -C $(HERE)/../net/test/hostcheck check

# The two names one curses header answers to.  base/lib/libcurses/curses.h is
# what libcurses.a is compiled against; the TOOLCHAIN's src/include/curses.h is
# what a program reaches without -I on that directory.  Both share the guard
# `# ifndef WINDOW', so whichever is read first silences the other, and a
# program built against the wrong one gets a different WINDOW layout and still
# links.  build-curses.sh enforces this on every archive build against the same
# pair; the target asks the question on its own.  It used to name
# $(OS)/include/curses.h, which no build ever created -- so the gate could only
# ever fail, and nothing that fails unconditionally is a gate.
.PHONY: check-curses-header
check-curses-header:
	@cmp $(OS)/base/lib/libcurses/curses.h $(C900_TOOLCHAIN)/src/include/curses.h \
	  && echo "curses.h: the toolchain's and base/lib/libcurses/ agree"

# The package FORMAT gate (check-package.sh) and the image gate (check-image.py)
# judge a packed artifact against the format commodore-900-dist owns; they
# live there.  `sh os/dist/check-package.sh -d <this tree>/dist/packages
# <archive>' judges what `make packages' cuts.

clean-stage:
	rm -rf $(B)/stage $(B)/mk

# The working copies the harnesses boot instead of the artifacts themselves
# (see workimg.sh).  One per image and port, so they are bounded; this is for
# reclaiming space, not correctness.
clean-work:
	rm -rf $(B)/work
