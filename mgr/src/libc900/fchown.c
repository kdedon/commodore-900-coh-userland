/* fchown -- no such syscall on COHERENT 3.2; MGR only re-owns its ptys. */
int
fchown(fd, owner, group)
int fd;
unsigned short owner, group;
{
	return 0;
}
