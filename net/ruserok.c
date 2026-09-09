/*
 * iruserok / ruserok -- the .rhosts and /etc/hosts.equiv check that decides
 * whether a remote user may act as a local one without a password.
 *
 * netlib.h has declared iruserok() since the stack was ported and nothing
 * implemented it; rshd is the first program that needs it.  This is the 4.3BSD
 * rule, written against this system's libc:
 *
 *	/etc/hosts.equiv	one host name per line.  A match makes the
 *				remote user equivalent to the LOCAL user of
 *				the same name.  Not consulted for root.
 *	~luser/.rhosts		"host" or "host ruser".  Must be a regular
 *				file owned by luser (or root) and not writable
 *				by anyone else.
 *
 * The host in the file is compared against the name gethostbyaddr() gives the
 * caller's address, and against that name's aliases; a file entry that is a
 * dotted quad is compared against the address itself, which is how a machine
 * with no reverse mapping can still be named.  There is no NIS and no netgroup
 * syntax here, so a leading `+' or `-' is not special and is matched literally
 * -- it can only ever fail to match a real host name.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <ansi.h>
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <net/gen/in.h>
#include <net/gen/inet.h>
#include <net/gen/socket.h>
#include <net/gen/netdb.h>
#include <net/netlib.h>

#define HOSTS_EQUIV	"/etc/hosts.equiv"

static int rhosts_file();
static int host_matches();

/*
 * Returns 0 if `ruser' on the machine at `raddr' may become `luser' here, and
 * -1 if not.  `superuser' non-zero means luser is root, for whom
 * /etc/hosts.equiv does not count -- only root's own .rhosts.
 */
int iruserok(raddr, superuser, ruser, luser)
unsigned long raddr;
int superuser;
char *ruser;
char *luser;
{
	struct passwd *pw;
	struct hostent *hp;
	char *hname;
	char rhosts[256];

	if ((pw= getpwnam(luser)) == (struct passwd *)0)
		return -1;

	/*
	 * The caller's name, once.  A machine with no reverse mapping has no
	 * name at all, and then only a dotted-quad entry can match it.
	 */
	hname= (char *)0;
	hp= gethostbyaddr((char *)&raddr, sizeof(raddr), AF_INET);
	if (hp != (struct hostent *)0)
		hname= hp->h_name;

	if (!superuser && rhosts_file(HOSTS_EQUIV, (ipaddr_t)raddr, hname, hp,
				      luser, ruser, 1) == 0)
		return 0;

	if (strlen(pw->pw_dir) + 9 > sizeof(rhosts))
		return -1;
	strcpy(rhosts, pw->pw_dir);
	strcat(rhosts, "/.rhosts");
	return rhosts_file(rhosts, (ipaddr_t)raddr, hname, hp, luser, ruser, 0);
}

/* The name-based form, for a caller that already has the peer's host name. */
int ruserok(rhost, superuser, ruser, luser)
char *rhost;
int superuser;
char *ruser;
char *luser;
{
	struct hostent *hp;
	ipaddr_t addr;

	addr= 0;
	if ((hp= gethostbyname(rhost)) != (struct hostent *)0 &&
	    hp->h_length == sizeof(addr))
		memcpy((char *)&addr, hp->h_addr, sizeof(addr));
	return iruserok((unsigned long)addr, superuser, ruser, luser);
}

/*
 * Walk one file.  `equiv' says this is hosts.equiv, where a line names a host
 * only and the user names must be equal; in a .rhosts file a second word names
 * the remote user explicitly.
 *
 * A .rhosts that anyone but its owner can write is ignored entirely -- it is a
 * password file for a machine, and one that the world can append to grants the
 * account to the world.
 */
static int rhosts_file(path, raddr, hname, hp, luser, ruser, equiv)
char *path;
ipaddr_t raddr;
char *hname;
struct hostent *hp;
char *luser;
char *ruser;
int equiv;
{
	FILE *f;
	struct stat st;
	struct passwd *pw;
	char line[256];
	char *host, *user, *p;

	if ((f= fopen(path, "r")) == (FILE *)0)
		return -1;
	if (!equiv)
	{
		if (fstat(fileno(f), &st) < 0 ||
		    (st.st_mode & S_IFMT) != S_IFREG ||
		    (st.st_mode & 022) != 0)
		{
			fclose(f);
			return -1;
		}
		pw= getpwnam(luser);
		if (pw == (struct passwd *)0 ||
		    (st.st_uid != 0 && st.st_uid != pw->pw_uid))
		{
			fclose(f);
			return -1;
		}
	}

	while (fgets(line, sizeof(line), f) != (char *)0)
	{
		p= line;
		while (*p && *p != '\n' && *p != '#')
			p++;
		*p= '\0';

		host= line;
		while (*host == ' ' || *host == '\t')
			host++;
		p= host;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		user= (char *)0;
		if (*p)
		{
			*p++= '\0';
			while (*p == ' ' || *p == '\t')
				p++;
			if (*p)
			{
				user= p;
				while (*p && *p != ' ' && *p != '\t')
					p++;
				*p= '\0';
			}
		}
		if (*host == '\0')
			continue;

		if (!host_matches(host, raddr, hname, hp))
			continue;
		if (equiv || user == (char *)0)
		{
			if (strcmp(ruser, luser) == 0)
			{
				fclose(f);
				return 0;
			}
			continue;
		}
		if (strcmp(user, ruser) == 0)
		{
			fclose(f);
			return 0;
		}
	}
	fclose(f);
	return -1;
}

/* Does one file entry name the calling machine?  By address if the entry is a
 * dotted quad, otherwise by canonical name or by any alias of it. */
static int host_matches(entry, raddr, hname, hp)
char *entry;
ipaddr_t raddr;
char *hname;
struct hostent *hp;
{
	char **ap;
	ipaddr_t a;

	a= inet_addr(entry);
	if (a != (ipaddr_t)0xFFFFFFFFL)
		return a == raddr;
	if (hname && strcmp(entry, hname) == 0)
		return 1;
	if (hp)
	{
		for (ap= hp->h_aliases; ap && *ap; ap++)
		{
			if (strcmp(entry, *ap) == 0)
				return 1;
		}
	}
	return 0;
}
