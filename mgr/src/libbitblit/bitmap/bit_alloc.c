/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
/*{{{}}}*/
/*{{{  #includes*/
#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#ifdef USE_X11
#include "../x11/bitmap.h"
#endif
#include <mgr/bitblit.h>
#include <mgr/share.h>
/*}}}  */

/*{{{  bit_alloc -- allocate space for, and create a memory bitmap*/
BITMAP *bit_alloc(wide, high, data, depth)
int wide;
int high;
DATA *data;
unsigned char depth;
{
  register BITMAP *result;
#ifdef USE_X11
  xdinfo *xd;
#endif

#ifdef DEBUG
  if (wide<=0 || high <=0 || !(depth==8 || depth==1))
  {
    fprintf(stderr,"bit_alloc boo-boo %d x %d x %d\r\n",wide,high,depth);
    return(NULL);
  }
#endif
#ifdef USE_X11
  if ((result=(BITMAP*)malloc(sizeof(BITMAP)+sizeof(xdinfo)))==(BITMAP*)0) return (result);
  result->deviceinfo = result+1;
  xd = result->deviceinfo;
  xd->d = 0;
#else
  if ((result=(BITMAP*)malloc(sizeof(BITMAP)))==(BITMAP*)0) return (result);
  result->deviceinfo = NULL;
#endif

  result->x0=0;
  result->y0=0;
  result->high=high;
  result->wide=wide;
  result->depth=depth;
  result->cache=NULL;
  result->color=0;

  if (data != (DATA *) 0)
  {
    result->data = data;
    /* convert from external to internal format (if required) */
#ifdef MOVIE
    log_alloc(result);
#endif
  }
  else
  {
    register long size=bit_size(wide,high,depth);

    /* malloc() takes a size_t, which is 16 bits on the Z8001, and one object
       may not span a segment: a bitmap whose data does not fit is refused
       here rather than allocated at a wrapped size.  BIT_MAXBYTES leaves room
       for malloc's own header within the 64 KB. */
    if (size < 1L || size > (long)BIT_MAXBYTES)
    {
      free(result);
      return ((BITMAP *) 0);
    }

    if ((result->data = (DATA *) malloc((unsigned int)size)) == (DATA *) 0)
    {
      free(result);
      return ((BITMAP *) 0);
    }
#ifdef MOVIE
  log_alloc(result);
#endif
  }

  result->primary = result;
  result->type = _MEMORY;
  result->id = 0;	/* assign elsewhere? */
  return (result);
}
/*}}}  */
