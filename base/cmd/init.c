/*
 * Copyright (c) 1977-1995 Robert Swartz.
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/* Initialization program for the user level of the OS--this is the
 * first thing that gets run: pid 1.
 *
 * Runs /etc/brc.  If it fails spawns a /bin/sh on the console (this is
 * single user mode.)  If /etc/brc exits cleanly it starts multi user
 * mode by running /etc/rc, then it spawns gettys on those terminals in
 * /etc/ttys.
 *
 * In multi user mode it respawns gettys as needed, removes tty locks
 * when a getty dies, and wait()s for orphans.
 *
 * Accepts two signals:  SIG_HUP and SIG_QUIT.
 * SIG_HUP causes init to kill (SIG_KILL) all processes and go to single
 * user mode.  SIG_QUIT causes /etc/ttys to be reread.
 * All other signals are ignored.
 */
#define NEWTTYS	1
/* No swapper is exec'd: the kernel swaps for itself, so argv[1], the swapper
 * slot, carries the loader's boot flags instead (see `single' below).  Drivers
 * ARE loaded -- argv[2..] name the loadable console the boot icode selected. */
#define	NOSWAPPER
#define	DEBUG	0

/*
 * Init
 *
 * Compile -s -n -i on machines other than pdp11.
 */

#include <sys/dir.h>
#include <sys/stat.h>
#include <signal.h>
#include <utmp.h>
#include <sgtty.h>
#include <errno.h>
#include <sys/malloc.h>
#include <access.h>

extern char *strrchr();

/*
 * Miscellaneous constants.
 */
#define	NULL	((char *)0)
#define	BRCFILE	"/etc/brc"

/* What the console says about a read-only root, and what to do about it. */
#define	ROMSG	"\nWARNING: Root file system was not unmounted cleanly.\nRepair it with 'check -s' on the root block device, then 'mount -w'.\n"
#define	ROSTILL	"\nRoot is still read-only.\nRepair it with 'check -s' on the root block device, then 'mount -w'.\n"
#define	CHECK	"/bin/check"
#define	MOUNT	"/etc/mount"

#if	DEBUG
#define	dbmsg(arglist)	msg arglist
int debug_fd = -1;
#else
#define	dbmsg(arglist)
#endif

/*
 * Structure containing information about each terminal.
 */
typedef struct	tty {
	struct	tty	*t_next;	/* Pointer to next entry */
	int	t_pid;			/* Process id */
	char	t_flag;			/* 0 == no getty, 1 == want getty */
	char	t_linetype;		/* Line type (local, remote, etc.) */
	char	t_baud[2];		/* Baud descriptor */
	char	t_tty[5+DIRSIZ+1];	/* tty name */
} TTY;

/* Console tty for simplified spawn */
TTY contty = {
	NULL, 0, 0, 'l', "P", "/dev/console"
};

/* Null tty for simplified spawn */
TTY nultty = {
	NULL, 0, 0, 'l', "P", "/dev/null"
};

/*
 * STAY single user: the boot loader was asked for it and said so.
 *
 * The kernel passes it in argv[1] -- the swapper slot, which this build ignores
 * (NOSWAPPER) and which is therefore the one argument already in the vector
 * with nothing in it.  Using it means argc does not change, so the driver loop
 * below still sees exactly the arguments it always did; a fourth argument would
 * have been handed to loaddrv() by every older init on any older image.
 *
 * WHAT IT CHANGES is only what happens when the single-user shell EXITS.  Every
 * boot already offers that shell (there is no /etc/brc), and normally leaving it
 * runs /etc/rc and brings the machine up multi user.  With the flag, leaving it
 * gives another one -- /etc/rc never runs, no getty is ever spawned -- because
 * the reason somebody asked for single user from a boot menu is that /etc/rc,
 * or something it starts, is what is broken.
 */
int	single;				/* -s from the loader: stay single user */

/*
 * Root came up READ-ONLY.  Stamping /etc/boottime doubles as the probe: the
 * kernel mounts a root it found dirty read-only, so the creat below fails, and
 * the stamp is deferred until root is made writable.
 *
 * A read-only root is not a state to run a session on top of -- /etc/rc writes
 * (utmp, /tmp, the lock directories) and every one of those writes fails
 * silently, leaving a machine that looks multi user and is not.  So the flag
 * holds init in the single-user shell, marks the prompt, and says so on the
 * console, until `check -s' plus a remount (or a reboot) makes root writable.
 */
