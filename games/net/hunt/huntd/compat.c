/*
 * compat.c -- the BSD bits hunt(6) expects that this system does not have.
 *
 * err/errx/warn/warnx and setegid.  Kept next to the game rather than added to
 * libc: they are a 4.4BSD convenience, not part of the C this system speaks,
 * and the game is the only thing here that wants them.
 *
 * THESE ARE NOT VARIADIC.  err(3) takes a printf format and arguments; these
 * take one plain string.  That is not a shortcut -- forwarding a run of
 * arguments to another function is not something K&R C can express, and this
 * libc's own printf does it by taking the address of its first parameter
 * (libc/stdio/printf.c), which only works when you ARE the variadic function.
 *
 * It is safe here because every one of hunt's forty-odd call sites passes a
 * bare string; there is not one format argument in the whole game.  A call site
 * that acquired one would print the format instead of the message, so if you
 * add one, write the fprintf out by hand.
 */
#include <stdio.h>
#include <errno.h>

extern int errno;
extern char *strerror();

/*
 * The program name, for the message prefix.  err(3) gets this from the C
 * library, which knows argv[0]; nothing here does, so main() sets it.
 */
char *progname = "hunt";

static void prefix()
{
	fprintf(stderr, "%s: ", progname);
}

/* warn: the message, then why -- errno's text, as err(3) appends it. */
void warn(msg)
char *msg;
{
	int e = errno;

	prefix();
	fprintf(stderr, "%s: %s\n", msg, strerror(e));
}

void warnx(msg)
char *msg;
{
	prefix();
	fprintf(stderr, "%s\n", msg);
}

void err(eval, msg)
int eval;
char *msg;
{
	warn(msg);
	exit(eval);
}

void errx(eval, msg)
int eval;
char *msg;
{
	warnx(msg);
	exit(eval);
}

/*
 * setsid -- detach from the controlling terminal.
 *
 * This system has setpgrp(), which is the V7 spelling of the same idea: a new
 * process group with no controlling terminal.  huntd calls setsid() once, at
 * startup, for exactly that.
 */
int setsid()
{
	return setpgrp();
}

/*
 * setegid -- there are no saved set-group-IDs on this system, and no
 * /var/games score file to protect with one.  The game calls it to drop and
 * regain the games group around its score file; here the score file is owned by
 * whoever plays, so there is nothing to drop.  Succeeds, and does nothing.
 */
int setegid(gid)
int gid;
{
	return 0;
}

int setregid(rgid, egid)
int rgid, egid;
{
	return 0;
}
