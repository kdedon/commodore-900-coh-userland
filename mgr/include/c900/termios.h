/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*
 * <termios.h> for the C900 build.  This shadows include/termios.h and is
 * reached first (Makefile.c900's INCS), because MGR wants the whole POSIX
 * terminal interface and this kernel's line discipline is termio: the struct
 * and the TCSA* actions below are spellings of termio and its ioctls.
 *
 * tcflow() is NOT one of those spellings.  It is a real libc call
 * (libc/gen/tcflow.c) over the kernel's TCXONC ioctl, and ify(1) is its one
 * caller: iconifying a window means stopping output on somebody else's line,
 * which no ^S typed at a window can do.  A macro expanding to 0 here would
 * make ify compile and iconify nothing.
 */
#include <termio.h>
#define termios termio
#define TCSANOW TCSETA
#define TCSADRAIN TCSETAW
#define TCSAFLUSH TCSETAF
#ifndef TCIOFLUSH
#define TCIOFLUSH TIOCFLUSH
#endif
/* IEXTEN names a c_lflag bit this line discipline does not implement, so it is
   0 and the |= that sets it is a no-op.  TIOCNOTTY is deliberately NOT defined:
   there is no ioctl that gives up a controlling terminal here, and defining it
   as 0 turned every `#ifdef TIOCNOTTY' block into ioctl(fd,0,...) -- a command
   the driver rejects, inside code written to believe the tty was surrendered.
   Dropping the terminal is done the other way round, by setsid()/setpgrp()
   before the open that claims the new one. */
#define IEXTEN 0
#define tcgetattr(x,y) ioctl(x,TCGETA,y)
#define tcsetattr(x,y,z) ioctl(x,y,z)
int tcflow();
#define TCOOFF	0
#define TCOON	1
#define TCIOFF	2
#define TCION	3
#ifndef XTABS
#define XTABS TAB3
#endif
