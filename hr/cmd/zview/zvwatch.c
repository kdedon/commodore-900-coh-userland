/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * zvwatch -- restore the console after a zview crash.
 *
 * Inherit the server as the only child.  A zero exit means it has already
 * cleaned up.  Otherwise clear HR_MAGIC, wake clients while the driver
 * is available, unload /drv/hr and restore the text console.
 * A separate executable keeps the waiting process small.
 */
#include <signal.h>
#include <errno.h>
#include "shmem.h"

main()
{
	int w, st, pid, fd, i;
	static char msg[] =
	    "\033[E\007zview: server died -- screen and keyboard restored\r\n";

	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
	while ( (w = wait(&st)) < 0 )
		if ( errno != EINTR )
		{
			/* No child at all: we were run by hand, not exec'd
			 * by srvwatch.  There is nothing to guard; tearing
			 * the driver down now would kill a healthy desktop. */
			exit(0);
		}
	if ( st == 0 )
		exit(0);			/* quitwm(): already cleaned up */
	/* Post-mortem breadcrumb: the wait() status of the dead server, in
	 * ASCII, on the root (low byte = the signal that killed it, e.g.
	 * 11 = SIGSEGV).  A crash on real hardware leaves no other trace. */
	{
		char nb[8];
		register int n, v;

		v = st & 0xffff;
		n = sizeof(nb);
		nb[--n] = '\n';
		do
			nb[--n] = '0' + v % 10;
		while ( (v /= 10) != 0 && n > 0 );
		if ( (fd = creat("/wscrash", 0644)) >= 0 )
		{
			write(fd, nb + n, sizeof(nb) - n);
			close(fd);
		}
	}
	/* End the session for the surviving clients: dead magic, then wake
	 * everyone so they see it (see the header comment).  The magic word
	 * is plain user-mapped VRAM, so this needs no driver; the wakes do,
	 * and on the Ctrl-Alt-HELP path the driver is already gone -- then
	 * the open fails and the timer-driven clients still exit off the
	 * magic alone. */
	hr_glob()->magic = 0;
	if ( (fd = open("/dev/dmgr", 2)) >= 0 )
	{
		for ( i = 0; i < EVQ_N; i++ )
			ioctl(fd, CIOEVWAKE, (char *)i);
		close(fd);
	}
	if ( (pid = fork()) == 0 )
	{
		execl("/etc/uload", "uload", "/drv/hr", (char *)0);
		_exit(1);
	}
	while ( pid > 0 && (w = wait(&st)) != pid && w >= 0 )
		;
	/* hrtty's clear-screen is ESC [ E (see zview.c wdconsole): wipe the
	 * dead desktop off the framebuffer and prove the console is back. */
	if ( (fd = open("/dev/console", 1)) >= 0 )
	{
		write(fd, msg, sizeof(msg) - 1);
		close(fd);
	}
	exit(0);
}