int	rootro;				/* root file system is read-only */

/* The block device root is mounted on, "/dev/" + the name findroot() matched. */
char	rootblk[6+DIRSIZ] = "/dev/";

/*
 * Announce the single-user shell on the console: the Ctrl+D hint, wording
 * matching Coherent 0.8 so the two systems read the same.  Without it the
 * bare `#' prompt is the only thing on the screen and nothing says how to
 * continue booting.  The release is the KERNEL's to announce and it does, in
 * the boot banner: a release named twice is a release that can disagree with
 * itself, and one of the two must then be built from something other than the
 * kernel the machine is running.
 *
 * Written straight to /dev/console: init has no stdio, and msg() exists only
 * under DEBUG (and prefixes every line with `:').
 */
sumsg()
{
	register int	fd;

	if ((fd = open("/dev/console", 1)) < 0)
		return;
	if (single)
		/* Say WHY, and say what will not happen.  Without it the only
		 * difference an operator can see between this and an ordinary
		 * boot is that Ctrl+D does not do what the line above it says
		 * -- which reads as a broken system, on a machine they came to
		 * this shell to fix. */
		write(fd, "\nSingle user requested by the boot loader: /etc/rc will not run\n",
		      strlen("\nSingle user requested by the boot loader: /etc/rc will not run\n"));
	else
		write(fd, "\nSingle user shell; hit Ctrl+D to go multi user\n",
		      strlen("\nSingle user shell; hit Ctrl+D to go multi user\n"));
	if (rootro)
		write(fd, ROMSG, strlen(ROMSG));
	close(fd);
}

/*
 * One line on the console, for the callers that are not sumsg().  init has no
 * stdio; opening the device per message costs nothing at the rate these are
 * written and keeps the descriptor out of the shell's way.
 */
comsg(s)
char	*s;
{
	register int	fd;

	if ((fd = open("/dev/console", 1)) < 0)
		return;
	write(fd, s, strlen(s));
	close(fd);
}

/*
 * Default environment list for shell.
 *
 * HOME is root's directory from /etc/passwd, which is what a login on any
 * other line ends up with as well (cmd/login/login.c fills its own copy in
 * from the password entry).  The single-user shell is spawned with argv[0]
 * "-sh", so it reads $HOME/.profile -- and /.profile is where TERM is chosen,
 * from the console driver name /etc/load recorded in /etc/console.  A HOME
 * naming any other directory leaves that shell with no TERM at all, because
 * neither /etc/profile nor /etc/.profile exists.
 */
char	*defenv0[] = {		/* Default environment for super user */
	"USER=root",
	"HOME=/",
	"PATH=/bin:/usr/bin:/etc:",
	"PS1=# ",
	NULL
};

/*
 * Variables.
 */
static char _version[]="init version 4.0";
struct	tty *ttyp;			/* Terminal list */
int	hangflag;			/* Go to single user */
int	quitflag;			/* Scan tty file */

/*
 * Functions.
 */
extern	int	sighang();
extern	int	sigquit();
extern	int	sigalrm();
extern	struct	tty *findtty();


