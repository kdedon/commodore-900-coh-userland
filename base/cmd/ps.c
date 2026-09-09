/*
 *	Modifications copyright INETCO Systems Ltd.
 *
 * Print out process statuses.
 *
 * $Log:	ps.c,v $
 * Revision 1.3  91/07/17  14:22:55  bin
 * stevesf supplied as INETCO source because previous sources are of
 * questionable origin... this one works
 * 
 * Revision 1.2	89/06/12  15:15:22 	src
 * Bug:	A directory at the end of the '/dev' directory would cause 'ps'
 * 	to crash with the message "Cannot open <dir> in /dev".
 * Fix:	A stat was being done without the return code being checked. (ART)
 * 
 * Revision 1.1	89/06/12  14:42:24 	src
 * Initial revision
 * 
 * 88/02/15	Allan Cornish	/usr/src/cmd/cmd/ps.c
 * Kernel processes are now displayed regardless of -ax flags.
 *
 * 87/11/25	Allan Cornish	/usr/src/cmd/cmd/ps.c
 * Debug flag now -D.  Drivers now displayed by -d flag.
 *
 * 87/11/12	Allan Cornish	/usr/src/cmd/cmd/ps.c
 * Modified to support Coherent 9.0 [protected mode] segmentation.
 *
 * 87/10/20	Allan Cornish	/usr/src/cmd/cmd/ps.c
 * Now extracts cmd name from u_comm field in uproc struct for kernel procs.
 * Kernel processes only displayed if -d [debug] flag given.
 *
 * 87/09/28	Phil Selby	/usr/src/cmd/cmd/ps.c
 * Altered so that the terminal name rather than major and minor
 * device number is printed out
 *
 * 84/08/21	Rec
 * Added gflag for group identification.
 *
 * 84/07/16	Lauren Weinstein
 * Initial version.
 */

#include <stdio.h>
#include <ctype.h>
#include <sys/const.h>
#include <l.out.h>
#include <pwd.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/dir.h>
#include <sys/seg.h>
#include <sys/stat.h>
#include <sys/uproc.h>

/*
 *	Terminal information stored in memory for use by ps in TTY field.
 *	dname holds a directory entry, which is DIRSIZ characters and carries
 *	no terminator when the name fills the field, so the member is one
 *	byte longer than the entry.
 */

struct handle {
	dev_t	dnum;		/* device number */
	char dname[DIRSIZ+1];	/* device name */
	struct handle *nptr;	/* pointer to next */
} *fptr, *lptr;

extern char *malloc();

/*
 *	Width of the TTY column.  shortty() abbreviates names to fit it, and
 *	the command-width budget `l' below is derived from it.
 */
#define TTYWIDE		3
char	*uname();

/*
 * Maximum sizes.
 */
#define ARGSIZE	512

/*
 * Functions to see if a pointer is in alloc space and to map a
 * pointer from alloc space to this process.
 */
#define range(p)	(p>=(char *)(aend) && p<(char *)(aend)+casize)
#define map(p)		(&allp[(char *)p - (char *)aend])

/*
 * For easy referencing.
 */
#define	aprocq		nl[0].n_value
#define	autime		nl[1].n_value
#define astime		nl[2].n_value
#define aasize		nl[3].n_value
#define	aend		nl[4].n_value

/*
 * Variables.
 */
int	aflag;				/* All processes */
int	dflag;				/* Driver flag */
int	dbflag;				/* Debug flag */
int	fflag;				/* Print out all fields */
int	gflag;				/* Print out process groups */
int	lflag;				/* Long format */
int	mflag;				/* Print scheduling values */
int	nflag;				/* No header */
int	rflag;				/* Print out real sizes */
int	tflag;				/* Print times */
int	wflag;				/* Wide format */
int	xflag;				/* Get special processes */
dev_t	ttdev;				/* Terminal device */

/*
 * Table for namelist.
 */
