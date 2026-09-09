/*
 * hostfs -- mount, sync and probe the host pass-through disk
 * (DEVELOPMENT DISTS ONLY; see sys/drv/hostfs.c for the device).
 *
 *	hostfs mount [dir]	load /drv/hostfs if needed, make the
 *				/dev nodes if missing, mount /dev/hfs
 *				on dir (default /mnt)
 *	hostfs umount [dir]	sync, unmount; the driver's last close
 *				asks the host to extract guest writes
 *	hostfs sync		flush the buffer cache, then ask the
 *				host to extract to its directory now
 *	hostfs status		say whether driver and daemon answer
 *
 * The heavy lifting is elsewhere: the device is an ordinary block
 * device, so mount(2)/umount(2) do the mounting, and the host daemon
 * (hostfsd) does every filesystem operation.  This tool only makes the
 * plumbing convenient from a root shell or a build script.
 */
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

#define	HFMAJOR		11
#define	HFIPING		(('h'<<8)|1)
#define	HFISYNC		(('h'<<8)|2)

#define	BDEV	"/dev/hfs"		/* block node, minor 0 */
#define	CDEV	"/dev/rhfs"		/* char node for ioctl */
#define	DRIVER	"/drv/hostfs"
#define	DEFDIR	"/mnt"

extern	int errno;

main(argc, argv)
int argc;
char *argv[];
{
	register char *dir;

	dir = (argc > 2) ? argv[2] : DEFDIR;
	if (argc < 2)
		usage();
	if (strcmp(argv[1], "mount") == 0)
		return (domount(dir));
	if (strcmp(argv[1], "umount") == 0)
		return (doumount(dir));
	if (strcmp(argv[1], "sync") == 0)
		return (dosync());
	if (strcmp(argv[1], "status") == 0)
		return (dostatus());
	usage();
}

usage()
{
	fprintf(stderr, "Usage: hostfs mount|umount [dir] | sync | status\n");
	exit(1);
}

/*
 * Make the device nodes if they are missing.  Dev dists ship no
 * hostfs nodes in /dev (the shipped devices table must not change),
 * so they are created here on first use.
 */
static
mkdevs()
{
	struct stat sb;

	if (stat(BDEV, &sb) < 0 &&
	    mknod(BDEV, S_IFBLK|0600, makedev(HFMAJOR, 0)) < 0) {
		perror(BDEV);
		return (-1);
	}
	if (stat(CDEV, &sb) < 0 &&
	    mknod(CDEV, S_IFCHR|0600, makedev(HFMAJOR, 0)) < 0) {
		perror(CDEV);
		return (-1);
	}
	return (0);
}

/*
 * Open the char node; if the driver is not loaded yet (ENXIO can also
 * mean "no daemon", so only one load attempt is made), run /etc/load.
 */
static int
opendrv()
{
	register int fd;

	if (mkdevs() < 0)
		return (-1);
	if ((fd = open(CDEV, 2)) >= 0)
		return (fd);
	system("/etc/load /drv/hostfs");
	if ((fd = open(CDEV, 2)) < 0)
		fprintf(stderr,
		    "hostfs: no driver or no hostfsd on the host (%s)\n",
		    CDEV);
	return (fd);
}

static
domount(dir)
char *dir;
{
	register int fd;

	if ((fd = opendrv()) < 0)
		return (1);
	close(fd);
	if (mount(BDEV, dir, 0) < 0) {
		fprintf(stderr, "hostfs: mount %s on %s failed (errno %d)\n",
		    BDEV, dir, errno);
		return (1);
	}
	printf("host directory mounted on %s\n", dir);
	return (0);
}

/*
 * umount(2) flushes the device's dirty buffers (iclose -> bflush), and
 * the explicit HFISYNC afterwards makes the host extract them; the
 * driver deliberately does nothing on close (see sys/drv/hostfs.c).
 */
static
doumount(dir)
char *dir;
{
	register int fd;

	sync();
	if (umount(BDEV) < 0) {
		fprintf(stderr, "hostfs: umount %s failed (errno %d)\n",
		    BDEV, errno);
		return (1);
	}
	if ((fd = opendrv()) < 0)
		return (1);
	if (ioctl(fd, HFISYNC, (char *)0) < 0) {
		fprintf(stderr, "hostfs: extract failed (errno %d)\n", errno);
		close(fd);
		return (1);
	}
	close(fd);
	printf("unmounted; host side extracted\n");
	return (0);
}

/*
 * sync(2) first: the extraction on the host reads the image, and the
 * guest's dirty buffers must reach it before the host walks the tree.
 */
static
dosync()
{
	register int fd;

	if ((fd = opendrv()) < 0)
		return (1);
	sync();
	sync();
	if (ioctl(fd, HFISYNC, (char *)0) < 0) {
		fprintf(stderr, "hostfs: sync ioctl failed (errno %d)\n",
		    errno);
		close(fd);
		return (1);
	}
	close(fd);
	printf("host side extracted\n");
	return (0);
}

static
dostatus()
{
	register int fd;

	if ((fd = opendrv()) < 0)
		return (1);
	if (ioctl(fd, HFIPING, (char *)0) < 0) {
		printf("driver loaded; hostfsd NOT answering\n");
		close(fd);
		return (1);
	}
	printf("driver loaded; hostfsd answering\n");
	close(fd);
	return (0);
}
