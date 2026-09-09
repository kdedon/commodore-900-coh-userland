/*	$NetBSD: score.c,v 1.17 2004/01/27 20:30:30 jsm Exp $	*/

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
 *	@(#)score.c	8.1 (Berkeley) 5/31/93
 */


# include	"robots.h"

char	*Scorefile = _PATH_SCORE;

int	Max_per_uid = MAX_PER_UID;

static SCORE	Top[MAXSCORES];

static long	numscores, max_uid;

/*
 * read_score:
 *	Read the score file.  Upstream stores the numbers in network byte
 *	order; a Z8001 long already IS network byte order, so the ntohl()
 *	and htonl() calls are gone rather than reimplemented.
 */
static void
read_score(inf)
	int inf;
{
	SCORE	*scp;

	if (read(inf, (char *)&max_uid, sizeof max_uid) == sizeof max_uid) {
		read(inf, (char *)Top, sizeof Top);
	}
	else {
		for (scp = Top; scp < &Top[MAXSCORES]; scp++)
			scp->s_score = 0;
		max_uid = Max_per_uid;
	}
}

/*
 * write_score:
 *	Write the score file back.
 */
static void
write_score(inf)
	int inf;
{
	lseek(inf, 0L, 0);
	write(inf, (char *)&max_uid, sizeof max_uid);
	write(inf, (char *)Top, sizeof Top);
}


/*
 * score:
 *	Post the player's score, if reasonable, and then print out the
 *	top list.
 */
void
score(score_wfd)
	int score_wfd;
{
	int			inf = score_wfd;
	SCORE			*scp;
	long			uid;
	bool			done_show = FALSE;

	Newscore = FALSE;
	if (inf < 0)
		return;

	read_score(inf);

	uid = getuid();
	if (Top[MAXSCORES-1].s_score <= Score) {
		numscores = 0;
		for (scp = Top; scp < &Top[MAXSCORES]; scp++)
			if ((scp->s_uid == uid && ++numscores == max_uid)) {
				if (scp->s_score > Score)
					break;
				scp->s_score = Score;
				scp->s_uid = uid;
				scp->s_auto = Auto_bot;
				scp->s_level = Level;
				set_name(scp);
				Newscore = TRUE;
				break;
			}
		if (scp == &Top[MAXSCORES]) {
			Top[MAXSCORES-1].s_score = Score;
			Top[MAXSCORES-1].s_uid = uid;
			Top[MAXSCORES-1].s_auto = Auto_bot;
			Top[MAXSCORES-1].s_level = Level;
			set_name(&Top[MAXSCORES-1]);
			Newscore = TRUE;
		}
		if (Newscore)
			qsort((char *)Top, MAXSCORES, sizeof Top[0], cmp_sc);
	}

	if (!Newscore) {
		Full_clear = FALSE;
		lseek(inf, 0L, 0);
		return;
	}
	else
		Full_clear = TRUE;

	move(1, 15);
	printw("%5.5s %5.5s %-9.9s %-8.8s %5.5s", "Rank", "Score", "User",
	    " ", "Level");

	for (scp = Top; scp < &Top[MAXSCORES]; scp++) {
		if (scp->s_score == 0)
			break;
		move((int)(scp - Top) + 2, 15);
		if (!done_show && scp->s_uid == uid && scp->s_score == Score)
			standout();
		printw("%5ld %5ld %-8.8s %-9.9s %5ld",
		    (long)(scp - Top) + 1, scp->s_score, scp->s_name,
		    scp->s_auto ? "(autobot)" : "", scp->s_level);
		if (!done_show && scp->s_uid == uid && scp->s_score == Score) {
			standend();
			done_show = TRUE;
		}
	}
	Num_scores = scp - Top;
	refresh();

	if (Newscore) {
		write_score(inf);
	}
	lseek(inf, 0L, 0);
}

void
set_name(scp)
	SCORE	*scp;
{
	PASSWD	*pp;
	static char unknown[] = "???";
	char	*name;

	/*
	 * Upstream writes `pp->pw_name = unknown' when getpwuid() returns
	 * NULL, which dereferences the null it just tested for.  Here that is
	 * not theoretical -- a game run by a uid with no passwd entry is
	 * ordinary -- so use the fallback name instead of storing through pp.
	 */
	name = ((pp = getpwuid((int)scp->s_uid)) == NULL) ? unknown : pp->pw_name;
	strncpy(scp->s_name, name, MAXNAME);
}

/*
 * cmp_sc:
 *	Compare two scores.  Upstream subtracts them and returns the
 *	difference as an int; scores are long here, so compare instead of
 *	subtracting -- a difference past 32767 would come back with the
 *	wrong sign and sort the table backwards.
 */
int
cmp_sc(s1, s2)
	char *s1, *s2;
{
	long	a, b;

	a = ((SCORE *)s2)->s_score;
	b = ((SCORE *)s1)->s_score;
	return (a < b) ? -1 : (a > b);
}

/*
 * show_score:
 *	Show the score list for the '-s' option.
 */
void
show_score()
{
	SCORE		*scp;
	int		inf;

	if ((inf = open(Scorefile, O_RDONLY)) < 0) {
		fprintf(stderr, "robots: %s: cannot open\n", Scorefile);
		return;
	}

	read_score(inf);
	close(inf);
	inf = 1;
	printf("%5.5s %5.5s %-9.9s %-8.8s %5.5s\n", "Rank", "Score", "User",
	    " ", "Level");
	for (scp = Top; scp < &Top[MAXSCORES]; scp++)
		if (scp->s_score > 0)
			printf("%5d %5ld %-8.8s %-9.9s %5ld\n",
			    inf++, scp->s_score, scp->s_name,
			    scp->s_auto ? "(autobot)" :  "", scp->s_level);
}
