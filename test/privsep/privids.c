/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */

/*
 * privids -- report the four identities the kernel keeps for a process, and
 * ask the kernel's own permission question about a named file.
 *
 * The privilege gate (tests/privsep/run.sh) needs three things the shipped
 * userland cannot say on its own:
 *
 *   ids		all four of real/effective uid and gid.  whoami(1)
 *			prints the EFFECTIVE user only and `who am i' answers
 *			from /etc/utmp, so neither can tell a setuid program's
 *			real uid from its effective one.  getuid(2) returns
 *			u_ruid and geteuid(2) returns u_uid (sys/coh/sys1.c),
 *			and this is the only place both are printed together.
 *   read/write <f>	open(2) the file and report the errno.  A shell can
 *			only see that `cat' failed; imode() (sys/coh/fs1.c)
 *			answers EACCES for a permission refusal and ENOENT for
 *			a name it cannot even reach, and a test that cannot
 *			tell those apart would pass on a missing file.
 *   access <f>		access(2), which the kernel answers for the REAL ids
 *			(uaccess() calls schizo() around the lookup).  A
 *			setuid program asking "may the caller do this?" uses
 *			it, and mkdir(1) does exactly that on the parent
 *			directory before it mknod()s.
 *
 * K&R C, int is 16 bits: every id fits, and every value printed with %d.
 */

#include <stdio.h>
#include <errno.h>

char *me = "privids";

main(argc, argv)
int argc;
char **argv;
{
	register int fd;
	int n;
	char buf[64];

	if (argc < 2) {
		fprintf(stderr, "Usage: %s ids | read file | write file | access file\n",
			me);
		exit(2);
	}
	if (strcmp(argv[1], "ids") == 0) {
		printf("%s ids ruid=%d euid=%d rgid=%d egid=%d\n",
			me, getuid(), geteuid(), getgid(), getegid());
		exit(0);
	}
	if (argc < 3) {
		fprintf(stderr, "%s: %s needs a file name\n", me, argv[1]);
		exit(2);
	}
	errno = 0;
	if (strcmp(argv[1], "read") == 0) {
		if ((fd = open(argv[2], 0)) < 0) {
			printf("%s read %s DENIED errno=%d\n", me, argv[2], errno);
			exit(1);
		}
		n = read(fd, buf, sizeof buf - 1);
		if (n < 0)
			n = 0;
		buf[n] = '\0';
		/*
		 * The bytes matter as well as the open: a read that returns
		 * nothing on a file with content is a different failure from a
		 * refusal, and only one of the two is a permission answer.
		 */
		while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r'))
			buf[--n] = '\0';
		close(fd);
		printf("%s read %s ALLOWED bytes=%d text=%s\n", me, argv[2], n, buf);
		exit(0);
	}
	if (strcmp(argv[1], "write") == 0) {
		if ((fd = open(argv[2], 1)) < 0) {
			printf("%s write %s DENIED errno=%d\n", me, argv[2], errno);
			exit(1);
		}
		close(fd);
		printf("%s write %s ALLOWED\n", me, argv[2]);
		exit(0);
	}
	if (strcmp(argv[1], "access") == 0) {
		/* 4 = IPR, the read bit access(2) and imode() agree on. */
		if (access(argv[2], 4) != 0) {
			printf("%s access %s DENIED errno=%d\n", me, argv[2], errno);
			exit(1);
		}
		printf("%s access %s ALLOWED\n", me, argv[2]);
		exit(0);
	}
	fprintf(stderr, "%s: no such request as %s\n", me, argv[1]);
	exit(2);
}