struct nlist nl[] ={
	"procq_",	0,	0,
	"utimer_",	0,	0,
	"stimer_",	0,	0,
	"asize_",	0,	0,
	"end_",		0,	0,
	""
};

/*
 * Symbols.
 */
char	 *allp;				/* Pointer to alloc space */
char	 *kfile;			/* Kernel data memory file */
char	 *nfile;			/* Namelist file */
char	 *mfile;			/* Memory file */
char	 *dfile;			/* Swap file */
int	 ownfile;			/* -c or -k named a file of the caller's */
char	 argp[ARGSIZE];			/* Arguments */
int	 kfd;				/* Kernel memory file descriptor */
int	 mfd;				/* Memory file descriptor */
int	 dfd;				/* Swap file descriptor */
struct	 uproc u;	 		/* User process area */
int	 cutime;			/* Unsigned time */
PROC	 cprocq;			/* Process queue header */
unsigned casize;			/* Size of alloc area */

main(argc, argv)
char *argv[];
{
	register int i;
	register char *cp;

	initialise();
	for (i=1; i<argc; i++) {
		for (cp=&argv[i][0]; *cp; cp++) {
			switch (*cp) {
			case '-':
				continue;
			case 'a':
				aflag++;
				continue;
			case 'c':
				if (++i >= argc)
					usage();
				nfile = argv[i];
				ownfile++;
				continue;
			case 'd':
				dflag++;
				continue;
			case 'D':
				dbflag++;
				continue;
			case 'f':
				fflag++;
				continue;
			case 'g':
				gflag++;
				continue;
			case 'k':
				if (++i >= argc)
					usage();
				mfile = argv[i];
				ownfile++;
				continue;
			case 'l':
				lflag++;
				continue;
			case 'm':
				mflag++;
				continue;
			case 'n':
				nflag++;
				continue;
			case 'r':
				rflag++;
				continue;
			case 't':
				tflag++;
				continue;
			case 'w':
				wflag++;
				continue;
			case 'x':
				xflag++;
				continue;
			default:
				usage();
			}
		}
	}
	execute();
	exit(0);
}

/*
 * Initialise.
 */
initialise()
{
	register char *cp;

	aflag = 0;
	gflag = 0;
	lflag = 0;
	mflag = 0;
	nflag = 0;
	xflag = 0;
	nfile = "/coherent";
	kfile = "/dev/kmem";
	mfile = "/dev/mem";
	dfile = "/dev/swap";
	if ((cp=malloc(BUFSIZ)) != NULL)
		setbuf(stdout, cp);
}

/*
 * Print out usage.
 */
usage()
{
	panic("Usage: ps [-][acdfgklmnrtwx]");
}

/*
 * Print out information about processes.
 */
