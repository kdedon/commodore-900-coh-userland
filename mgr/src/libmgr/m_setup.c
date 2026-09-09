/*{{{}}}*/
/*{{{  #includes*/
#include <mgr/mgr.h>
#include <stdio.h>
/*}}}  */

/* The escape stream needs a buffer of its own.  COHERENT's stdio gives a
   stream that is a terminal and is not stdout NO buffer at all (libc/stdio/
   finit.c: `_FSTBUF || isatty(fd) && fp!=stdout' selects the unbuffered
   _fputc), and m_termout is exactly that -- a second FILE on /dev/tty.  So
   every character of every escape sequence becomes its own write(2) through
   the window's pty and its own wakeup of the server: one ico(6) frame is 621
   bytes of protocol and was therefore 621 system calls.  setbuf() before the
   first character puts the stream on the buffered _fputb path, where the same
   frame is two.

   Buffering is what the protocol already assumes.  Every escape that asks the
   server a question flushes on its own (m_getinfo, m_gets, m_getevent), the
   M_FLUSH flag flushes after each write for a client that wants it, and
   upstream runs on stdio implementations that buffer a terminal too, so a
   client that draws and then waits without flushing is already broken there.

   A static buffer, not malloc: m_setup() runs before a client has a chance to
   check for failure, and this must not be the allocation that fails. */
static char termoutbuf[BUFSIZ];

/*{{{  m_setup*/
int m_setup(flags)
int flags;
{
  m_flags = flags;

  if (!(m_flags&M_DEBUG))
  {
    m_termout = fopen(M_DEVICEOUT,"w");
    if (m_termout != NULL) setbuf(m_termout,termoutbuf);
    m_termin = fopen(M_DEVICEIN,"r");
  }

  if (m_termin == NULL || m_termout == NULL) m_flags |= M_DEBUG;

  if (m_flags&M_DEBUG) 
  {
    m_termin = stdin;
    m_termout = stdout;
  }
  return(m_flags);
}
/*}}}  */
