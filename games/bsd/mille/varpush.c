/*	$NetBSD: varpush.c,v 1.8 2004/01/27 20:30:30 jsm Exp $	*/

/*
 * Copyright (c) 1982, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


# include	<paths.h>
# include	"mille.h"

/*
 * @(#)varpush.c	1.1 (Berkeley) 4/1/82
 */

/*
 *	push variables around via the routine func() on the file
 * channel file.  func() is either read or write.
 */
/*
 *	Move the game state to or from a save file.
 *
 * Upstream hands the whole state to readv(2)/writev(2) as one iovec array.
 * Neither call exists here and neither does <sys/uio.h>, so the same table is
 * walked with plain read(2)/write(2) -- one call per field instead of one for
 * all of them, which produces a byte-identical file because a scatter/gather
 * transfer is defined to be the concatenation of its segments.  `func' is now
 * read or write itself, and readv/writev are gone from the callers with it.
 *
 * The field table stays in declaration order: the file has no self-description,
 * so a reordering silently invalidates every existing save file.
 */
struct field {
	char	*f_addr;
	int	f_len;
};

#define	NFIELDS	13

bool
varpush(file, func)
	int	file;
	int	(*func)();
{
	int		temp;
	int		i;
	static struct field vec[NFIELDS] = {
		{ (char *) &Debug, sizeof Debug },
		{ (char *) &Finished, sizeof Finished },
		{ (char *) &Order, sizeof Order },
		{ (char *) &End, sizeof End },
		{ (char *) &On_exit, sizeof On_exit },
		{ (char *) &Handstart, sizeof Handstart },
		{ (char *) &Numgos, sizeof Numgos },
		{ (char *) Numseen, sizeof Numseen },
		{ (char *) &Play, sizeof Play },
		{ (char *) &Window, sizeof Window },
		{ (char *) Deck, sizeof Deck },
		{ (char *) &Discard, sizeof Discard },
		{ (char *) Player, sizeof Player }
	};

	for (i = 0; i < NFIELDS; i++)
		if ((*func)(file, vec[i].f_addr, vec[i].f_len) < 0) {
			error(strerror(errno));
			return FALSE;
		}
	if (func == read) {
		if ((read(file, (char *) &temp, sizeof temp)) < 0) {
			error(strerror(errno));
			return FALSE;
		}
		Topcard = &Deck[temp];
#ifdef DEBUG
		if (Debug) {
			char	buf[80];
over:
			printf("Debug file:");
			gets(buf);
			if ((outf = fopen(buf, "w")) == NULL) {
				warn("%s", buf);
				goto over;
			}
			if (strcmp(buf, _PATH_DEVNULL) != 0)
				setbuf(outf, (char *)NULL);
		}
#endif
	} else {
		temp = Topcard - Deck;
		if ((write(file, (char *) &temp, sizeof temp)) < 0) {
			error(strerror(errno));
			return FALSE;
		}
	}
	return TRUE;
}