main(argc, argv) register int argc; char *argv[];
{
	register TTY *tp;
	register int i, n, multi;
	int started;
	unsigned status;

dbmsg(("Entering init  ", NULL));

	/* Multi user is where a boot goes.  The three things that hold it in
	 * the single-user shell instead each clear this below: an /etc/brc
	 * that failed, the loader's -s, and a root that could not be
	 * repaired. */
	multi = 1;
	started = 0;	/* /etc/rc has not been run for this session */
	/* Make sure that every flag has a usable initial value.  */
	quitflag = 0;	/* Do not rescan /etc/ttys.  */
	hangflag = 0;	/* Do not shut down.  */

	if (getpid() != 1)
		exit(1);
	umask(022);
dbmsg(("About to fakearg  ", NULL));
	/* argv[1] before fakearg(): the loader's boot flags, as a `-' word.
	 * Read as characters rather than compared as a string so that a second
	 * flag is a character rather than a new argument -- the vector is fixed
	 * in the kernel's icode (sys/z8001/src/md.s) and its length is not a
	 * thing that can grow.  An unknown letter is IGNORED: it comes from a
	 * loader newer than this init, and refusing to boot over a flag we do
	 * not recognise would make every future flag a flag day. */
	if (argc >= 2 && argv[1][0] == '-')
		for (n = 1; argv[1][n] != '\0'; n++)
			if (argv[1][n] == 's')
				single = 1;
	fakearg(0, argv);
	if ((n = creat("/etc/boottime", 0644)) >= 0) {
		close(n);
		rootro = 0;
	} else
		rootro = 1;
dbmsg(("CREATED boottime ", NULL));
#ifndef NOSWAPPER
	if (argc >= 2)
		loadswp(argv[1]);
#endif
#ifndef NODRIVERS
	for (n=2; n<argc; n++)
		loaddrv(argv[n]);
#endif
dbmsg(("About to putwtmp  ", NULL));
	putwtmp("~", "");

	/*
	 * Ignore all possible signals.  We do not want to be able to
	 * accidentally kill init.
	 */
	for (i=1; i<=NSIG; i++)
		signal(i, SIG_IGN);

	/*
	 * We MUST NOT ignore SIGCHLD--we care deeply about our children.
	 */

#if _I386
dbmsg(("About to default for SIGCHLD  ", NULL));
	signal(SIGCHLD, SIG_DFL);
#endif

dbmsg(("About to trap for SIGHUP  ", NULL));
	signal(SIGHUP, sighang);

dbmsg(("About to fork()  ", NULL));
	if (fork() == 0) {			/* paranoid sync */
		sync();
		exit(0);
	}
	/*
	 * Root came up read-only because it was dirty.  Repair it in place and
	 * remount it read/write, so an unclean shutdown costs a boot and not an
	 * operator; only a repair that fails holds the machine in the shell.
	 */
	if (rootro)
		rootro = fixroot();
dbmsg(("About to access brc file  ", NULL));
	if (access(BRCFILE, AEXISTS) == 0) {
		dbmsg(("executing /etc/brc", NULL));
		n = spawn(&contty, "/bin/sh", "sh", BRCFILE, NULL);
		while (wait(&status) != n)
			;
		multi = (status == 0);
	}
	/* The loader has the last word.  A machine told to come up single user
	 * must do so even where /etc/brc succeeded, because that is the whole
	 * request: the operator is standing in front of a system they already
	 * know does not come up. */
	if (single || rootro)
		multi = 0;

	/*
	 * The single user shell and /etc/rc belong to the start of a session,
	 * not to the state of /etc/ttys: a system whose ttys file enables no
	 * line at all (the console is handed to a program started from
	 * /etc/rc, say) is a running multi-user system with nothing to getty,
	 * and must sit in the orphan wait below rather than lay a second
	 * shell over whatever holds the console.
	 */
	for (;;) {
		while (started == 0) {
			if (!multi) {
				/* Single user - no multi-user ttys active */
				/* No rescan signals accepted */
				signal(SIGQUIT, SIG_IGN);
				/* Wait for things to quiet down */
				/* Necessitated by system shared segment bug */
				/* But don't wait for hung processes */
				signal(SIGALRM, sigalrm);
				alarm(2);
				while (wait(NULL) >= 0)
					;
				alarm(0);

				dbmsg(("sync()ing", NULL));
				sync();	/* Obviates need for user sync.  */
				/* Initiate single user state */
				dbmsg(("spawn single user shell", NULL));
				defenv0[3] = rootro ? "PS1=(ro)# "
						    : "PS1=# ";
				sumsg();
				n = spawn(&contty, "/bin/sh", "-sh", NULL);
				/* Wait for shell to exit */
				if (waitc(n) < 0) {
					hangflag = 0;
					kill9(-1);
					continue;
				}
				/* Asked for single user: give the shell back
				 * rather than falling through to /etc/rc.
				 * `continue' re-enters this same loop, so a
				 * shell that is exited (or that dies) is
				 * replaced, which is what makes the console
				 * on a broken machine unloseable. */
				if (single)
					continue;
			}
			/*
			 * Root must be writable before /etc/rc runs.  Retry
			 * the stamp: `mount -w' from the single-user shell is
			 * the ordinary way out, and it leaves nothing else to
			 * observe.  Still read-only, drop back to that shell
			 * rather than lay a session over a file system that
			 * cannot record it -- multi is cleared as well, or a
			 * /etc/brc that already asked for multi user would
			 * spin here with no shell to fix it from.
			 */
			if (rootro) {
				if ((n = creat("/etc/boottime", 0644)) >= 0) {
					close(n);
					rootro = 0;
				} else {
					comsg(ROSTILL);
					multi = 0;
					continue;
				}
			}
			/* Start multi-user state */
			dbmsg(("executing /etc/rc", NULL));
			n = spawn(&nultty, "/bin/sh", "sh", "/etc/rc", NULL);
			if (waitc(n) < 0) {
				hangflag = 0;
				kill9(-1);
				continue;
			}
			/* Scan the ttys file */
			scantty(); /* scantty() may reset quitflag */
			/* Prepare for rescan ttys signal */
			quitflag = 0;
			signal(SIGQUIT, sigquit);
			started = 1;
		}
		/* Service the terminals /etc/ttys enables, if any */
		inittys();
		/* Wait for orphaned processes */
		n = wait(&status);
		if (hangflag) {
			/* Return to single user */
			dbmsg(("going single user", NULL));
			hangflag = 0;
			started = 0;
			kill9(-1);
			for (tp=ttyp; tp!=NULL; tp=tp->t_next)  {
				tp->t_pid = 0;
				tp->t_flag = 0;
			}
			if (fork() == 0) {		/* paranoid sync */
				sync();
				exit(0);
			}
			multi = 0;
			continue;
		}
		if (quitflag) {
			/* Scan for ttys with changes in status */
			dbmsg(("quit signal, rescan ttys", NULL));
			quitflag = 0;
			scantty();
			continue;
		}
		if (n < 0 && errno == ECHILD && inittys() == 0) {
			/*
			 * No child is left to wait for and no line is enabled,
			 * so nothing can reach the machine: start the session
			 * again from the single user shell.
			 */
			multi = 0;
			started = 0;
			continue;
		}
		if (n > 0) {
			/* Logout process n. */
			for (tp=ttyp; tp; tp=tp->t_next) {
				if (n != tp->t_pid)
					continue;
				tp->t_pid = 0;
				dbmsg(("logout process on tty ", tp->t_tty, NULL));
				putwtmp(&tp->t_tty[5], "");
				clrutmp(&tp->t_tty[5]);
				chmod(tp->t_tty, 0700);
				chown(tp->t_tty, 0, 0);

				/*
				 * Unlock the tty; it was locked by 
				 * process n: originally login.
				 *
				 * We don't care about the return value;
				 * if it was locked by somebody else,
				 * locknrm() won't remove the lock.
				 */
 				unlockntty(strrchr(tp->t_tty, '/')+1, n);
				/* See if we panicked */
				if ((status>>8) == 0377)
					tp->t_flag = 0;
			}
		}
	}
}
/*
 * Called when we get a hangup.
 */
