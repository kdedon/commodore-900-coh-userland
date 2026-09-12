/*
 * Copyright (c) 1977-1995 Robert Swartz.
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * sh/main.c
 * The Bourne shell.
 * Main program, initialization and miscellaneous routines.
 */

#include <sys/param.h>
#include "sh.h"

main(argc, argv, envp)
char *argv[];
char *envp[];
{

	sarg0 = argc>0 ? argv[0] : "";
	fakearg(0, argc, argv, envp);
	if (argc>0 && argv[0][0]=='-') {
		lgnflag = 1;
		umask(ufmask=022);
	} else if (argc>0 && argv[0][0]=='+') {
		lgnflag = 2;
		umask(ufmask=022);
	} else {
		umask(ufmask=umask(ufmask));
	}

	if (setjmp(restart) != 0) {
		/* reentry for shell command file execution */
		fakearg(1, nargc, nargv, nenvp);
		argc = nargc;
		argv = nargv;
		envp = nenvp;
		cmdflag++;
		nllflag = 0;
	}

	shpid = getpid();
	initvar(envp);
	cleanup(1, NULL);
	if (set(argc, argv, 1))
		return(1);
	/*
	 * The restricted shell.  /usr/bin/rsh is this same program under a
	 * second name, so being invoked as `rsh' sets rflag exactly as -r
	 * does.  A login shell arrives with its name prefixed by `-', and may
	 * arrive as a pathname, so strip both before comparing.
	 */
	{
		register char *cp, *bp;

		cp = sarg0;
		if (*cp == '-')
			cp += 1;
		for (bp = cp; *cp; cp += 1)
			if (*cp == '/')
				bp = cp + 1;
		if (strcmp(bp, "rsh") == 0)
			rflag = 'r';
	}
	/*
	 * The restrictions begin only once the profiles have run.  That
	 * ordering is the whole feature: the administrator's /etc/profile and
	 * .profile build the environment -- the confined PATH above all -- and
	 * they cannot do that under a shell that already refuses to set PATH.
	 * session() moves the flag back out of rshpend at that point.
	 */
	if (rflag) {
		rshpend = rflag;
		rflag = 0;
	}
	if (cflag) {
		if (sargp[0]==NULL) {
			printe("No string for -c?");
			return(1);
		}
		--sargc;
		session(SARGS, *sargp++);
	} else if (!sflag && !iflag && sargc!=0) {
		sarg0 = *sargp++;
		--sargc;
		if (scmdp == NULL)
			scmdp = sarg0;
		session(SFILE, scmdp);
	} else {
		session(SSTR, stdin);
	}
	cleanup(2, NULL);
	return (slret);
}

/*
 * Make the arg listing of ps come out right.
 *	f == 0, first entry, determine buffer limits.
 *	f != 0, later entry, fill buffer with lies.
 */
fakearg(f, argc, argv, envp)
int f, argc;
char **argv, **envp;
{
	static char *fbuf;
	static int nbuf;
	register int n;

	if (f == 0) {
		fbuf = argv[0];
		nbuf = 0;
		if (envp != NULL && envp[0] != NULL) {
			while (envp[1] != NULL)
				envp += 1;
			nbuf = envp[0] - fbuf + strlen(envp[0]) - 1;
		} else if (argc > 0)
			nbuf = argv[argc-1] - fbuf + strlen(argv[argc-1]) - 1;
	} else {
		if (fbuf == NULL || nbuf == 0)
			return;
		n = 0;
		fbuf[0] = 0;
		while (--argc > 0) {
			argv += 1;
			n += strlen(argv[0]) + 1;
			if (n >= nbuf)
				break;
			strcat(fbuf, argv[0]);
			strcat(fbuf, " ");
		}
		strcat(fbuf, "\1");	/* non-ascii terminator */
	}
}

/*
 * Loop on input.
 */
