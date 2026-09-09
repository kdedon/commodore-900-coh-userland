#include <string.h>
#include <stdlib.h>

/*
 * putenv -- absent from libc.  Replaces or appends NAME=VALUE in environ,
 * growing a private copy of the vector the first time it has to.
 */
extern char **environ;

static char **myenv = (char **)0;
static int mysize = 0;

int
putenv(entry)
char *entry;
{
	register char **e;
	register char *eq;
	int n, len;

	if ((eq = strchr(entry, '=')) == (char *)0)
		return -1;
	len = eq - entry + 1;

	for (e = environ, n = 0; *e != (char *)0; e++, n++)
		if (strncmp(*e, entry, len) == 0) {
			*e = entry;
			return 0;
		}

	if (myenv == (char **)0 || n + 2 > mysize) {
		mysize = n + 16;
		if ((e = (char **)malloc((unsigned)((n + 17) * sizeof(char *))))
		    == (char **)0)
			return -1;
		memcpy((char *)e, (char *)environ, n * sizeof(char *));
		myenv = e;
		environ = e;
	}
	environ[n] = entry;
	environ[n + 1] = (char *)0;
	return 0;
}
