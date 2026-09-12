/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*	$NetBSD: instr.c,v 1.11 2005/02/15 12:56:20 jsm Exp $	*/

/*-
 * Copyright (c) 1990, 1993
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



#include <stdio.h>

#include "deck.h"
#include "cribbage.h"

/* Upstream generates this path into a pathnames.h. */
#define	_PATH_INSTR	"/usr/games/lib/cribbage.instr"

/*
 * Print the rules.
 *
 * Upstream vforks a child that dup2()s the file onto stdin and execs $PAGER
 * through sh -c, then reaps it with waitpid() and EXITS THE GAME if the pager
 * returned non-zero.  None of that survives here: there is no vfork(2), no
 * waitpid(2) and no pager in the base system, and losing the game because
 * $PAGER was unset is not an improvement.  The file is copied to stdout in
 * process, which is what wump(6) in this tree does for the same reason.  This
 * runs before curses is initialised, so plain putchar is correct.
 */
void
instructions()
{
	FILE	*fp;
	int	c;

	if ((fp = fopen(_PATH_INSTR, "r")) == NULL) {
		printf("Sorry, no rules file (%s).\n", _PATH_INSTR);
		return;
	}
	while ((c = getc(fp)) != EOF)
		putchar(c);
	(void) fclose(fp);
}
