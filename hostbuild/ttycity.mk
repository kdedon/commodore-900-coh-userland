# ttycity.mk -- resolve the ttycity checkout, for makefiles and for the games
# sweep.  NOT WIRED UP YET: nothing includes this file while games/ttycity
# is still in this tree.
#
# ttycity is Micropolis (SimCity) for the terminal -- tenox7's ncurses port of
# Open Source Micropolis, GPL v3 PLUS additional terms imposed by Electronic
# Arts under GPL section 7 (no trademark rights, indemnity, no misrepresentation
# of origin; the COPYING in that repository is not boilerplate and is meant to
# be read).  This repository is BSD-3.  A game whose licence differs from its
# host tree's, which no default image ships, and whose upstream is somebody
# else's repository, is a CONSUMED CHECKOUT for the same reason kboot and the
# toolchain are: it belongs to nobody here, it versions on its own clock, and a
# copy that ships is not a dormant duplicate but the thing under test.
#
# The consumed checkout is a FORK of tenox7/ttycity carrying the C900 port --
# the K&R de-ANSI'd sources, the 16-bit audit, c900.h/nc_c900.c and hrtiles.c.
# Stock upstream will NOT build here: 273 of its functions are ANSI definitions
# with no return type and cc0 is K&R.  That is why the sentinel below is the
# port's own header and not one of upstream's files -- resolving a stock
# checkout would trade one legible refusal for several hundred parse errors.
#
#   $(C900_TTYCITY)  the checkout.  Unset, the first of $(C900_TCITY_SEARCH)
#                    that has a src/c900.h in it wins, so a plain `make' works
#                    when the fork is checked out beside this repository;
#                    override it to build against a checkout somewhere else.
#                    The search is a LIST for the same reason toolchain.mk's and
#                    kboot.mk's are: this repository is consumed both as a
#                    sibling checkout and from inside a `repos/' staging
#                    directory, and a single relative path is silently wrong in
#                    whichever of the two it was not written for.  Both the
#                    `commodore-900-ttycity' spelling used by the other consumed
#                    repositories and a fork that kept upstream's plain
#                    `ttycity' name are searched, in that order.
#
# Defines $(TTYCITY), or leaves it empty.  EMPTY IS NOT AN ERROR ANYWHERE.  The
# games are optional and this one is held back from every image today, so the
# sweep SKIPS the target and says so, exactly as it already prints
# "ttycity: held back" under TTYCITY=0.  A caller that genuinely cannot proceed
# without the checkout -- someone who asked for it by name with TTYCITY=1 -- uses
# $(TTYCITY_ERR) at the point of use, which is kboot.mk's arrangement and the
# reason this file has no optional-build knob of its own: toolchain.mk needs
# $(C900_TC_OPTIONAL) because it REFUSES at parse time and the flag suppresses
# the refusal, and there is nothing here to suppress.
C900_TCITY_DIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
C900_TCITY_SEARCH := $(abspath $(C900_TCITY_DIR)/../../commodore-900-ttycity) \
		     $(abspath $(C900_TCITY_DIR)/../repos/commodore-900-ttycity) \
		     $(abspath $(C900_TCITY_DIR)/../../ttycity) \
		     $(abspath $(C900_TCITY_DIR)/../repos/ttycity)
C900_TTYCITY ?= $(firstword $(patsubst %/src/c900.h,%,\
		 $(wildcard $(addsuffix /src/c900.h,$(C900_TCITY_SEARCH)))))
TTYCITY := $(C900_TTYCITY)

# The three things a caller wants out of the checkout, named here so that no
# rule elsewhere spells the fork's internal layout for itself.
#
#   $(TTYCITY_BUILD)   the directory holding the port's Makefile.  `make -C' it
#                      with CCZ, CURSES, OBJ and BIN set, as the sweep does now.
#   $(TTYCITY_RES)     stri.*/snro.* string and sound resources, and
#   $(TTYCITY_CITIES)  the 24 bundled .cty cities.  Both are STAGED, not linked
#                      in: unlike upstream this port does not bake res_data.h
#                      into the binary (870 KB of static data cannot live in one
#                      64K hardware segment), so w_resrc.c reads them from
#                      /usr/games/lib/ttycity at run time and dist/lists/
#                      games.list needs a line for each directory.
TTYCITY_BUILD  = $(TTYCITY)/src
TTYCITY_RES    = $(TTYCITY)/res
TTYCITY_CITIES = $(TTYCITY)/cities

# One message, naming the variable, the repository and the paths tried.  Without
# it the first symptom of a missing checkout is `No rule to make target' on a
# path with an empty prefix, several hundred lines into a games sweep, which
# says nothing about what to do next.
TTYCITY_ERR = { echo "no ttycity checkout: set C900_TTYCITY to a clone of the"; \
		echo "  C900 fork of tenox7/ttycity (the one carrying src/c900.h),"; \
		echo "  or put one at one of:"; \
		for d in $(C900_TCITY_SEARCH); do echo "    $$d"; done; \
		echo "  It is an optional game and no default image ships it;"; \
		echo "  nothing else in this tree needs it."; \
		exit 2; }
# end of ttycity.mk
