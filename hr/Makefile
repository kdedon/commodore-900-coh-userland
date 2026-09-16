# The hi-res window system for the Commodore 900: the /drv/hr driver, the
# window server and its clients, the GUI launcher and the Vellum drawing
# suite, with the headers and libraries they link against.
#
# ../hostbuild/Makefile is this repository's entry point and stages the userland;
# it reaches this file for the hr targets, which are kernel-adjacent code that
# the flat command sweep does not build.  Output is staged under the BUILD
# directory it passes in; intermediate files use build/obj.

# ../mk/compiler.mk is included before any rule below and defines its own targets,
# so the default goal must be named explicitly or a bare `make' builds nothing.
.DEFAULT_GOAL := all

# ---------------------------------------------------------------------------
# Toolchain -- selected in ../mk/compiler.mk, not hard-coded; `make compiler-info'
# says what the selection resolved to.  The compiler is the one this project
# builds from source, in commodore-900-toolchain.
# ---------------------------------------------------------------------------
# ../hostbuild/mkfont.py renders the raw FM fonts; overridable for a machine whose
# python3 is elsewhere.
PYTHON ?= python3

C900_CC_DEFAULT = mwc
include ../mk/compiler.mk
CC     = $(C900_CC)
AS     = $(C900_AS)
LD     = $(C900_LD)
CPP    = $(C900_CPP)
AR     = $(C900_AR)
RANLIB = $(C900_RANLIB)
YACC   = $(C900_YACC)

# A missing compiler is diagnosed at the first compile, not while parsing:
# targets that touch no compiler (e.g. `dist') must still run on a machine
# that cannot rebuild the sources.
ifneq (,$(C900_CC_WHY))
CC     = @echo "COMPILER=$(COMPILER): $(C900_CC_WHY)" >&2; false
AS     = $(CC)
LD     = $(CC)
CPP    = $(CC)
endif

# Dialect flags come from the compiler flavour, not from here: ../mk/compiler.mk
# sets C900_CFLAGS per flavour, and its one flavour, mwc, wants none.  There is no
# tree-wide include directory: the system headers come from the toolchain,
# which ccz puts on every compile, and a header this tree defines is reached
# from the component that defines it.
CFLAGS  = $(C900_CFLAGS)
ASFLAGS = -g

# Archive $^ into a fresh $@ in basename-alphabetical order, then index it.
# The order is load-bearing for libc's optional-float-printf trick: the real
# FP formatter (dtefg.o, defines _dtefg_ and _dtoa_) must precede printf.o /
# scanf.o, and the "No floating point!" stub (sdtoa.o, also defines _dtefg_)
# must follow them, so the single-pass ld picks the stub by default and the
# real routine only when a bare _dtoa_ reference is linked before libc.a.
# C-locale alphabetical order (dtefg < printf < scanf < sdtoa) satisfies this;
# do NOT regroup the FP objects.
#
# LC_ALL=C on the sort is part of that order: a UTF-8 locale collates
# punctuation as if absent, putting _finish.o after exit.o, and the loader
# then refuses finit.o ("symbol _finish_: redefined"), failing every stdio
# link.  The reference libc.a is in C order.
define ar-sorted
	@mkdir -p $(dir $@)
	@rm -f $@
	@# tr -d '\r': on a Windows PATH `sort'/`cut' may write CRLF, and a stray
	@# \r would make ar try to open "name.o\r".
	@objs=`for o in $^; do echo "$$(basename $$o)|$$o"; done | LC_ALL=C sort | cut -d'|' -f2 | tr -d '\r'`; \
	 $(AR) cr $@ $$objs
	$(RANLIB) $@
endef

# Archive $^ into a fresh $@ in the order GIVEN (the order the recipe's
# prerequisite list names them), for libhrgfx.a -- ZView's rendering engine.
#
# ar-sorted's C-locale resort exists only for libc's optional-float-printf
# trick (see above); this archive has no such duplicate-symbol tie-break, so
# there is nothing for a resort to buy.  It would also be
# pointless twice over: our nld (src/ld/pass1.c rdfile()) does not require
# a single forward pass over an archive -- ranlib index or not, it loops
# ("do { ... } while (nundef&&found)") re-scanning the archive until a pass
# pulls in nothing new, so a member may reference one that comes after it.
#
# A plain quick-append in prerequisite order, no sort, no ranlib pass (ranlib
# itself has to fork ar to build its index, which the emulator runner cannot do
# -- see C900_RANLIB := true above).  $(RANLIB) is kept: a real no-op under
# every current flavour, cheap insurance if that ever changes.
define ar-kernel
	@mkdir -p $(dir $@)
	@rm -f $@
	$(AR) cr $@ $^
	$(RANLIB) $@
endef

# buildmap: $(1) = the inputs of the invocation that writes $@.
#
# THE BUILD RECORD.  $C900_BUILD_MAP (commodore-900-toolchain host/buildlog.sh,
# c900_buildmap) is where a compile or link says what it produced and from what,
# and ../hostbuild/ulsrcmap.py folds it into the source map every -src package
# is cut from.  ccz writes the record for every compile it runs; the links here
# and the driver's three-pass compile run nld and the passes directly, so they
# write their own rows, in the same four columns: an `o' row naming the output
# and the recipe ($C900_BUILD_RECIPE, this Makefile), one `i' row per input.
# Paths are written as this Makefile spells them, with $PWD beside them, which
# is how the reader joins a link's object to the compile that made it.  Unset,
# nothing is written and nothing about the build differs.
define buildmap
	@if [ -n "$$C900_BUILD_MAP" ]; then \
	  printf 'o\t%s\t%s\t%s\n' "$$PWD" '$@' "$${C900_BUILD_RECIPE:-}" >>"$$C900_BUILD_MAP"; \
	  for i in $(1); do \
	    printf 'i\t%s\t%s\t%s\n' "$$PWD" '$@' "$$i" >>"$$C900_BUILD_MAP"; \
	  done; \
	fi
endef

# ld-checked: $(1) = the WHOLE nld argument list, output file included.
#
# Every link goes through here, userland and kernel alike.  nld reports a
# duplicate symbol ("symbol s_: redefined") on stderr but EXITS 0, silently
# dropping one definition; treat that as a failure and remove the half-written
# output so a later make cannot find it newer than its inputs.  Other nld
# warnings are printed but do not stop the build.
#
# The link is recorded (buildmap) before it runs and not on its succeeding: a
# failed link leaves the previous binary in place, and a record of what was
# attempted is the truth about what that binary is for.  The inputs are the
# argument list less the switches, the output itself, and the startup object and
# C library: those are built from the toolchain repository's src/csu and
# src/libc, which the source map names for every program unconditionally, the
# same way ccz puts them on every link without recording them.
define ld-checked
	@mkdir -p $(dir $@)
	$(call buildmap,$(filter-out -% $@ $(CRT) $(LIBC),$(1)))
	@echo '$(LD) $(1)'
	@out=`$(LD) $(1) 2>&1`; st=$$?; \
	 if [ -n "$$out" ]; then echo "$$out" >&2; fi; \
	 if [ $$st -ne 0 ]; then rm -f $@; exit $$st; fi; \
	 case "$$out" in *redefined*) \
	   echo "link $@: nld reported a redefinition and exited 0 -- failing the build" >&2; \
	   rm -f $@; exit 1;; esac
endef

# ---------------------------------------------------------------------------
# Layout
# ---------------------------------------------------------------------------
# The compile root is this directory: an object mirrors the path of the source
# it is compiled from, so every component reaches $(OBJ) by the one tree-wide
# rule below and two components may each hold a command of the same name
# without their objects colliding.
STOCK  = ../stock

# base: the component that owns the libraries and the commands both systems
# share.  Its libraries carry their own public headers, so the compile lines
# name the library directory rather than a tree-wide include path.
BASE     = ../base
EXTCMDS  = $(BASE)/cmd
LIBFSSRC = $(BASE)/lib/libfs
LIBMPSRC = $(BASE)/lib/libmp
LIBYSRC  = $(BASE)/lib/liby

# hr: the window system.  Named here and not only in its own section below,
# because /usr/include is staged out of its header directory too.
HRSRC  = .
HRHDR  = $(HRSRC)/include