execute()
{
	int c, l;
	register PROC *pp1, *pp2;

	/*
	 * This program is setuid root, because /dev/kmem, /dev/mem and
	 * /dev/swap are 600 root.  -c and -k replace two of the files it reads
	 * with files the caller names, and those are read with the caller's own
	 * rights: the privilege goes before the first open, so no file outside
	 * /dev is ever opened with an effective uid of 0.
	 */
	if (ownfile)
		setuid(getuid());

	nlist(nfile, nl);
	if (nl[0].n_type == 0)
		panic("Bad namelist file %s", nfile);
	if ((mfd=open(mfile, 0)) < 0)
		panic("Cannot open %s", mfile);
	if ((kfd = open(kfile, 0)) < 0)
		panic("Cannot open %s", kfile);

	/*
	 * Open swap device if it exists
	 */
	dfd = open(dfile, 0);

	/*
	 * Turn off setuid privileges.  The three descriptors above are the
	 * whole reason for the bit; the walk below reads nothing else, and
	 * fttys() and getpwuid() read files any user may read.
	 */
	setuid(getuid());

	kread((long)aasize, &casize, sizeof (casize));
	kread((long)aprocq, &cprocq, sizeof (cprocq));

	if ((allp=malloc(casize)) == NULL)
		panic( "Out of core or invalid kernel specified" );

	kread((long)aend, allp, casize);
	kread((long)autime, &cutime, sizeof (cutime));
	settdev();
	fttys( "/dev");	/* load all the devices in dev */
	l = TTYWIDE + 6;		/* the TTY column plus " %5d" of PID */
	if (dbflag)
		l += 9;
	if (gflag)
		l += 6;
	if (lflag)
		l += 36;
	if (mflag)
		l += 26;
	if (tflag)
		l += 12;
	if (nflag == 0) {
		if (dbflag)
			printf("         ");
		printf("%-*.*s %5s", TTYWIDE, TTYWIDE, "TTY", "PID");
		if (gflag)
			printf(" GROUP");
		if (lflag)
			printf("  PPID      UID    K    F S    EVENT");
		if (mflag)
			printf("  CVAL  SVAL   IVAL   RVAL");
		if (tflag)
			printf(" UTIME STIME");
		putchar('\n');
		fflush(stdout);
	}
	pp1 = &cprocq;
	while ((pp2=pp1->p_nback) != aprocq) {

		if (range((char *)pp2) == 0)
			panic("Fragmented list");
		pp1 = (PROC *) map(pp2);

		/*
		 * Kernel process - display only if '-d' argument given.
		 */
		if ( pp1->p_flags & PFKERN ) {
			if ( dflag == 0 )
				continue;
		}

		/*
		 * Unattached process - display only if '-x' argument given.
		 */
		else if ( pp1->p_ttdev == NODEV ) {
			if ( xflag == 0 )
				continue;
		}

		/*
		 * Attached to other terminal - display only if '-a' arg given.
		 */
		else if ( pp1->p_ttdev != ttdev ) {
			if ( aflag == 0 )
				continue;
		}

		/* A PROC * is a 32-bit far pointer here, so it is printed
		 * whole: `%06o' consumed only its first word, which is the
		 * segment, so every process in the table showed the same
		 * value. */
		if (dbflag)
			printf("%08lx ", (long)pp2);
		ptty(pp1);
		printf(" %5d", pp1->p_pid);
		if (gflag)
			printf(" %5d", pp1->p_group);
		if (lflag) {
			printf(" %5d", pp1->p_ppid);
			printf(" %8.8s", uname(pp1));
			psize(pp1);
			printf(" %4o", pp1->p_flags);
			printf(" %c", c=state(pp1, pp2));
			/* p_event is a far pointer too, and had the same
			 * first-word-only bug: EVENT named the segment a
			 * process slept in rather than the channel. */
			if (c == 'S')
				printf(" %08lx", (long)pp1->p_event);
			else {
				if (fflag)
					printf("        -");
				else
					printf("         ");
			}
		}
		if (mflag) {
			printf(" %5u", cval(pp1, pp2));
			printf(" %5u", pp1->p_sval);
			printf(" %6d", pp1->p_ival);
			printf(" %6d", pp1->p_rval);
		}
		if (tflag) {
			ptime(pp1->p_utime);
			ptime(pp1->p_stime);
		}
		printf("  ");
		printl(pp1, (wflag?132:80)-l-1);
		putchar('\n');
		fflush(stdout);
	}
	rttys();	/* release all alloced space */
}

/*
 * Set our terminal device.
 */
settdev()
{
	register PROC *pp1, *pp2;
	register int p;

	p = getpid();
	pp1 = &cprocq;
	while ((pp2=pp1->p_nforw) != aprocq) {
		if (range((char *)pp2) == 0)
			break;
		pp1 = (PROC *) map(pp2);
		if (pp1->p_pid == p) {
			ttdev = pp1->p_ttdev;
			return;
		}
	}
	ttdev = NODEV;
}

