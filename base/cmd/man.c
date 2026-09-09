/*
 * man.c
 * 9/2/93
 * Usage: man [ -w ] [ topic ... ]
 * Quick and dirty man hack.
 * Read manual index, print manual sections to screen via $PAGER.
 *
 * Index lines come in two forms:
 *	relative-path article			("old", space separated)
 *	relative-path<tab>article<tab>description	("new", release 4.2)
 * Both are accepted.  The index is scanned a line at a time, so its size is
 * not bounded by a buffer.
 */

#include <stdio.h>
#include <string.h>

#define	DEFPAGER	"exec /bin/scat -1"	/* default $PAGER	*/
#define	MANINX		"/usr/man/man.index"	/* manual index	file	*/
#define	NBUF		160			/* buf[] buffer size	*/
#define	NCMD		512			/* cmd[] buffer size	*/
#define	USAGE		"man article ..."
#define	VERSION		"1.5"			/* version id		*/

extern	char	*getenv();

/* Globals. */
char	buf[NBUF];
char	cmd[NCMD];
int	wflag;

/* Forward. */
void	cmdcat();
void	fatal();
char	*lookup();
void	nonfatal();

main(argc, argv) int argc; char *argv[];
{
	int	i, status, found, nfound;
	char	*cp;
	FILE	*fp;

	status = 0;

	/* Look for environmental $PAGER. */
	if ((cp = getenv("PAGER")) == NULL)
		cp = DEFPAGER;
	strcpy(cmd, cp);

	/* Parse option args. */
	if (argc > 1 && strcmp(argv[1], "-w") == 0) {
		++wflag;
		--argc;
		++argv;
	}
	if (argc > 1 && strcmp(argv[1], "-V") == 0) {
		fprintf(stderr, "man: V%s\n", VERSION);
		--argc;
		++argv;
	}

	/* If no args, print usage. */
	if (argc == 1) {
		fprintf(stderr, "%s\n", USAGE);
		exit(status);
	}

	/* Args given.  Open the index. */
	if ((fp = fopen(MANINX, "r")) == NULL)
		fatal("cannot open manual index %s: on-line manual probably not installed", MANINX);

	/* Look up each arg. */
	nfound = 0;
	for (i = 1; i < argc; i++) {
		rewind(fp);
		found = 0;

		/* Look up arg in index.  May find multiple hits. */
		while ((cp = lookup(fp, argv[i])) != NULL) {
			found++;
			nfound++;
			if (wflag)
				printf("/usr/man/%s\n", cp);
			else {
				cmdcat(" ");
				cmdcat("/usr/man/");
				cmdcat(cp);
			}
		}
		if (found == 0) {
			nonfatal("%s not found in manual", argv[i]);
			status = 1;
		}
	}
	fclose(fp);
	if (!wflag && nfound)
		system(cmd);
	exit(status);
}

/*
 * Concatenate given string to cmd buffer.
 * Complain if too long.
 */
void
cmdcat(s) register char *s;
{
	register int len;

	len = strlen(cmd);
	if (len + strlen(s) + 1 >= NCMD)
		fatal("command buffer overflow");
	strcpy(&cmd[len], s);
}

/* Cry and die. */
void
fatal(s) char *s;
{
	fprintf(stderr, "man: %r\n", &s);
	exit(1);
}

/*
 * Read index lines from fp until one names article s.
 * Return its relative path name on match, else NULL.
 */
char *
lookup(fp, s) FILE *fp; char *s;
{
	register char *namep, *endptr;

	while (fgets(buf, NBUF, fp) != NULL) {
		if ((endptr = strchr(buf, '\n')) != NULL)
			*endptr = '\0';

		/* crop off description - fwb, 4/13/93 */
		if ((endptr = strrchr(buf, '\t')) != NULL)
			*endptr = '\0';

		/* crop off terminal parentheses, if any - fwb, 4/13/93 */
		if ((endptr = strrchr(buf, '(')) != NULL)
			*endptr = '\0';

		/* split the article name from the file name */
		if ((namep = strchr(buf, '\t')) == NULL)
			if ((namep = strrchr(buf, ' ')) == NULL)
				continue;

		*namep++ = '\0';	/* NUL-terminate index entry */

		/* crop off terminal parentheses, if any - fwb, 4/13/93 */
		if ((endptr = strrchr(namep, '(')) != NULL)
			*endptr = '\0';

		if (strcmp(namep, s) == 0)
			return buf;		/* gotcha */
	}
	return NULL;				/* no match */
}

void
nonfatal(s) char *s;
{
	fprintf(stderr, "man: %r\n", &s);
}

/* end of man.c */
