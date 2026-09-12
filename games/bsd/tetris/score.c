/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*
 * Copyright 1989 Phill Everson & Martyn Shortley
 * Copyright 1989 Roberto Biancardi
 *
 * This notice and any statement of authorship must be reproduced on all
 * copies.  The full terms, including the restrictions on sale and on charging
 * a fee, are in COPYRIGHT beside this file; COHERENT port by Matt Kimmel and
 * Udo Munk.
 */
#include "defs.h"
#include <curses.h>
#ifndef COHERENT /* Coherent only kinda supports utsname.  See further mods below. */
#include <sys/utsname.h>
#endif

#define HIGH_SCORE_TABLE	"/usr/games/lib/Tetris_scores"

#define HIGH_TABLE_SIZE	10

/*
 * Field widths: upstream gives name, hostname and date a BUFSIZ each, which is
 * 15 KB of BSS for ten entries plus a BUFSIZ line buffer on the stack.  A login
 * name, one host name and a ctime() string are all that go in them.  score is a
 * long: a long game passes 32767 points, and it is written and read back as a
 * decimal field, so a 16-bit int would wrap and then sort the table wrongly.
 */
#define NAMELEN		16
#define DATELEN		32

static struct score_table {
        char    name[NAMELEN];
        long    score;
        int     rows;
        int     level;
	char	hostname[NAMELEN];
        char    date[DATELEN];
} high_scores[HIGH_TABLE_SIZE];

update_highscore_table()
{
        int     i, j;
        long    when;
        extern char *ctime();
        extern long time(), atol();
        char    buf[DATELEN + 8];
#ifndef COHERENT
	struct utsname utsname;
#endif


        /* re-read high-score table in case someone else on the network is
         * playing at the same time */
        read_high_scores();

        /* Next line finds score greater than current one */
        for (i = 0; ((i < HIGH_TABLE_SIZE) && (score >= high_scores[i].score)); i++);
        i--;
        score_position = i;
        if (i >= 0) {
                for (j = 0; j < i; j++)
                        high_scores[j] = high_scores[j + 1];
                strncpy(high_scores[i].name, name, NAMELEN - 1);
                high_scores[i].name[NAMELEN - 1] = '\0';
                high_scores[i].score = score;
                high_scores[i].rows = rows;
                high_scores[i].level = rows / 10;
#ifdef COHERENT
		strcpy(high_scores[i].hostname, "c900");
#else
		if ( uname(&utsname) < 0 )
                        strcpy(high_scores[i].hostname, "unknown-host");
                else
                        strcpy(high_scores[i].hostname, utsname.nodename);
#endif
                time(&when);
                strncpy(buf, ctime(&when), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';      /* ctime() adds a newline
                                                 * char */
                strip_eoln(buf);/* so remove it          */
                strncpy(high_scores[i].date, buf, DATELEN - 1);
                high_scores[i].date[DATELEN - 1] = '\0';
                write_high_scores();
        }
}

read_high_scores()
{
        FILE   *fp;
        int     i;
        char   buf[DATELEN + 8];

        for (i = 0; i < HIGH_TABLE_SIZE; i++) {
                strcpy(high_scores[i].name, " ");
                high_scores[i].score = 0;
                high_scores[i].rows = 0;
                high_scores[i].level = 0;
                strcpy(high_scores[i].hostname, " ");
                strcpy(high_scores[i].date, " ");
        }
        if ((fp = fopen(HIGH_SCORE_TABLE, "r")) == NULL) {
                fprintf(stderr, "tetris: No High score file\n");
                return;
        }
        for (i = 0; i < HIGH_TABLE_SIZE; i++) {
                fgets(buf, sizeof(buf), fp);
                strip_eoln(buf);
                strncpy(high_scores[i].name, buf, NAMELEN - 1);
                fgets(buf, sizeof(buf), fp);
                strip_eoln(buf);
                high_scores[i].score = atol(buf);
                fgets(buf, sizeof(buf), fp);
                strip_eoln(buf);
                high_scores[i].rows = atoi(buf);
                fgets(buf, sizeof(buf), fp);
                strip_eoln(buf);
                high_scores[i].level = atoi(buf);
                fgets(buf, sizeof(buf), fp);
                strip_eoln(buf);
                strncpy(high_scores[i].hostname, buf, NAMELEN - 1);
                fgets(buf, sizeof(buf), fp);
                strip_eoln(buf);
                strncpy(high_scores[i].date, buf, DATELEN - 1);
        }
        fclose(fp);
}

strip_eoln(s)
        char   *s;
{
        char   *s1;

        while (*s != '\0') {
                if (*s == '\n') {       /* End of line char */
                        s1 = s;
                        do {
                                *s1 = *(s1 + 1);        /* Copy rest of string */
                                s1++;
                        } while (*s1 != '\0');
                } else
                        s++;
        }
}

write_high_scores()
{
        FILE   *fp;
        int     i;

        if ((fp = fopen(HIGH_SCORE_TABLE, "w")) == NULL) {
                fprintf(stderr, "tetris: Couldn't open high score file %s\n", HIGH_SCORE_TABLE);
                return;
        }
        for (i = 0; i < HIGH_TABLE_SIZE; i++)
                fprintf(fp, "%s\n%ld\n%d\n%d\n%s\n%s\n",
                        high_scores[i].name,
                        high_scores[i].score,
                        high_scores[i].rows,
                        high_scores[i].level,
                        high_scores[i].hostname,
                        high_scores[i].date);
        fclose(fp);
}

void print_high_scores()
{
        int     i;
        char    buf[DATELEN + 8];

        /* re-read high-score table in case someone else on the network is
         * playing at the same time */
        read_high_scores();

	clear();
	wtext(10,1,"T e t r i s   H i g h e s t   r e s u l t s", 0);

	wtext(4,3,"Pos  Name             Score  Rows Lev  When", 0);
	wtext(4,4,"===  ====             =====  ==== ===  ====", 0);
	for ( i = 0; i < HIGH_TABLE_SIZE; i++ ) {
                sprintf(buf, "%3d) %-15s %6ld %5d %3d  %s\n",
                        HIGH_TABLE_SIZE - i,
                        high_scores[i].name,
                        high_scores[i].score,
                        high_scores[i].rows,
                        high_scores[i].level,
                        high_scores[i].date);
		wtext(4,14-i,buf, score_position == i);
        }
}

print_authors()
{
	int i;
	static char *au[] = {
"\n",
"    Tetris Version 1.1\n\n",
"This version of tetris was modified to run on ascii terminals by:\n",
"    Roberto Biancardi     <..!unido!tmpmbx!deejay!i2ack!usixth!bob>\n",
"Based on the version posted by Phill Everson <everson@cs.bris.ac.uk>\n",
"and Martyn Shortley <shortley@cs.bris.ac.uk>, based on the version posted\n",
"to comp.sources.games by Adam Marguilies <vespa@ssyx.ucsc.edu>\n",
"\n",
"Wed Jun 21 22:35:52 ITA 1989\n\n",
"Modified to run under Coherent and -g (game speed) option added by\n",
"Matt Kimmel <kimmel@umvlsi.ecs.umass.edu> 6/1/91\n",
NULL
};

	for ( i=0; au[i] != NULL; i++ )
		write(1,au[i],strlen(au[i]));
	sleep(2);
}