/*
 *	Finds all special file in the specified directory (e.g. /dev)
 */

fttys( dirname)

char *dirname;

{
	int filein;		/* directory file descriptor */
	struct stat sbuf;	/* stat structure */
	struct direct dbuf;	/* directory buffer structure */
	char name[DIRSIZ+1];	/* d_name, terminated */

	/* move to the appropriate directory and open file for reading */
	
	chdir( dirname );

	if( ( filein = open( ".", 0) ) < 0 ) {
		fprintf( stderr, "Cannot open '%s' in /dev\n", dirname );
		exit( 1 );
	}

	read( filein, &dbuf, sizeof(dbuf) );	/* read . */
	read( filein, &dbuf, sizeof(dbuf) );	/* read .. */

	while( read( filein, &dbuf, sizeof(dbuf) ) ) {
		strncpy( name, dbuf.d_name, DIRSIZ );
		name[DIRSIZ] = '\0';

		if( stat( name, &sbuf ) < 0 )
			continue;

		if( S_ISCHR( sbuf.st_mode ) )
			addname( name, sbuf.st_rdev );

		else if( S_ISDIR( sbuf.st_mode ) )
			fttys( name );
	}

	close( filein );
	chdir( ".." );
}

/*
 *	Adds a name and and device number of type handle
 */

addname( devname, devnum )

char *devname;
dev_t devnum;

{
	extern struct handle *fptr;
	extern struct handle *lptr;
	
	if( fptr == (struct handle *) NULL ) {
		if( (fptr=(struct handle *) malloc( sizeof(struct handle))) ==
				(struct handle *) NULL ) {
			fprintf( stderr, "Out of memory\n");
			exit( 1 );
		}

		fptr->nptr = (struct handle *) NULL;
		strcpy( fptr->dname, devname);
		fptr->dnum = devnum;
	}

	else {
		lptr = fptr;

		while( ( lptr->nptr != (struct handle *) NULL ) &&
			( lptr->dnum != devnum ) ) lptr = lptr->nptr;

		if( lptr->dnum == devnum )
			return;

		if( (lptr->nptr=(struct handle *)malloc(sizeof(struct handle)))
				== (struct handle *) NULL ) {
			fprintf( stderr, "Out of memory\n");
			exit( 1 );
		}

		lptr = lptr->nptr;
		lptr->nptr = (struct handle *) NULL;
		strcpy( lptr->dname, devname);
		lptr->dnum = devnum;
	}
}

/*
 *	Release allocated space on exit (default action anyway)
 */

rttys()
{
	/* release allocated space */

	if( fptr == (struct handle *) NULL )
		return;
	lptr = fptr;
	while( lptr->nptr != (struct handle *) NULL ) {
		fptr = lptr;
		lptr = fptr->nptr;
		free( fptr);
	}
	free( lptr);
}

/*
 *	Abbreviate a /dev name for the TTY column, which is TTYWIDE wide.
 *	`console' becomes `con', a pseudo-terminal ttypN becomes pN, and a
 *	numbered line ttyNN becomes its number.  Any other name keeps its
 *	leading characters.  buf must hold TTYWIDE+1 characters.
 */

char *
shortty( name, buf )
char *name;
char *buf;
{
	register char *from;

	if( strcmp( name, "console" ) == 0 )
		from = "con";
	else if( strncmp( name, "ttyp", 4 ) == 0 ) {
		buf[0] = 'p';
		strncpy( buf + 1, name + 4, TTYWIDE - 1 );
		buf[TTYWIDE] = '\0';
		return buf;
	} else if( strncmp( name, "tty", 3 ) == 0 && name[3] != '\0' )
		from = name + 3;
	else
		from = name;

	strncpy( buf, from, TTYWIDE );
	buf[TTYWIDE] = '\0';
	return buf;
}

/*
 *	Print the controlling terminal, abbreviated.  If the name is not
 *	found it prints the major and minor device numbers.  A process with
 *	no controlling terminal prints a single dash.
 */

