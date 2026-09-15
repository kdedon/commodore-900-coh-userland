/*
 * c900.h -- COHERENT 3.2 / Z8001 (Commodore 900) emulation for the generic
 * MGR 0.69 sources, in the manner of upstream MGR's own shims for
 * COHERENT 4.0.  Pulled in from <mgr/bitblit.h> and, for the few server
 * files that do not include it, by hand.  Everything here supplies a name
 * this system lacks; nothing here changes behaviour that already works.
 */
#ifndef C900_H
#define C900_H

/* K&R cc0 has no prototypes, so every declaration that MGR wraps in
   _PROTOTYPE() degrades to () by itself.  These are the plain gaps. */

typedef int pid_t;			/* absent from <sys/types.h> */
typedef unsigned short uid_t;
typedef unsigned short gid_t;

#include <path.h>
#ifndef _POSIX_PATH_MAX
#define _POSIX_PATH_MAX MAXPATH		/* <limits.h> has no PATH_MAX */
#endif

#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif

/*
 * Signals.  This kernel has the twelve V7 signals only (<signal.h>:60-74):
 * HUP INT QUIT ALRM TERM REST SYS PIPE KILL TRAP SEGV, NSIG 16.  MGR
 * switches over the whole SysV set, so the absent names get distinct
 * numbers ABOVE NSIG: the `for (i=0;i<NSIG;i++)' arming loop never reaches
 * them, each switch case stays unique, and a stray signal()/kill() on one
 * fails harmlessly with EINVAL instead of hitting signal 0.
 */
#ifndef SIGCHLD
#define SIGCHLD  996	/* absent: dead windows arrive as pty POLLHUP/EIO */
#endif
#ifndef SIGCONT
#define SIGCONT  995
#endif
#ifndef SIGTTIN
#define SIGTTIN  994
#endif
#ifndef SIGTTOU
#define SIGTTOU  993
#endif
#ifndef SIGUSR1
#define SIGUSR1  992
#endif
#ifndef SIGUSR2
#define SIGUSR2  991
#endif
#ifndef SIGILL
#define SIGILL   990
#endif
#ifndef SIGIOT
#define SIGIOT   989
#endif

/*
 * <signal.h> declares signal() and defines SIG_ERR only under _I386, so on
 * this machine `handler = signal(...)' truncates a 32-bit function pointer
 * through a defaulted int return.
 */
extern void (*signal())();
#ifndef SIG_ERR
#define SIG_ERR ((int (*)())-1)
#endif

/*
 * Waiting.  uwait() takes no flags: there is no waitpid, wait3 or WNOHANG, and
 * no SIGCHLD either, so nothing here is told that a child has died.  A window
 * whose program is gone is recognised from its pty instead (mgr.c: EIO on the
 * master is the slave's last close) and reaped in destroy(), and the reaper is
 * src/libc900/waitnohang.c -- wait(2) under the alarm clock, which answers 0
 * for `nothing ready' as WNOHANG does.  wait3()'s flags and rusage are
 * discarded because that is all the one caller passes.
 */
#ifndef WNOHANG
#define WNOHANG 1
#endif
extern int waitnohang();
#define wait3(statusp, flags, rusage) waitnohang(statusp)

/*
 * Terminal window size.  No TIOCGWINSZ/TIOCSWINSZ and no SIGWINCH, so a
 * window resize cannot be told to the program inside it; the struct exists
 * only so put_window.c compiles and the ioctl fails with EINVAL.
 */
struct winsize {
	unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel;
};
#ifndef TIOCSWINSZ
#define TIOCSWINSZ (('t'<<8)|103)
#endif
#ifndef TIOCGWINSZ
#define TIOCGWINSZ (('t'<<8)|104)
#endif

/*
 * BSD spellings.  Only names that src/mgr/proto.h does NOT declare as a
 * function may be macros here -- a function-like macro over a _PROTOTYPE()
 * declaration is an arity clash in cc0.  fchmod, fchown, getdtablesize,
 * gethostname, initgroups, killpg, setregid, setreuid, vfork and strpbrk
 * are real functions in src/libc900/ for that reason.
 */
#define bzero(x,y)	memset(x,0,y)
#define bcmp(x,y,z)	memcmp(x,y,z)
#define bcopy(x,y,z)	memcpy(y,x,z)
#define random		rand
#define srandom		srand

/*
 * setsid() is a real function in src/libc900/ -- it must be, because the
 * caller's whole purpose is the process-group change that lets the following
 * open() claim a controlling terminal (sys/drv/tty.c ttsetgrp(), which acts
 * only when p_group == p_pid).  There is no ftruncate(2) on this system at
 * all; the one caller truncates by re-creating the file by name.
 */

#endif	/* C900_H */
