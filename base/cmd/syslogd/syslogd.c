/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * syslogd -- read log records from /dev/log and file them where
 * /etc/syslog.conf says.
 *
 *	syslogd [-d] [-f conffile] [-p fifo]
 *
 *	-d	stay in the foreground and echo every record to standard
 *		output as well as filing it
 *	-f	read a configuration file other than /etc/syslog.conf
 *	-p	listen on a FIFO other than /dev/log
 *
 * THE TRANSPORT IS A FIFO, AND IT IS OPENED O_RDWR.  That is not a
 * convenience: it decides three of this program's failure modes.
 *
 *   A reader that opens O_RDONLY blocks in the kernel's popen() until some
 *   process opens the FIFO for writing, so syslogd would not exist until its
 *   first client ran.  O_RDWR takes the IPR|IPW arm, which never sleeps.
 *
 *   With no writer, a read of a pipe returns 0 -- end of file.  Every client
 *   closes its descriptor after a burst of messages, so a reader that treated
 *   0 as end of file would exit at the first quiet moment, and one that
 *   treated it as "try again" would spin on the CPU forever.  Holding the
 *   write end itself means there is always a writer, so read(2) blocks
 *   instead of returning 0 and neither case arises.
 *
 *   A record is one write(2) of less than PIPSIZE (5120) bytes, which this
 *   kernel guarantees is atomic, so records from different processes cannot
 *   interleave.  libc's syslog() keeps a record under LOG_RECMAX = 256.  A
 *   read can still land mid-record when more is queued than fits in one
 *   buffer, so the tail of a short read is carried forward rather than
 *   filed as a line of its own.
 *
 * SIGNALS.  SIGHUP rereads the configuration; SIGTERM (which is 5 on this
 * system, not 15) files a closing message and exits.  Both arrive while the
 * process is asleep in a read of the FIFO -- the path that used to panic this
 * kernel -- so each handler only sets a flag and the work is done in the main
 * loop after read(2) returns.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

extern	int	errno;
extern	char	*ctime();
extern	long	time();
extern	char	*strchr();

#define	CONFFILE	"/etc/syslog.conf"
#define	DEFLOG		"/usr/adm/syslog"
#define	NRULE		12		/* configuration lines honoured */
#define	PATHMAX		64
#define	RBUF		1024		/* one read of the FIFO */
#define	LBUF		(LOG_RECMAX*2)	/* one assembled record */

/*
 * One configuration line: for each facility, the set of levels it takes,
 * and the file the record is appended to.  A level bit is set for the level
 * itself and every more urgent one, which is what `mail.info' has always
 * meant.
 */
struct rule {
	unsigned char	r_lev[LOG_NFACILITIES];
	char		r_path[PATHMAX];
};

static	struct rule	Rules[NRULE];
static	int		Nrule;

static	char	*Conf = CONFFILE;
static	char	*Fifo = _PATH_LOG;
static	int	Debug;
static	int	Fd = -1;
static	int	Reread;			/* SIGHUP arrived */
static	int	Quit;			/* SIGTERM arrived */

static	char	*Levname[] = {
	"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"
};
static	char	*Facname[LOG_NFACILITIES] = {
	"kern", "user", "mail", "daemon", "auth", "syslog", "lpr", "news",
	"uucp", "cron", 0, 0, 0, 0, 0, 0,
	"local0", "local1", "local2", "local3",
	"local4", "local5", "local6", "local7"
};

static	void	onhup();
static	void	onterm();

/*
 * Name to number, or -1.  `*' is handled by the caller, not here.
 */
static
levelof(name)
char *name;
{
	register int i;

	for (i = 0; i < 8; i++)
		if (strcmp(name, Levname[i]) == 0)
			return (i);
	if (strcmp(name, "none") == 0)
		return (-2);
	return (-1);
}

static
facilityof(name)
char *name;
{
	register int i;

	for (i = 0; i < LOG_NFACILITIES; i++)
		if (Facname[i] != (char *)0 && strcmp(name, Facname[i]) == 0)
			return (i);
	return (-1);
}

/*
 * Apply one selector -- "fac[,fac...].level", or "*.level" -- to a rule.
 * Returns 0 if the selector could not be read.
 */
