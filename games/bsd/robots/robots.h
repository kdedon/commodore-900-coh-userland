/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*	$NetBSD: robots.h,v 1.18 2004/01/27 20:30:30 jsm Exp $	*/

/*
 * Copyright (c) 1980, 1993
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
 *
 *	@(#)robots.h	8.1 (Berkeley) 5/31/93
 */

# include	<curses.h>
# include	<ctype.h>
# include	<errno.h>
# include	<fcntl.h>
# include	<pwd.h>
# include	<setjmp.h>
# include	<signal.h>
# include	<stdio.h>

/* K&R: routines returning a pointer or a long must be declared, or they
 * default to int -- which is 16 bits here while a pointer is a 32-bit far
 * pointer, so the segment is lost. */
extern char	*strcpy(), *strncpy(), *strrchr(), *memset();
extern long	lseek();
extern struct passwd	*getpwuid();

/* From <sys/ttydefaults.h> upstream; the whole header is not wanted here. */
# ifndef CTRL
# define	CTRL(c)		((c) & 037)
# endif

/* TCFLSH's argument, for libc's tcflush() shim.  <termio.h> declares it, but
 * that header describes a discipline this port does not link, and curses.h has
 * already pulled in <sgtty.h>. */
# ifndef TCIFLUSH
# define	TCIFLUSH	0
# endif

/*
 * miscellaneous constants
 */

# define	Y_FIELDSIZE	23
# define	X_FIELDSIZE	60
# define	Y_SIZE		24
# define	X_SIZE		80
# define	MAXLEVELS	4
# define	MAXROBOTS	(MAXLEVELS * 10)
# define	ROB_SCORE	10
# undef		S_BONUS
# define	S_BONUS		(60 * ROB_SCORE)
# define	Y_SCORE		21
# define	X_SCORE		(X_FIELDSIZE + 9)
# define	Y_PROMPT	(Y_FIELDSIZE - 1)
# define	X_PROMPT	(X_FIELDSIZE + 2)
# define	MAXSCORES	(Y_SIZE - 2)
# define	MAXNAME		16
# define	MS_NAME		"Ten"

# ifndef MAX_PER_UID
# define	MAX_PER_UID	5
# endif

/* Upstream substitutes this at configure time; there is no configure here. */
# define	_PATH_SCORE	"/usr/games/lib/robots_roll"

/*
 * "FAR" distances.  Upstream seeds its minimum-distance search with 1000000,
 * which does not fit in a 16-bit int -- the field is 60x23, so any value past
 * the board works and this one does not overflow.
 */
# define	FAR_AWAY	30000

/*
 * characters on screen
 */

# define	ROBOT	'+'
# define	HEAP	'*'
# define	PLAYER	'@'

/*
 * type definitions
 */

typedef struct {
	int	y, x;
} COORD;

/*
 * The score file is written and read only by this program on this machine, and
 * upstream's u_int32_t fields with htonl()/ntohl() around them come out as
 * plain big-endian longs on a Z8001 -- the byte order is already network order.
 * So: long, and no byte swapping.  int would NOT do: a score passes 32767.
 */
typedef struct {
	long		s_uid;
	long		s_score;
	long		s_auto;
	long		s_level;
	char		s_name[MAXNAME];
} SCORE;

typedef struct passwd	PASSWD;

/*
 * global variables
 */

extern bool	Dead, Full_clear, Jump, Newscore, Real_time, Running,
		Teleport, Waiting, Was_bonus, Auto_bot;

extern char	Cnt_move, Field[Y_FIELDSIZE][X_FIELDSIZE], Run_ch;
extern char	*Next_move, *Move_list;

extern int	Count, Level, Num_robots, Num_scrap, Num_scores,
		Start_level, Wait_bonus, Num_games;

extern long	Score;

extern COORD	Max, Min, My_pos, Robots[], Scrap[];

extern jmp_buf	End_move;

/*
 * function types.  K&R: declarations carry the return type only.
 */
void	add_score();
bool	another();
char	automove();
int	cmp_sc();
bool	do_move();
bool	eaten();
void	flush_in();
void	get_move();
void	init_field();
bool	jumping();
void	make_level();
void	move_robots();
bool	must_telep();
void	play_level();
int	query();
void	quit();
void	reset_count();
int	rnd();
COORD  *rnd_pos();
void	score();
void	set_name();
void	show_score();
int	sign();
void	telmsg();
