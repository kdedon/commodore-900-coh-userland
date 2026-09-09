# emulator.mk -- resolve the Commodore 900 emulator, for makefiles.
# The shell equivalent is emulator.sh beside it.
#
# The emulator is not in this repository; this tree consumes a checkout of
#
#	https://github.com/MichalPleban/commodore-900-emulator
#
# Two things are wanted from it: `c900 --exec' is a process runner (runs the
# Z8001 binaries in a host build), and `c900' with a
# firmware directory is a machine, for booting an image.
#
#   $(C900_EMU)   the c900 binary.  Unset, the first of $(C900_EMU_SEARCH)
#                 that has a bin/c900 in it wins; override it to use one
#                 somewhere else.  The search is a list because this
#                 repository is consumed both as a sibling checkout and from
#                 inside a `repos/' staging directory.  Same paths, same order,
#                 as emulator.sh: the pinned release in deps/, a c900 on $$PATH,
#                 a checkout beside this repository or beside one of its THREE
#                 enclosing directories, then one under `repos/'.  Three is
#                 what reaches the enclosing workspace from a repository staged
#                 at <workspace>/repos/<repo>; further out is not a sibling, it
#                 is a coincidence, and a walk that keeps going finds another
#                 job's checkout and reports a false success.
#
# Defines $(C900_EMU), or leaves it empty, and $(C900_EMU_ERR), the message a
# rule that cannot proceed without one should print.  Empty is not an error
# here: the refusal belongs at the command that wanted the emulator.
#
# $(C900_ROOT) must be set before including this file.
C900_EMU_PATHC900 := $(shell command -v c900 2>/dev/null)
C900_EMU_SEARCH := $(abspath $(C900_ROOT)/deps/commodore-900-emulator) \
		   $(if $(C900_EMU_PATHC900),$(patsubst %/bin/c900,%,$(C900_EMU_PATHC900))) \
		   $(abspath $(C900_ROOT)/../commodore-900-emulator) \
		   $(abspath $(C900_ROOT)/../../commodore-900-emulator) \
		   $(abspath $(C900_ROOT)/../../../commodore-900-emulator) \
		   $(abspath $(C900_ROOT)/repos/commodore-900-emulator)
C900_EMU ?= $(firstword $(wildcard $(addsuffix /bin/c900,$(C900_EMU_SEARCH))))
# A directory names the checkout; a file names the binary.  Both spellings are
# in use, and emulator.sh accepts both, so this side must too.
C900_EMU := $(if $(wildcard $(C900_EMU)/bin/c900),$(C900_EMU)/bin/c900,$(C900_EMU))

# One message, naming the variable, the repository and the paths tried.
C900_EMU_ERR = { echo "no emulator: clone and build"; \
		 echo "    https://github.com/MichalPleban/commodore-900-emulator"; \
		 echo "  to one of:"; \
		 for d in $(C900_EMU_SEARCH); do echo "    $$d"; done; \
		 echo "  -- or put its c900 on \$$PATH, or set C900_EMU to it."; \
		 exit 2; }
# end of emulator.mk