static
selector(rp, sel)
register struct rule *rp;
char *sel;
{
	char	*dot;
	char	*fac;
	char	*next;
	int	lev, f, allfac;
	unsigned char bits;

	if ((dot = strchr(sel, '.')) == (char *)0)
		return (0);
	*dot = '\0';
	if (strcmp(dot+1, "*") == 0)
		lev = LOG_DEBUG;
	else if ((lev = levelof(dot+1)) == -1)
		return (0);
	/* "none" clears; anything else sets this level and every more
	 * urgent one, since 0 is the most urgent. */
	bits = 0;
	if (lev >= 0) {
		for (f = 0; f <= lev; f++)
			bits |= LOG_MASK(f);
	}
	for (fac = sel; fac != (char *)0; fac = next) {
		if ((next = strchr(fac, ',')) != (char *)0)
			*next++ = '\0';
		allfac = (strcmp(fac, "*") == 0);
		for (f = 0; f < LOG_NFACILITIES; f++) {
			if (allfac) {
				if (Facname[f] == (char *)0)
					continue;
			} else if (facilityof(fac) != f)
				continue;
			if (lev == -2)
				rp->r_lev[f] = 0;
			else
				rp->r_lev[f] |= bits;
		}
	}
	return (1);
}

/*
 * Read the configuration.  A file that cannot be read, or that selects
 * nothing, leaves one rule sending everything to DEFLOG -- a syslogd that
 * silently discarded the machine's log because a config line had a typo
 * would be worse than one that over-files.
 */
static
readconf()
{
	FILE	*fp;
	char	line[128];
	char	*p, *sel, *act, *semi;
	register struct rule *rp;

	Nrule = 0;
	if ((fp = fopen(Conf, "r")) != (FILE *)0) {
		while (Nrule < NRULE && fgets(line, sizeof(line), fp) != (char *)0) {
			if ((p = strchr(line, '\n')) != (char *)0)
				*p = '\0';
			for (p = line; *p == ' ' || *p == '\t'; p++)
				;
			if (*p == '\0' || *p == '#')
				continue;
			sel = p;
			while (*p != '\0' && *p != ' ' && *p != '\t')
				p++;
			if (*p == '\0')
				continue;
			*p++ = '\0';
			while (*p == ' ' || *p == '\t')
				p++;
			if (*p == '\0')
				continue;
			act = p;
			/* Only file actions are served: this machine has no
			 * network to forward to and no wall to write on. */
			if (*act != '/')
				continue;
			rp = &Rules[Nrule];
			memset((char *)rp, 0, sizeof(*rp));
			strncpy(rp->r_path, act, PATHMAX-1);
			rp->r_path[PATHMAX-1] = '\0';
			for (; sel != (char *)0; sel = semi) {
				if ((semi = strchr(sel, ';')) != (char *)0)
					*semi++ = '\0';
				selector(rp, sel);
			}
			Nrule++;
		}
		fclose(fp);
	}
	if (Nrule == 0) {
		/* selector() splits its argument in place, so the fallback
		 * selector is a writable array and not a literal. */
		static char everything[] = "*.debug";
		char	fallback[sizeof(everything)];

		rp = &Rules[0];
		memset((char *)rp, 0, sizeof(*rp));
		strcpy(rp->r_path, DEFLOG);
		strcpy(fallback, everything);
		selector(rp, fallback);
		Nrule = 1;
	}
}

/*
 * Append one finished line to one file.  Opened and closed per record: this
 * machine has twenty descriptors, the log is written a few times a minute,
 * and a file held open across a truncation would keep writing at the old
 * offset.  A device (/dev/console) is written but never created.
 */
static
filerec(path, line)
char *path;
char *line;
{
	int	fd, n;

	if (strncmp(path, "/dev/", 5) == 0)
		fd = open(path, O_WRONLY|O_NDELAY);
	else
		fd = open(path, O_WRONLY|O_APPEND|O_CREAT, 0644);
	if (fd < 0)
		return;
	n = strlen(line);
	write(fd, line, n);
	write(fd, "\n", 1);
	close(fd);
}

/*
 * One complete record, without its newline.  Strip the <pri> prefix, decide
 * where it goes, and put it there.
 */
static
dispatch(rec)
char *rec;
{
	register char *p;
	int	pri, fac, lev, i;

	pri = -1;
	p = rec;
	if (*p == '<') {
		pri = 0;
		for (p++; *p >= '0' && *p <= '9'; p++)
			pri = pri*10 + (*p - '0');
		if (*p == '>')
			p++;
		else {
			pri = -1;		/* not a prefix after all */
			p = rec;
		}
	}
	if (pri < 0)
		pri = LOG_USER|LOG_NOTICE;
	fac = LOG_FAC(pri);
	lev = LOG_PRI(pri);
	if (fac >= LOG_NFACILITIES)
		fac = LOG_FAC(LOG_USER);
	if (*p == '\0')
		return;
	if (Debug)
		printf("syslogd: [%s.%s] %s\n", Facname[fac] ? Facname[fac] : "?",
			Levname[lev], p);
	for (i = 0; i < Nrule; i++)
		if (Rules[i].r_lev[fac] & LOG_MASK(lev))
			filerec(Rules[i].r_path, p);
}