session(t, p)
register char *p;
{
	SES s;
	register int rcode;

	s.s_next = sesp;
	sesp = &s;
	s.s_bpp = savebuf();

	switch (s.s_type = t) {
	case SARGS:
		s.s_strp = p;
		s.s_flag = 0;
		break;
	case SARGV:
		s.s_argv = (char **) p;
		if ((s.s_strp = s.s_argv[0]) == NULL)
			return (0);
		s.s_flag = 0;
		break;
	case SFILE:
		s.s_strp = p;
		if ((s.s_ifp = fopen(s.s_strp, "r")) == NULL) {
			ecantopen(s.s_strp);
			/*
			 * The status carries the failure out: main() returns
			 * slret, and the session that ran a `.' on a file it
			 * could not read leaves the same answer in $?.
			 */
			slret = SLFAIL;
			freebuf(s.s_bpp);
			sesp = s.s_next;
			return (SLFAIL);
		}
		s.s_flag = isatty(fileno(s.s_ifp)) && isatty(2);
		break;
	case SSTR:
		s.s_strp = NULL;
		s.s_ifp = (FILE *) p;
		s.s_flag = isatty(fileno(s.s_ifp)) && isatty(2);
		break;
	}

	if (s.s_next == NULL) {		/* Initial entry */
		if (iflag)
			s.s_flag = iflag;
		else
			iflag = s.s_flag;
		dflttrp(IRDY);
	}

	/* Loop on input */
	for (;;) {
		rcode = setjmp(s.s_envl);
		switch (rcode) {
		case RSET:	/* initial setjmp call */
			switch (lgnflag) {
			case 1:		/* - sign invocation */
				lgnflag = 0;
				if (ffind("/etc", "profile", 4))
					session(SFILE, duplstr(strt, 0));
				recover(IPROF);
				if (*vhome && ffind(vhome, ".profile", 4))
					session(SFILE, duplstr(strt, 0));
				break;
			case 2:		/* + sign invocation */
				lgnflag = 0;
				if (ffind("/etc", "profile", 4))
					session(SFILE, duplstr(strt, 0));
				recover(IPROF);
				return exshell( findvar("SHELL") );
			}
			/*
			 * Restrict from here on.  The test on s_next keeps
			 * this out of the nested sessions the profiles above
			 * run in: those are the last unrestricted commands a
			 * restricted shell executes.  A shell that is not a
			 * login shell reaches this on its first pass with no
			 * profile having run, so it restricts immediately.
			 */
			if (rshpend && s.s_next == NULL) {
				rflag = rshpend;
				rshpend = 0;
			}
			checkmail();
			comflag = 1;
			errflag = 0;
			recover(IRDY);
			freebuf(s.s_bpp);
			s.s_bpp = savebuf();
			synclear();
			if (yyparse() != 0)
				syntax();
		case REOF:
			recover(IRDY);
			break;
		case RCMD:
			recover(IRDY);
			s.s_con = NULL;
			command(s.s_node);
			if ((tflag && tflag++ >= 2))
				break;
			continue;
		case RERR:
			recover(IRDY);
			if ( ! errflag)
				syntax();
			if ( ! iflag || (tflag && tflag++ >= 2))
				break;
			continue;
		case RINT:
			if (s.s_next != NULL) {
				sesp = s.s_next;
				reset(RINT);
				NOTREACHED;
			}
			prpflag = 2;
			if ( ! iflag || (tflag && tflag++ >= 2))
				break;
			continue;
		case RUEXITS:
		case RUABORT:
			if (s.s_next != NULL) {
				sesp = s.s_next;
				reset(rcode);
				NOTREACHED;
			}
			if (rcode == RUEXITS || !iflag || (tflag && tflag++ >= 2))
				break;
			continue;
		case RNOSBRK:
		case RSYSER:
		case RBRKCON:
		case RNOWAY:
		default:
			if (s.s_next!=NULL)
				break;
			if ( ! iflag || (tflag && tflag++ >= 2))
				break;
			continue;
		}
		break;
	}
	freebuf(s.s_bpp);
	if (s.s_type == SFILE)
		fclose(s.s_ifp);
	if (s.s_next == NULL) {
		sigintr(0);
		recover(IRDY);
	}
	sesp = s.s_next;
	return (slret);
}

reset(f)
{
	longjmp(sesp->s_envl, f);
	NOTREACHED;
}

/*
 * Kludge cleanup.
 */
cleanup(flag, file)
char *file;
{
	static char *files[8];
	static int nfiles = 0;
	register char **pp;

	if (flag) {
		for (pp=files; pp<files+8; pp+=1)
			if (*pp != NULL) {
				if (flag==2)
					unlink(*pp);
				sfree(*pp);
				*pp = NULL;
			}
		nfiles = 0;
	} else {
		pp = files + nfiles;
		if (*pp != NULL) {
			unlink(*pp);
			sfree(*pp);
		}
		*pp = duplstr(file, 1);
		nfiles = (nfiles + 1) & 7;
	}
}

/*
 * Make a temp file name.
 */
char *
shtmp()
{
	static char tmpfile[] = "/tmp/shXXXXXX";
	static int tmpflag = 0;

	sprintf(tmpfile+6, "%05d%c", shpid, (tmpflag++%26) + 'a');
	return (tmpfile);
}

