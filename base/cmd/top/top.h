/*
 *  Top - a top users display for Berkeley Unix
 *
 *  General (global) definitions
 */

/* Current major version number */
#define VERSION		3

/* Number of lines of header information on the standard screen */
#define Header_lines	6

/* Maximum number of columns allowed for display */
#define MAX_COLS	128

/* Log base 2 of 1024 is 10 (2^10 == 1024) */
#define LOG1024		10

char *itoa();
char *itoa7();

char *version_string();

/* Special atoi routine returns either a non-negative number or one of: */
#define Infinity	-1
#define Invalid		-2

/*
 * The largest process count the display will accept.  It is assigned to
 * plain ints (topn, and display_resize()'s return), and int is 16 bits on
 * this machine, so it is the largest positive int and not 0x7fffffff.
 */
#define Largest		32767

/*
 * The entire display is based on these next numbers being defined as is.
 */

#define NUM_AVERAGES    3

