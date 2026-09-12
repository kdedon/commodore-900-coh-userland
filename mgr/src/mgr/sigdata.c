/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*{{{}}}*/
/*{{{  #includes*/
#include <stdio.h>
#ifdef C900
#include <c900/c900.h>
#endif

#include "proto.h"
#include "do_button.h"
/*}}}  */

/*{{{  signal names, descriptions (for debugging)*/
/*	One row per signal this system delivers, indexed by signal number.  The
	numbers are the machine's own (<signal.h> and <sys/msig.h>): they are
	not the BSD or SysV set, and eight of them collide with a different BSD
	name at the same number -- 5 is SIGTERM here and SIGTRAP there -- so a
	table borrowed from either reports the wrong fault under its own name.
	NSIGNAME is the length, and catch() bounds its index by it: a signal
	number the kernel grows past this table must read as unknown, not as
	whatever lies after it.
*/
static struct signame {
   char *symbol;
   int  number;
   char *desc;
   } signames[] = {
   { "SIGNONE",	0,	"internal mgr error", },
   { "SIGHUP",	1,	"hangup", },
   { "SIGINT",	2,	"interrupt", },
   { "SIGQUIT",	3,	"quit", },
   { "SIGALRM",	4,	"alarm clock", },
   { "SIGTERM",	5,	"software termination signal from kill", },
   { "SIGREST",	6,	"restart", },
   { "SIGSYS",	7,	"bad argument to system call", },
   { "SIGPIPE",	8,	"write on a pipe with no one to read it", },
   { "SIGKILL",	9,	"kill (cannot be caught or ignored)", },
   { "SIGTRAP",	10,	"breakpoint", },
   { "SIGSEGV",	11,	"segmentation violation", },
   { "SIGEPA",	12,	"extended processor (EPU) trap", },
   { "SIGPRV",	13,	"privileged instruction -- this is what abort() raises", },
   { "SIGNO14",	14,	"signal 14", },
   { "SIGNO15",	15,	"signal 15", },
   { "SIGNO16",	16,	"signal 16", },
};
#define NSIGNAME	(sizeof(signames)/sizeof(signames[0]))
/*}}}  */

/*{{{  catch -- unexpected signals go here, print message, restore state, then die!*/
void catch(sig)
int sig;
{
  char *symbol = "SIGUNKNOWN";
  char *desc = "unknown signal";

  if (sig >= 0 && sig < NSIGNAME) {
     symbol = signames[sig].symbol;
     desc = signames[sig].desc;
     }

  /* The fault goes out before _quit() runs: _quit() touches the keyboard, the
     ttys and every window, so a server that is already damaged can die inside
     it, and then the only record of what happened is this line. */
  fprintf(stderr,"got a %s (%d):%s\r\n",symbol,sig,desc);
  fflush(stderr);
  _quit();
  abort();
}
/*}}}  */
