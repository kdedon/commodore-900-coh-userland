/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * whoami.c
 * Usage: whoami
 * Print the login name of the EFFECTIVE user id.
 *
 * Not the same question as `who am i', which reads /etc/utmp and answers with
 * whoever logged this terminal in: that is still the login name after a su, and
 * it is no answer at all for a process with no terminal.  This reports the
 * identity the kernel would enforce.
 */

#include <stdio.h>
#include <pwd.h>

main()
{
	register struct passwd *pw;
	int uid;

	uid = geteuid();
	if ((pw = getpwuid(uid)) == NULL) {
		/*
		 * No passwd entry: the number is still the truthful answer, and
		 * printing it beats printing nothing.
		 */
		fprintf(stderr, "whoami: no name for uid %d\n", uid);
		printf("%d\n", uid);
		exit(1);
	}
	printf("%s\n", pw->pw_name);
	exit(0);
}
