# tests/ptraudit -- does a 32-bit return value survive the call?
#
# ptraudit captures the result of every libc call whose return type is wider
# than an int and checks it: a long must come back whole rather than modulo
# 65536, and a pointer must come back with its segment rather than segment 0.
# Since K&R C defaults an undeclared function to int, and the ABI puts a 16-bit
# result in R1 alone but a 32-bit one in R0:R1, each check passes only if a
# declaration of that function is reachable from include on this target.
#
# calleesave/calleesaved load R6..R12 with sentinels, call one library routine
# and report the first register that came back changed (0 = all intact).
# calleesave covers the string routines, calleesaved the double runtime, whose
# register set is saved with a stack hole (ldm (rr14),r6,$8) rather than pushl.
#
#	make		build all three
#	make check	run them on the host under n2z8001 -runexec
#	make stage	copy ptraudit into the image staging bin dir
#
# `make check' needs a Z8001 guest runner: the C emulator, which takes
# -runexec:
#	make check N2=$$(sh $(C900_TOOLCHAIN)/host/runner.sh)

include ../../hostbuild/toolchain.mk
CCZ	= $(TC)/ccz
N2	= /tmp/n2z8001
STAGE	= ../../hostbuild/build/bin

all: ptraudit calleesave calleesaved

ptraudit: ptraudit.c
	$(CCZ) -o $@ ptraudit.c

calleesave: calleesave.s csmain.c
	$(CCZ) -o $@ csmain.c calleesave.s

calleesaved: calleesaved.s csdmain.c
	$(CCZ) -o $@ csdmain.c calleesaved.s

check: all
	$(N2) -runexec ./ptraudit
	$(N2) -runexec ./calleesave
	$(N2) -runexec ./calleesaved

# The negative control: is this program capable of failing at all?
#
# What ptraudit tests is whether the declarations in include are reachable,
# because K&R C defaults an undeclared function to int and the ABI then reads a
# 32-bit result out of R1 alone.  So the mutation is to take the declarations
# away: the SAME source is compiled against a header directory holding empty
# string.h, stdlib.h, unistd.h and mtype.h, which shadow the real ones.
#
# Both directions are required.  The declared build must PASS and the
# undeclared build must FAIL; a run where the second also passed would mean the
# checks are not reading the truncated values they claim to read, and the
# program proves nothing.  This is the -runexec half of tests/hostcheck: the
# host cannot host this one, because a host pointer cast to long is nonzero
# whatever happens to it.
NODECL	= nodecl

mutate: ptraudit
	@rm -rf $(NODECL) ptraudit-nodecl
	@mkdir -p $(NODECL)
	@for h in string.h stdlib.h unistd.h; do \
		echo '/* empty on purpose: tests/ptraudit `make mutate` */' \
			> $(NODECL)/$$h; done
	@{ echo '/* empty on purpose, but for the one CONSTANT ptraudit needs */'; \
	   echo '#define M_Z8001 0x0b'; } > $(NODECL)/mtype.h
	@$(CCZ) -I $(NODECL) -o ptraudit-nodecl ptraudit.c > /dev/null 2>&1
	@$(N2) -runexec ./ptraudit | grep -q '^PASS' || \
		{ echo "  FAIL ptraudit: the DECLARED build does not pass"; \
		  exit 1; }
	@echo "  ok   ptraudit passes with the declarations in scope"
	@$(N2) -runexec ./ptraudit-nodecl | grep -q '^FAIL' || \
		{ echo "  FAIL ptraudit: it passes WITHOUT the declarations"; \
		  echo "       too, so none of its checks reads a truncated"; \
		  echo "       value and the program proves nothing."; \
		  exit 1; }
	@echo "  ok   and fails with them taken away"

stage: ptraudit
	cp ptraudit $(STAGE)/ptraudit

clean:
	rm -rf ptraudit calleesave calleesaved ptraudit-nodecl $(NODECL)
