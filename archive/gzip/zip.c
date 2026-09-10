/* zip.c -- compress files to the gzip or pkzip format
 * Copyright (C) 1992-1993 Jean-loup Gailly
 * This is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License, see the file COPYING.
 */

#ifndef lint
static char rcsid[] = "$Id: zip.c,v 0.9 1993/01/26 19:12:42 jloup Exp $";
#endif

#include "tailor.h"
#include "gzip.h"
#include "crypt.h"

#include <ctype.h>
#include <stdio.h>

#if HAVE_UNISTD_H
#  include <sys/types.h>
#  include <unistd.h>
#endif


/* The following OS codes are defined in pkzip appnote.txt */
#ifdef AMIGA
#  define OS_CODE  0x100
#endif
#ifdef VMS
#  define OS_CODE  0x200
#endif
/* unix    3 */
/* vms/cms 4 */
#ifdef ATARI_ST
#  define OS_CODE  0x500
#endif
#ifdef OS2
#  define OS_CODE  0x600
#endif
#ifdef MACOS
#  define OS_CODE  0x700
#endif
/* z system 8 */
/* cp/m     9 */
#ifdef TOPS20
#  define OS_CODE  0xa00 /* agreed with PKware, but not yet official */
#endif
#ifdef WIN32
#  define OS_CODE  0xb00 /* agreed with PKware, but not yet official */
#endif
/* qdos 12 */

#if defined(MSDOS) && !defined(OS_CODE)
#  define OS_CODE  0x000
#endif
#ifndef OS_CODE
#  define OS_CODE  0x300  /* assume Unix */
#endif

local ulg crc;       /* crc on uncompressed file data */
long overhead;       /* number of bytes in gzip header */

/* ===========================================================================
 * Deflate in to out.
 * IN assertions: the input and output buffers are cleared.
 */
void zip(in, out)
    int in, out;            /* input and output file descriptors */
{
    uch  flags = 0;         /* general purpose bit flags */
    ush  attr = 0;          /* ascii/binary flag */
    ush  deflate_flags = 0; /* pkzip -es, -en or -ex equivalent */

    ifd = in;
    ofd = out;
    outcnt = 0;

    /* Write the header to the gzip file. See algorithm.doc for the format */

    method = DEFLATED;
    put_byte(GZIP_MAGIC[0]); /* magic header */
    put_byte(GZIP_MAGIC[1]);
    put_byte(DEFLATED);      /* compression method */

    if (save_orig_name) {
	flags |= ORIG_NAME;
    }
    /* Should look ahead a little in the input to determine the
     * ascii/binary flag. To be done.
     */

    put_byte(flags);         /* general flags */
    put_long(time_stamp);

    /* Write deflated file to zip file */
    crc = updcrc((uch *)0, 0);

    bi_init(out);
    ct_init(&attr, &method);
    lm_init(level, &deflate_flags);

    put_byte((uch)deflate_flags); /* extra flags */
    put_byte(OS_CODE);            /* OS identifier */

    if (save_orig_name) {
	char *p = basename(ifname); /* Don't save the directory part. */
	do {
	    put_byte(casemap(*p));
	} while (*p++);
    }
    overhead = (long)outcnt;

    (void)deflate();

#if !defined(MSDOS) && !defined(VMS)
  /* Check input size (but not in VMS -- variable record lengths mess it up)
   * and not on MSDOS -- diet in TSR mode reports an incorrect file size)
   */
    if (ifile_size != -1L && isize != (ulg)ifile_size) {
	Trace((stderr, " actual=%ld, read=%ld ", ifile_size, isize));
	fprintf(stderr, " file size changed while zipping %s\n", ifname);
    }
#endif

    /* Write the crc and uncompressed size */
    put_long(crc);
    put_long(isize);

    flush_outbuf();
}


/* ===========================================================================
 * Read a new buffer from the current input file, perform end-of-line
 * translation, and update the crc and input file size.
 * IN assertion: size >= 2 (for end-of-line translation)
 */
int file_read(buf, size)
    char *buf;
    unsigned size;
{
    unsigned len;

    Assert(insize == 0, "inbuf not empty");

    len = read(ifd, buf, size);
    if (len == (unsigned)(-1) || len == 0) return len;

    crc = updcrc((uch*)buf, len);
    isize += (ulg)len;
    return len;
}