sighang()
{
	signal(SIGHUP, sighang);
	hangflag = 1;
}

/*
 * Called when a quit is received.
 */
sigquit()
{
	signal(SIGQUIT, sigquit);
	quitflag = 1;
}

/*
 * Called when an alarm is received in single user mode.
 */
sigalrm()
{
}

/*
 * Called when an alarm is received in multi user mode.
 */
mulsigalrm()
{
	kill(1, SIGQUIT);	/* Cause a rescan of /etc/ttys.  */
}

#ifndef NOSWAPPER
/*
 * Load the swapper.
 */
loadswp(np)
char *np;
{
	if (np[0] == '\0')
		return;
	if (fork() != 0)
		return;
	execve(np, NULL, NULL);
	panic("cannot load ", np);
}
#endif

#ifndef NODRIVERS
/*
 * Load the given driver.
 */
loaddrv(np)
char *np;
{
	register int pid;
	int status;

	/* /dev/null, not the console: the console IS the driver being loaded,
	 * so it cannot be opened until this succeeds. */
	pid = spawn(&nultty, "/etc/load", "load", np, NULL);
	while (wait(&status) != pid)
		;
	if (status != 0)
		exit(status);
}
#endif

/*
 * Find the block device the root file system is mounted on, by matching each
 * /dev entry's st_rdev against st_dev of "/" -- so the right device is
 * repaired whether root is a hard disk or a floppy.  Fills rootblk with
 * "/dev/<name>".  Returns 0 on success.
 */
