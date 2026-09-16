# ttycity -- Micropolis (SimCity) for the terminal, on COHERENT 3.5 / Z8001.
# The linked text is 189,808 bytes: three hardware segments (the toolchain's
# tests/multiseg-text.sh links that shape).
#
# Driven from the host cross toolchain, so the entry point is a variable and not
# a hard-coded path; hostbuild's game rule should set CCZ, OBJ and BIN and then
# just `make -C games/ttycity'.  Nothing here runs on the target.
#
#   -i  separated I/D, text shared
#   -L  large model: text and data may span several 64K segments
#   -DCOHERENT selects sim.h's c900.h include and mac.h's `QUAD long'

OS	= ../..
include $(OS)/hostbuild/toolchain.mk
CCZ	= $(TC)/ccz
CURSES	= $(TCB)/curses
OBJ	= obj
BIN	= .

CFLAGS	= -c -i -L -DCOHERENT \
	  -I$(OS)/base/lib/libcurses -I.
LDFLAGS	= -i -L -s
LIBS	= $(CURSES)/libcurses.a $(CURSES)/libterm.a
# ccz links crt0.o and libc-z8001.a implicitly, so make cannot see them;
# named here they become link prerequisites, so a rebuilt libc relinks both
# binaries.  The archives are inputs to the LINK only -- objects do not
# depend on them.
LIBC	= $(TCB)/libc-z8001/libc-z8001.a
LINKDEP	= $(LIBS) $(LIBC) $(TCB)/libc-z8001/crt0.o

# The simulation engine, unchanged Micropolis apart from the int16 and QUAD
# audit.
ENGINE	= s_alloc s_disast s_eval s_fileio s_gen s_init s_msg s_power s_scan \
	  s_sim s_traf s_zone g_ani w_con w_tool w_sprite w_budget w_eval \
	  w_keys w_resrc w_update w_stubs w_util w_sound rand random

# The terminal front end, plus nc_c900.c -- this port's curses/libc shims.
FRONT	= nc_main nc_render nc_gfx nc_aa nc_input nc_status nc_minimap \
	  nc_menu nc_dialogs nc_stubs nc_c900

# games/lib/cursrc/waddch.c overrides the archive member of the same name,
# which draws a NUL for every single-character output on this machine; the file
# says why.  It must precede the archives on the link line, so it is part of
# OBJS rather than of LIBS.
OBJS	= $(ENGINE:%=$(OBJ)/%.o) $(FRONT:%=$(OBJ)/%.o) $(OBJ)/waddch.o

all: $(BIN)/ttycity $(BIN)/hrtiles

$(BIN)/ttycity: $(OBJS) $(LINKDEP)
	$(CCZ) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(OBJ)/waddch.o: $(OS)/games/lib/cursrc/waddch.c
	@mkdir -p $(OBJ)
	$(CCZ) $(CFLAGS) -o $@ $(OS)/games/lib/cursrc/waddch.c

# hrtiles -- the hi-res soft-font tile demo.  Standalone: no curses, no
# engine, 3.7 KB of text.
$(BIN)/hrtiles: $(OBJ)/hrtiles.o $(LINKDEP)
	$(CCZ) -i -s -o $@ $(OBJ)/hrtiles.o

$(OBJ)/%.o: %.c
	@mkdir -p $(OBJ)
	$(CCZ) $(CFLAGS) -o $@ $<

$(OBJS): sim.h mac.h macros.h view.h c900.h

$(OBJ)/nc_main.o $(OBJ)/nc_render.o $(OBJ)/nc_gfx.o $(OBJ)/nc_aa.o \
$(OBJ)/nc_input.o $(OBJ)/nc_status.o $(OBJ)/nc_minimap.o $(OBJ)/nc_menu.o \
$(OBJ)/nc_dialogs.o $(OBJ)/nc_stubs.o $(OBJ)/nc_c900.o: nc.h

$(OBJ)/g_ani.o: animtab.h

clean:
	rm -rf $(OBJ) $(BIN)/ttycity $(BIN)/hrtiles

.PHONY: all clean