BUILD  = build
OBJ    = $(BUILD)/obj
ROOT   = $(BUILD)/root
LIBDIR = $(ROOT)/lib
INCDIR = $(ROOT)/usr/include

# Command install locations inside the staging image.  The split between
# /bin, /usr/bin, /etc and /usr/lib is taken from the original Coherent
# filesystem listing (recovered; the cmd/* makefiles do not encode it).
BINDIR    = $(ROOT)/bin
ETCDIR    = $(ROOT)/etc
USRBINDIR = $(ROOT)/usr/bin
USRLIBDIR = $(ROOT)/usr/lib

# The two command homes, and the object directory each mirrors into.
CMDS      = $(STOCK)/cmd
CMDOBJ    = $(OBJ)/$(CMDS)
EXTCMDOBJ = $(OBJ)/$(EXTCMDS)

# ---------------------------------------------------------------------------
# The release string
# ---------------------------------------------------------------------------
# The version is the git tag, read through the same hostbuild/version.sh that
# names a package, so a binary and the archive it travels in cannot disagree.
# One program is compiled against it: zlogin, whose window title states the
# release it was built from.
#
# The shell's $VERSION is NOT this.  It is the release the RUNNING kernel
# reports, asked at start-up in ../base/cmd/sh/var.c, because a version
# compiled into a program names that program's build and not the system it
# ends up on.
VERSION := $(strip $(shell sh ../hostbuild/version.sh))

