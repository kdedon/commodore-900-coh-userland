# tests/top -- the namelist-and-/dev/kmem route, from outside.
#
# top(1) resolves five symbols out of a kernel image, seeks /dev/kmem to the
# low half of each, and walks a chain of far pointers through the kernel's
# alloc arena.  Everything that can go wrong with that route is a property of
# the kernel's own data layout -- a chain link out of the arena, a namelist
# whose addresses now hold something else, an arena the device will not read
# back -- and no host stand-in has that layout: on the host a PROC is a
# different size, a far pointer is a host pointer and nlist() has no l.out to
# read.  So this probe's mutations run under n2z8001 -runexec, the way
# ../ptraudit's do, and tests/hostcheck delegates to `make mutate' here.
#
# mkkmem forges the /dev/kmem image, taking its offsets from a REAL kernel
# image through the same nlist() call top makes.  The table it lays down is
# fixed and known, which is what lets the good case be checked by its output
# and not merely by its exit status.
#
#	make			build mkkmem
#	make check		forge a good image and read it with top
#	make mutate		the whole matrix: one good image, five broken
#				systems, each of which top must refuse by name
#
# `make mutate' needs three things it does not build, and says so rather than
# passing without them:
#   - a Z8001 guest runner, for -runexec: the C emulator,
#	make mutate N2=$$(sh $(C900_TOOLCHAIN)/host/runner.sh)
#   - a linked kernel, for its namelist:	kobj/kernel.out in the kernel's
#	export, which the c900-kernel-v<V> release `make deps' places carries
#   - the target top, from the userland build:	../../hostbuild/build/bin/top
# Nothing here relinks the kernel or rebuilds top; both are read as they stand.
#
# NOT COVERED, and it is not an oversight.  Every interval-dependent thing top
# does -- the per-process %CPU delta, the load average, and the clamp that
# keeps a wrapped 16-bit utimer from being believed -- needs the
# kernel to be in two states, and a forged image is one state: top's first
# sample is discarded (`primed'), and its second reads the same file and
# measures a zero interval.  A wrap is also not a failure top reports; it is a
# value top clamps, so there is no exit status to want.  Reaching it needs the
# image to change under top between samples, which is a wall-clock race around
# a guest that counts in guest time, and a flaky case in a gate is worse than
# an absent one.

include ../../hostbuild/toolchain.mk
CCZ	= $(TC)/ccz
N2	= /tmp/n2z8001
# The linked kernel belongs to the kernel repository; this tree links none.
# $(KDIR) is that repository's export, resolved by toolchain.mk through the
# same `kernel' edge as $(KINC), and empty when only a headers release did.
KERN	= $(KDIR)/kobj/kernel.out
TOP	= ../../hostbuild/build/bin/top
# mkkmem reads the kernel's own PROC layout: <l.out.h> and <proc.h>, and
# <sys/proc.h> in turn wants <sys/timeout.h>, which only the kernel repository
# has.  $(KINC) is that repository's include directory (toolchain.mk); empty
# means no kernel resolved, and the rule refuses by name.
INC	= -I $(KINC) -I $(KINC)/sys
ROOT	= root

all: mkkmem

mkkmem: mkkmem.c
	@test -n "$(KINC)" || { \
	  echo "mkkmem: needs the kernel's headers; sh mk/deps.sh -n kernel" >&2; \
	  exit 2; }
	$(CCZ) -s -i -L $(INC) -o $@ mkkmem.c

# The forged root n2z8001 -runexec resolves the guest's paths against: the
# kernel image under the name top looks a namelist up in, and the device it
# seeks.
check: mkkmem
	@test -f "$(KERN)" || { \
	  echo "top: needs the linked kernel $(KERN);" >&2; \
	  echo "  make deps DEP=kernel places the release that carries it" >&2; \
	  exit 2; }
	@test -x "$(TOP)" || { \
	  echo "top: needs the target top $(TOP); sh hostbuild/build-cmd.sh top" >&2; \
	  exit 2; }
	@mkdir -p $(ROOT)/dev
	cp $(KERN) $(ROOT)/coherent
	N2ROOT=$(ROOT) $(N2) -runexec ./mkkmem /coherent /dev/kmem
	N2ROOT=$(ROOT) $(N2) -runexec $(TOP) -b 40

