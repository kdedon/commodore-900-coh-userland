/*{{{}}}*/
/*{{{  Notes*/
/*
mgrload - show cpu load average in an mgr window.

Original version by: Mark Dapoz 90/06/21, mdapoz@hybrid.UUCP or mdapoz%hybrid@cs.toronto.edu
Heavily edited by: Michael Haardt
*/
/*}}}  */
/*{{{  #includes*/
#include <mgr/mgr.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>

#include "getload.h"
/*}}}  */
/*{{{  #defines*/
#define INTERVAL        5      /* time interval (sec) between samples*/
#define PSIZE           100    /* size of one display partition; .50 proc */
/* getload() answers in centiloads, so PSIZE partitions the graph every 1.00
   of a process; the backend's sampling interval is INTERVAL too. */

#define CONTEXT P_POSITION | P_WINDOW | P_FLAGS | P_EVENT | P_CURSOR
/*}}}  */

/*{{{  variables*/
char *malloc();

int xscale, yscale, rscale;
int xmin, xmax, ymin, ymax;
int x,y;
int border;
/*}}}  */

/*{{{  max*/
static int max(nums) /* find maximum load avarage in queue */
int *nums;
{
  static int i,j;

  for (i=j=0; i < xmax; nums++, i++)
  j=*nums > j ? *nums : j;
  return(j);
}
/*}}}  */
/*{{{  draw_scale -- draw scale lines on the graph*/
/* One horizontal line per whole partition boundary below the top of the graph,
   so a graph scaled to one partition draws none: the only boundary it has is
   its own top edge.  A blank graph is what an idle machine looks like.

   The row is formed in long.  i*PSIZE*yscale reaches sections*100*ymax, which
   passes 32767 at the fifth line of a 64-pixel-high graph, and a 16-bit int
   then wraps it to a row outside the window. */
static void draw_scale(sections, all) int sections; int all;
{
  int i,j;

  m_func(BIT_XOR);
  for (i=1; i < sections; i++)
  {
    j=ymax-(int)((long)i*PSIZE*yscale/rscale);
    m_line(all ? 0 : xmax-1, j, all ? xmax : xmax-1, j);
  }
}
/*}}}  */
/*{{{  draw_bar*/
/* A bar is one pixel wide and load/rscale of the graph high, so it is invisible
   below rscale/ymax centiloads -- 0.02 of a process on a 64-pixel graph, and 0
   exactly on an idle machine.

   In long for the same reason as draw_scale: load*yscale wraps above 511
   centiloads, and a negative height is a bitwrite outside the window. */
void draw_bar(load, column) int load; int column;
{
  int foo=(int)((long)load*yscale/rscale);

  if (column==xmax-1) { m_func(BIT_CLR); m_bitwrite(xmax-1, 0, 1, ymax-foo); }
  m_func(BIT_SET); m_bitwrite(column,ymax-foo,1,foo);
}
/*}}}  */
/*{{{  redraw -- redraw graph from history*/
static void redraw(nums, head) int *nums; int head;
{
  register int j,p;

  m_clear();
  for (p=0, j=head+1; p <= xmax-1; p++)
  {
    j=j%xmax;
    if (nums[j]) draw_bar(nums[j],p);
    j++;
  }
}
/*}}}  */
/*{{{  timer_event*/
/* The request has to LEAVE this handler.  It interrupts the read the main loop
   is blocked in, so nothing after it runs until the server answers -- and the
   server cannot answer a request still sitting in the output buffer.  (It used
   to leave anyway, because COHERENT gives a second stream on a terminal no
   buffer at all; that is no longer so, see libmgr/m_setup.c.) */
static void timer_event() /* cause the load average to be sampled */
{
  signal(SIGALRM, SIG_IGN);
  m_sendme("S\n"); /* sample load average event */
  m_flush();
  signal(SIGALRM, timer_event);
}
/*}}}  */
/*{{{  done*/
static void done() /* general purpose exit */
{
  m_ttyreset(); /* reset communication channel */
  m_popall(); /* restore window */
  exit(0);
}
/*}}}  */