# ---------------------------------------------------------------------------
# Installed headers
# ---------------------------------------------------------------------------
# /usr/include is staged additively: each producer contributes the headers for
# what it ships.  The toolchain contributes the ones its C library, maths
# library and object formats are compiled against; this tree contributes the
# ones its own libraries and commands define.  A name is owned by exactly one
# of them, so the two sets are disjoint and the copy rules cannot race.
TCINC      := $(C900_TOOLCHAIN)/src/include
TCHDRS     := $(wildcard $(TCINC)/*.h $(TCINC)/sys/*.h $(TCINC)/netinet/*.h)
# This tree's own four: each lives with the code that defines it -- the
# file-system access layer's public header with the library, the check tools'
# exit-status bits with the check tools, the video and I/O-port definitions
# with the window system -- and each is staged flat into /usr/include, which
# is where a program compiled ON the machine finds it.
ULHDRS     := $(LIBFSSRC)/fs.h $(EXTCMDS)/check.h $(HRHDR)/ioport.h \
	      $(HRHDR)/rico.h
HDRS       := $(ULHDRS) $(TCHDRS)
INC_TARGET := $(addprefix $(INCDIR)/,$(notdir $(ULHDRS))) \
	      $(patsubst $(TCINC)/%,$(INCDIR)/%,$(TCHDRS))

# $(TCSYSFLAT): the toolchain sys/ headers that are ALSO reachable by a flat
# name -- <stat.h> and <sys/stat.h> are one header, and this tree's own sources
# say the flat one (18 of them `#include <stat.h>', 11 <dir.h>, 8 <const.h>).
#
# The flat name is not a convenience.  The target compiler has exactly ONE
# default include directory: the toolchain's src/cc/h/z8001/cppmch.h sets
# DEFDISK to "/usr/include" and src/cc/n0/cc0.c hands cppi() that single path.
# There is no implicit sys/ search, so on the machine a source that says
# <stat.h> resolves only if /usr/include/stat.h is there -- which is what lets
# the image compile its own sources with no -I at all.
#
# 17 of the 71 sys/ headers get an alias, so this is a list and not a pattern
# over the wildcard.  Both names are staged as copies of the one source and the
# image packs them to a single inode: the flat name is the file and
# sys/<name>.h the link for the first fourteen, and the
# other way round for times.h, tty.h and types.h.  Both names are the one
# source staged twice.
TCSYSFLAT  := chars const dir fblk filsys ino inode machine mdata msig param \
	      stat timeb uproc \
	      times tty types
INC_FLAT   := $(addprefix $(INCDIR)/,$(addsuffix .h,$(TCSYSFLAT)))
INC_TARGET += $(INC_FLAT)

# ---------------------------------------------------------------------------
# Header dependencies
# ---------------------------------------------------------------------------
# The compile rules see one .c at a time and the compiler emits no dependency
# information, so nothing here can know which headers a source pulled in.  Each
# subsystem therefore declares its header set ($(HDRS) here, $(KHDRS), $(HRHDRS),
# $(HRGUIHDRS) below) and EVERY object of that subsystem depends on ALL of it.
# This coarse approach catches stale object bugs at the cost of rebuilding more
# than strictly necessary when a header changes.

# ---------------------------------------------------------------------------
# libc - compile every .c; assemble a .s only when there is no matching .c
# (the paired .s files are stale output of the original compiler; the .s-only
# files are the hand-written primitives: syscall stubs, strcmp, setjmp, ...).
# ---------------------------------------------------------------------------
# crt0.o, dtoa.o, libc-z8001.a and libm-z8001.a are BUILT BY THE TOOLCHAIN, from
# its own {csu,libc,libm} with the compiler that emits the calls into them.
# This tree stages them; it does not compile them.
TCLIBC = $(C900_TCB)/libc-z8001
TCLIBM = $(C900_TCB)/libm-z8001

# ld: the target-native linker.  Its source (src/ld) is the toolchain
# repository's -- it is what the toolchain's own host cross build links
# kernels and images WITH, so keeping a second copy here would duplicate it.
# The toolchain cross-builds it to
# run ON the Z8001 (host/build-native.sh: src/ld/all.c, unity build) for the
# same reason it cross-builds a native `as' and `cc' -- so this image can
# self-host.  This tree stages the result; it does not compile it.
TCNATIVE = $(C900_TCB)/native

# libmp / liby / libfs - all .c in the directory (per their run/compile scripts).
# The toolchain does not build them, and nothing links them but this userland.
# They live in $(BASE), with their consumers: bc and dc link libmp, find and
# test link liby's yacc runtime, and icheck/dcheck/ncheck link libfs.  Both
# systems build them from that one home.
libmp_c  := $(wildcard $(LIBMPSRC)/*.c)
liby_c   := $(wildcard $(LIBYSRC)/*.c)
libfs_c  := $(wildcard $(LIBFSSRC)/*.c)
LIBMP_OBJ := $(patsubst %.c,$(OBJ)/%.o,$(libmp_c))
LIBY_OBJ  := $(patsubst %.c,$(OBJ)/%.o,$(liby_c))
LIBFS_OBJ := $(patsubst %.c,$(OBJ)/%.o,$(libfs_c))

# ---------------------------------------------------------------------------
# Top-level targets
# ---------------------------------------------------------------------------
CRT   = $(LIBDIR)/crts0.o
DTOA  = $(LIBDIR)/dtoa.o
LIBC  = $(LIBDIR)/libc.a
LIBM  = $(LIBDIR)/libm.a
# the reference image keeps libmp.a in /usr/lib, not /lib
LIBMP = $(USRLIBDIR)/libmp.a
LIBY  = $(LIBDIR)/liby.a
# libfs: shared file-system access for the check tools (icheck/dcheck/ncheck).
LIBFS = $(LIBDIR)/libfs.a

LIBS = $(CRT) $(DTOA) $(LIBC) $(LIBM) $(LIBMP) $(LIBY) $(LIBFS)

.PHONY: all headers libs kernel dist hrgui vellum help
help:
	@printf '%s\n' \
	  'make                 build the headers, libraries and the hi-res system' \
	  'make headers         stage system headers' \
	  'make libs            build and stage libraries' \
	  'make hrgui           the hi-res driver, the desktop and its applications' \
	  'make vellum          the drawing suite on its own' \
	  'make compiler-info   show the selected compiler'

# `all' is what this file builds: the hi-res window system, and the headers and
# libraries it links against.  The COMMAND SWEEP is hostbuild/Makefile's -- it
# is what stages a userland -- and this file is reached from there for the hr
# targets alone.  `kernel' and `dist' refuse by name, because the kernel
# belongs to commodore-900-coh-kernel3 and image-packing to
# commodore-900-dist, which is what those targets are for.
all: headers libs hrgui
headers: $(INC_TARGET)
libs: $(LIBS)

# ---------------------------------------------------------------------------
# Rules
# ---------------------------------------------------------------------------
# This tree's own headers, each named where it lives.  Static pattern rules
# over the four names, one per home directory, so a header staged from the
# wrong tree cannot match by accident.
$(INCDIR)/fs.h: $(LIBFSSRC)/fs.h
	@mkdir -p $(dir $@)
	cp $< $@

$(INCDIR)/check.h: $(EXTCMDS)/check.h
	@mkdir -p $(dir $@)
	cp $< $@

$(INCDIR)/ioport.h $(INCDIR)/rico.h: $(INCDIR)/%.h: $(HRHDR)/%.h
	@mkdir -p $(dir $@)
	cp $< $@

$(INCDIR)/%.h: $(TCINC)/%.h
	@mkdir -p $(dir $@)
	cp $< $@

# The flat aliases of $(TCSYSFLAT).  A static pattern rule, so it beats the
# implicit one above -- which would look for a flat $(TCINC)/<name>.h that the
# toolchain's header set does not have.
$(INC_FLAT): $(INCDIR)/%.h: $(TCINC)/sys/%.h
	@mkdir -p $(dir $@)
	cp $< $@

# The tree-wide compile rules.  Every component -- $(BASE), $(STOCK), the
# games, and any other -- is reached by the same rule, and the object tree
# mirrors the source tree name for name.
$(OBJ)/%.o: %.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ)/%.o: %.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ $<

# Each library $(BASE) owns carries its public header in its own directory, so
# that directory goes on the include path and no tree-wide one is needed.
$(LIBMP_OBJ): CFLAGS += -I$(LIBMPSRC)
$(LIBY_OBJ):  CFLAGS += -I$(LIBYSRC)
$(LIBFS_OBJ): CFLAGS += -I$(LIBFSSRC)

# Generated sources (e.g. a yacc y.tab.c) live in the obj tree, not beside the source.
$(OBJ)/%.o: $(OBJ)/%.c $(HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# crt0, libc and libm come from the toolchain, which compiles them from its own
# {csu,libc,libm} with the compiler that emits the calls into them.  They
# are staged into $(LIBDIR) under the names the link rules and the image expect.
$(LIBDIR)/crts0.o: $(TCLIBC)/crt0.o
	@mkdir -p $(dir $@)
	cp $< $@

$(LIBDIR)/dtoa.o: $(TCLIBC)/dtoa.o
	@mkdir -p $(dir $@)
	cp $< $@

$(LIBDIR)/libc.a: $(TCLIBC)/libc-z8001.a
	@mkdir -p $(dir $@)
	cp $< $@

$(LIBDIR)/libm.a: $(TCLIBM)/libm-z8001.a
	@mkdir -p $(dir $@)
	cp $< $@

# ld is staged the same way, from the toolchain's native build; see TCNATIVE
# above for why it is not compiled here.
$(BINDIR)/ld: $(TCNATIVE)/ld
	@mkdir -p $(dir $@)
	cp $< $@
	chmod 755 $@

$(USRLIBDIR)/libmp.a: $(LIBMP_OBJ)
	$(ar-sorted)

$(LIBDIR)/liby.a: $(LIBY_OBJ)
	$(ar-sorted)

$(LIBDIR)/libfs.a: $(LIBFS_OBJ)
	$(ar-sorted)

# ===========================================================================
# Kernel artifacts  (commodore-900-coh-kernel3's export -> build/root)
# ===========================================================================
# DISTDIR/DRVDIR/FLOPPYDIR are used below (the ZView `hr' driver and the
# floppy staging tree), so they stay defined here.
DISTDIR   = $(BUILD)/dist
DRVDIR    = $(ROOT)/drv
FLOPPYDIR = $(BUILD)/floppy

# The kernel's header directory, reached over ../mk/deps.sh's `kernel' edge.
#
# $(KINC) is the kernel's userland-facing os/include, which is what supplies
# <sys/timeout.h> and, now, <romconf.h> (the Commodore ROM configuration
# block): kheaders.py ships it from os/include like any other header the
# kernel's own sources use, so this repository needs no second, kernel-SOURCE
# include path of its own to reach it.
C900_KERNEL := $(shell C900_KERNEL= sh ../mk/deps.sh kernel)
ifneq (,$(wildcard $(C900_KERNEL)/os/include))
KINC := $(C900_KERNEL)/os/include
else ifneq (,$(wildcard $(C900_KERNEL)/include))
KINC := $(C900_KERNEL)/include
else
KINC :=
endif

KERNEL_ERR = { echo "kernel: the kernel is built by commodore-900-coh-kernel3."; \
	       echo "  This repository stages what that build exports; it links no"; \
	       echo "  kernel of its own.  Run \`make kernel drivers' there."; \
	       exit 2; }

.PHONY: kernel
kernel:
	@$(KERNEL_ERR)

# $(KEXPORT): the kernel's build products, in the one spelling that answers for
# both shapes the `kernel' edge resolves to.  A checkout holds them under
# os/hostbuild; pack-kernel.sh lays a release out with a hostbuild/ view of its
# own (kobj/kernel.out and build/drv are relative symlinks into boot/ and drv/)
# precisely so that a consumer needs one path and not two.
ifneq (,$(wildcard $(C900_KERNEL)/os/hostbuild))
KEXPORT := $(C900_KERNEL)/os/hostbuild
else
KEXPORT := $(C900_KERNEL)/hostbuild
endif
KDRV      := $(KEXPORT)/build/drv
KIMG      := $(KEXPORT)/kobj/kernel.out
KDRVSTAMP := $(KDRV)/.drvstamp
KDRVCONF  := $(KDRV)/.kdrv.conf

# What the image takes from that export.  /coherent is the symboled kernel --
# ps, mem and mgrload nlist() it at run time, so it is the same file the
# machine is running and not a stripped copy of it.  The three console drivers
# are loadable l.out modules /etc/load installs at boot; the kernel's icode
# names one of them in init's argument vector after the video probe, so all
# three ship whatever board is fitted.  /drv/hostfs is in the export too and is
# deliberately not staged: it is a development driver and the manifest, which
# is authoritative for what ships, does not list it.
KERNEL_DRV   := $(addprefix $(DRVDIR)/,hrtty lrtty notty)
KERNEL_STAGE := $(KERNEL_DRV) $(ROOT)/coherent

# The refusal has to be the RULE, not a guard inside one: make resolves
# prerequisites before it runs any
# recipe, so a rule whose source directory is absent dies with "No rule to make
# target" and an order-only guard never runs.  Selecting the whole rule on
# whether the export is there makes the explaining branch reachable exactly
# when the real one cannot work.
#
# The test is the kernel image AND the driver pairing stamp: build-drivers.sh
# writes that stamp only when every driver linked, so a half-built build/drv
# reads as no export at all rather than as three files, one of which is stale.
ifeq (,$(if $(wildcard $(KIMG)),$(wildcard $(KDRVSTAMP))))

$(KERNEL_STAGE):
	@echo "$@ is built by commodore-900-coh-kernel3 and staged from its export."
	@echo "  Wanted $(KIMG)"
	@echo "     and $(KDRVSTAMP)"
	@echo "  C900_KERNEL = $(if $(C900_KERNEL),$(C900_KERNEL),<unset: run \`sh ../mk/deps.sh -n kernel' for the paths tried>)"
	@echo "  Run \`make kernel drivers' in that repository, or \`make deps DEP=kernel' at this repository's root."
	@exit 2

else

$(KERNEL_DRV): $(DRVDIR)/%: $(KDRV)/%
	@mkdir -p $(dir $@)
	cp $< $@
	chmod +x $@

$(ROOT)/coherent: $(KIMG)
	@mkdir -p $(dir $@)
	cp $< $@

endif

# Staged by `dist': a prerequisite named on the `dist:' line above this point
# would expand to the empty list, because make expands prerequisites when it
# reads the rule.
dist: $(KERNEL_STAGE)

# --- fonts --- the 16 raw FM fonts are content, not output: no tool in any
# repository generates them.  They are the component's own content at
# $(HRFONTDIR) and install straight into the image.  zview requires "sysfont"
# (the system font, == gacha.b.8) at startup, and the three .hf faces below are
# generated from them, so both forms ship.
HRFONTDIR := $(HRSRC)/fonts
HRFONTS := $(patsubst $(HRFONTDIR)/%,$(ROOT)/usr/hr/fonts/%,\
	$(wildcard $(HRFONTDIR)/*))
$(ROOT)/usr/hr/fonts/%: $(HRFONTDIR)/%
	@mkdir -p $(dir $@)
	cp $< $@

# --- kernel driver (hr) --- there is one hr driver and one rule for it, in the
# ZView section below, which builds $(DRVDIR)/hr from $(HRSRC)/drv/.

# ZView - the rebuilt windowing system  (-> build/root).
# Built as part of the normal `all'/`image' build (its outputs are in
# HRGUI_TARGETS, folded into `image' below) -- there is no separate target.
# The rendering engine from the _graphics/hr smgr is built as a standalone
# library libhrgfx.a, with a single blocking-read window server over pipe(2)
# + a shared-VRAM ring on top.  (The `hr' target above builds the original
# stack for reference.)
#
#   gfx/     libhrgfx.a  - the rendering engine (asm blitters, bitblt, layer
#                          clipper, line/point/text/font rasterizers).
#                          globals.o holds the engine's global state and is
#                          linked DIRECTLY by each consumer (it defines only
#                          tentative/common symbols, which Coherent's one-pass
#                          ld will not pull from an archive).
#   test/gfxtest         - standalone draw test (links libhrgfx alone).
HRGUISRC = $(HRSRC)
HRGFXDIR = $(HRGUISRC)/gfx
HRGUIOBJ = $(OBJ)/hrgui
HRGUIBIN = $(ROOT)/usr/hr/bin

# The engine's headers (<smgr.h>, <structs.h>, ...) and the client/server
# contract (<wire.h>, <hrapp.h>, ...) are one set in $(HRHDR): no name occurs
# twice there, and a new client cannot be forgotten in a per-object list and
# silently miss wire.h.  wire.h and hrapp.h matter most -- they define records
# and structs the server and EVERY client must agree on byte for byte, so a
# client object left unrebuilt against a changed wire.h is a live memory bug.
HRGFXCFLAGS = $(C900_CFLAGS) -I$(HRHDR) -I$(HRGFXDIR)

HRGUIHDRS := $(wildcard $(HRHDR)/*.h) $(HDRS)

# ZView object pattern rules (own CFLAGS; the .s use cpp directives, so
# preprocess with cpp -P before as).
$(HRGUIOBJ)/%.o: $(HRGUISRC)/%.c $(HRGUIHDRS)
	@mkdir -p $(dir $@)
	$(CC) $(HRGFXCFLAGS) -c $< -o $@
$(HRGUIOBJ)/%.o: $(HRGUISRC)/%.s $(HRGUIHDRS)
	@mkdir -p $(dir $@)
	$(call buildmap,$<)
	$(CPP) -P -I$(HRHDR) $< $@.i
	$(AS) $(ASFLAGS) -o $@ $@.i

# libhrgfx.a: the engine code (functions).  globals.o is deliberately excluded
# (linked directly by consumers, see above).  Member order is not load-bearing
# here, so use the plain kernel-style archive rule.
HRGFX_ASM := $(addprefix $(HRGUIOBJ)/gfx/,ablt.o small1.o small2.o block1.o block2.o ptrmath.o fcpy.o glftn.o)
HRGFX_C   := $(addprefix $(HRGUIOBJ)/gfx/,bitblt.o lblt.o layer.o masks.o rmath.o gcoord.o gline.o gpoint.o gtext.o gtext2.o f2.o gfxhooks.o)
HRGFX_GLOB := $(HRGUIOBJ)/gfx/globals.o
# libhrgfx.a is a build-time-only artifact (statically linked into the server
# and test); keep it in the obj tree so it is not packed into the disk image.
LIBHRGFX  := $(HRGUIOBJ)/libhrgfx.a

$(LIBHRGFX): $(HRGFX_ASM) $(HRGFX_C)
	$(ar-kernel)

# Standalone draw test: links libhrgfx + globals.o + libc only (no server, no
# IPC), proving the engine has no undefined message symbols.
$(HRGUIBIN)/gfxtest: $(HRGUIOBJ)/test/gfxtest.o $(HRGFX_GLOB) $(LIBHRGFX) $(CRT) $(LIBC)
	@mkdir -p $(dir $@)
	$(call ld-checked,-s -o $@ $(CRT) $(HRGUIOBJ)/test/gfxtest.o $(HRGFX_GLOB) $(LIBHRGFX) $(LIBC))

# zlogin: the graphical greeter on /etc/zlogin.  Same link set as gfxtest --
# libhrgfx + globals.o + libc, no server, no clgfx, no /drv/hr -- because it
# owns the whole screen and reads the console tty directly.  It draws the
# release string in its title, so it takes -D RELEASE like /etc/init does.
$(HRGUIOBJ)/cmd/zlogin/zlogin.o: HRGFXCFLAGS += -D RELEASE=\"$(VERSION)\"
$(ETCDIR)/zlogin: $(HRGUIOBJ)/cmd/zlogin/zlogin.o $(HRGFX_GLOB) $(LIBHRGFX) \
		$(CRT) $(LIBC)
	@mkdir -p $(dir $@)
	$(call ld-checked,-s -o $@ $(CRT) $(HRGUIOBJ)/cmd/zlogin/zlogin.o $(HRGFX_GLOB) $(LIBHRGFX) $(LIBC))

# --- window server + clients ---
# clgfx.o: the client-side direct-render draw library.  Clients link it +
# globals.o + libhrgfx.a, which pulls ONLY bitblt + its asm inner loops
# + rmath + masks + gfxhooks (not the server-only layer/daemon code).
# hrlock.o + hrtas.o: the global drawing lock -- a userland TSET futex fast path
# (hrtas.s) with a kernel slow path (CIOMLOCK/CIOMUNLOCK) taken only on
# contention.  Linked into every client AND the server.
# hrapp.o: client start-up (hr_open) -- an app declares its own window (title,
# size, icon, flags) to the server instead of taking them on the command line.
HRTAS  := $(HRGUIOBJ)/lib/clgfx/hrtas.o
HRLOCK := $(HRGUIOBJ)/lib/clgfx/hrlock.o $(HRTAS)
# hrsel.o: the PRIMARY selection store (inc/shmem.h).  It needs the TSET but NOT
# hrlock.o -- the selection has its own lock word (SHM_SELLOCK) precisely so that
# a copy never takes the global drawing lock and stalls the UI.  Kept apart from
# HRLOCK so a consumer that only wants the selection (hrpump, hrclip) links
# $(HRSEL) $(HRTAS) and no drawing-lock code at all.
HRSEL := $(HRGUIOBJ)/lib/clgfx/hrsel.o
# NOT in CLGFX: a client that never touches the selection (zclock) should not
# carry the store.  Consumers name $(HRSEL) explicitly.
# clrow.o: the asm row-store inner loop clgfx.c calls (cl_ldrow).
CLGFX := $(HRGUIOBJ)/lib/clgfx/clgfx.o $(HRGUIOBJ)/lib/clgfx/clrow.o \
	$(HRGUIOBJ)/lib/clgfx/hrapp.o $(HRLOCK)
# hrdlg.o: the modal-dialog widget kit (inc/hrdlg.h).  Same rule as HRSEL --
# NOT in CLGFX; a client with no dialogs should not carry the widget code.
HRDLG := $(HRGUIOBJ)/lib/clgfx/hrdlg.o
# hrsbar.o: the vertical-scrollbar common control (inc/hrsbar.h) -- a window-
# content sibling of the dialog widgets.  Named explicitly by clients that
# scroll a view.
HRSBAR := $(HRGUIOBJ)/lib/clgfx/hrsbar.o
# hrwl.o: reads the window list the server publishes in the shared segment
# (shmem.h SHM_WINLIST), for a client that shows what else is running.
HRWL := $(HRGUIOBJ)/lib/clgfx/hrwl.o
# hrwidg.o: dock-widget start-up (inc/hrwidg.h).  A widget is not an app: the
# dock draws it in a cell of its own bar, so it takes this instead of hrapp.o.
HRWIDG := $(HRGUIOBJ)/lib/clgfx/hrwidg.o $(HRGUIOBJ)/lib/clgfx/clgfx.o \
	$(HRGUIOBJ)/lib/clgfx/clrow.o $(HRWL) $(HRLOCK)

# zview owns the screen: links the engine (libhrgfx) + globals.o directly.
# Fonts are loaded at runtime from /usr/hr/fonts/*.hf into the shared VRAM tail
# (inc/shmem.h) and blitted with the engine's bitblt -- no embedded/kernel font.
#
# -L: the window server is the one program here whose TEXT passes 64K (69702
# bytes), so ld must place it across consecutive segments; without it the link
# stops with "address wraparound, use `-L'".
$(HRGUIBIN)/zview: $(HRGUIOBJ)/cmd/zview/zview.o $(HRLOCK) $(HRSEL) $(HRGFX_GLOB) $(LIBHRGFX) $(CRT) $(LIBC)
	@mkdir -p $(dir $@)
	$(call ld-checked,-s -L -o $@ $(CRT) $(HRGUIOBJ)/cmd/zview/zview.o $(HRLOCK) $(HRSEL) $(HRGFX_GLOB) $(LIBHRGFX) $(LIBC))

# ptytest is a plain client: no gfx, just exercises the kernel pty driver.
$(HRGUIBIN)/ptytest: $(HRGUIOBJ)/test/ptytest/ptytest.o $(CRT) $(LIBC)
	@mkdir -p $(dir $@)
	$(call ld-checked,-s -o $@ $(CRT) $(HRGUIOBJ)/test/ptytest/ptytest.o $(LIBC))

# zmon reads the live kernel through /dev/kmem.  Like ps(1) it names the shared
# kernel headers QUALIFIED, so proc.h, sched.h, seg.h and uproc.h come from the
# toolchain -- the set the kernel is built against.  What it still needs from
# here is <romconf.h>, the Commodore ROM configuration block, and <param.h>'s
# NBUF, the buffer-cache tuning constant: both are kernel headers exported to
# os/include, so $(KINC) supplies them the same way it supplies
# <sys/timeout.h>, which <sys/proc.h> includes and the toolchain has not got;
# its other headers are the toolchain's own, byte for byte.
#
# $(TCINC) comes before it on the path, which is the order $(KDRVINC) spells,
# so a name the toolchain also carries resolves to the toolchain's copy.
$(HRGUIOBJ)/cmd/zmon/zmon.o: HRGFXCFLAGS += -I$(KINC) -I$(TCINC) -I$(TCINC)/sys

# --- the GUI applications ---
# Every one of these is the same kind of program: it declares its window to the
# server (hrapp.o) and then blits its own content straight to VRAM through the
# client draw library, so they share one link rule rather than a dozen copies
# of it.  A program says what it needs beyond that in two places:
#
#   HROBJ-<name>    its objects, when it is more than one translation unit
#                   (the default is <name>/<name>.o);
#   HREXTRA-<name>  libraries this one uses and the others should not carry --
#                   the dialog kit, the scrollbar, the selection store, the
#                   window list, libm.
#
# The catalogs the desktop reads name these paths: /usr/hr/etc/apps (the
# server's right-click menu) and /usr/hr/etc/dock (the dock's icon bar).  A
# program missing here is one the desktop offers and cannot start.
HRAPPS := zclock zterm zedit zdlg zcalc zdock zfile zmail zman zmaze zmon \
	zprint zpuzzle

# zmaze is three translation units: the game, its maze core, and an asm inner
# loop for the wall renderer.
HROBJ-zmaze := $(HRGUIOBJ)/cmd/zmaze/zmaze.o $(HRGUIOBJ)/cmd/zmaze/zmcore.o \
	$(HRGUIOBJ)/cmd/zmaze/zmaze_a.o
$(foreach a,$(HRAPPS),$(eval HROBJ-$a ?= $(HRGUIOBJ)/cmd/$a/$a.o))

HREXTRA-zclock  := $(HRDLG) $(LIBM)
HREXTRA-zterm   := $(HRSBAR) $(HRSEL)
HREXTRA-zedit   := $(HRSBAR) $(HRSEL) $(HRDLG)
HREXTRA-zdlg    := $(HRDLG)
HREXTRA-zcalc   := $(HRDLG) $(LIBM)
HREXTRA-zdock   := $(HRWL) $(HRDLG)
HREXTRA-zfile   := $(HRSBAR) $(HRSEL) $(HRDLG)
HREXTRA-zmail   := $(HRSBAR) $(HRSEL) $(HRDLG)
HREXTRA-zman    := $(HRSBAR) $(HRSEL) $(HRDLG)
HREXTRA-zmon    := $(HRSBAR) $(HRWL) $(HRDLG)
HREXTRA-zprint  := $(HRSBAR) $(HRDLG)
HREXTRA-zpuzzle :=
HREXTRA-zmaze   := $(HRSEL) $(HRDLG)

$(foreach a,$(HRAPPS),$(eval $(HRGUIBIN)/$a: $$(HROBJ-$a) $$(CLGFX) \
	$$(HREXTRA-$a) $$(HRGFX_GLOB) $$(LIBHRGFX) $$(CRT) $$(LIBC)))
$(addprefix $(HRGUIBIN)/,$(HRAPPS)):
	@mkdir -p $(dir $@)
	$(call ld-checked,-s -o $@ $(CRT) $(HROBJ-$(@F)) $(CLGFX) \
		$(HREXTRA-$(@F)) $(HRGFX_GLOB) $(LIBHRGFX) $(LIBC))

# --- the dock widgets ---
# A widget is a small resident program the dock starts itself and draws in a
# cell of its own bar; it has no window of its own, so it links hrwidg.o in
# place of the application start-up.
HRWIDGETS := zwclock zwmem zwwin
$(foreach w,$(HRWIDGETS),$(eval $(HRGUIBIN)/$w: $(HRGUIOBJ)/cmd/zwidg/$w.o $$(HRWIDG) \
	$$(HRGFX_GLOB) $$(LIBHRGFX) $$(CRT) $$(LIBC)))
$(addprefix $(HRGUIBIN)/,$(HRWIDGETS)):
	@mkdir -p $(dir $@)
	$(call ld-checked,-s -o $@ $(CRT) $(HRGUIOBJ)/cmd/zwidg/$(@F).o $(HRWIDG) \
		$(HRGFX_GLOB) $(LIBHRGFX) $(LIBC))

# zvpump / zvwatch: zview's input pump and crash watchdog, tiny libc-only
# programs the server execs rather than holding fork copies of its own ~65K
# text for the whole session (the same cure as zterm's hrpump).
$(HRGUIBIN)/zvpump: $(HRGUIOBJ)/cmd/zview/zvpump.o $(CRT) $(LIBC)
	@mkdir -p $(dir $@)
	$(call ld-checked,-s -o $@ $(CRT) $(HRGUIOBJ)/cmd/zview/zvpump.o $(LIBC))

$(HRGUIBIN)/zvwatch: $(HRGUIOBJ)/cmd/zview/zvwatch.o $(CRT) $(LIBC)
	@mkdir -p $(dir $@)
	$(call ld-checked,-s -o $@ $(CRT) $(HRGUIOBJ)/cmd/zview/zvwatch.o $(LIBC))

# hrpump: the terminal's I/O pumps, a tiny libc-only helper zterm execs instead
# of forking copies of itself (memory: keeps a few open terminals from exhausting
# RAM).  NO libhrgfx/clgfx -- it only shuffles bytes between fds.
$(HRGUIBIN)/hrpump: $(HRGUIOBJ)/cmd/zterm/hrpump.o $(HRSEL) $(HRLOCK) $(CRT) $(LIBC)
	@mkdir -p $(dir $@)
	$(call ld-checked,-s -o $@ $(CRT) $(HRGUIOBJ)/cmd/zterm/hrpump.o $(HRSEL) $(HRLOCK) $(LIBC))

# hrclip: read/set the PRIMARY selection from a shell.  Not a GUI client -- no
# window, no clgfx, no engine; just the selection store, whose writer side needs
# hrtas.o for the TSET.  It is also how the store is tested on the plain serial
# emulator, before any mouse plumbing exists.
$(HRGUIBIN)/hrclip: $(HRGUIOBJ)/cmd/hrclip/hrclip.o $(HRSEL) $(HRLOCK) $(CRT) $(LIBC)
	@mkdir -p $(dir $@)
	$(call ld-checked,-s -o $@ $(CRT) $(HRGUIOBJ)/cmd/hrclip/hrclip.o $(HRSEL) $(HRLOCK) $(LIBC))

# --- Vellum: the drawing suite ---
# A SUITE of small tools over one library, not one program with a mode flag,
# installed under its own prefix (/usr/vellum/{bin,lib,sym,etc,eg,frag}) --
# which is the prefix its own data files name, so the layout is the program's
# and not a packaging choice:
#
#   vellum    the editor -- and NOTHING else: it draws
#   velplot   a drawing onto a page (-T lp|ps|hpgl, -tile, -fit, -wide)
#   velpic    a drawing as troff pic source
#   veldxf    the DXF border, both directions (-x writes)
#   velnet    what it is CONNECTED as (netlist, -bom, -spice, -renum)
#   velcheck  judge it (drawings; -sym judges the stencil libraries)
#   veldiff   two revisions compared (-mark writes the markup drawing)
#   velinfo   what it SAYS (-len, -where, -symsheet, -book)
#   velsym    a drawing becomes a stencil
#   velgraph  x/y data becomes a drawing
#   symedit   the stencil-library editor
#
# Every tool is HEADLESS except the editor, symedit and the velprev preview
# window, so the whole pipeline runs on a machine with no bitmap card -- which
# is what a Makefile wants -- and none of them carries code it never runs.
# No floats anywhere: circle radii come from an integer square root.
VELBIN    := $(ROOT)/usr/vellum/bin
VELLIBDIR := $(ROOT)/usr/vellum/lib
VELLIBSRC  = $(HRGUISRC)/lib/vellum
VELCMDSRC  = $(HRGUISRC)/cmd/vellum

# libvellum.a: everything more than one tool needs.  A tool links the library;
# the library never calls a tool.  It is vellum's own -- no part of it has a
# counterpart in lib/clgfx, which is the window-system client contract.
#   velbase   the DRAWING: object list, pools, layer/sheet/selection/view
#             state, and the geometry over them
#   velsymg   the SYMBOL half: the parsed stencil libraries and symbol-space
#             geometry -- no drawing, no pools
#   velmath   isqrt and the B-spline: geometry with no state at all
#   vellib    the .sym LIBRARY loader (and the tokenizer)
#   velfile   the plain-text "vellum1" .d format
#   velsheet  one sheet of a command line: a file, or "-" for the pipe form.
#             Its own member because the editor opens files through a dialog,
#             not off argv
#   velsnap   the model packed into one heap block: the EDITOR's undo
#             pre-image, sized to the drawing
#   velwalk   the device-coordinate walker and its 9-function backend
#   velnetc   the CONNECTIVITY (union-find over wires, pins, net names) and
#             chk(), the finding line judging tools print
#   velrept   naming an object the way a REPORT names it (veldiff, velinfo)
#   velgfx    styled drawing helpers -- the GUI member.  It touches NO drawing
#             state, only velmath, which is what lets a client that merely
#             paints stencils link it
# Member ORDER is load-bearing: Coherent's ld makes ONE pass over an archive,
# so a member may only reference members BELOW it.  The order is therefore the
# dependency order, consumers first and the stateless leaf last -- and it is
# also the bill: a headless tool pulls neither velgfx nor the window system.
VELLIBOBJ := $(addprefix $(HRGUIOBJ)/lib/vellum/,velnetc.o velrept.o \
	velwalk.o velsnap.o velsheet.o velfile.o velbase.o vellib.o \
	velgfx.o velsymg.o velmath.o)
LIBVEL := $(VELLIBDIR)/libvellum.a

# one main + its own operation, per tool
VELPLOTOBJ  := $(HRGUIOBJ)/cmd/vellum/velplot.o
VELPICOBJ   := $(HRGUIOBJ)/cmd/vellum/velpic.o
VELDXFOBJ   := $(HRGUIOBJ)/cmd/vellum/veldxf.o $(HRGUIOBJ)/cmd/vellum/veldxfo.o
VELNETOBJ   := $(HRGUIOBJ)/cmd/vellum/velnet.o
VELCHECKOBJ := $(HRGUIOBJ)/cmd/vellum/velcheck.o
VELDIFFOBJ  := $(HRGUIOBJ)/cmd/vellum/veldiff.o
VELINFOOBJ  := $(HRGUIOBJ)/cmd/vellum/velinfo.o
VELSYMOBJ   := $(HRGUIOBJ)/cmd/vellum/velsym.o
VELGOBJ     := $(HRGUIOBJ)/cmd/vellum/velgraph.o

# The three hi-res members of the suite.  Each is an ordinary window-system
# client -- it links the client draw library the same way the z* applications
# do -- so what separates them from the tools above is the window, not the
# model: they read and write the same drawings through the same libvellum.a.
#   vellum   the editor: the drawing, its commands (velcmd) and the two
#            dialog layers (veldlg on-window, veldlgm the modal cards)
#   symedit  the stencil-library editor
#   velprev  the print-preview window -- the tools' walker (velwalk) behind a
#            cl_* backend built from velgfx's styled primitives, so the
#            preview draws exactly the walk the printer bands get.  It runs on
#            its PARENT's window (hr_attach) and lives in lib/, not bin/:
#            the editor execs it, a user does not.
VELOBJ := $(addprefix $(HRGUIOBJ)/cmd/vellum/,vellum.o velcmd.o veldlg.o \
	veldlgm.o)
VELSYMEOBJ := $(HRGUIOBJ)/cmd/vellum/symedit.o
VELPREVOBJ := $(HRGUIOBJ)/cmd/vellum/velprev.o

VELALLOBJ := $(VELLIBOBJ) $(VELPLOTOBJ) $(VELPICOBJ) $(VELDXFOBJ) \
	$(VELNETOBJ) $(VELCHECKOBJ) $(VELDIFFOBJ) $(VELINFOOBJ) \
	$(VELSYMOBJ) $(VELGOBJ) $(VELOBJ) $(VELSYMEOBJ) $(VELPREVOBJ) \
	$(HRGUIOBJ)/test/vellum/snaptest.o

$(LIBVEL): $(VELLIBOBJ)
	$(ar-kernel)

# A HEADLESS tool: its own objects, the library, and libc.  Nothing here links
# the graphics library, which is why the pipeline works on a machine that has
# no bitmap card at all.
#
# Every tool links LARGE MODEL (-L).  A tool's DATA is the model (the
# 400-object table, the symbol pools, the text pool) plus whatever tables its
# own operation keeps, and that lands within a few hundred bytes of one 64 K
# hardware segment before anything is added -- velnet is already past it.  The
# kernel maps a data segment across several hardware segments and ld bumps a
# module that would STRADDLE a boundary to the next segment start, so -L is a
# link-line word and nothing else.
define vellink
	$(call ld-checked,-s -L -o $@ $(CRT) \
		$(filter-out $(CRT) $(LIBC),$^) $(LIBC))
endef

$(VELBIN)/velplot:  $(VELPLOTOBJ)  $(LIBVEL) $(CRT) $(LIBC) ; $(vellink)
$(VELBIN)/velpic:   $(VELPICOBJ)   $(LIBVEL) $(CRT) $(LIBC) ; $(vellink)
$(VELBIN)/veldxf:   $(VELDXFOBJ)   $(LIBVEL) $(CRT) $(LIBC) ; $(vellink)
$(VELBIN)/velnet:   $(VELNETOBJ)   $(LIBVEL) $(CRT) $(LIBC) ; $(vellink)
$(VELBIN)/velcheck: $(VELCHECKOBJ) $(LIBVEL) $(CRT) $(LIBC) ; $(vellink)
# veldiff is the one that holds TWO drawings: the old revision is shadowed
# whole (a second object table and both pools, ~19 K) while the new one stays
# live.  Nobody else pays for that -- which is the point of a tool per
# operation.
$(VELBIN)/veldiff:  $(VELDIFFOBJ)  $(LIBVEL) $(CRT) $(LIBC) ; $(vellink)
$(VELBIN)/velinfo:  $(VELINFOOBJ)  $(LIBVEL) $(CRT) $(LIBC) ; $(vellink)
$(VELBIN)/velsym:   $(VELSYMOBJ)   $(LIBVEL) $(CRT) $(LIBC) ; $(vellink)
# velgraph: x/y data -> a vellum drawing -- axes, 1-2-5 ticks and styled
# polylines, fixed-point throughout, no gfx.
$(VELBIN)/velgraph: $(VELGOBJ)     $(LIBVEL) $(CRT) $(LIBC) ; $(vellink)

# The GUI three.  Order is the single-pass rule: own objects, then
# libvellum.a, then the window-system client library and the engine, then
# libc -- libvellum's velgfx member calls cl_*, so clgfx must follow it.
#
# The editor links -L: its text passes 64K, so ld must place it across
# consecutive segments (the same reason zview takes the flag).  No single
# module exceeds 64K, which is the rule that actually binds.
define vellink-gui
	$(call ld-checked,-s $(1) -o $@ $(CRT) $(2) $(LIBVEL) $(CLGFX) \
		$(HRDLG) $(HRGFX_GLOB) $(LIBHRGFX) $(LIBC))
endef

$(VELBIN)/vellum: $(VELOBJ) $(LIBVEL) $(CLGFX) $(HRDLG) $(HRSBAR) \
		$(HRSEL) $(HRGFX_GLOB) $(LIBHRGFX) $(CRT) $(LIBC)
	@mkdir -p $(dir $@)
	$(call ld-checked,-s -L -o $@ $(CRT) $(VELOBJ) $(LIBVEL) $(CLGFX) \
		$(HRDLG) $(HRSBAR) $(HRSEL) $(HRGFX_GLOB) $(LIBHRGFX) $(LIBC))

$(VELBIN)/symedit: $(VELSYMEOBJ) $(LIBVEL) $(CLGFX) $(HRDLG) \
		$(HRGFX_GLOB) $(LIBHRGFX) $(CRT) $(LIBC)
	$(call vellink-gui,,$(VELSYMEOBJ))

# velprev borrows the window of the editor that exec'd it (hr_attach), so it
# is a helper binary in lib/, not a command in bin/.  It pulls the whole
# walker out of libvellum.a, so its text needs -L like the editor's.
$(VELLIBDIR)/velprev: $(VELPREVOBJ) $(LIBVEL) $(CLGFX) $(HRDLG) \
		$(HRGFX_GLOB) $(LIBHRGFX) $(CRT) $(LIBC)
	$(call vellink-gui,-L,$(VELPREVOBJ))

# snaptest: the undo pre-image pack/restore (lib/vellum/velsnap.c) exercised
# WITHOUT a window -- the editor's snapshot is a heap block sized to the
# drawing, and what can go wrong is the pointer arithmetic that finds the two
# pools inside it.  A test, so it stays in the obj tree and off the image.
VELSNAPTEST := $(HRGUIOBJ)/test/vellum/snaptest
$(VELSNAPTEST): $(HRGUIOBJ)/test/vellum/snaptest.o $(LIBVEL) $(CRT) $(LIBC)
	$(vellink)

# Vellum's data files (cmd/vellum -> the /usr/vellum tree).
$(ROOT)/usr/vellum/sym/%.sym: $(VELCMDSRC)/sym/%.sym
	@mkdir -p $(dir $@)
	cp $< $@
VELSYM_TARGETS := $(patsubst $(VELCMDSRC)/sym/%,$(ROOT)/usr/vellum/sym/%,\
	$(wildcard $(VELCMDSRC)/sym/*.sym))

# The sample gallery (/usr/vellum/eg): one drawing per domain, each a working
# demo of the manual's features -- schematic, logic, flowchart, network map,
# structure chart, mechanical sketch, P&ID, one-line, floor plan, and a
# two-sheet set for the sheet-set/netlist-merge story.  amp.d.A is amp.d's
# APPROVED REVISION, shipped beside it: a revision is a FILE and the record is
# the directory, so the -diff / -diff -mark demo is a pair of gallery files and
# nothing else.  plate.dxf is the DXF import demo's SOURCE, shipped beside the
# imported plate.d so the border crossing is a file you can rebuild.
$(ROOT)/usr/vellum/eg/%: $(VELCMDSRC)/eg/%
	@mkdir -p $(dir $@)
	cp $< $@
VELEG_TARGETS := $(patsubst $(VELCMDSRC)/eg/%,$(ROOT)/usr/vellum/eg/%,\
	$(wildcard $(VELCMDSRC)/eg/*))

# The stencil SKETCH gallery (/usr/vellum/eg/sk): the nine ordinary drawings
# that BUILD /usr/vellum/sym/pid.sym through `vellum -mksym', driven by
# etc/library.mk.  A library is a build product, its sources are drawings, and
# the shop's stencils therefore get the whole revision workflow because they
# ARE drawings.
$(ROOT)/usr/vellum/eg/sk/%: $(VELCMDSRC)/sk/%
	@mkdir -p $(dir $@)
	cp $< $@
VELSK_TARGETS := $(patsubst $(VELCMDSRC)/sk/%,$(ROOT)/usr/vellum/eg/sk/%,\
	$(wildcard $(VELCMDSRC)/sk/*))

# The FRAGMENT library (/usr/vellum/frag): shop-standard details as ordinary
# drawings.  No code ships with them -- `hrclip < frag/x.d' and a middle click
# in the editor is the whole mechanism.
$(ROOT)/usr/vellum/frag/%: $(VELCMDSRC)/frag/%
	@mkdir -p $(dir $@)
	cp $< $@
VELFRAG_TARGETS := $(patsubst $(VELCMDSRC)/frag/%,$(ROOT)/usr/vellum/frag/%,\
	$(wildcard $(VELCMDSRC)/frag/*))

# /usr/vellum/etc: the start-up library list (libs), the user scratch library
# (symbols), the title-block frame, and the two Makefile skeletons a shop
# copies -- project.mk for a sheet set, library.mk for a stencil library.
$(ROOT)/usr/vellum/etc/%: $(VELCMDSRC)/etc/%
	@mkdir -p $(dir $@)
	cp $< $@
VELETC_TARGETS := $(patsubst $(VELCMDSRC)/etc/%,$(ROOT)/usr/vellum/etc/%,\
	$(wildcard $(VELCMDSRC)/etc/*))

VELTOOLS := velplot velpic veldxf velnet velcheck veldiff velinfo velsym \
	velgraph vellum symedit
VEL_TARGETS := $(LIBVEL) $(addprefix $(VELBIN)/,$(VELTOOLS)) \
	$(VELLIBDIR)/velprev \
	$(VELSNAPTEST) $(VELSYM_TARGETS) $(VELEG_TARGETS) $(VELSK_TARGETS) \
	$(VELFRAG_TARGETS) $(VELETC_TARGETS)

# Build the drawing suite on its own (it is otherwise reached through `hrgui').
vellum: $(VEL_TARGETS)

# The ZView system fonts: generated into the shared-VRAM .hf format the server
# loads into the tail (../hostbuild/mkfont.py); every client blits from that single
# copy -- no relink, no per-glyph trap.  All three come from the FM fonts:
# gacha.r.hf 8x15 (terminal -- 8px cell = byte-aligned glyph output),
# gacha.b.hf 9x16 (UI chrome) and sail.hf 6x8 (icon labels).
#
# The FM fonts are read from the component's own $(HRFONTDIR), where they are
# content with no generator, not from the staged copy of them -- one run reads all three and
# writes all three, so this is a GROUPED target (&:); as three ordinary rules
# with one recipe, `make -j' ran the script three times over its own output.
HRGUIFONTS = $(ROOT)/usr/hr/fonts/gacha.r.hf $(ROOT)/usr/hr/fonts/gacha.b.hf \
	$(ROOT)/usr/hr/fonts/sail.hf
HRFMDIR    = $(HRFONTDIR)
$(HRGUIFONTS) &: ../hostbuild/mkfont.py $(HRFMDIR)/gacha.r.7 \
		$(HRFMDIR)/gacha.b.8 $(HRFMDIR)/sail.r.6
	@mkdir -p $(ROOT)/usr/hr/fonts
	$(PYTHON) ../hostbuild/mkfont.py --fm $(HRFMDIR) --out $(ROOT)/usr/hr/fonts

# the launchable-app catalog zview reads at startup (src -> staging image).
$(ROOT)/usr/hr/etc/apps: $(HRSRC)/etc/apps
	@mkdir -p $(dir $@)
	cp $< $@

# the dock's own icon catalog, read by zdock (src -> staging image).
$(ROOT)/usr/hr/etc/dock: $(HRSRC)/etc/dock
	@mkdir -p $(dir $@)
	cp $< $@

# the desktop start-up script: a /bin/sh script zview runs when it comes up,
# naming the apps to open (with their -P/-S/-H options) -- so the first screen
# is configuration, not server code (src -> staging image).
$(ROOT)/usr/hr/etc/rc: $(HRSRC)/etc/rc
	@mkdir -p $(dir $@)
	cp $< $@

# desktop icons (.icn) zview blits for minimised windows (src -> staging image).
HRGUIICONS := $(patsubst $(HRSRC)/icons/%,$(ROOT)/usr/hr/icons/%,\
	$(wildcard $(HRSRC)/icons/*.icn))
$(ROOT)/usr/hr/icons/%: $(HRSRC)/icons/%
	@mkdir -p $(dir $@)
	cp $< $@

# --- the hi-res driver (/drv/hr) ---
# Keyboard ISR, polled mouse, hardware cursor, and the drawing-lock futex slow
# path (CIOMLOCK/CIOMUNLOCK).  Loadable l.out: keep globals (-X) so `load' finds
# the hrcon_ config symbol, bind -k against the symboled kernel, and it must be +x.
#
# It is a KERNEL translation unit but it lives under the ZView tree because it
# belongs to this subsystem rather than to the generic kernel -- it is versioned
# and changed together with the server and clients that depend on it.  Its
# ancestor is the driver at $(HRSRC)/drv/hripc (see the hr section above).
#
# It is built like the kernel's own loadable drivers and not like the rest of
# this tree: kernel code, so it takes the kernel's compiler settings and the
# kernel's header set, and it is bound to one kernel image by `ld -k', which
# bakes that kernel's absolute symbol addresses into it.  Those settings are
# not restated here -- the kernel publishes them beside the drivers it built
# with them, as $(KDRVCONF), and a value copied into this file would be a
# second opinion about the kernel's own frame convention.  The compiler is this
# repository's toolchain edge (the same passes the kernel's build names), the
# variant/cc2 mode/defines/link flags are the kernel's, and the `-k' symbol
# source is the exported kernel image, so a relinked kernel relinks this
# driver.
KDRVVAR   := $(shell sed -n 's/^ccvar=//p' $(KDRVCONF) 2>/dev/null)
KDRVMODE  := $(shell sed -n 's/^cc2mode=//p' $(KDRVCONF) 2>/dev/null)
KDRVDEFS  := $(shell sed -n 's/^cdefs=//p' $(KDRVCONF) 2>/dev/null)
KDRVLD    := $(shell sed -n 's/^ldflags=//p' $(KDRVCONF) 2>/dev/null)
# $(KINC) alone now answers for the kernel's exported header set (romconf.h
# ships from os/include like the rest), so this needs no second, kernel-
# SOURCE include path.  $(HRHDR) is on the path for <rico.h>.
KDRVINC   := -I$(KINC) -I$(KINC)/sys -I$(TCINC) -I$(TCINC)/sys -I$(HRHDR)
KDRVCC0   := $(C900_TCB)/z8001/cc0-z8001
KDRVCC1   := $(C900_TCB)/z8001/cc1-z8001
KDRVCC2   := $(C900_TCB)/z8001/cc2-z8001
KDRVAS    := $(C900_TCB)/as-z8001
KDRVLINK  := $(C900_TCB)/ld-z8001

# Selected on whether every input exists, so that the refusal is a rule and not
# an unreachable guard (see the kernel-artifacts section above).  Computed into
# a variable first: a line continuation inside the condition would leave the
# indent of the next line in the value, and a test for emptiness would then
# always find whitespace and take the wrong branch.
KDRV_HR_OK := $(strip $(if $(wildcard $(KIMG)),$(if $(wildcard $(KDRVCONF)),$(wildcard $(KDRVCC0)))))
ifeq (,$(KDRV_HR_OK))

$(DRVDIR)/hr:
	@echo "$@ is kernel code: it compiles against the kernel's header set and"
	@echo "  links against the kernel image commodore-900-coh-kernel3 exports."
	@echo "  Wanted $(KIMG)"
	@echo "     and $(KDRVCONF)"
	@echo "     and $(KDRVCC0)"
	@echo "  C900_KERNEL = $(if $(C900_KERNEL),$(C900_KERNEL),<unset: run \`sh ../mk/deps.sh -n kernel' for the paths tried>)"
	@echo "  Run \`make kernel drivers' in that repository, or \`make deps DEP=kernel' at this repository's root."
	@exit 2

else

# hr.c #includes hr2.c, so the two are a single object.  The passes are run
# individually rather than through a driver because the cc2 mode is what puts
# this object on the kernel's frame convention.
$(HRGUIOBJ)/drv/hr.o: $(HRGUISRC)/drv/hr.c $(HRGUISRC)/drv/hr2.c \
		$(HRGUISRC)/drv/hr.h $(KDRVCONF)
	@mkdir -p $(dir $@)
	$(call buildmap,$< $(HRGUISRC)/drv/hr2.c $(HRGUISRC)/drv/hr.h)
	$(KDRVCC0) $(KDRVVAR) $< $(@:.o=.z0) $(KDRVINC) $(KDRVDEFS)
	$(KDRVCC1) $(KDRVVAR) $(@:.o=.z0) $(@:.o=.z1)
	$(KDRVCC2) $(KDRVMODE) $(@:.o=.z1) $@ $(@:.o=.scr) 0

$(HRGUIOBJ)/drv/hrasm.o: $(HRGUISRC)/drv/hrasm.s $(KDRVCONF)
	@mkdir -p $(dir $@)
	$(call buildmap,$<)
	cpp -traditional-cpp -P $(KDRVINC) $< $(@:.o=.i)
	$(KDRVAS) -g -o $@ $(@:.o=.i)

$(DRVDIR)/hr: $(HRGUIOBJ)/drv/hr.o $(HRGUIOBJ)/drv/hrasm.o $(KIMG)
	@mkdir -p $(dir $@)
	$(call buildmap,$(HRGUIOBJ)/drv/hr.o $(HRGUIOBJ)/drv/hrasm.o)
	$(KDRVLINK) $(KDRVLD) -o $@ $(HRGUIOBJ)/drv/hr.o $(HRGUIOBJ)/drv/hrasm.o -k$(KIMG)
	chmod +x $@

endif

HRGUI_TARGETS := $(DRVDIR)/hr $(LIBHRGFX) $(HRGUIBIN)/gfxtest $(HRGUIBIN)/zview \
	$(ETCDIR)/zlogin \
	$(HRGUIBIN)/zvpump $(HRGUIBIN)/zvwatch \
	$(addprefix $(HRGUIBIN)/,$(HRAPPS)) \
	$(addprefix $(HRGUIBIN)/,$(HRWIDGETS)) \
	$(HRGUIBIN)/ptytest $(HRGUIBIN)/hrpump $(HRGUIBIN)/hrclip \
	$(HRGUIFONTS) $(HRFONTS) \
	$(VEL_TARGETS) \
	$(ROOT)/usr/hr/etc/apps $(ROOT)/usr/hr/etc/dock $(ROOT)/usr/hr/etc/rc \
	$(HRGUIICONS)

# Build the ZView desktop and its clients on their own (they are otherwise
# only reachable through `image', which repacks the whole disk).
hrgui: $(HRGUI_TARGETS)

