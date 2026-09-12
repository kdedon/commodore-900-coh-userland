/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
#ifndef _MGR_BLITLIB_H
#define _MGR_BLITLIB_H

#ifdef C900
#include <c900/c900.h>
#endif

#include <stdio.h>

#include "window.h"

/* basic frame buffer word size */
#ifndef DATA
#ifdef C900
#define DATA unsigned		/* K&R has no `void *'; HR frame word = 16 bits */
#else
#define DATA void
#endif
#endif

/* Largest bitmap bit_alloc() will hand to malloc().  On the Z8001 a size_t is
   16 bits and no one object may span a segment, so the ceiling is under 64 KB;
   62 KB also keeps malloc's own `roundup(size + sizeof(alloc_t), 512)' clear
   of wrapping its 16-bit length.  Elsewhere it is only a sanity limit. */
#ifndef BIT_MAXBYTES
#ifdef C900
#define BIT_MAXBYTES	63488L
#else
#define BIT_MAXBYTES	0x7ffff000L
#endif
#endif

/* NULL bitmap data */
#define NULL_DATA	((DATA *) 0)

/* NULL bitmap pointer */
#define BIT_NULL	((BITMAP *) 0)

/* frame buffer */
#define _SCREEN		1

/* malloc'd space */
#define _MEMORY		2

/* don't free space at destroy time */
#define _STATIC		3

/* data is in external format */
#define _FLIP		4

/* data is "dirty" */
#define _DIRTY          8

/* member access macros */

#define IS_SCREEN(x)	((3&(x)->type)==_SCREEN)	/* bitmap is on the display */
#define IS_MEMORY(x)	((3&(x)->type)==_MEMORY)	/* bitmap space malloc'd */
#define IS_STATIC(x)	((3&(x)->type)==_STATIC)	/* bitmap space is static */
#define IS_PRIMARY(x)	((x)->primary == (x))
#define SET_FLIP(x)     ((x)->primary->type |= DOFLIP ? _FLIP : 0)

#define BIT_X(x)	((x)->x0)
#define BIT_Y(x)	((x)->y0)
#define BIT_DATA(x)	((x)->data)
#define BIT_WIDE(x)	((x)->wide)
#define BIT_HIGH(x)	((x)->high)
#define BIT_DEPTH(x)	((int) ((x)->depth))
#define BIT_CACHE(x)    ((x)->primary->cache)
#define BIT_CHCLR(x)    ((x)->primary->color)

#define SET_DIRTY(x) (bit_destroy(BIT_CACHE(x)),BIT_CACHE(x)=NULL)

/* structure and type definitions */

typedef struct bitmap
{
  DATA *data;              /* bitmap data */
  struct bitmap	*primary;  /* pointer to primary bitmap (server only) */
  int x0, y0;              /* starting coordinates, in bits */
  int wide, high;          /* bitmap size, in bits */
  unsigned char depth;     /* bitmap depth */
  char type;               /* bitmap type (server only) */
  unsigned short int id;   /* bitmap ID for movie mgr */
  struct bitmap *cache;    /* cached 8 bit expansion of monochrome images */
  int color;		   /* cached color (op>>4) */
  char *deviceinfo;	   /* dev-dep stuff needed by screen driver, if any */
} BITMAP;

/* Macro to declare a 1 bit per pixel static bitmap */
#define bit_static(name,wide,high,data,depth,id) \
BITMAP name = { (DATA *)data, &(name), 0, 0, wide, high, depth, _STATIC, id, \
		NULL, 0, NULL }

int bitmaphead();
BITMAP *bitmapread();
int bitmapwrite();

/*
 * The macro "GET Most Significant Bits" defines how the bits in each
 * word map from memory to pixels on the display.  The top left most
 * pixel on the display comes from either the *high* order or *low* order
 * bit of the first frame buffer word.  Use "<<" in the first case, ">>"
 * in the second.
 * 
 * The macro "GET Least Significant Bits" does the inverse of GETMSB
 */

#define GETMSB(word,shift)	\
	((word) << (shift))		/* get most significant bits in word */
#define GETLSB(word,shift) \
	((word) >> (shift))		/* get least significant bits in word */

/* these probably won't need changing */

#define MSB (~GETLSB((DATA)~0,1)) /* most sig, actually leftmost, bit set */
#define LSB (~GETMSB((DATA)~0,1)) /* least sig, actually rightmost, bit set */

/*
 * bitmap data has 2 formats, an internal format and an external format.
 * (Sometimes the formats are the same).  The external format is native
 * 68020 SUN/3, DATA aligned 1=black, 0=white.  The internal format is
 * whatever the frame buffer is.  If DOFLIP is set, data is converted
 * from external to internal format the first time it is used.  Bitmap
 * data is recognized as being in external format if the _FLIP flag is
 * set in the type field.  The installation routine flip() does the
 * conversion.
 */

/* need to flip bytes */

#define DOFLIP GETLSB( 1, 1)

/* Function declarations */

extern BITMAP *bit_load();
extern char *bit_save();
extern BITMAP *bit_alloc();
extern void bit_blit();
extern BITMAP *bit_create();
extern void bit_destroy();
extern void bit_line();
extern int bit_on();
extern BITMAP *bit_open();
extern int bit_point();
/* A bitmap's byte count does not fit in a 16-bit int: 1024x800x1 is 102400
   bytes.  bit_size() is long, and every caller must see this declaration. */
extern long bit_size();
extern void bit_grafscreen();
extern void bit_textscreen();
extern DATA *bit_initscreen();
extern void bit_bytescroll();
extern BITMAP *bit_shrink();
extern int timestamp();
extern unsigned int fg_color_idx();
extern void setpalette();
extern void getpalette();

#endif
/*{{{}}}*/
