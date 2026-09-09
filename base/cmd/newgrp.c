/*
 * Copyright (c) 1977-1995 Robert Swartz.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * newgrp groupname
 * this is exec'd by the shell
 * to make it work:
 *			chmod u+s /bin/newgrp
 */
#include <stdio.h>
#include <grp.h>

extern char **environ;

main(argc,argv,envp)
char **argv,**envp;
{
	register char *unam;
	register int *flag;
	register struct group *grp;
	char *cp;
	char *crypt(),*getpass(),*getlogin();
	int namect=0,inflg=0,outflg=0;

	environ=envp;
	if (argc<2)
		perrx("Usage: newgrp groupname");
	if ((grp=getgrnam(argv[1]))==NULL)
		perrx("non-existent group");
	unam=getlogin();
	while (*grp->gr_mem!=NULL) {
		if (**grp->gr_mem=='!') {
			++*grp->gr_mem;
			flag=&outflg;
		} else {
			flag=&inflg;
			++namect;
		}
		/*
		 * No login name -- no utmp entry for this terminal -- matches
		 * no entry in the list, so a group with an access list refuses
		 * and a group without one is unaffected.
		 */
		if (unam!=NULL && strcmp(*grp->gr_mem,unam)==0)
			++*flag;
		++grp->gr_mem;
	}
	if (outflg)
		perrx("access explicitly denied");
	if (!inflg && *grp->gr_passwd=='\0' && namect)
		perrx("not in access list");
	if (!inflg && *grp->gr_passwd!='\0') {
		if ((cp=getpass("Password:"))==NULL || *cp=='\0')
			perrx("no");
		cp=crypt(cp,grp->gr_passwd);
		if (strcmp(cp,grp->gr_passwd)!=0)
			perrx("no");
	}
	/*
	 * The new group is the whole point, so a setgid that does not take is a
	 * failure and not a detail: without it the shell below would run with
	 * the caller's own group and look like a success.  It needs the setuid
	 * bit on this file (see above) to take at all.
	 */
	if (setgid(grp->gr_gid)!=0)
		perrx("cannot change group id");
	if (setuid(getuid())!=0)
		perrx("cannot change user id");
	execle("/bin/sh","-",NULL,environ);
	perrx("cannot execute /bin/sh");
}

/*
 * Report and exit non-zero.  Every caller is an error path, and the shell
 * belongs to the success path alone: the shell exec'd newgrp in place of
 * itself, so a shell started here would be an interactive login shell handed
 * out for a group the caller was refused -- or for no group named at all.
 */
perrx(s)
char *s;
{
	fprintf(stderr,"%s\n",s);
	exit(1);
}
