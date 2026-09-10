/*
 * help.c
 *
 * This source herein may be modified and/or distributed by anybody who
 * so desires, with the following restrictions:
 *    1.)  No portion of this notice shall be removed.
 *    2.)  Credit shall not be taken for the creation of this source.
 *    3.)  This code is not to be traded, sold, or used for personal
 *         gain or profit.
 *
 */

 /*
  *	I thought that some online help would be a great thing...
  *	Added this help facility.
  *	Nils M. Holm (nmh@sunrise.rni.sub.org)
  */

#include <stdio.h>
#include <string.h>
#ifdef MSDOS
#	define R_OK	1
#else
#	ifdef COHERENT
#		include <access.h>
#		define R_OK	AREAD
#	else
#		include <unistd.h>
#	endif
#endif
#include "rogue.h"
#include "config.h"

#ifdef ONLINE_HELP	/* undef it in config.h if you don't want it */

extern char	*getenv();

char	Screen[DROWS][DCOLS];


void save_scr()
{
   int   i, j;

   for (j=0; j<DROWS; j++)
	for (i=0; i<DCOLS; i++)
	   Screen[j][i] = mvinch(j, i);
}


void restore_scr()
{
   int   i, j;

   for (j=0; j<DROWS; j++)
	for (i=0; i<DCOLS; i++)
	   mvaddch(j, i, Screen[j][i]);
   refresh();
}


void ident_character(c)
int  c;
{
   char  p[STRL], err[STRL];
   FILE  *data;

   move(0, 0);
   clrtoeol();
   refresh();

   sprintf(p, "%s/%s", ROGUE_DIR, H_DATA);
   if ((data = fopen(p, "r")) == NULL) {
	sprintf(err, "Cannot open data file: \"%s\"", p);
	message(err, 0);
	return;
   }

   for (fgets(p, STRL, data); !feof(data); fgets(p, STRL, data))
	if (c == p[0]) {
	   fclose(data);
	   sprintf(err, "%c  -  %s %s", (char) p[0],
	   strchr("AEIOUaeiou", p[2])!=NULL ? "an" : "a", p+2);
	   message(err, 0);
	   return;
	}

   fclose(data);

   sprintf(err, "Never heard about that: '%c'", (char) c);
   message(err, 0);
}


void what_is()
{
   int  c;

   mvaddstr(0, 0, "Enter character to identify (ESC=quit): ");
   refresh();
   if ((c = rgetchar()) == 033) {
	move(0, 0);
	clrtoeol();
	refresh();
	return;
   }
   ident_character(c);
}


void browse(file)
char  *file;
{
   char  *pager;
   char  ws[STRL], err[STRL];
   int   rc;

   move(0, 0);
   clrtoeol();

   if ((pager = getenv("PAGER")) == NULL)
	pager = DFL_PAGER;

   sprintf(ws, "%s/%s", ROGUE_DIR, file);
   if (access(ws, R_OK)) {
	sprintf(err, "%s: no such file: \"%s\".", pager, ws);
	message(err, 0);
	return;
   }

   save_scr();

   sprintf(ws, "%s %s/%s", pager, ROGUE_DIR, file);
   clear();
   refresh();

   stop_window();
   rc = system(ws);
   printf("-- Press ENTER to return to game --");
   getchar();
   initscr();
   start_window();

   restore_scr();

   if (rc) {
	sprintf(ws, "Could not execute pager: \"%s\".", pager);
	message(ws, 0);
   }
}


void online_help()
{
   mvaddstr(0, 0, "short (H)elp, (M)anual, or the (G)uide to the Dungeons "
   "of Doom?");

   refresh();

   switch (rgetchar()) {
	case 'h':
	case 'H':	browse(H_HELP); break;
	case 'm':
	case 'M':	browse(H_MANUAL); break;
	case 'g':
	case 'G':	browse(H_GUIDE); break;
	default:	move(0, 0); clrtoeol(); refresh();
   }
}
#endif /* ONLINE_HELP */
