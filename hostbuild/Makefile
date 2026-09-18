# Makefile -- build entry point for the userland host build.
#
#   make userland              the full cmd/games/libs sweep
#   make toolchain             the Z8001 cross toolchain (a separate repository)
#   make libc                  libc-z8001.a + crt0.o, built from toolchain sources
#   make man                   /usr/man, from the pages every component tracks
#   make packages              export every component as bin/src/man archives
#   make publish               build the sweep and stamp it, for the dist repo
#   make check-publish-fresh   assert publish ran after everything else, not eyeball it
#   make clean-work            drop the harnesses' working copies
#   make clean                 remove everything the build writes
#
# This repository builds programs; it does not link a kernel, and the only
# image it packs is the test image its booted checks run on (test/image).  The
# kernel and the distribution images live in separate repositories
# (commodore-900-coh-kernel3 and commodore-900-dist).
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
# toolchain.mk defines TC and exports COHERENT_OS.  Set INFO_GOALS before
# including it so queries can run without a toolchain.
INFO_GOALS = help clean
C900_TC_OPTIONAL := $(if $(filter $(INFO_GOALS),$(MAKECMDGOALS)),1,)
include $(HERE)/toolchain.mk
# B is fixed here: the recipes compute "$HERE/build/..." internally.
# ld-z8001 cannot create files whose paths exceed ~96 characters.
B	:= $(HERE)/build
# Append compiled source paths across builds.  Remove the log to reset it.
C900_BUILD_LOG := $(B)/compiled-sources.log
export C900_BUILD_LOG
# Record which program each source contributed to.  publish-userland.sh
# folds this append-only log into build/.ulsrcmap.
C900_BUILD_MAP := $(B)/compiled-programs.log
export C900_BUILD_MAP
# Refuse missing source directories before find can return an empty list.
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
# Ask MGR for its client names.  Clear MAKE_RESTARTS so recursive make
# does not add directory messages to the result.
MGRCLIENTS := $(shell MAKE_RESTARTS= $(MAKE) -s --no-print-directory -C $(OS)/mgr -f Makefile.c900 echo-clients 2>/dev/null)
MGRCLBIN = $(MGRCLIENTS:%=$(B)/mgr/bin/cl/%)

.PHONY: FORCE all userland net net-hostcheck hunt netcmds curses screen tests man toolchain libc \
	archive comms mgr hr publish check-publish-fresh clean clean-stage clean-work help

# Build the programs, libraries and manual.
SWEEP = userland net hunt netcmds screen archive comms tests mgr hr man

# Publish only after the sweep completes; .ulstamp identifies its output.
all: publish

publish: $(SWEEP)
	sh $(HERE)/publish-userland.sh

# Check that publication followed every build output.  Exclude .tcstamp
# and .tcid, which toolchain.mk maintains while parsing this Makefile -- the
# gate's own invocation rewrites them, so they cannot be evidence about it.
# Exclude the two directories `packages' owns as well: they are derived FROM
# the published tree rather than inputs to it, so publish has nothing to say
# about them and `all' followed by `packages' is the order packages requires.
.PHONY: check-publish-fresh
check-publish-fresh:
	@test -f "$(B)/.ulstamp" || \
	    { echo "check-publish-fresh: no $(B)/.ulstamp -- publish never ran"; exit 1; }
	@newer=$$(find "$(B)" -type f ! -name .ulstamp ! -name .tcstamp ! -name .tcid \
	    ! -path "$(PKGOUT)/*" ! -path "$(B)/licences/*" -newer "$(B)/.ulstamp" | head -1); \
	if [ -n "$$newer" ]; then \
	    echo "check-publish-fresh: $$newer is newer than $(B)/.ulstamp"; \
	    echo "check-publish-fresh: publish did not run after this build finished"; \
	    exit 1; \
	fi
	@echo "check-publish-fresh: ok -- $(B)/.ulstamp is not older than anything else under $(B)"
help:
	@sed -n '2,19p' $(HERE)/Makefile

FORCE:

# --- the parts ----------------------------------------------------------------
userland: $(ULSTAMP)
# Inputs compiled by the userland sweep, including yacc grammars.
# Toolchain headers are covered by TCSTAMP.  Kernel headers are not tracked
# here; remove ULSTAMP after changing them to force the sweep.
ULDIRS = $(OS)/base/cmd $(OS)/base/lib $(OS)/editors $(OS)/archive/gzip \
	 $(OS)/games
