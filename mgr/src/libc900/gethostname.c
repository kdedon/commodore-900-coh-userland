#include <sys/utsname.h>
#include <string.h>

/* gethostname -- no gethostname(2); uname()'s nodename is the answer. */
int
gethostname(name, len)
char *name;
unsigned int len;
{
	struct utsname u;

	if (uname(&u) < 0)
		return -1;
	strncpy(name, u.nodename, (int)len);
	name[len - 1] = '\0';
	return 0;
}
