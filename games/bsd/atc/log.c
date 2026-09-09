/*	$NetBSD: log.c,v 1.13 2004/09/07 13:20:39 jrf Exp $	*/

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
 */

/*
 * Copyright (c) 1987 by Ed James, UC Berkeley.  All rights reserved.
 *
 * Copy permission is hereby granted provided that this notice is
 * retained on all partial or complete copies.
 *
 * For more info on this and all of my stuff, mail edjames@berkeley.edu.
 */


#include "include.h"

static FILE *score_fp;

int
compar(va, vb)
	 char *va, *vb;
{
	 SCORE	*a, *b;

	a = ( SCORE *)va;
	b = ( SCORE *)vb;
	if (b->planes == a->planes)
		return (b->time - a->time);
	else
		return (b->planes - a->planes);
}

/* Long constants: SECAHOUR * HOURADAY is 86400, which does not fit in the
 * 16-bit int a plain constant expression is evaluated in here. */
#define SECAMIN		60L
#define MINAHOUR	60L
#define HOURADAY	24L
#define SECAHOUR	(SECAMIN * MINAHOUR)
#define SECADAY		(SECAHOUR * HOURADAY)
#define DAY(t)		((t) / SECADAY)
#define HOUR(t)		(((t) % SECADAY) / SECAHOUR)
#define MIN(t)		(((t) % SECAHOUR) / SECAMIN)
#define SEC(t)		((t) % SECAMIN)

 char	*
timestr(t)
	long t;			/* elapsed seconds: a long, see SECADAY */
{
	static char	s[80];

	if (DAY(t) > 0)
		(void)sprintf(s, "%ldd+%02ldhrs", DAY(t), HOUR(t));
	else if (HOUR(t) > 0)
		(void)sprintf(s, "%ld:%02ld:%02ld", HOUR(t), MIN(t), SEC(t));
	else if (MIN(t) > 0)
		(void)sprintf(s, "%ld:%02ld", MIN(t), SEC(t));
	else if (SEC(t) > 0)
		(void)sprintf(s, ":%02ld", SEC(t));
	else
		*s = '\0';

	return (s);
}

void
open_score_file()
{
	int old_mask;
	int score_fd;

	old_mask = umask(0);
	score_fd = open(_PATH_SCORE, O_CREAT|O_RDWR, 0664);
	umask(old_mask);
	if (score_fd < 0) {
		warn("open %s", _PATH_SCORE);
		return;
	}
	if (score_fd < 3)
		exit(1);
	/*
	 * No close-on-exec: the score file is not privileged here (the game is
	 * not setgid) and the only exec is the help pager.  fdopen so stdio can
	 * be used on a descriptor that open(2) was allowed to O_CREAT.
	 */
	score_fp = fdopen(score_fd, "r+");
	if (score_fp == NULL) {
		warn("fdopen %s", _PATH_SCORE);
		return;
	}
}

