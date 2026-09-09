/* initgroups -- this system has no supplementary group list. */
int
initgroups(name, basegid)
char *name;
unsigned short basegid;
{
	return 0;
}
