/*
 * config.h
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
 *	Configuration file for additional features...
 *	Added 1993 by Nils M. Holm
 */

#define STRL		256

/* Define if you want to have help screens in your game (? and / commands) */
#define ONLINE_HELP

/* Define if you want arrow key support */
/* This does not work on Coherent */
/* #define ALTERNATE_KEYS */

/* Define this to be the wizard password instead of the default */
#define READABLE_PASSWD	"lint"

/* Define to disable save file checks (may be considered cheating!) */
#define RESTORE_ANY

/* Define to keep the save file when continuing a game (cheating!) */
#define KEEP_SAVEFILE

/* Define this if you want some additional ROGUEOPTS, like flush, and grafix */
/* `flush' flushes the keyboard queue before reading each command character */
/* `grafix'  makes rogue use IBM grafix characters to draw the map */
/* These do not work on Coherent */
/* #define NEWOPTS */

/* Where to put intermediate files... */
#define TMP_DIR		"/tmp"

/* Default pager for the help facility (if $PAGER is undefined) */
#ifdef MSDOS
 #define DFL_PAGER	"more"
#else
 #define DFL_PAGER	"/usr/bin/more"
#endif

/* This is the directory to contain the help pages and the score  file */
/* This variable must match ROGUE_DIR in Makefile */
#define ROGUE_DIR	"/usr/local/lib"

/* The score file must be in the above directory! */
#define SCORE_FILE	"/usr/local/lib/rogue.scores"

/* Help file names, should be right */
#ifdef MSDOS
#define H_HELP		"rogue.hel"
#define H_MANUAL	"rogue.man"
#define H_GUIDE		"rogue_bs.doc"
#define H_DATA		"rogue.data"
#else
#define H_HELP		"rogue.help"
#define H_MANUAL	"rogue.man"
#define H_GUIDE		"rogue_bsd.doc"
#define H_DATA		"rogue.data"
#endif