/*
 * Print formatted.
 */
/*
printv(av)
register char **av;
{
	while (*av) prints("\t%s\n", *av++);
}
*/

prints(a1)
char *a1;
{
	fprintf(stderr, "%r", &a1);
}

/*
 * Make a core dump in /tmp and longjmp back to session -
 *	there's a possibility we'll die horribly.
 */
panic(i) register int i;
{
#ifdef PARANOID
	register int f;

	if ((f=fork())==0) {
		abort();
		NOTREACHED;
	}
	waitc(f);
#endif
	printe("Internal shell assertion %d failed", i);
	reset(RNOWAY);
	NOTREACHED;
}

/*
 * Print out an error message.
 */
printe(a1)
char *a1;
{
	errflag += 1;
	if (! noeflag)
		fprintf(stderr, "%r\n", &a1);
}

/*
 * Some familiar errors.
 */
ecantopen(s) char *s; { printe("Cannot open %s", s); }
ecantfind(s) char *s; { printe("Cannot find %s", s); }
e2big(s) char *s; { printe("File to big to execute: %s", s); }
ecantmake(s) char *s; { printe("Cannot create %s", s); }
emisschar(c) { printe("Missing `%c'", c); }
ecantfdop() { printe("Fdopen failed"); }
enotdef(s) char *s; { printe("Cannot find variable %s", s); }
eillvar(s) char *s; { printe("Illegal variable name: %s", s); }
eredir() { printe("Illegal redirection"); }
erestrict(s) char *s; { printe("Restricted: %s", s); }
etoolong() { printe("Argument too long: %.*s", STRSIZE, strt); }

/*
 * The token the parser was holding when it rejected the input.  A newline is
 * held as an empty string: syntax() has its own wording for a construct that
 * ran off the end of a line.
 */
static char syntok[32] = "";

/*
 * Begin a parse with nothing carried over from the one before.  A command
 * line leaves yyparse() by longjmp rather than by returning, so the parser's
 * recovery counter -- which suppresses the report of an error following too
 * closely on another -- is still counting down from the previous line, and
 * the next line's error would be rejected without a word about the token.
 */
static
synclear()
{
	extern short yyerrflag;

	yyerrflag = 0;
	syntok[0] = '\0';
}

/*
 * The parser calls this the moment it rejects a token, and then reads on to
 * the next newline looking for a place to resume.  That read overwrites the
 * lexer's token buffer, so the token is copied out here rather than in
 * syntax(), which prints the message afterwards.  The message itself is
 * syntax()'s, not the parser's.
 */
yyerror()
{
	register char *cp;
	register int n;

	cp = strt;
	if (cp == NULL || *cp == '\n')
		cp = "";
	for (n = 0; n < sizeof(syntok)-1 && cp[n] != '\0'; n += 1)
		syntok[n] = cp[n];
	syntok[n] = '\0';
}

/*
 * print out the prompt given the prompt to write
 */
prompt(vps)
char *vps;
{
	prints("%s", vps);
#if RSX
	fflush(stdout);
#endif
}

/*
 * Syntax error message - print where the parse failed, and the file it was
 *	reading if that was a file.
 *
 * The line number is left off only for input typed at a terminal, where the
 * count restarts at every line and so always reads 1.  Everything else -- a
 * script, a redirected standard input, the string of a `-c' -- is counted
 * from its first line and says which one failed.
 *
 * A syntax error is the shell's own failure rather than a command's, so it
 * becomes the shell's exit status: a noninteractive shell stops here, and
 * nothing else would report the failure to whatever ran it.
 */
syntax()
{
	register SES *sp;
	register int hasfp;
	char msg[80];

	sp = sesp;
	/* Only these two read through a stream; the others leave s_ifp unset. */
	hasfp = sp->s_type==SFILE || sp->s_type==SSTR;
	if (hasfp && feof(sp->s_ifp))
		strcpy(msg, "Syntax error at EOF");
	else if (syntok[0] != '\0')
		sprintf(msg, "Syntax error near `%s'", syntok);
	else
		strcpy(msg, "Syntax error at newline");
	if ( ! (hasfp && sp->s_flag))
		sprintf(msg+strlen(msg), " in line %d", yyline);
	if (sp->s_type == SFILE)
		printe("%s: %s", sp->s_strp, msg);
	else
		printe("%s", msg);
	slret = SLFAIL;
}

/* end of sh/main.c */