findroot()
{
	register int fd;
	struct stat rs, ds;
	struct direct dir;

	if (stat("/", &rs) < 0)
		return (-1);
	if ((fd = open("/dev", 0)) < 0)
		return (-1);
	while (read(fd, (char *)&dir, sizeof(dir)) == sizeof(dir)) {
		if (dir.d_ino == 0)
			continue;
		strncpy(&rootblk[5], dir.d_name, DIRSIZ);
		rootblk[5+DIRSIZ] = '\0';
		if (stat(rootblk, &ds) < 0)
			continue;
		if ((ds.st_mode&S_IFMT) == S_IFBLK && ds.st_rdev == rs.st_dev) {
			close(fd);
			return (0);
		}
	}
	close(fd);
	return (-1);
}

/*
 * Root came up read only: it was not unmounted cleanly (the kernel found
 * s_dirty set).  Repair it in place, with no operator: the file system is
 * quiescent -- nothing can be open for writing on a read-only root -- so run
 * `check -s' on the BLOCK device (exactly what `mount -c' does; the raw device
 * refuses libfs's non-sector-sized reads with "non-aligned dma"), then ask the
 * kernel to remount root read/write (`mount -w' -> fsremount, which drops
 * stale cached blocks, re-reads the repaired super block, and refuses while
 * the file system is still dirty).  check exits 0 (clean) or 1 (errors found
 * and fixed); anything else -- or a check that died -- leaves root alone.
 *
 * check and mount are separate distribution components from this one, so both
 * are tested for before either is spawned: spawn() panics on a failed exec,
 * and a panicking init is a machine that does not boot at all.
 *
 * Returns 0 when root is writable; 1 falls into the single-user shell.
 */
fixroot()
{
	register int pid;
	register int n;
	unsigned status;

	if (access(CHECK, AEXEC) != 0 || access(MOUNT, AEXEC) != 0) {
		comsg("Automatic repair needs /bin/check and /etc/mount\n");
		return (1);
	}
	if (findroot() != 0) {
		comsg("Cannot find the root device in /dev\n");
		return (1);
	}
	comsg("The root file system was not unmounted cleanly, repairing\n");
	if ((pid = spawn(&contty, CHECK, "check", "-s", rootblk, NULL)) < 0) {
		comsg("Automatic repair failed\n");
		return (1);
	}
	while ((n = wait(&status)) >= 0 && n != pid)
		;
	if (n != pid || (status&0377) != 0 || ((status>>8)&0377) > 1) {
		comsg("Automatic repair failed\n");
		return (1);
	}
	if ((pid = spawn(&contty, MOUNT, "mount", rootblk, "/", "-w", NULL)) >= 0)
		waitc(pid);
	if ((n = creat("/etc/boottime", 0644)) >= 0) {
		close(n);
		comsg("Root file system repaired\n");
		return (0);
	}
	comsg("The root file system is still read only\n");
	return (1);
}

/*
 * If there are any terminals which need to be serviced, service them.
 * Return the number of active terminals.
*/
inittys()
{
	register TTY *tp;
	register int n;

	n = 0;
	for (tp=ttyp; tp!=NULL; tp=tp->t_next) {
		if (tp->t_pid == 0 && tp->t_flag != 0)
			login(tp);
		n += tp->t_flag;
	}
	return (n);
}

/*
 * Wait for the given process to complete.
 */
waitc(p1)
register int p1;
{
	register int p2;

	while ((p2=wait(NULL))>=0 && p2!=p1)
		;
	return (p2);
}

/*
 * Scan the tty file.
 */
