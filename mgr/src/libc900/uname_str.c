/*
 * mgr_uname -- get_info.c's G_SYSTEM reply.  There is no uname(2) answer
 * worth printing on this machine, so the system string is a constant.
 */
char *
mgr_uname()
{
	return "COHERENT 3.5 Z8001 Commodore 900";
}
