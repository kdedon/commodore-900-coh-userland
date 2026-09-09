/*
 * halt -- bring the machine to a stop with every filesystem marked clean.
 *
 * 	/etc/halt [-n] [-u]
 *
 * 	-n	do not sync; stop the machine as it stands
 * 	-u	do not unmount anything; sync and stop
 *
 * WHAT CLEARS THE DIRTY FLAG IS sync(2), NOT the unmount.  A modified
 * filesystem carries FSDIRTY in its super block (sys/coh/fs2.c smod()) and the
 * next mount of it is forced read only until check(1) has passed over it.  The
 * byte is rewritten FSCLEAN by msync(), which sync(2) calls for every mounted
 * filesystem once its inodes and blocks are on the disk -- so a filesystem
 * that is still mounted when the machine stops is as clean as one that was
 * unmounted, provided the last thing to touch it was a sync.  That is why the
 * sync here is last and why -n has to be asked for explicitly.
 *
 * The unmount is still worth doing and is done first: an unmounted filesystem
 * cannot be dirtied again between the sync and the stop, and umount(2) refuses
 * while anything holds a file or a working directory on it, which is a report
 * about the state of the machine that a bare sync does not give.  A refused
 * unmount is therefore printed and then ignored -- it is not a reason to leave
 * the disk dirty, which is what abandoning the halt would do.
 *
 * THE TABLE HOLDS A BASENAME, NOT A PATH.  mount(1) writes the special file
 * through mcopy(), which keeps only what follows the last `/' (cmd/mount.c),
 * so the entry for /dev/hd6 reads `hd6' -- and umount(2) wants a path.  The
 * `/dev/' is put back here.  That is the same assumption mount(1) and
 * umount(1) already make of the table, since they compare basenames to decide
 * which record a device owns; a block special outside /dev is not something
 * this table can describe.
 *
 * ROOT IS IN /etc/mnttab AND MUST NOT BE UNMOUNTED.  /etc/rc mounts it with
 * `mount /dev/hd4 / -u', which writes the table entry without mounting
 * anything, so the table names the root filesystem like any other and the
 * entry has to be recognised and skipped.  The rest are unmounted in reverse
 * table order, because rc mounts shallow before deep (/usr before /usr/man)
 * and a mount point cannot be unmounted while another filesystem sits inside
 * it.  umount(1) zeroes a record in place rather than closing the gap, so an
 * all-zero entry is a hole and not the end of the table.
 *
 * THE MACHINE STOPS INSIDE halt(2), and what "stops" means depends on the
 * kernel: without KDDT, md.s halt_ disables interrupts and jumps to itself,
 * which is the stop this command is named for; with KDDT linked the same entry
 * is the in-kernel debugger's, and halt(2) returns when the debugger is told
 * to continue.  So the call is not the end of the program -- a return from it
 * is reported, because a machine that carried on is not a machine that halted.
 *
 * Everything is written and flushed before the call.  Nothing after it can
 * reach the console on the kernel that does stop.
 */
#include <stdio.h>
#include <mnttab.h>
#include <errno.h>

extern	int	errno;

char	mnttabf[] = "/etc/mnttab";

#define	NMNT	16		/* table entries read; more are ignored */

struct	mnttab	tab[NMNT];

main(argc, argv)
int argc;
char *argv[];
{
	register int i;
	register FILE *fp;
	int nmnt, nfail, dosync, dounmount;
	char *cp;
	char spec[MNTNSIZ+6];

	dosync = 1;
	dounmount = 1;
	while (--argc > 0 && (*++argv)[0] == '-')
		for (cp = argv[0]+1; *cp != '\0'; cp++)
			switch (*cp) {
			case 'n':
				dosync = 0;
				break;
			case 'u':
				dounmount = 0;
				break;
			default:
				fprintf(stderr,
				    "Usage: /etc/halt [-n] [-u]\n");
				exit(1);
			}
	if (argc > 0) {
		fprintf(stderr, "Usage: /etc/halt [-n] [-u]\n");
		exit(1);
	}
	if (getuid() != 0) {
		fprintf(stderr, "halt: only the superuser can halt the machine\n");
		exit(1);
	}

	nmnt = 0;
	nfail = 0;
	if (dounmount) {
		if ((fp = fopen(mnttabf, "r")) == NULL)
			printf("halt: no %s, nothing to unmount\n", mnttabf);
		else {
			while (nmnt < NMNT
			    && fread(&tab[nmnt], sizeof(tab[0]), 1, fp) == 1)
				nmnt++;
			fclose(fp);
		}
		/* Flush before unmounting: umount(2) writes what is still
		 * dirty on the filesystem it is detaching, and a filesystem
		 * with nothing outstanding is one less thing for it to fail
		 * at. */
		sync();
		for (i = nmnt - 1; i >= 0; i--) {
			if (tab[i].mt_dev[0] == '\0')
				continue;
			if (strncmp(tab[i].mt_dev, "/", MNTNSIZ) == 0)
				continue;
			strcpy(spec, "/dev/");
			strncat(spec, tab[i].mt_filsys, MNTNSIZ);
			if (umount(spec) < 0) {
				printf("halt: %s (%s) still mounted: %s\n",
					spec, tab[i].mt_dev,
					errno == EBUSY ? "in use" :
					errno == EINVAL ? "not mounted" :
					"cannot unmount");
				nfail++;
			} else
				printf("halt: unmounted %s\n", spec);
		}
	}

	if (dosync) {
		sync();
		sync();
		printf("halt: filesystems are clean\n");
	} else
		printf("halt: NOT synced, as asked\n");
	if (nfail != 0)
		printf("halt: %d filesystem(s) remain mounted; they are synced\n",
			nfail);
	printf("halt: the machine is stopping -- it is safe to switch off\n");
	fflush(stdout);
	fflush(stderr);

	halt();

	printf("halt: halt(2) returned -- the machine did NOT stop\n");
	if (errno != 0)
		printf("halt: errno %d%s\n", errno,
			errno == EPERM ? " (EPERM: not root)" : "");
	fflush(stdout);
	exit(1);
}
