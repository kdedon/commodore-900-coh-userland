/* fchmod -- no such syscall on COHERENT 3.2; MGR only tightens pty modes. */
int
fchmod(fd, mode)
int fd;
unsigned short mode;
{
	return 0;
}
