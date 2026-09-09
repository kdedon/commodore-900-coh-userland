/* setreuid/setregid -- only the real id exists here. */
int
setreuid(ruid, euid)
unsigned short ruid, euid;
{
	return setuid((int)ruid);
}

int
setregid(rgid, egid)
unsigned short rgid, egid;
{
	return setgid((int)rgid);
}