/*
 * A record of syslogd's own, filed through the same rules rather than
 * written straight to a file: a start or stop that does not appear where
 * every other record appears is a start or stop nobody sees.
 */
static
selflog(lev, text)
int lev;
char *text;
{
	char	rec[LOG_RECMAX];
	long	now;
	char	*ts;

	time(&now);
	ts = ctime(&now);
	sprintf(rec, "<%d>%.15s syslogd: %s", LOG_SYSLOG|lev, ts + 4, text);
	dispatch(rec);
}

static void
onhup()
{
	signal(SIGHUP, onhup);
	Reread = 1;
}

static void
onterm()
{
	Quit = 1;
}

main(argc, argv)
int argc;
char **argv;
{
	char	rbuf[RBUF];
	char	line[LBUF];
	int	nl;			/* bytes held in line */
	int	n, i, rerr;
	int	zeros;			/* consecutive end-of-file reads */
	struct stat sb;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-d") == 0)
			Debug = 1;
		else if (strcmp(argv[i], "-f") == 0 && i+1 < argc)
			Conf = argv[++i];
		else if (strcmp(argv[i], "-p") == 0 && i+1 < argc)
			Fifo = argv[++i];
		else {
			fprintf(stderr,
			    "usage: syslogd [-d] [-f conffile] [-p fifo]\n");
			exit(1);
		}
	}

	readconf();

	/* The FIFO is made here rather than by rc: syslogd is the only
	 * program that knows the path, and one that starts before its node
	 * exists would fail for a reason nothing records.
	 *
	 * The mode is set after the fact and not left to mkfifo, whose
	 * argument the umask reduces: root's 022 turns 0666 into 0644, and a
	 * log every unprivileged program is refused by is not a system log.
	 */
	if (stat(Fifo, &sb) < 0) {
		if (mkfifo(Fifo, 0666) < 0) {
			fprintf(stderr, "syslogd: cannot make %s: %s\n",
				Fifo, strerror(errno));
			exit(1);
		}
	} else if (!S_ISFIFO(sb.st_mode)) {
		fprintf(stderr, "syslogd: %s exists and is not a fifo\n", Fifo);
		exit(1);
	}
	chmod(Fifo, 0666);
	if ((Fd = open(Fifo, O_RDWR)) < 0) {
		fprintf(stderr, "syslogd: cannot open %s: %s\n",
			Fifo, strerror(errno));
		exit(1);
	}

	signal(SIGHUP, onhup);
	signal(SIGTERM, onterm);
	signal(SIGPIPE, SIG_IGN);

	selflog(LOG_INFO, "start");
	if (Debug) {
		printf("syslogd: ready on %s, %d rule(s) from %s\n",
			Fifo, Nrule, Conf);
		fflush(stdout);
	}

	nl = 0;
	zeros = 0;
	while (!Quit) {
		n = read(Fd, rbuf, sizeof(rbuf));
		/* Why the read ended is decided BEFORE anything else runs:
		 * rereading the configuration and filing a record both open
		 * and close files, and errno belongs to whichever of those
		 * ran last, not to the read.  Read first, judge first. */
		rerr = errno;
		if (Reread) {
			Reread = 0;
			readconf();
			selflog(LOG_INFO, "configuration reread");
			if (Debug) {
				printf("syslogd: reread %s, %d rule(s)\n",
					Conf, Nrule);
				fflush(stdout);
			}
		}
		if (n < 0) {
			if (rerr == EINTR)
				continue;	/* a caught signal, not an error */
			fprintf(stderr, "syslogd: read %s: %s\n",
				Fifo, strerror(rerr));
			break;
		}
		/* Held open O_RDWR, so this cannot be a writerless pipe and
		 * an end of file is not expected.  One is tolerated in case
		 * a signal woke the reader with nothing queued; a run of
		 * them is a kernel that will never block again, and a daemon
		 * that answered that by looping would take the machine's CPU
		 * and leave nobody to say why. */
		if (n == 0) {
			if (++zeros < 8)
				continue;
			fprintf(stderr,
			    "syslogd: %s returned end of file %d times; giving up\n",
			    Fifo, zeros);
			break;
		}
		zeros = 0;
		for (i = 0; i < n; i++) {
			if (rbuf[i] == '\n') {
				line[nl] = '\0';
				if (nl > 0)
					dispatch(line);
				nl = 0;
			} else if (nl < LBUF-1)
				line[nl++] = rbuf[i];
		}
	}

	selflog(LOG_INFO, "exiting");
	if (Debug) {
		printf("syslogd: exiting\n");
		fflush(stdout);
	}
	close(Fd);
	exit(0);
}