scantty()
{
	register TTY *tp;	/* Used to pick entries from ttyp.  */
	register int fd;	/* File descriptor for /etc/ttys.  */
	TTY tty;		/* Used to hold entries from /etc/ttys.  */

	extern char *sbrk();

	dbmsg(("Rescan", NULL));

	unlockntty("console", 0);	/* Wipe out locks on the console.  */
	
	if ((fd=open("/etc/ttys", 0)) < 0)
		return;
	while (readtty(&tty, fd) != 0) {
		/* If there is no record of this tty, create one.  */
		if ((tp=findtty(&tty)) == NULL) {
			if ((tp = sbrk(sizeof(*tp))) == BADSBRK)
				panic("too many ttys");
			*tp = tty;
			tp->t_next = ttyp;
			ttyp = tp;
			continue;
		}

		/*
		 * If /etc/ttys has changed for this tty,
		 * adjust the in-memory version to the desired state.
		 */
		if (tp->t_flag != tty.t_flag
		 || tp->t_baud[0] != tty.t_baud[0]
		 || tp->t_linetype != tty.t_linetype) {
			/*
			 * If this tty is locked, and we want to start a
			 * getty, do not do it until the lock goes away.
			 *
			 * Ignore locks on the console.
			 */
			if ((0 != strcmp(tty.t_tty, "/dev/console")) &&
			    lockttyexist(strrchr(tty.t_tty, '/')+1) &&
			    0 != tty.t_flag) {
				dbmsg(("Setting an alarm", NULL));
				/* Check again in a few seconds.  */
				signal(SIGALRM, mulsigalrm);
				alarm(10);
			} else {
				dbmsg(("Starting getty", NULL));
				tp->t_flag = tty.t_flag;
				tp->t_baud[0] = tty.t_baud[0];
					tp->t_linetype = tty.t_linetype;
				/* Kill off any process lingering on this tty.  */
				if (tp->t_pid != 0)
					kill9(tp->t_pid);
			}
		}
	}
	close(fd);
}

/*
 * Read a line from the terminal file and save the appropriate fields in
 * the terminal structure.
 */
readtty(tp, fd)
register TTY *tp;
{
	register char *lp;
	char c[1];
	char line[3+DIRSIZ+1];

	lp = line;
	for (;;) {
		if (read(fd, c, sizeof(c)) != sizeof(c))
			return (0);
		if (c[0] == '\n')
			break;
		if (lp < &line[3+DIRSIZ])
			*lp++ = c[0];
	}
	*lp++ = '\0';
	if (lp < &line[5])
		return (0);
	lp = line;
	tp->t_flag = (*lp++) != '0';
#if	NEWTTYS
	tp->t_linetype = *lp++;
#else
	tp->t_linetype = 'l';
#endif
	tp->t_pid = 0;
	tp->t_baud[0] = *lp++;
	tp->t_baud[1] = '\0';
	strcpy(tp->t_tty, "/dev/");
	strncpy(&tp->t_tty[5], lp, DIRSIZ);
	tp->t_tty[5+DIRSIZ] = '\0';
	dbmsg(("readtty: ", tp->t_tty, NULL));
	return (1);
}

/*
 * Given a terminal structure containing the name of a terminal,
 * find the entry in the terminal list.
 */
TTY *
findtty(tp1)
register TTY *tp1;
{
	register TTY *tp2;

	for (tp2=ttyp; tp2!=NULL; tp2=tp2->t_next)
		if (strcmp(tp1->t_tty, tp2->t_tty) == 0)
			return (tp2);
	return (NULL);
}

/*
 * Given a terminal structure, spawn off a login (getty).
 */
login(tp)
register TTY *tp;
{
	register int pid;

	pid = spawn(tp,
		"/etc/getty",
		(tp->t_linetype == 'l') ? "-" : "-r",
		tp->t_baud,
		NULL);
	if (pid < 0) {
		tp->t_flag = 0;
		dbmsg(("spawn failed tty ", tp->t_tty, NULL));
	} else
		tp->t_pid = pid;
}

/*
 * Spawn off a command.
 */
spawn(tp, np, ap)
TTY *tp;
char *np, *ap;
{
	register int pid;
	register int fd;
	int i;

	if ((pid=fork()) != 0)
		return (pid);
#if DEBUG
	close(debug_fd);
#endif
	setpgrp();
	fakearg(1, tp->t_tty);
	while ((fd=open(tp->t_tty, 2)) < 0 && errno==EDBUSY)
		sleep(1);
	if (fd < 0)
		panic("cannot open ", tp->t_tty, NULL);
	ioctl(fd, TIOCSETG);
#if	NEWTTYS
	if (tp->t_linetype == 'r')      /* remote line? */
	   ioctl(fd, TIOCHPCL);   /* "hangup" on last close */
#endif
	dup2(0, 1);
	dup2(0, 2);

	/* Restore all signals for any child process.  */
	for (i=1; i<=NSIG; i++)
		signal(i, SIG_DFL);

	execve(np, &ap, defenv0);
	panic("cannot execute ", np, NULL);
	return (pid);
}