/*{{{  main*/
main(argc,argv) int argc; char **argv;
{
  /*{{{  variables*/
  int *samples;           /* circular queue of samples, centiloads */
  int head=0;                     /* queue pointer */
  int partitions = 0, last_part = 0;
  char event[80];              /* mgr event queue */
  /*}}}  */

  /*{{{  take the first sample before anything else*/
  /* The backend resolves whatever it reads the load average from on its first
     call, and on a system where that needs privilege (COHERENT: /dev/kmem is
     600 root and this client is setuid) it hands the privilege back there.
     Doing it here means the window, the tty and every file below are the real
     user's.  The value is discarded: the graph starts at the first sample of
     its own loop. */
  (void)getload();
  /*}}}  */
  /*{{{  check if an mgr terminal*/
  ckmgrterm(*argv);
  /*}}}  */
  /*{{{  init mgr*/
  m_setup(M_MODEOK);
  m_push(CONTEXT);
  m_setmode(M_ABS);
  m_setcursor(CS_INVIS);
  m_ttyset();
  /*}}}  */
  /*{{{  init variables*/
  xmin = 0;       /* mgr virtual window size */
  ymin = 0;
  /* Every one of these is a question put to the server and read back off the
     window's tty, and every one can fail.  Unchecked, a failure leaves xmax
     and ymax at zero, and then the graph is xmax-1 = -1 columns wide: nothing
     is ever drawn, no error is ever printed, and the tile is blank in exactly
     the way an idle machine's is.  A blank tile has to mean one thing. */
  if (m_getwindowsize(&xmax,&ymax) < 0 || xmax <= 0 || ymax <= 0)
  {
    fprintf(stderr,"mgrload: the server did not answer for the window size\r\n");
    exit(1);
  }
  if (m_getwindowposition(&x,&y) < 0)
  {
    fprintf(stderr,"mgrload: the server did not answer for the window position\r\n");
    exit(1);
  }
  if ((border=m_getbordersize()) < 0)
  {
    fprintf(stderr,"mgrload: the server did not answer for the border size\r\n");
    exit(1);
  }
  xscale = xmax-xmin;
  yscale = ymax-ymin;
  samples=(int *)malloc(sizeof(int)*xmax);
  if (samples==(int *)0) { fprintf(stderr,"mgrload: out of memory\n"); exit(1); }
  memset((char *)samples, 0, xmax*sizeof(int));
  /*}}}  */
  /*{{{  set up signal handlers*/
  signal(SIGALRM, timer_event);
  signal(SIGTERM, done);
  signal(SIGINT, done);
  signal(SIGQUIT, done);
  signal(SIGHUP, done);
  /*}}}  */
  /*{{{  set up mgr events*/
  m_setevent(RESHAPE,   "H\n");
  m_setevent(REDRAW,    "R\n");
  /*}}}  */
  /*{{{  set up window and start first event*/
  m_clear();
  m_sendme("S\n");
  /*}}}  */
  while (1)
  {
    m_flush();
    /*{{{  get event*/
    if (m_gets(event) == (char*)0)
    /* restart interrupted call */
    if (errno==EINTR
#    ifdef EAGAIN
    || errno==EAGAIN
#    endif
    ) continue; else break;
    /*}}}  */
    alarm(0);
    switch (*event)
    {
      /*{{{  R -- redraw, H -- reshape*/
      case 'R':       /* redraw window */
      case 'H':       /* reshape window */
      m_getwindowposition(&x,&y);
      m_shapewindow(x,y,2*border+xmax,2*border+ymax);
      redraw(samples, head-1 < 0 ? xmax-1 : head-1);
      draw_scale(partitions, 1);
      break;
      /*}}}  */
      /*{{{  S -- get load average*/
      case 'S':
      samples[head]=getload(); /* 1 min avg, in centiloads */
      partitions=max(samples)/PSIZE+1;
      rscale=partitions*PSIZE;
      if (last_part == partitions)
      /*{{{  fits on last scale*/
      {
        /* scroll graph left */
        m_func(BIT_SRC); m_bitcopy(0, 0, xmax-1, ymax, 1, 0);
        draw_bar(samples[head],xmax-1);
        draw_scale(partitions, 0);
      }
      /*}}}  */
      else
      /*{{{  change scale*/
      {
        last_part=partitions;
        redraw(samples, head);
        draw_scale(partitions, 1);
      }
      /*}}}  */
      head=((head+1)%xmax);
      break;
      /*}}}  */
      /*{{{  default*/
      default: break;
      /*}}}  */
    }
    alarm(INTERVAL);
  }
  exit(0);
}
/*}}}  */
