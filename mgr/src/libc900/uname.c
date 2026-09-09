#include <sys/utsname.h>
#include <string.h>

/*
 * uname -- there is no uname(2) on this kernel and no in-core name table,
 * so the answer is the build-time identity of the system.  MGR uses it for
 * the G_SYSTEM protocol reply and, through gethostname(), for window titles.
 */
#ifndef C900_SYSNAME
#define C900_SYSNAME  "COHERENT"
#endif
#ifndef C900_NODENAME
#define C900_NODENAME "c900"
#endif
#ifndef C900_RELEASE
#define C900_RELEASE  "3.5"
#endif
#ifndef C900_MACHINE
#define C900_MACHINE  "z8001"
#endif

int
uname(u)
struct utsname *u;
{
	strncpy(u->sysname,  C900_SYSNAME,  SYS_NMLN);
	strncpy(u->nodename, C900_NODENAME, SYS_NMLN);
	strncpy(u->release,  C900_RELEASE,  SYS_NMLN);
	strncpy(u->version,  "",            SYS_NMLN);
	strncpy(u->machine,  C900_MACHINE,  SYS_NMLN);
	u->sysname[SYS_NMLN-1] = u->nodename[SYS_NMLN-1] = '\0';
	u->release[SYS_NMLN-1] = u->machine[SYS_NMLN-1] = '\0';
	return 0;
}
