/*
 * opendir/readdir/closedir over the COHERENT filesystem's raw directory
 * records.  <sys/dir.h> makes DIR a bare `char *' and this libc ships no
 * directory-stream calls, so a directory is read here as what it is on disk:
 * an array of `struct direct', 16 bytes each, with a d_ino of 0 marking a
 * free slot and a d_name that is NOT terminated when it fills all DIRSIZ
 * bytes.  readdir() hands back a record with two extra name bytes, so the
 * name a caller walks is always terminated.
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/dir.h>
#include <fcntl.h>

struct cohent {				/* struct direct, name-terminated */
	ino_t	d_ino;
	char	d_name[DIRSIZ + 2];
};

struct cohdir {
	int		cd_fd;		/* open directory */
	struct cohent	cd_ent;		/* record handed to the caller */
};

char *malloc();

DIR
opendir(name)
char *name;
{
	register struct cohdir *dp;

	if ((dp = (struct cohdir *)malloc(sizeof(struct cohdir))) == NULL)
		return (NULL);
	if ((dp->cd_fd = open(name, O_RDONLY)) < 0) {
		free((char *)dp);
		return (NULL);
	}
	return ((DIR)dp);
}

struct direct *
readdir(dirp)
DIR dirp;
{
	register struct cohdir *dp = (struct cohdir *)dirp;
	struct direct raw;

	while (read(dp->cd_fd, (char *)&raw, sizeof raw) == sizeof raw) {
		if (raw.d_ino == 0)
			continue;
		dp->cd_ent.d_ino = raw.d_ino;
		strncpy(dp->cd_ent.d_name, raw.d_name, DIRSIZ);
		dp->cd_ent.d_name[DIRSIZ] = '\0';
		return ((struct direct *)&dp->cd_ent);
	}
	return (NULL);
}

closedir(dirp)
DIR dirp;
{
	register struct cohdir *dp = (struct cohdir *)dirp;

	close(dp->cd_fd);
	free((char *)dp);
	return (0);
}