# ------------------------------------------------------------------------
# The negative controls.  Each line below is one defect of the namelist route,
# and the claim is that top refuses it BY NAME -- so each is matched against
# the diagnostic it must produce, not merely against a nonzero exit.  A run
# where a broken image printed a table would mean the arena bounds check and
# the short-read check are decoration.
#
# n2z8001 does not pass the guest's exit status on to the shell, so the verdict
# is taken from the output, as ../ptraudit's is.
# ------------------------------------------------------------------------
mutate: mkkmem
	@[ -x $(N2) ] || { echo "  FAIL top: no $(N2)"; exit 1; }
	@[ -f $(KERN) ] || { echo "  FAIL top: no $(KERN)"; exit 1; }
	@[ -x $(TOP) ] || { echo "  FAIL top: no $(TOP)"; exit 1; }
	@mkdir -p $(ROOT)/dev
	@$(MAKE) -s one WANT=0 \
	    IMG= WHY="a chain top must accept" \
	    PAT="3 processes:   1 sleeping, 2 running"
	@$(MAKE) -s one WANT=1 \
	    IMG=-bad WHY="the first link leaves the arena" \
	    PAT="process chain leaves the arena"
	@$(MAKE) -s one WANT=1 \
	    IMG=-stale WHY="the image was laid out by a different link" \
	    PAT="alloc arena is empty"
	@$(MAKE) -s one WANT=1 \
	    IMG=-short WHY="the arena is past the end of the device" \
	    PAT="short read from /dev/kmem"
	@$(MAKE) -s one WANT=2 \
	    IMG=-reused WHY="an entry's block is another structure's now" \
	    PAT="1 processes:   1 sleeping"
	@$(MAKE) -s one WANT=1 \
	    KIMG=$(TOP) WHY="the namelist is not a kernel image" \
	    PAT="no namelist -- not a kernel image"
	@$(MAKE) -s one WANT=1 \
	    IMG= MODE=000 WHY="/dev/kmem is not readable" \
	    PAT="/dev/kmem: permission denied"
	@echo "  ok   the idle process is left out and pid 42 leads the table"

# one WANT=<0|1|2> IMG=<mkkmem flag> PAT=<what top must say> WHY=<the defect>
#
# WANT=0 also checks that the forged table came out the way mkkmem laid it
# down: pid 0 is the idle process and must not appear, pid 42 has used the most
# time and must lead the table, and the memory line must be the sum over the
# forged segment queue -- six segments, 120 K, out of a 896 K pool.  WANT=2 is the walk that has to stop
# early: the entries beyond the break must be absent, which is the difference
# between a short table and an invented one.
#
# The kernel image is a file in the forged root and not an option, because top
# reads /coherent by name: $(KIMG) is what is put there, and the case that
# wants a namelist top cannot read puts something that is not a kernel.
WANT	= 0
IMG	=
KIMG	= $(KERN)
MODE	= 644
PAT	=
WHY	=

one:
	@cp $(KIMG) $(ROOT)/coherent
	@chmod 644 $(ROOT)/dev/kmem 2>/dev/null || true
	@cp $(KERN) $(ROOT)/kernel.out
	@N2ROOT=$(ROOT) $(N2) -runexec ./mkkmem $(IMG) /kernel.out /dev/kmem \
		> mutate.log 2>&1 || true
	@chmod $(MODE) $(ROOT)/dev/kmem
	@N2ROOT=$(ROOT) $(N2) -runexec $(TOP) -b 40 > mutate.log 2>&1 || true
	@chmod 644 $(ROOT)/dev/kmem
	@grep -q "$(PAT)" mutate.log || { \
		echo "  FAIL top ($(WHY)): no \"$(PAT)\" in the output"; \
		sed 's/^/       | /' mutate.log; exit 1; }
	@if [ $(WANT) = 0 ]; then \
		grep -q '^    0 ' mutate.log && { \
			echo "  FAIL top: the idle process is in the table"; \
			exit 1; }; \
		sed -n '/^  PID /{n;p;}' mutate.log | grep -q '^   42 ' || { \
			echo "  FAIL top: pid 42 does not lead the table"; \
			sed 's/^/       | /' mutate.log; exit 1; }; \
		grep -q '^Memory: 1024K total, 120K used, 776K free' mutate.log || { \
			echo "  FAIL top: the memory line is not the queue's sum"; \
			sed 's/^/       | /' mutate.log; exit 1; }; \
		echo "  ok   top reads a forged arena: $(WHY)"; \
	elif [ $(WANT) = 2 ]; then \
		grep -q '^   42 \|^    7 ' mutate.log && { \
			echo "  FAIL top: it reported an entry past the break"; \
			sed 's/^/       | /' mutate.log; exit 1; }; \
		echo "  ok   top stops the table there: $(WHY)"; \
	else \
		echo "  ok   top refuses it by name: $(WHY)"; \
	fi
	@rm -f mutate.log

clean:
	rm -f mkkmem mutate.log
	rm -rf $(ROOT)
