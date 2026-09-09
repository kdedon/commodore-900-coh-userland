/* killpg -- kill(2) takes a negative pid for a process group. */
int
killpg(pgrp, sig)
int pgrp, sig;
{
	return kill(-pgrp, sig);
}