$(call dirs-exist,ULDIRS,$(ULDIRS))
ULSRC := $(shell find $(ULDIRS) \
	   \( -name '*.c' -o -name '*.h' -o -name '*.s' -o -name '*.y' \))
# Game data and the curses selection list also affect the staged tree, and
# adventure's tables and message file are generated from glorkz by the host
# generator in games/bsd/adventure/host.
ULSRC += $(wildcard $(OS)/games/bsd/CURSES.list) \
	 $(shell find $(OS)/games/lib -type f 2>/dev/null) \
	 $(wildcard $(OS)/games/bsd/adventure/glorkz \
		    $(OS)/games/bsd/adventure/host/*)
# Where build-hunt.sh finds libcurses/libterm: the OS's own archives, staged
# in the toolchain's build directory beside libc-z8001.a, where ccz looks.
CURSESDIR := $(TCB)/curses

# Each sweep script controls compiler and linker options, so changes to
# any script must invalidate the sweep stamp.
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
# Grouped, like the curses archives: this step produces libsocket.a as well as
# the stamp, so a consumer that links the archive can name the archive.
$(NETSTAMP) $(OS)/net/libsocket.a &: \
		$(TCSTAMP) $(LIBCSTAMP) $(HERE)/build-net.sh $(NETSRC)
	sh $(HERE)/build-net.sh
	@mkdir -p $(dir $(NETSTAMP)) && touch $(NETSTAMP)

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

# Directory-based network clients and the mail component's SMTP programs.
# They link libsocket.a.
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

# Build MGR and its clients through their own Makefile.  Keep BUILD short
# for ld-z8001's path limit.  maze links net/libsocket.a.
mgr: net $(MGRBIN) $(MGRCLBIN)
# Group the outputs to run one sub-make.  Record Makefile.c900 as their
# build recipe in the source map.
$(MGRBIN) $(MGRCLBIN) &: FORCE
	C900_BUILD_RECIPE=$(OS)/mgr/Makefile.c900 \
	$(MAKE) -C $(OS)/mgr -f Makefile.c900 BUILD=$(B)/mgr mgr hrlight clients

# Build the HR driver and GUI through the component Makefile.  The driver
# uses kernel headers and binds to the exported kernel image with ld -k.
# The sub-make checks freshness and records each program's source map.
hr: $(LIBCSTAMP) FORCE
	C900_BUILD_RECIPE=$(OS)/hr/Makefile \
	$(MAKE) -C $(OS)/hr BUILD=$(B) $(B)/root/drv/hr hrgui
	@mkdir -p $(B)/drv
	cp $(B)/root/drv/hr $(B)/drv/hr
	chmod 755 $(B)/drv/hr

man: $(MAN)
# Assemble the manual from each component's tracked pages.
MANSRC := $(shell find $(OS)/man $(addsuffix /man,$(addprefix $(OS)/,base net \
	editors mail archive comms games hr mgr test)) \
	-type f 2>/dev/null)
$(MAN): $(HERE)/extract-man.sh $(MANSRC)
	sh $(HERE)/extract-man.sh

toolchain: $(TCSTAMP) $(LIBCSTAMP)

# A toolchain release supplies compiled passes and libc.  Depend on the
# artifacts so an incomplete archive cannot satisfy either stamp.
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

# Build libc and crt0 from the toolchain's sources.
libc: $(LIBCSTAMP)
# libc and startup sources belong to the toolchain; TCSTAMP covers headers.
LIBCDIRS = $(C900_TOOLCHAIN)/src/libc $(C900_TOOLCHAIN)/src/csu
$(call dirs-exist,LIBCDIRS,$(LIBCDIRS))
LIBCSRC := $(shell find $(LIBCDIRS) \
	     \( -name '*.c' -o -name '*.s' -o -name '*.h' \))
# The libc script also runs smoke tests.  If it fails after producing both
# fresh artifacts, report the failure and continue with an untested libc.
# The .try timestamp prevents accepting output from an earlier build.
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

# Packages: runtime files, corresponding source, manual and development files.
#
#   make packages        pack all available component kinds
#   make package PKG=net pack one component kind
#   make package-list    list components and available kinds
#
# Archives go to PKGOUT; licence texts go to B/licences.
# The formats are declared in dist/packages/component-*.pkg.
# Each cut starts from an empty PKGOUT: pack-component.sh only ever adds
# archives there, so one left from an earlier or dirty build would sit beside
# the new cut and be just as collectible.
PKGOUT ?= $(B)/packages
COMPONENT = $(PYTHON) $(OS)/hostbuild/component.py

.PHONY: packages package package-list
package:
	@test -n "$(PKG)" || { echo "usage: make package PKG=<component>[-src|-man|-dev]"; exit 2; }
	sh $(OS)/dist/pack-component.sh -o $(PKGOUT) $(PKG)

# Attempt each available component kind and report successes and refusals.
# A refusal is a component that publishes nothing, so the sweep fails on one:
# an install set short of a component is not an install set.
packages:
	@rm -rf $(PKGOUT); mkdir -p $(PKGOUT); ok=0; skip=0; \
	$(COMPONENT) components | awk '$$2 ~ /^bin/ {print $$1, $$2}' | \
	while read -r n kinds; do \
		for k in $$(echo "$$kinds" | tr ',' ' '); do \
			if sh $(OS)/dist/pack-component.sh -o $(PKGOUT) "$$n" "$$k" \
			   2>$(PKGOUT)/.why; then ok=$$((ok+1)); \
			else skip=$$((skip+1)); \
			   echo "-- $$n-$$k not cut: $$(head -2 $(PKGOUT)/.why | tr '\n' ' ')"; \
			fi; \
		done; \
		echo "$$ok $$skip" > $(PKGOUT)/.count; \
	done; rm -f $(PKGOUT)/.why; \
	test -s $(PKGOUT)/.count || { \
	    echo "== packages: no component kind was attempted" >&2; exit 1; }; \
	read -r ok skip < $(PKGOUT)/.count; rm -f $(PKGOUT)/.count; \
	echo "== packages: $$ok cut, $$skip refused, in $(PKGOUT)"; \
	ls -1 $(PKGOUT)/*.tar.gz 2>/dev/null | wc -l | \
	    xargs -I{} echo "== {} archive(s) present"; \
	test "$$skip" -eq 0 || { \
	    echo "== packages: $$skip component-kind(s) published nothing" >&2; exit 1; }
	@$(PYTHON) $(OS)/dist/licences.py --collect $(B)/licences

package-list:
	@$(COMPONENT) components

# --- verification -------------------------------------------------------------
# Image boot tests live in the distribution repository.
# What follows are host-only checks that need no image, no emulator, no simulator.

# Compile network probes with host stubs and injected faults to check both
# successful operation and detection of the faults they test.
.PHONY: net-hostcheck
net-hostcheck:
	$(MAKE) -C $(HERE)/../net/test/hostcheck check

# The package FORMAT gate (check-package.sh) and the image gate (check-image.py)
# judge a packed artifact against the format commodore-900-dist owns; they
# live there.  `sh os/dist/check-package.sh -d <this tree>/dist/packages
# <archive>' judges what `make packages' cuts.

clean-stage:
	rm -rf $(B)/stage $(B)/mk

# Remove the harnesses' working image copies to reclaim disk space.
clean-work:
	rm -rf $(B)/work

# Remove everything the build writes: the build and log directories, the
# Python cache the packer leaves, the generated ckcpro.c and adventure's
# tables.c, and the objects and
# programs net, hunt and the tests build beside their sources, through each
# directory's own `clean'.  A test directory without a Makefile holds one
# program, named for the directory, which build-tests.sh links in place.
# test/hostcheck is the host-side mutation gate, which build-tests.sh does not
# build.  The sub-makes that include toolchain.mk get C900_TC_OPTIONAL, since
# removing files compiles nothing.  Libraries this build writes into the
# toolchain's build directory belong to that repository's `clean'.
clean:
	$(MAKE) -C $(OS)/net clean
	$(MAKE) -C $(OS)/net/test/hostcheck clean
	$(MAKE) -C $(OS)/games/net/hunt clean
	@for d in $(OS)/test/*/; do n=$$(basename $$d); \
		if [ "$$n" = hostcheck ]; then continue; fi; \
		if [ -f $${d}Makefile ]; then \
			$(MAKE) -C $$d C900_TC_OPTIONAL=1 clean || exit 1; \
		else \
			echo "rm -f $$d$$n $$d.$$n.new"; rm -f $$d$$n $$d.$$n.new; \
		fi; \
	done
	rm -f $(OS)/comms/ckermit/ckcpro.c $(OS)/games/bsd/adventure/tables.c
	rm -rf $(B) $(HERE)/logs $(HERE)/__pycache__
