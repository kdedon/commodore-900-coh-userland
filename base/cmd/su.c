/*
 * Copyright (c) 1977-1995 Robert Swartz.
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Rec'd from Lauren Weinstein, 7-16-84.
 * Substitute user-id temporarily or become super user (as you wish).
 * Compile -s -n -i.
 * Hacked by steve 10/4/90 to correct password bug and for clarity.
 */

#include <stdio.h>
#include <pwd.h>
#include <sys/stat.h>

extern	char *getenv();
extern	char *getpass();
extern	char *malloc();

#define ACCNAME "remacc"	/* Remote access password dummy username */
#define	DEFSHELL "/bin/sh"	/* Default shell pathname */
#define PASSLEN 13		/* Encrypted password length */
/*
 * The local console's driver, slot 8 of the kernel's device table (drvl[] in
 * every one of sys/z8001/{con,rec}/*.c names `kvcon' there, whichever of the
 * three consoles that kernel was configured with).  A terminal on this major is
 * a keyboard and a screen attached to this machine; major 5 is an SCC serial
 * port and major 9 a pseudo-tty, which is what a network login arrives on.
 * login(1) identifies the console the same way.
 */
#define KVCON_MAJOR 8		/* Major device of the local console */

#define	bye()	fatal("Sorry")

/* Forward. */
extern	void	addenviron();
extern	void	fatal();
extern	void	getuname();
extern	int	atconsole();

/* Globals. */
char	*defargs[] = { "su", NULL };
/* The following are all set by getuname(). */
short	gid;
char	*password;
char	salt[3];
short	uid;

main(argc, argv) int argc; char *argv[];
{
	register int ouid, ogid;
	char *command, *prompta, *promptb, *passp;
	char **args;

	getuname(argc>1 ? argv[1] : "0");	/* check username */
	/*
	 * A password field that is not an encrypted password is not a
	 * credential: an empty one skips the check below entirely, and one
	 * shorter than PASSLEN cannot be matched by anything crypt(3) returns.
	 * /etc/passwd gives uid 0 an empty field on purpose, so that a machine
	 * whose only input is the keyboard in front of it is never locked out of
	 * itself -- which makes uid 0 with no credential a CONSOLE privilege
	 * here, exactly as it is for login(1), and not one to be had from a
	 * terminal somewhere else.  So becoming the super user without a
	 * password is refused unless one of this process's standard descriptors
	 * is open on one of this machine's own consoles.
	 *
	 * All three are asked because a shell redirecting su's output is still a
	 * console session; a session that is not on the console has none of the
	 * three on the console driver, and no unprivileged program can make a
	 * node on it (mknod(2) refuses a character device to anyone but the
	 * super user).
	 *
	 * A root account that HAS a password is unaffected: the field is then a
	 * credential and the check below asks for it, from any terminal.
	 */
	if (uid == 0 && getuid() != 0 && strlen(password) != PASSLEN
	 && !atconsole())
		fatal("root has no password: su to it from the console only");
	if (password[0] != '\0' && getuid()) {	/* check password if not already su */
		passp = getpass("Password: ");	/* get input password choice */
		if ((strlen(password) != PASSLEN)
		 || (strcmp(crypt(passp, salt), password) != 0)) {
			ouid = uid;
			ogid = gid;
			getuname("0");		/* check root password too */ 
			if ((strlen(password) != PASSLEN)
			 || (strcmp(crypt(passp, salt), password) != 0))
				bye();		/* failure */
			uid = ouid;
			gid = ogid;
		}
	}

	if (argc > 2) {
		command = argv[2];
		args = &argv[2];
	} else {
		command = getenv("SHELL");
		if (command == NULL || strlen(command) < 1)
			command = DEFSHELL;
		args = defargs;
	}
	setgid(gid);
	setuid(uid);
	prompta = getenv("PSN");		/* check for normal prompt */
	promptb = getenv("PSS");		/* check for desired su prompt */
	addenviron(uid == 0 ? (promptb ? promptb : "# ")
			    : (prompta ? prompta : "$ "));
						/* change prompt as appropriate */
	execvp(command, args);
	fatal("%s: not found", command);
}

/*
 * Add string 's' to the environment as "PS1".
 */
void
addenviron(s) char *s;
{
	extern char **environ;
	register char **epp1, **epp2;
	register char **newenv;
	int n;
	char *prompt;
	static char prbuf[50];

	for (epp1 = environ; *epp1!=NULL; epp1++)
		;
	n = (epp1-environ+2) * sizeof (char *);
	if ((newenv = (char **)malloc(n)) == NULL)
		fatal("Out of memory for environments");
	prompt = prbuf;
	strcpy(prompt, "PS1=");
	strcat(prompt, s);
	for (epp1=environ, epp2=newenv; *epp1 != NULL; epp1++)
		if (strncmp(*epp1, "PS1=", 4) != 0)
			*epp2++ = *epp1;
		else {
			*epp2++ = prompt;
			prompt = NULL;
		}
	*epp2++ = prompt;
	*epp2 = NULL;
	environ = newenv;
}

/*
 * Is any of standard input, output or error open on one of this machine's own
 * consoles?  The DEVICE is what is asked, not the name: a node called
 * /dev/console that some other driver answers is not a console, and a second
 * node made on the console driver is one.
 *
 * A failed fstat(2), or a descriptor that is not a character device, counts
 * against the caller: this is a test a privilege is refused on, so anything it
 * cannot establish answers "not the console".
 */
int
atconsole()
{
	struct stat sbuf;
	int fd;

	for (fd = 0; fd < 3; fd++) {
		if (fstat(fd, &sbuf) < 0)
			continue;
		if ((sbuf.st_mode & S_IFMT) != S_IFCHR)
			continue;
		if (major(sbuf.st_rdev) == KVCON_MAJOR)
			return 1;
	}
	return 0;
}

void
fatal(s) char *s;
{
	fprintf(stderr, "%r\n", &s);
	exit(1);
}

/*
 * Get a user-name from string 's'.
 * If the string starts with a numeric, use it directly as a uid.
 * Set globals password, salt[], gid and uid with the user's password info.
 * Die if not found or illegal.
 */
void
getuname(s) register char *s;
{
	register struct passwd *pwp;

	if (*s >= '0' && *s <= '9') {
		uid = atoi(s);
		if ((pwp = getpwuid(uid)) == NULL)
			fatal("%d: bad user number", uid);
	} else if ((pwp = getpwnam(s)) == NULL)
		fatal("%s: not a user name", s);
	if (strcmp(pwp->pw_name, ACCNAME) == 0)	/* dummy access username? */
		bye();				/* yes, sorry */
	password = pwp->pw_passwd;
	salt[0] = pwp->pw_passwd[0];
	salt[1] = pwp->pw_passwd[1];
	salt[2] = '\0';
	gid = pwp->pw_gid;
	uid = pwp->pw_uid;
}

/* end of su.c */
