/* revision.h -- define the version number
 * Copyright (C) 1992-1993 Jean-loup Gailly.
 * This is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License, see the file COPYING.
 */

#define VERSION "0.8.2"
#define PATCHLEVEL 0
#define REVDATE "26 Jan 93"

/* This version supports only decompression of old compress format: */
#ifdef LZW
#  undef LZW
#endif

/* $Id: revision.h,v 0.10 1993/01/26 19:12:42 jloup Exp $ */
