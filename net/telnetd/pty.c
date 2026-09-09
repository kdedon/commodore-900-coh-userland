/*
 * TNET		A server program for MINIX which implements the TCP/IP
 *		suite of networking protocols.  It is based on the
 *		TCP/IP code written by Phil Karn et al, as found in
 *		his NET package for Packet Radio communications.
 *
 *		Handle the allocation of a PTY.
 *
 * Author:	Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 */
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "telnetd.h"


#define DEV_DIR		"/dev"
#define PTY_FIRST	'p'		/* masters are /dev/pty[p-v][0-f]   */
#define PTY_LAST	'v'
#define PTY_NDIGIT	16		/* channels per letter		   */

/*
 * The kernel's error for an exclusive device already held (include/errno.h).
 * <net/include/errno.h> comes first on this include path and has no name for
 * it, and a pty master is exclusive.
 */
#ifndef EDBUSY
#define EDBUSY		39
#endif

char *pty_reason = "";			/* why the last get_pty() failed   */

static char reason[80];

/*
 * Allocate a PTY by opening masters in turn until one succeeds.
 *
 * The scan stops at the first name that does not exist, rather than walking all
 * 112 possible names: only the first NUPTY channels have nodes, and they are
 * made in order (a missing digit 0 means the letter is absent and so is every
 * letter after it).  Anything other than "not there" and "in use" is kept, so
 * a failure can say which of the three reasons it was -- an absent driver
 * answers ENXIO for every channel, which is not "all ptys busy".
 */
int get_pty(pty_fdp, tty_namep)
int *pty_fdp;
char **tty_namep;
{
  char buff[128], temp[128];
  int i, j;
  int pty_fd;
  int busy, hard, last;
  static char tty_name[128];

  pty_fd = -1;
  busy = hard = last = 0;

  for (i = PTY_FIRST; i <= PTY_LAST; i++) {
	for (j = 0; j < PTY_NDIGIT; j++) {
		sprintf(buff, "%s/pty%c%c",
			DEV_DIR, i, (j < 10) ? j + '0' : j + 'a' - 10);

		pty_fd = open(buff, O_RDWR);

		if (opt_d == 1) {
			if (pty_fd < 0)
				sprintf(temp, "%s: errno %d\r\n", buff, errno);
			else
				sprintf(temp, "%s: OK\r\n", buff);
			(void) write(2, temp, strlen(temp));
		}

		if (pty_fd >= 0) break;

		if (errno == ENOENT) {
			if (j == 0) last = 1;
			break;
		}
		if (errno == EDBUSY || errno == EBUSY) busy++;
		  else if (hard == 0) {
			/* Kept now, with the name that produced it: the scan
			 * goes on and buff is a later channel by the end. */
			hard = errno;
			if (hard == ENXIO)
				sprintf(reason,
				  "open %s gives ENXIO: no pty driver in this kernel",
				  buff);
			else
				sprintf(reason, "open %s fails with errno %d",
					buff, hard);
		}
	}
	if (pty_fd >= 0 || last) break;
  }

  if (pty_fd < 0) {
	if (hard == 0) {		/* else reason is set at the failing open */
		if (busy > 0)
			sprintf(reason, "all %d pty channels are in use", busy);
		else
			sprintf(reason, "no %s node", buff);
	}
	pty_reason = reason;
	return(-1);
  }

  if (opt_d == 1) {
	sprintf(temp, "File %s, desc %d\r\n", buff, pty_fd);
	(void) write(2, temp, strlen(temp));
  }

  sprintf(tty_name, "%s/tty%c%c", DEV_DIR,
  					i, (j < 10) ? j + '0' : j + 'a' - 10);

  *pty_fdp = pty_fd;
  *tty_namep = tty_name;
  pty_reason = "";
  return(0);
}
