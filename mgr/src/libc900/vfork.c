/* vfork -- no copy-on-write here; a plain fork is the whole of it. */
int
vfork()
{
	return fork();
}
