/*
 * config.h -- COHERENT 3.5 (the 3.2 source tree) / Z8001.
 *
 * Derived from the COHERENT config.h by Hal Snyder (hal@mwc.com), shipped with
 * screen_3.2 on the MWC BBS (autometer-ftp/mwcbbs/386/screen/screen.tar.Z),
 * which was itself based on Allen D. Ball's Interactive SysV 3.2 file.  Hal's
 * was for COHERENT 4.0 on the 386; the differences below are all things the
 * 3.2 Z8001 target does not have.
 *
 * COHERENT must be defined on the command line, not here: the sources test it
 * in their leading #include blocks, before config.h is read.
 */

#undef POSIX			/* no termios, no setsid, no sigaction	*/
#undef BSDJOBS			/* no job control, no TIOCSPGRP		*/
#define TERMIO			/* <termio.h>, struct termio, TCGETA	*/
#define TERMINFO		/* names the .screenrc `terminfo' verb	*/
#define SYSV			/* setpgrp(), killpg = kill(-pgrp)	*/
#undef SIGVOID			/* handlers return int (msig.h SIG_DFL)	*/
#undef DIRENT			/* no <dirent.h>, no opendir/readdir	*/
#define SUIDROOT
#undef UTMPOK			/* V7 utmp has no ut_type/ut_pid/ut_host */
#define LOGINDEFAULT	0
#undef GETUTENT
#undef UTHOST
#undef USRLIMIT
#undef LOCKPTY
#define NOREUID			/* no setreuid/setresuid		*/
#undef LOADAV
#undef  LOADAV_3DOUBLES
#undef  LOADAV_3LONGS
#undef  LOADAV_4LONGS
#undef GETTTYENT
#undef NFS_HACK
#define LOCALSOCKDIR

#ifdef LOCALSOCKDIR
# ifndef TMPTEST
#  define SOCKDIR "/tmp/screens"
# else
#  define SOCKDIR "/tmp/testscreens"
# endif
#endif
#undef USEBCOPY			/* screen.c supplies bcopy()		*/
#undef TOPSTAT
#undef USEVARARGS		/* Msg() is rewritten over printf's %r	*/
#define NAMEDPIPE		/* FIFOs, not AF_UNIX sockets		*/
#undef LOCK			/* no /etc/shadow, no vhangup; -x drops	*/
#undef PASSWORD
#define COPY_PASTE
#undef REMOTE_DETACH
#undef POW_DETACH
#undef NETHACK
#define ETCSCREENRC "/etc/screenrc"
#define NEEDSETENV		/* no putenv/setenv/unsetenv in libc; putenv.c */

/*
 * Small-memory settings.  MAXPATH sizes `struct msg', the fixed record the
 * attacher and the backend pass over the FIFO, four times over; at BSD's 1024
 * that union alone is 2 KB of a 64K data segment and every path in it is a
 * Coherent pathname of at most a few dozen bytes.  MAXWIN stays at screen's
 * 10 -- it only sizes wtab[], and the real ceiling is NUPTY (4) in
 * sys/drv/pty.c, which OpenPTY() reports as it hits it.
 */
#define MAXPATH		128

/*
 * screen's own prototype wrapper.  cc0 is strict K&R: no prototypes at all.
 */
#define __P(a) ()

/*
 * screen means by `sig_t' the return type of a signal handler.  COHERENT's
 * <sys/types.h> already has a sig_t and means something else entirely by it --
 * a long bit MASK of signals, as used by struct proc -- so screen.h's typedef
 * collides.  Take screen's name away from it: handlers return int here, since
 * <sys/msig.h> spells SIG_DFL `(int(*)())0'.
 *
 * pid_t/uid_t/gid_t are NOT in <sys/types.h> on 3.2, so screen.h's own COHERENT
 * typedefs of those are wanted and PID_T_DEFINED must stay off.
 */
#define SIG_T_DEFINED
#define sig_t	int
#undef PID_T_DEFINED

/*
 * There is no SIGCLD/SIGCHLD: <sys/msig.h> stops at NSIG 16 and the kernel never
 * signals a parent when a child dies.  Nor is there a non-blocking wait --
 * uwait() in sys/coh/sys1.c takes no flags -- so DoWait() cannot be driven by a
 * handler at all.  screen.c's CheckWindows() takes over: kill(wpid, 0) answers
 * ESRCH once the child is an unreaped zombie, which is the one thing that does
 * change, since this pty driver reports a hangup neither through poll() nor
 * through a non-blocking master read.
 */
#define NOSIGCLD
#define SETPGID_DECLARED
#define MEMFUNCS_DECLARED	/* <string.h>/<memory.h> declare them	*/
#define WAITSTUFF_DECLARED	/* no wait3()/waitpid(); see NOSIGCLD	*/
#define CRYPT_DECLARED
#define REUID_DECLARED
