/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*	$NetBSD: extern.h,v 1.11 2004/01/27 20:30:28 jsm Exp $	*/

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ed James.
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
 *
 *	@(#)extern.h	8.1 (Berkeley) 5/31/93
 */

/*
 * Copyright (c) 1987 by Ed James, UC Berkeley.  All rights reserved.
 *
 * Copy permission is hereby granted provided that this notice is
 * retained on all partial or complete copies.
 *
 * For more info on this and all of my stuff, mail edjames@berkeley.edu.
 */

extern char		GAMES[];
extern  char	*file;

extern int		clck, safe_planes, test_mode;
extern long		start_time;	/* a time(2) value: 16 bits will not hold one */

extern FILE		*filein, *fileout;

extern C_SCREEN		screen, *sp;

extern LIST		air, ground;

extern struct termio	tty_start, tty_new;

extern DISPLACEMENT	displacement[MAXDIR];

int		addplane();
void		append();
void		check_adir();
void		check_edge();
void		check_edir();
void		check_line();
void		check_linepoint();
void		check_point();
int		checkdefs();
void		chopnl();
int		compar();
void		delete();
int		dir_deg();
int		dir_no();
void		done_screen();
void		draw_all();
void		draw_line();
void		erase_all();
int		getAChar();
int		getcommand();
int		gettoken();
void		init_gr();
void		ioaddstr();
void		ioclrtobot();
void		ioclrtoeol();
void		ioerror();
void		iomove();
int		list_games();
int		log_score();
void		log_score_quit();
void		loser();
int		main();
char		name();
int		next_plane();
void		noise();
void		nowindow();
int		number();
void		open_score_file();
void		planewin();
int		pop();
void		push();
void		quit();
int		read_file();
void		redraw();
void		rezero();
void		setup_screen();
int		too_close();
void		update();
int		yyerror();
int		yylex();
#ifndef YYEMPTY
int		yyparse();
#endif
 char     *Left();
 char     *Right();
 char     *airport();
 char     *beacon();
 char     *benum();
 char     *circle();
 char     *climb();
 char     *command();
 char     *default_game();
 char     *delayb();
 char     *descend();
 char     *ex_it();
PLANE	       *findplane();
 char     *ignore();
 char     *left();
 char     *mark();
PLANE	       *newplane();
 char     *okay_game();
 char     *rel_dir();
 char     *right();
 char     *setalt();
 char     *setplane();
 char     *setrelalt();
 char     *timestr();
 char     *to_dir();
 char     *turn();
 char     *unmark();