ptty( pp )
register PROC *pp;
{
	register int d;
	char buf[TTYWIDE + 1];

	/* when supplied with a device number, look for the name
	   of the special file from the dev directory */

	if( ( d = pp->p_ttdev ) == NODEV ) {
		printf( "%-*.*s", TTYWIDE, TTYWIDE, "-");
		return;
	}

	if( ( lptr = fptr ) == (struct handle *) NULL ) {
		pdev( d );
		return;
	}
	while( lptr->nptr != (struct handle *) NULL ) {
		if( lptr->dnum == d ) {
			printf( "%-*.*s", TTYWIDE, TTYWIDE,
			    shortty( lptr->dname, buf ) );
			return;
		}
		lptr = lptr->nptr;
	}
	if( lptr->dnum == d ) {
		printf( "%-*.*s", TTYWIDE, TTYWIDE,
		    shortty( lptr->dname, buf ) );
		return;
	}
	pdev( d );
	return;
}

/*
 *	No name for this device: print the major and minor numbers packed
 *	into the column.
 */

pdev( d )
int d;
{
	char buf[16];

	sprintf( buf, "%d:%d", major( d ), minor( d ) );
	printf( "%-*.*s", TTYWIDE, TTYWIDE, buf );
}

/*
 * Given a process, return it's user name.
 */
char *
uname(pp)
register PROC *pp;
{
	static char name[8];
	register struct passwd *pwp;

	if ((pwp=getpwuid(pp->p_ruid)) != NULL)
		return (pwp->pw_name);
	sprintf(name, "%d", pp->p_ruid);
	return (name);
}

/*
 * Return the core value for a process.
 */
cval(pp1, pp2)
register PROC *pp1, *pp2;
{
	unsigned u;
	register PROC *pp3, *pp4;

	if (pp1->p_state == PSSLEEP) {
		u = (cutime-pp1->p_lctim) * 1;
		if (pp1->p_cval+u > pp1->p_cval)
			return (pp1->p_cval+u);
		return (-1);
	}
	u = 0;
	pp3 = &cprocq;
	while ((pp4=pp3->p_lforw) != aprocq) {
		if (range((char *)pp4) == 0)
			break;
		pp3 = (PROC *) map(pp4);
		u -= pp3->p_cval;
		if (pp2 == pp4)
			return (u);
	}
	if (pp1->p_pid == getpid())
		return (pp1->p_cval);
	return (0);
}

/*
 * Return the size in K of the given process.
 */
psize(pp)
register PROC *pp;
{
	long l;
	register SEG *sp;
	register int n;

	l = 0;
	for (n=0; n<NUSEG+1; n++) {
		if (rflag == 0)
			if (n==SIUSERP || n==SIAUXIL)
				continue;
		if ((sp=pp->p_segp[n]) == NULL)
			continue;
		if (range((char *)sp) == 0) {
			printf("   ?K");
			return;
		}
		sp = (SEG *) map(sp);
		l += sp->s_size;
	}
	if (l != 0)
		printf("%4ldK", l/1024);
	else {
		if (fflag)
			printf("    -");
		else
			printf("     ");
	}
}

/*
 * Get the state of the process.
 */
state(pp1, pp2)
register PROC *pp1, *pp2;
{
	register int s;

	s = pp1->p_state;
	if (s == PSSLEEP) {
		if ((PROC *)pp1->p_event == pp2)
			return ('W');
		if ((pp1->p_flags&PFSTOP) != 0)
			return ('T');
		return ('S');
	}
	if (s == PSRUN)
		return ('R');
	if (s == PSDEAD)
		return ('Z');
	return ('?');
}

/*
 * Given a time in HZ, print it out.
 */
