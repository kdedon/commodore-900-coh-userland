/*
 * setsid() over setpgrp().  This kernel has no sessions: sys/drv/tty.c
 * claims a line as a process' controlling terminal when its process group
 * equals its own pid, which is exactly what setpgrp() arranges, so making a
 * process a group leader is the whole of what setsid() can mean here.
 *
 * <c900/c900.h> turns setsid() into nothing for the sources that include it;
 * the clients under src/clients do not include it, so the name has to exist
 * as a function as well.
 */

int
setsid()
{
	return (setpgrp());
}
