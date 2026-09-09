/*
 * logger -- make an entry in the system log.
 *
 *	logger [-t tag] [-p pri] [-i] [message ...]
 *
 *	-t	tag the entry with this name instead of `logger'
 *	-p	facility.level, or a bare level, or a decimal code
 *		(default user.notice)
 *	-i	include the process id
 *
 * With no message arguments the standard input is read and each line is
 * logged separately, so a script can pipe a command's output into the log.
 *
 * This is a syslog(3) client and nothing more: it is what says, from a shell,
 * whether the logging path works end to end -- syslog(3) formatting the
 * record, the FIFO carrying it, syslogd routing it and a file receiving it.
 */

#include <stdio.h>
#include <string.h>
#include <syslog.h>

static	char	*Levname[] = {
	"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"
};
static	char	*Facname[LOG_NFACILITIES] = {
	"kern", "user", "mail", "daemon", "auth", "syslog", "lpr", "news",
	"uucp", "cron", 0, 0, 0, 0, 0, 0,
	"local0", "local1", "local2", "local3",
	"local4", "local5", "local6", "local7"
};

/*
 * "facility.level", "level" or a decimal priority code.  Returns -1 if the
 * argument names nothing, which is refused rather than silently logged at
 * some default: a message filed under the wrong facility is a message the
 * person looking for it will not find.
 */
static
priof(s)
char *s;
{
	char	buf[32];
	char	*dot;
	int	i, fac, lev;

	if (*s >= '0' && *s <= '9')
		return (atoi(s));
	strncpy(buf, s, sizeof(buf)-1);
	buf[sizeof(buf)-1] = '\0';
	fac = LOG_USER;
	if ((dot = strchr(buf, '.')) != (char *)0) {
		*dot++ = '\0';
		fac = -1;
		for (i = 0; i < LOG_NFACILITIES; i++)
			if (Facname[i] != (char *)0 && strcmp(buf, Facname[i]) == 0)
				fac = i << 3;
		if (fac < 0)
			return (-1);
	} else
		dot = buf;
	lev = -1;
	for (i = 0; i < 8; i++)
		if (strcmp(dot, Levname[i]) == 0)
			lev = i;
	if (lev < 0)
		return (-1);
	return (fac | lev);
}

main(argc, argv)
int argc;
char **argv;
{
	char	*tag = "logger";
	int	pri = LOG_USER|LOG_NOTICE;
	int	stat = 0;
	char	line[256];
	char	msg[512];
	int	i, n;

	for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
		if (strcmp(argv[i], "-i") == 0)
			stat |= LOG_PID;
		else if (strcmp(argv[i], "-t") == 0 && i+1 < argc)
			tag = argv[++i];
		else if (strcmp(argv[i], "-p") == 0 && i+1 < argc) {
			if ((pri = priof(argv[++i])) < 0) {
				fprintf(stderr, "logger: unknown priority %s\n",
					argv[i]);
				exit(1);
			}
		} else {
			fprintf(stderr,
			    "usage: logger [-i] [-t tag] [-p pri] [message ...]\n");
			exit(1);
		}
	}

	openlog(tag, stat, 0);

	if (i < argc) {
		msg[0] = '\0';
		for (n = 0; i < argc; i++) {
			if (n != 0 && strlen(msg) + 1 < sizeof(msg))
				strcat(msg, " ");
			strncat(msg, argv[i], sizeof(msg) - strlen(msg) - 1);
			n++;
		}
		syslog(pri, "%s", msg);
	} else {
		while (fgets(line, sizeof(line), stdin) != (char *)0) {
			if ((n = strlen(line)) > 0 && line[n-1] == '\n')
				line[n-1] = '\0';
			if (line[0] != '\0')
				syslog(pri, "%s", line);
		}
	}
	closelog();
	exit(0);
}
