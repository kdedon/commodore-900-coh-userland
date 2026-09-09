/* strpbrk -- absent from libc; first occurrence in s of any byte of set. */
char *
strpbrk(s, set)
register char *s, *set;
{
	register char *p;

	for (; *s != '\0'; s++)
		for (p = set; *p != '\0'; p++)
			if (*p == *s)
				return s;
	return (char *)0;
}