ptime(l)
long l;
{
	register unsigned m;

	if ((l=(l+HZ/2)/HZ) == 0) {
		if (fflag)
			printf("     -");
		else
			printf("      ");
		return;
	}
	if ((m=l/60) >= 100) {
		printf("%6d", m);
		return;
	}
	printf(" %2d:%02d", m, (unsigned)l%60);
}

/*
 * Print out the command line of a process.
 */
printl(pp, m)
register PROC *pp;
{
	register char *cp;
	register int c;
	register int argc;
	register int n;

	if (pp->p_state == PSDEAD)
		return;
	if (segread(pp->p_segp[SIUSERP], 0, &u, sizeof (u)) == 0)
		return;

	/*
	 * Handle kernel processes.  This precedes the pid 0 case, which used to
	 * print `<idle>' from the pid alone: the kernel names its own processes
	 * in u_comm and idle is one of them, so the name comes from the same
	 * place for all of them and an unnamed one shows as `<>' rather than
	 * being covered up.
	 */
	if ( pp->p_flags & PFKERN ) {
		printf("<%.*s>",sizeof(u.u_comm), u.u_comm[0] ? u.u_comm : "");
		return;
	}
	if (pp->p_pid == 0) {
		printf("<idle>");
		return;
	}

	/* u_argp is a 16-bit offset and sr_base a 32-bit seg:off virtual base,
	 * so the difference is taken in 16 bits and is SIGNED: the stack
	 * segment grows down and argv[0] lies below sr_base. */
	n = segread(pp->p_segp[SISTACK],
		(int)(u.u_argp-u.u_segl[SISTACK].sr_base),
		argp, sizeof (argp));

	if (n == 0)
		return;

	if ((argc=u.u_argc) <= 0)
		return;

	m -= 2;
	cp = argp;

	while (argc--) {
		while ((c=*cp++) != '\0') {
			if (!isascii(c) || !isprint(c))
				return;
			if (m-- == 0)
				return;
			putchar(c);
		}
		if (m-- == 0)
			return;
		if (argc != 0)
			putchar(' ');
	}
}

/*
 * Given a segment pointer `sp' and an offset into the segment,
 * `s', read `n' bytes from the segment into the buffer `bp'.
 */
segread(sp, s, bp, n)

SEG *sp;
int s;			/* SIGNED offset -- see printl() */
char *bp;

{
	register SEG *sp1;

	if (range((char *)sp) == 0)
		return (0);

	sp1 = (SEG *) map(sp);

	if ((sp1->s_flags&SFCORE) != 0)
		mread( (long)sp1->s_paddr + s, bp, n );

	else
		if( dread( (long)sp1->s_daddr*BSIZE + s, bp, n ) < 0 )
			return( 0 );

	return (1);
}

/*
 * Read `n' bytes into the buffer `bp' from kernel memory
 * starting at seek position `s'.
 */
kread(s, bp, n)
long s;
char *bp;
{
	lseek(kfd, (long)s, 0);
	if (read(kfd, bp, n) != n)
		panic("Kernel memory read error");
}

/*
 * Read `n' bytes into the buffer `bp' from absolute memory
 * starting at seek position `s'.
 */
mread(s, bp, n)
long s;
char *bp;
{
	lseek(mfd, (long)s, 0);
	if (read(mfd, bp, n) != n)
		panic("Memory read error");
}

/*
 * Read `n' bytes into the buffer `bp' from the swap file
 * starting at seek position `s'.
 */
dread(s, bp, n)
long s;
char *bp;
{
	/*
	 * If swap device exists go look at it
	 */
	if( dfd > 0 ) {
		lseek(dfd, (long)s, 0);

		if (read(dfd, bp, n) != n)
			panic("Swap read error");

		return( 1 );
	}

	return( 0 );
}

/*
 * Print out an error message and exit.
 */
panic(a1)
char *a1;
{
	fflush(stdout);
	fprintf(stderr, "%r", &a1);
	fprintf(stderr, "\n");
	exit(1);
}