int
log_score(list_em)
	int list_em;
{
	int		i, num_scores = 0, good, changed = 0, found = 0;
	struct passwd	*pw;
	char		*cp;
	SCORE		score[MAX_SCORES], thisscore;

	if (score_fp == NULL) {
		warnx("no score file available");
		return (-1);
	}

	/* No score-file lock: this libc has neither flock() nor lockf(), and two
	 * simultaneous games on one machine would be needed to race. */
	for (;;) {
		good = fscanf(score_fp, SCORE_SCANF_FMT,
			score[num_scores].name, 
			score[num_scores].host, 
			score[num_scores].game,
			&score[num_scores].planes, 
			&score[num_scores].time,
			&score[num_scores].real_time);
		if (good != 6 || ++num_scores >= NUM_SCORES ||
		    num_scores >= MAX_SCORES)
			break;
	}
	if (!test_mode && !list_em) {
		if ((pw = (struct passwd *) getpwuid(getuid())) == NULL) {
			fprintf(stderr, 
				"getpwuid failed for uid %d.  Who are you?\n",
				(int)getuid());
			return (-1);
		}
		strncpy(thisscore.name, pw->pw_name, sizeof(thisscore.name) - 1);
		thisscore.name[sizeof(thisscore.name) - 1] = '\0';
		/* There is no uname(2) here and one machine plays this game. */
		strcpy(thisscore.host, HOSTNAME);

		/* `file' is set by read_file(); a score logged before any
		 * scenario was read has no game name to record. */
		if (file == NULL) {
			fprintf(stderr, "log: no game played.\n");
			return (-1);
		}
		cp = strrchr(file, '/');
		if (cp == NULL) {
			fprintf(stderr, "log: where's the '/' in %s?\n", file);
			return (-1);
		}
		cp++;
		strcpy(thisscore.game, cp);

		thisscore.time = clck;
		thisscore.planes = safe_planes;
		thisscore.real_time = time((long *) 0) - start_time;

		for (i = 0; i < num_scores; i++) {
			if (strcmp(thisscore.name, score[i].name) == 0 &&
			    strcmp(thisscore.host, score[i].host) == 0 &&
			    strcmp(thisscore.game, score[i].game) == 0) {
				if (thisscore.time > score[i].time) {
					score[i].time = thisscore.time;
					score[i].planes = thisscore.planes;
					score[i].real_time =
						thisscore.real_time;
					changed++;
				}
				found++;
				break;
			}
		}
		if (!found) {
			for (i = 0; i < num_scores; i++) {
				if (thisscore.time > score[i].time) {
					if (num_scores < NUM_SCORES)
						num_scores++;
					memcpy(&score[num_scores - 1],
					       &score[i],
					       sizeof (score[i]));
					memcpy(&score[i], &thisscore,
					       sizeof (score[i]));
					changed++;
					break;
				}
			}
		}
		if (!found && !changed && num_scores < NUM_SCORES) {
			memcpy(&score[num_scores], &thisscore,
			       sizeof (score[num_scores]));
			num_scores++;
			changed++;
		}

		if (changed) {
			if (found)
				puts("You beat your previous score!");
			else
				puts("You made the top players list!");
			qsort(score, num_scores, sizeof (*score), compar);
			/* freopen "w" in place of rewind + ftruncate: there is
			 * no ftruncate(2) here, and without one a table that
			 * got shorter would leave a stale last line that the
			 * next run would read back as a real score. */
			score_fp = freopen(_PATH_SCORE, "w", score_fp);
			if (score_fp == NULL) {
				warn("freopen %s", _PATH_SCORE);
				return (-1);
			}
			for (i = 0; i < num_scores; i++)
				fprintf(score_fp, "%s %s %s %d %d %ld\n",
					score[i].name, score[i].host, 
					score[i].game, score[i].planes,
					score[i].time, score[i].real_time);
			fflush(score_fp);
			if (ferror(score_fp))
				warn("error writing %s", _PATH_SCORE);
		} else {
			if (found)
				puts("You didn't beat your previous score.");
			else
				puts("You didn't make the top players list.");
		}
		putchar('\n');
	}
	fclose(score_fp);
	printf("%2s:  %-8s  %-8s  %-18s  %4s  %9s  %4s\n", "#", "name", "host", 
		"game", "time", "real time", "planes safe");
	puts("-------------------------------------------------------------------------------");
	for (i = 0; i < num_scores; i++) {
		cp = strchr(score[i].host, '.');
		if (cp != NULL)
			*cp = '\0';
		printf("%2d:  %-8s  %-8s  %-18s  %4d  %9s  %4d\n", i + 1,
			score[i].name, score[i].host, score[i].game,
			score[i].time, timestr(score[i].real_time),
			score[i].planes);
	}
	putchar('\n');
	return (0);
}

void
log_score_quit(dummy)
	int dummy ;
{
	(void)log_score(0);
	exit(0);
}
