/* tailor.h -- target dependent definitions
 * Copyright (C) 1992-1993 Jean-loup Gailly.
 * This is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License, see the file COPYING.
 */

/* The target dependent definitions should be defined here only.
 * The target dependent functions should be defined in tailor.c.
 */

/* $Id: tailor.h,v 0.7 1993/01/21 18:32:58 jloup Exp $ */

extern int errno;

#ifdef COHERENT

/* COHERENT on the Z8001: 16 bit int, 32 bit long, 32 bit far pointers.
 * No single object may exceed 64K, so the sliding window, the hash table
 * and the hash chains are each held under that limit; see gzip.h.
 * <string.h>, <utime.h>, chown(2), utime(2) and getopt(3) are in the C
 * library; readdir(3) and lstat(2) are not, so -r and the symbolic link
 * tests are compiled out.
 */
#  define HAVE_STRING_H
#  define HAVE_UNISTD_H 1
#  define HAVE_UTIME_H
#  define DYN_ALLOC
#  define NO_LZW
/* inbuf and outbuf are static, and the whole static image shares one 64K
 * data segment with the trees, the name buffers and the string tables.
 */
#  define INBUFSIZ  8192
#  define OUTBUFSIZ 8192
#  define MAXSEG_64K
#  define NO_DIR
#  define NO_SYMLINK
#  define PATH_SEP '/'
#  define near

#endif /* COHERENT */

#ifdef MSDOS
#  define MAXSEG_64K
#  define PATH_SEP '\\'
#  define NO_MULTIPLE_DOTS
#  define NO_CHOWN
#  define PROTO
#  define STDC_HEADERS
#  define casemap(c) tolow(c) /* Force file names to lower case */
#  include <io.h>
#endif

#ifndef near
#  define near
#endif

/* calloc(3) returns a far pointer, so a declaration must be in scope at every
 * fcalloc call: K&R's int default would truncate the result to its 16-bit
 * offset and lose the segment, putting every one of these arrays in segment
 * 0 on top of the stack.
 */
#ifdef COHERENT
#  include <stdlib.h>
#endif

#define fcalloc(items,size) calloc((unsigned)(items), (unsigned)(size))

#ifndef PATH_SEP
#  define PATH_SEP '/'
#endif

#ifndef casemap
#  define casemap(c) (c)
#endif

/* Wild card expansion */
#define EXPAND(argc,argv)

/* Force binary mode on open file */
#define SET_BINARY_MODE(fd)
