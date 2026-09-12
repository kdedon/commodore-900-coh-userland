/*
 * Copyright (c) 1977-1995 Robert Swartz.
 * Copyright (c) 2026 Kevin Dedon.
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 * SPDX-License-Identifier: BSD-3-Clause
 */
/* (-lgl
 * 	COHERENT Version 4.2
 * 	Copyright (c) 1982, 1993 by Mark Williams Company.
 * 	All rights reserved. May not be copied without permission.
 -lgl) */
/*
 * uname.c
 * Print information about current COHERENT system.
 *
 * -a prints the system name as well.  The Lexicon says "-a print all
 * information"; the i386 source leaves the system name out of it, and here the
 * system name is the id a package index matches on, so the flag that means
 * everything says it.
 *
 * -K and -F print the two halves of the version field on their own.  They are
 * what a package script wants: the field is `kabi.fsabi', and the two are
 * compared as integers -- 1.10 is newer than 1.9 and sorts before it -- so a
 * script that splits the string itself is a string comparison waiting to be
 * written.  See sys/utsname.h for what each id covers.
 *
 * -S sets this machine's short name in /etc/uucpname, which is the file
 * uname(2) reports as the node name.  /etc/hostname, which gethostname(3)
 * reads, holds the same name with its domain and is too long for a utsname
 * field; a machine whose two names disagree needs both rewritten.
 */

#include	<stdio.h>
#include	<fcntl.h>
#include	<sys/utsname.h>
#include	<string.h>

#define	NODEFILE	"/etc/uucpname"

main(argc, argv)
int	argc;
char	*argv[];
{
	extern char	*optarg;
	extern int	optind;
	static int	snf = 0,	/* Default */
			nnf = 0,
			srf = 0,
			svf = 0,
			mhf = 0,
			kaf = 0,
			fsf = 0,
			Snf = 0;
	char		*sname;
	int		c;
	struct utsname	tsname;

	while ((c = getopt(argc, argv, "snrvmaKFS:")) != EOF)
		switch (c) {
		case 's':	/* Print system name (default). */
			snf = 1;
			break;
		case 'n':	/* Print node name */
			nnf = 1;
			break;
		case 'r':	/* Print system release */
			srf = 1;
			break;
		case 'v':	/* Print system version */
			svf = 1;
			break;
		case 'm':	/* Print machine hardware name */
			mhf = 1;
			break;
		case 'a':	/* Print all above */
			snf = nnf = srf = svf = mhf = 1;
			break;
		case 'K':	/* Print the kernel struct layout id */
			kaf = 1;
			break;
		case 'F':	/* Print the filesystem format id */
			fsf = 1;
			break;
		case 'S':	/* Change system name */
			Snf = 1;
			sname = optarg;
			break;
		default:
			usage();
		}
	if ((snf || nnf || srf || svf || mhf || kaf || fsf) && Snf)
		usage();
	if (!(snf || nnf || srf || svf || mhf || kaf || fsf || Snf))
		snf = 1;

	if (Snf)
		changename(sname);
	else {
		int space;

		if (uname(&tsname) < 0) {
			perror("uname");
			exit(1);
		}
		space = 0;
		if (snf) {
			printf("%.*s", SYS_NMLN, tsname.sysname);
			space = 1;
		}
		if (nnf) {
			if (space)
				putchar(' ');
			printf("%.*s", SYS_NMLN, tsname.nodename);
			space = 1;
		}
		if (srf) {
			if (space)
				putchar(' ');
			printf("%.*s", SYS_NMLN, tsname.release);
			space = 1;
		}
		if (svf) {
			if (space)
				putchar(' ');
			printf("%.*s", SYS_NMLN, tsname.version);
			space = 1;
		}
		if (mhf) {
			if (space)
				putchar(' ');
			printf("%.*s", SYS_NMLN, tsname.machine);
			space = 1;
		}
		if (kaf) {
			if (space)
				putchar(' ');
			printf("%d", abiid(tsname.version, 0));
			space = 1;
		}
		if (fsf) {
			if (space)
				putchar(' ');
			printf("%d", abiid(tsname.version, 1));
		}
		putchar('\n');
	}
	exit(0);
}

/*
 * One half of the version field as a number: half 0 is kabi, half 1 is fsabi.
 * A field the kernel did not fill in the expected shape reads as -1, which no
 * comparison against a real id can match.
 */
abiid(version, half)
char	*version;
int	half;
{
	register char	*cp;
	register int	n;

	cp = version;
	if (half != 0) {
		while (*cp != '\0' && *cp != '.')
			cp++;
		if (*cp != '.')
			return (-1);
		cp++;
	}
	if (*cp < '0' || *cp > '9')
		return (-1);
	for (n = 0; *cp >= '0' && *cp <= '9'; cp++)
		n = n * 10 + (*cp - '0');
	return (n);
}

/*
 * Change system name.
 */
changename(sname)
char	*sname;
{
	char	newname[SYS_NMLN + 2];
	int	fd;

	if (strlen(sname) >= SYS_NMLN) {
		fprintf(stderr, "uname: name must be <= %d characters.\n",
			SYS_NMLN - 1);
		exit(1);
	}
	strcpy(newname, sname);
	strcat(newname, "\n");
	if ((fd = open(NODEFILE, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
		perror("uname");
		exit(1);
	}
	if (write(fd, newname, strlen(newname)) != strlen(newname)) {
		perror("uname");
		exit(1);
	}
	close(fd);
}

usage()
{
	printf("usage:\tuname [-snrvmaKF]\n\tuname [-S system_name]\n");
	exit(1);
}

/* end of uname.c */
