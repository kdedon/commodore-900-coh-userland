/*
 * Copyright (c) 1977-1995 Robert Swartz.
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * fdformat -- format a floppy disk.
 *
 * The named device is opened read/write and the format request is handed to
 * the driver as a single argumentless ioctl.  <sys/fdioctl.h> describes
 * FDFORMAT as a per-track command taking an array of `struct fform', but that
 * is the interface of the general floppy driver; the Commodore controller
 * formats a whole disk in one command and its driver takes no parameter block
 * at all (wdioctl()/wdformat(), CFFMT).
 *
 * The driver accepts the request on the floppy minors alone and only for the
 * super-user; anything else comes back ENODEV or EPERM and is reported here
 * as the errno the call returned.
 */
#include <stdio.h>
#include <sys/fdioctl.h>

/*
 * For the C compiler.
 */
int	usage();

/*
 * Variables.
 */
char	*progname;			/* argv[0], for messages */

main(argc, argv)
int argc;
char *argv[];
{
	register int fd;

	progname = argv[0];
	if (argc != 2)
		usage();
	fd = open(argv[1], 2);
	if (fd < 0) {
		fprintf(stderr, "%s: can't open %s\n", progname, argv[1]);
		exit(1);
	}
	if (ioctl(fd, FDFORMAT, (char *)0) < 0) {
		perror(progname);
		exit(1);
	}
	exit(0);
}

usage()
{
	fprintf(stderr, "Usage: %s device\n", progname);
	exit(1);
}
