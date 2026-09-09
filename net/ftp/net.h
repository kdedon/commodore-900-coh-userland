/* net.h
 *
 * This file is part of ftp.
 *
 *
 * 01/25/96 Initial Release	Michael Temari, <temari@ix.netcom.com>
 */

#include <ansi.h>		/* _PROTOTYPE degrades to () for K&R */


_PROTOTYPE(void NETinit, (void));
_PROTOTYPE(int DOopen, (void));
_PROTOTYPE(int DOclose, (void));
_PROTOTYPE(int DOquit, (void));
_PROTOTYPE(int DOdata, (char *datacom, char *file, int direction, int fd));