/*
 * Write an entry onto the wtmp file.
 */
putwtmp(lp, np) char *lp, *np;
{
	register int fd;
	struct utmp utmp;
	extern time_t time();

	if ((fd=open("/usr/adm/wtmp", 1)) < 0)
		return;
	strncpy(utmp.ut_line, lp, 8);
	strncpy(utmp.ut_name, np, DIRSIZ);
	utmp.ut_time = time(NULL);
	lseek(fd, 0L, 2);
	write(fd, (char *)&utmp, sizeof(utmp));
	close(fd);
}

/*
 * Clear out a utmp entry.
 */
clrutmp(tty)
char *tty;
{
	register int fd;
	struct utmp utmp;
	static struct utmp ctmp;

	if ((fd=open("/etc/utmp", 2)) < 0)
		return;
	while (read(fd, &utmp, sizeof(utmp)) == sizeof(utmp)) 
	{  if (strncmp(utmp.ut_line, tty, 8))  /* no match? */
		 continue;		    /* yes, go for next record */
	   lseek(fd, (long)-sizeof(utmp), 1);
	   write(fd, &ctmp, sizeof(ctmp));   /* clear utmp record */
	   break;
	}
	close(fd);
}

/*
 * Print out a list of error messages and exit.
 */
panic(cp)
char *cp;
{
	register char **cpp;

	close(0);
	open("/dev/console", 2);
	write(0, "/etc/init: ", 11);
	for (cpp=&cp; *cpp!=NULL; cpp++)
		write(0, *cpp, strlen(*cpp));
	write(0, "\n", 1);
	exit(0377);
}

/*
 * Make the arg listing of ps come out right.
 *	f == 0, first entry, determine buffer limits.
 *	In this case, name the forks of init -tty until the
 *	tty opens.
 *	f != 0, later entry, fill buffer with lies.
 */
fakearg(f, argv)
int f;
char **argv;
{
	static char *fbuf;
	static int nbuf;
	register int n;
	register char *p;

	if (f == 0) {
		fbuf = argv[0];
		nbuf = 0;
		while (argv[1] != NULL)
			argv += 1;
		nbuf = argv[0] - fbuf + strlen(argv[0]) - 1;
	} else {
		if (fbuf == NULL || nbuf == 0)
			return;
		p = (char *)argv;
		p += 5;			/* tty part of /dev/tty* */
		n = 1;
		*fbuf++ = '-';
		do
			*fbuf++ = *p;
		while (++n < nbuf && *p++ != 0);
		*fbuf = 01;		/* non-ascii terminator */
	}
}

/*
 * Send SIGKILL to process, delaying and sending twice to ensure death.
 */
kill9(pid) register int pid;
{
	kill(pid, SIGKILL);
	sleep(1);
	kill(pid, SIGKILL);
}

#if	DEBUG
#define SCREEN_ADDR	0xb0000L	/* Physical address of screen.
					 * Use 0xb8000 for color screen.
					 */
#define SCREEN_SIZE	(80*25*2)	/* Size of screen in bytes.  */

/*
 * Write a debug message to the console.
 * The args should be a NULL-terminated list of strings.
 */
msg(cp) char *cp;
{
	register char **cpp;
#if 0
/* Old init couldn't write to console because it messed up process groups. */
	int fd;
	static long mp = SCREEN_ADDR;
	int i;

	if (mp >= SCREEN_ADDR + SCREEN_SIZE)
		mp = SCREEN_ADDR;
	fd = open("/dev/mem", 2);
	lseek(fd, mp, 0);
	write(fd, ":", 1);
	lseek(fd, 1L, 1);
	mp += 2;
	for (cpp=&cp; *cpp!=NULL; cpp++) {
		for (i = 0; i < strlen(*cpp); i++) {
			write(fd, (*cpp)+i, 1);
			lseek(fd, 1L, 1);
			mp += 2;
		}
		write(fd, " ", 1);
		lseek(fd, 1L, 1);
		mp += 2;
	}	
	close(fd);
#else
	if (debug_fd == -1)
		debug_fd = open("/dev/console", 2);
	write(debug_fd, ":", 1);
	for (cpp=&cp; *cpp!=NULL; cpp++) {
		write(debug_fd, *cpp, strlen(cp));
		write(debug_fd, " ", 1);
	}	
#endif
}
#endif
