/* $Header: version.c,v 2.0 86/09/17 15:40:11 lwall Exp $
 *
 * $Log:	version.c,v $
 * Revision 2.0  86/09/17  15:40:11  lwall
 * Baseline for netwide release.
 * 
 */

#include "EXTERN.h"
#include "common.h"
#include "util.h"
#include "INTERN.h"
#include "patchlevel.h"
#include "version.h"

/* Print out the version number and die. */

void
version()
{
    void my_exit();
    extern char rcsid[];

#ifdef lint
    rcsid[0] = rcsid[0];
#else
    fprintf(stderr, "%s\nPatch level: %s\n", rcsid, PATCHLEVEL);
    my_exit(0);
#endif
}
