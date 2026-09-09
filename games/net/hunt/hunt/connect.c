/*
 * Copyright (c) 1983-2003, Regents of the University of California.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are 
 * met:
 * 
 * + Redistributions of source code must retain the above copyright 
 *   notice, this list of conditions and the following disclaimer.
 * + Redistributions in binary form must reproduce the above copyright 
 *   notice, this list of conditions and the following disclaimer in the 
 *   documentation and/or other materials provided with the distribution.
 * + Neither the name of the University of California, San Francisco nor 
 *   the names of its contributors may be used to endorse or promote 
 *   products derived from this software without specific prior written 
 *   permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS 
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED 
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A 
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


# include	"hunt.h"
# include	<signal.h>
# include	<unistd.h>

void
do_connect(name, team, enter_status)
	char	*name;
	char	team;
	long	enter_status;
{
	static long	uid;
	static long	mode;
	/*
	 * The team byte is sent from a LOCAL, not from &team.  A char argument
	 * is promoted to int by the call, so the parameter occupies a 16-bit
	 * stack slot, and on this big-endian machine &team names the high half
	 * of it -- zero for every team letter.  huntd then reads '\0' as the
	 * team, so `team == " "' is false there and every player is identified
	 * with a NUL team.  A local char is a whole object.
	 */
	char		teamb;

	teamb = team;
	if (uid == 0)
		uid = htonl(getuid());
	(void) write(Socket, (char *) &uid, LONGLEN);
	(void) write(Socket, name, NAMELEN);
	(void) write(Socket, &teamb, 1);
	enter_status = htonl(enter_status);
	(void) write(Socket, (char *) &enter_status, LONGLEN);
	/*
	 * ttyname(3) returns NULL for anything that is not a terminal, and the
	 * original hands that straight to strcpy.  On BSD it is a latent crash
	 * nobody triggers because hunt has already refused to start unless fd 0
	 * is a tty; here it is one syscall away from happening, because our
	 * ttyname() also returns NULL when it cannot find the device's inode
	 * under /dev -- and it is STDERR being named, which isatty(0) said
	 * nothing about.  The driver only uses this string to label the player,
	 * so an unknown name costs nothing.
	 */
	{
		char *tn = ttyname(fileno(stderr));

		(void) strcpy(Buf, tn != NULL ? tn : "?");
	}
	(void) write(Socket, Buf, NAMELEN);
# ifdef INTERNET
	if (Send_message != NULL)
		mode = C_MESSAGE;
	else
# endif
# ifdef MONITOR
	if (Am_monitor)
		mode = C_MONITOR;
	else
# endif
		mode = C_PLAYER;
	mode = htonl(mode);
	(void) write(Socket, (char *) &mode, sizeof mode);
}
