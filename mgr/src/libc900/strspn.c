/*
 * strspn -- length of the initial run of `s' made up of characters from
 * `set'.  Absent from this libc, as strpbrk is.
 */

int
strspn(s, set)
char *s;
char *set;
{
	register char *p;
	register int n;

	for (n = 0; s[n] != '\0'; n++) {
		for (p = set; *p != '\0' && *p != s[n]; p++)
			;
		if (*p == '\0')
			break;
	}
	return (n);
}
