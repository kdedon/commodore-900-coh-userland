/*
 *  Top - a top users display for Berkeley Unix
 *
 *  Definitions for things that might vary between installations.
 *
 *  The distribution ships this file as top.local.H with %name% placeholders
 *  that its Configure script fills in.  This is that file with the answers
 *  for this system written in.
 */

/*
 *  The space command forces an immediate update.  Sometimes, on loaded
 *  systems, this update will take a significant period of time (because all
 *  the output is buffered).  So, if the short-term load average is above
 *  "LoadMax", then top will put the cursor home immediately after the space
 *  is pressed before the next update is attempted.  This serves as a visual
 *  acknowledgement of the command.
 *
 *  Load averages are hundredths in a long here (see machine.h), so this is
 *  a load of 5.00 rather than the distribution's floating-point 5.0.
 */
#ifndef LoadMax
#define LoadMax  500L
#endif

/*
 *  "Table_size" defines the size of the hash tables used to map uid to
 *  username.  The number of users in /etc/passwd CANNOT be greater than
 *  this number.  If the error message "table overflow: too many users"
 *  is printed by top, then "Table_size" needs to be increased.  Things will
 *  work best if the number is a prime number that is about twice the number
 *  of lines in /etc/passwd.
 */
#ifndef Table_size
#define Table_size	11
#endif

/*
 *  "Nominal_TOPN" is used as the default TOPN when Default_TOPN is Infinity
 *  and the output is a dumb terminal.  If we didn't do this, then
 *  installations who use a default TOPN of Infinity will get every
 *  process in the system when running top on a dumb terminal (or redirected
 *  to a file).  Note that Nominal_TOPN is a default:  it can still be
 *  overridden on the command line, even with the value "infinity".
 */
#ifndef Nominal_TOPN
#define Nominal_TOPN	18
#endif

#ifndef Default_TOPN
#define Default_TOPN	Infinity
#endif

#ifndef Default_DELAY
#define Default_DELAY	5
#endif

/*
 *  If the local system's getpwnam interface uses random access to retrieve
 *  a record (i.e.: 4.3 systems, Sun "yellow pages"), then defining
 *  RANDOM_PW will take advantage of that fact.  If RANDOM_PW is defined,
 *  then getpwnam is used and the result is cached.  If not, then getpwent
 *  is used to read and cache the password entries sequentially until the
 *  desired one is found.
 *
 *  We initially set RANDOM_PW to something which is controllable by the
 *  Configure script.  Then if its value is 0, we undef it.
 */

/*
 *  getpwuid() here is a linear scan of /etc/passwd, so it is not random
 *  access and the sequential getpwent() path that caches every entry it
 *  walks past is the cheaper one.
 */
#define RANDOM_PW	0
#if RANDOM_PW == 0
#undef RANDOM_PW
#endif
