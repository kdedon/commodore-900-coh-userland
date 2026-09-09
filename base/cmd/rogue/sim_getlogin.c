/* getlogin() for the runexec sim: the 0.7.3 libc reads /etc/utmp (absent
 * on the harness), so fall back to $USER / $LOGNAME.  Linked ahead of
 * libc so it wins.  Harmless on the real machine if utmp is unset too. */
char *
getlogin()
{
	char *getenv(), *u;

	if ((u = getenv("USER")) != 0 && *u)
		return (u);
	if ((u = getenv("LOGNAME")) != 0 && *u)
		return (u);
	return ("player");
}
