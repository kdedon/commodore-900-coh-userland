/* gzip (GNU zip) -- compress files with zip algorithm and 'compress' interface
 * Copyright (C) 1992-1993 Jean-loup Gailly
 * The unzip code was written and put in the public domain by Mark Adler.
 * Portions of the lzw code are derived from the public domain 'compress'
 * written by Spencer Thomas, Joe Orost, James Woods, Jim McKie, Steve Davies,
 * Ken Turkowski, Dave Mack and Peter Jannesen.
 *
 * See the license_msg below and the file COPYING for the software license.
 * See the file algorithm.doc for the compression algorithms and file formats.
 */

static char  *license_msg[] = {
"   Copyright (C) 1992-1993 Jean-loup Gailly",
"   This program is free software; you can redistribute it and/or modify",
"   it under the terms of the GNU General Public License as published by",
"   the Free Software Foundation; either version 2, or (at your option)",
"   any later version.",
"",
"   This program is distributed in the hope that it will be useful,",
"   but WITHOUT ANY WARRANTY; without even the implied warranty of",
"   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the",
"   GNU General Public License for more details.",
"",
"   You should have received a copy of the GNU General Public License",
"   along with this program; if not, write to the Free Software",
"   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.",
0};

/* Compress files with zip algorithm and 'compress' interface.
 * See usage() and help() functions below for all options.
 * Outputs:
 *        file.z:   compressed file with same mode, owner, and utimes
 *        file.Z:   same with -Z option (old compress format)
 *     or stdout with -c option or if stdin used as input.
 * If the OS does not support file names with multiple dots (MSDOS, VMS) or
 * if the output file name had to be truncated, the original name is kept
 * in the compressed .z file. (Feature not available in old compress format.)
 * On MSDOS, file.tmp -> file.tmz
 *
 * For the meaning of all compilation flags, see comments in Makefile.in.
 */

#ifndef lint
static char rcsid[] = "$Id: gzip.c,v 0.10 1993/01/26 19:12:42 jloup Exp $";
#endif

#include "tailor.h"
#include "gzip.h"
#include "lzw.h"
#include "revision.h"

#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

		/* configuration */

#if defined(HAVE_UNISTD_H)
#  include <unistd.h>
#endif

#if defined(STDC_HEADERS) || defined(HAVE_STDLIB_H)
#  include <stdlib.h>
#else
   extern int errno;
#endif

#if !defined(NO_DIR) && (defined(DIRENT) || defined(_POSIX_VERSION))
#  include <dirent.h>
   typedef struct dirent dir_type;
#  define NLENGTH(dirent) ((int)strlen((dirent)->d_name))
#  define DIR_OPT "DIRENT"
#else
#  define NLENGTH(dirent) ((dirent)->d_namlen)
#  ifdef SYSDIR
#    include <sys/dir.h>
     typedef struct direct dir_type;
#    define DIR_OPT "SYSDIR"
#  else
#    ifdef SYSNDIR
#      include <sys/ndir.h>
       typedef struct direct dir_type;
#      define DIR_OPT "SYSNDIR"
#    else
#      ifdef NDIR
#        include <ndir.h>
         typedef struct direct dir_type;
#        define DIR_OPT "NDIR"
#      else
#        define NO_DIR
#        define DIR_OPT "NO_DIR"
#      endif
#    endif
#  endif
#endif

#ifdef HAVE_UTIME_H
#  include <utime.h>
#  define TIME_OPT "UTIME"
#else
#  ifdef HAVE_SYS_UTIME_H
#    include <sys/utime.h>
#    define TIME_OPT "SYS_UTIME"
#  else
     struct utimbuf {
         time_t actime;
         time_t modtime;
     };
#    define TIME_OPT ""
#  endif
#endif

#if !defined(S_ISDIR) && defined(S_IFDIR)
#  define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#if !defined(S_ISREG) && defined(S_IFREG)
#  define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

typedef RETSIGTYPE (*sig_type)();

#ifndef	O_BINARY
#  define  O_BINARY  0  /* creation mode for open() */
#endif

#define RW_USER 0600    /* creation mode for open() */

#define MAX_PATH_LEN   1024 /* max pathname length */

#define MAX_HEADER_LEN   16
/* max length of a compressed file header, fixed part only */

#ifndef Z_SUFFIX
#  define Z_SUFFIX ".gz"
#endif
#define Z_SUFLEN (sizeof(Z_SUFFIX)-1)
/* Suffix added when compressing.  Decompression also accepts the .z of
 * gzip 0.x, the .Z of compress and the .zip of pkzip.
 */

		/* local variables */

int to_stdout = 0;    /* output to stdout (-c) */
int decompress = 0;   /* decompress (-d) */
int force = 0;        /* don't ask questions, compress links (-f) */
int recursive = 0;    /* recurse through directories (-r) */
int verbose = 0;      /* be verbose (-v) */
int do_lzw = 0;       /* generate output compatible with old compress (-Z) */
int test = 0;         /* test .z file integrity */
int foreground;       /* set if program run in foreground */
char *progname;       /* program name */
int maxbits = BITS;   /* max bits per code for LZW */
int method = DEFLATED;/* compression method */
int level = 5;        /* compression level */
int exit_code = 0;    /* program exit code */
int save_orig_name;   /* set if original name must be saved */
int single_member;    /* set for .zip and .Z files */
ulg time_stamp;       /* original time stamp (modification time) */
long ifile_size;      /* input file size, -1 for devices (debug only) */

long bytes_in;             /* number of input bytes */
long bytes_out;            /* number of output bytes */
char ifname[MAX_PATH_LEN]; /* input filename */
char ofname[MAX_PATH_LEN]; /* output filename */
int  remove_ofname = 0;	   /* remove output file on error */
struct stat istat;         /* status for input file */
int  ifd;                  /* input file descriptor */
int  ofd;                  /* output file descriptor */
uch  inbuf[INBUFSIZ+64];   /* input buffer (+64 for unlzw, to be removed) */
uch  outbuf[OUTBUFSIZ+2048];/* output buf (+2048 for unlzw, to be removed) */
unsigned insize;           /* valid bytes in inbuf */
unsigned inptr;            /* index of next byte to be processed in inbuf */
unsigned outcnt;           /* bytes in output buffer */


/* local functions */

local void usage        OF((void));
local void help         OF((void));
local void license      OF((void));
local void version      OF((void));
local void treat_stdin  OF((void));
local void treat_file   OF((char *iname));
local int create_outfile OF((void));
local int has_z_suffix  OF((char *name, int len));
local int  do_stat      OF((char *name, struct stat *sbuf));
local int  get_istat    OF((char *iname, struct stat *sbuf));
local int  make_ofname  OF((void));
local int  same_file    OF((struct stat *stat1, struct stat *stat2));
local int name_too_long OF((char *name, struct stat *statb));
local int  get_method   OF((int in));
local int  check_ofname OF((void));
local void copy_stat    OF((struct stat *ifstat));
local void treat_dir    OF((char *dir));

void (*work) OF((int infile, int outfile)) = zip; /* function to call */

/* ======================================================================== */
local void usage()
{
    fprintf(stderr,
#ifdef LZW
#  ifdef NO_DIR
            "usage: %s [-cdfhLtvVZ19] [-b maxbits] [file ...]\n",
#  else
            "usage: %s [-cdfhLrtvVZ19] [-b maxbits] [file ...]\n",
#  endif
#else /* !LZW */
#  ifdef NO_DIR
            "usage: %s [-cdfhLtvV19] [file ...]\n",
#  else
            "usage: %s [-cdfhLrtvV19] [file ...]\n",
#  endif
#endif /* LZW */
             progname);
}
/* ======================================================================== */
local void help()
{
    static char  *help_msg[] = {
/* -a --ascii       ascii text; convert end-of-lines to local OS conventions */
 " -c --stdout      write on standard output, keep original files unchanged",
 " -d --decompress  decompress",
/* -e --encrypt     encrypt */
 " -f --force       force overwrite of output file and compress links",
 " -h --help        give this help",
/* -k --pkzip       force output in pkzip format */
/* -l --list        list .z file contents */
 " -L --license     display software license",
#ifndef NO_DIR
 " -r --recurse     recurse through directories",
#endif
 " -t --test        test compressed file integrity",
 " -v --verbose     verbose mode",
 " -V --version     display version number",
 " -1 --fast        compress faster",
 " -9 --best        compress better",
#ifdef LZW
 " -Z --lzw         produce output compatible with old compress",
 " -b --bits maxbits   max number of bits per code (implies -Z)",
#endif
 " file...          files to (de)compress. If none given, use standard input.",
  0};
    char **p = help_msg;

    fprintf(stderr,"%s %s (%s)\n", progname, VERSION, REVDATE);
    usage();
    while (*p) fprintf(stderr, "%s\n", *p++);
}

/* ======================================================================== */
local void license()
{
    char **p = license_msg;

    fprintf(stderr,"%s %s (%s)\n", progname, VERSION, REVDATE);
    while (*p) fprintf(stderr, "%s\n", *p++);
}

/* ======================================================================== */
local void version()
{
    fprintf(stderr,"%s %s (%s)\n", progname, VERSION, REVDATE);

    fprintf(stderr, "Compilation options:\n%s %s ", DIR_OPT, TIME_OPT);
#ifdef STDC_HEADERS
    fprintf(stderr, "STDC_HEADERS ");
#endif
#ifdef HAVE_UNISTD_H
    fprintf(stderr, "HAVE_UNISTD_H ");
#endif
#ifdef HAVE_MEMORY_H
    fprintf(stderr, "HAVE_MEMORY_H ");
#endif
#ifdef HAVE_STRING_H
    fprintf(stderr, "HAVE_STRING_H ");
#endif
#ifdef NO_SYMLINK
    fprintf(stderr, "NO_SYMLINK ");
#endif
#ifdef NO_MULTIPLE_DOTS
    fprintf(stderr, "NO_MULTIPLE_DOTS ");
#endif
#ifdef NO_UTIME
    fprintf(stderr, "NO_UTIME ");
#endif
#ifdef NO_CHOWN
    fprintf(stderr, "NO_CHOWN ");
#endif
#ifdef PROTO
    fprintf(stderr, "PROTO ");
#endif
#ifdef ASMV
    fprintf(stderr, "ASMV ");
#endif
#ifdef DEBUG
    fprintf(stderr, "DEBUG ");
#endif
#ifdef DYN_ALLOC
    fprintf(stderr, "DYN_ALLOC ");
#endif
#ifdef MAXSEG_64K
    fprintf(stderr, "MAXSEG_64K");
#endif
    fprintf(stderr, "\n");
}

/* ======================================================================== */
void main (argc, argv)
    int argc;
    char **argv;
{
    int file_count = 0; /* number of files to precess */
    int proglen;        /* length of progname */
    int optc;           /* current option */

    EXPAND(argc, argv); /* wild card expansion if necessary */

    foreground = signal(SIGINT, SIG_IGN) != SIG_IGN;
    if (foreground != 0) {
	signal (SIGINT, (sig_type)abort_gzip);
    }
#ifdef SIGTERM
    signal(SIGTERM, (sig_type)abort_gzip);
#endif
#ifdef SIGHUP
    signal(SIGHUP,  (sig_type)abort_gzip);
#endif

    progname = basename(argv[0]);
    proglen = strlen(progname);
    /* Suppress .EXE for MSDOS, OS/2 and VMS: */
    if (proglen > 4 && (strcmp(progname+proglen-4, ".EXE") == 0
			|| strcmp(progname+proglen-4, ".exe") == 0)) {
        progname[proglen-4] = '\0';
        strlwr(progname);
    }

    /* For compatibility with old compress, use program name as an option.
     * Systems which do not support links can still use -d or -dc.
     * Ignore an .exe extension for MSDOS or VMS.
     */
    if (  strncmp(progname, "un",  2) == 0       /* ungzip, uncompress */
       || strncmp(progname, "gun", 3) == 0) {    /* gunzip */
	decompress = 1;
    } else if (strcmp(progname+1, "cat") == 0    /* zcat, pcat */
	    || strcmp(progname, "gzcat") == 0) { /* gzcat */
	decompress = to_stdout = 1;
    }

    while ((optc = getopt (argc, argv, "b:cdfhLrtvVZ123456789")) != EOF) {
	switch (optc) {
	case 'b':
	    maxbits = atoi(optarg);
	    break;
	case 'c':
	    to_stdout = 1; break;
	case 'd':
	    decompress = 1; break;
	case 'f':
	    force++; break;
	case 'h':
	    help(); exit(0); break;
	case 'L':
	    license(); exit(0); break;
	case 'r':
#ifdef NO_DIR
	    fprintf(stderr, "-r not supported on this system\n");
	    usage();
	    exit(1); break;
#else
	    recursive = 1; break;
#endif
	case 't':
	    test = decompress = to_stdout = 1;
	    break;
	case 'v':
	    verbose++; break;
	case 'V':
	    version(); exit(0); break;
	case 'Z':
#ifdef LZW
	    do_lzw = 1; break;
#else
	    fprintf(stderr, "-Z not supported in this version\n");
	    usage();
	    exit(1); break;
#endif
	case '1':  case '2':  case '3':  case '4':
	case '5':  case '6':  case '7':  case '8':  case '9':
	    level = optc - '0';
	    break;
	default:
	    /* Error message already emitted by getopt. */
	    usage();
	    exit(1);
	}
    } /* loop on all arguments */

    file_count = argc - optind;

    if (do_lzw && !decompress) work = lzw;

    /* The window array is shared between zip, unzip and unpack.  It holds
     * WSIZE bytes: the decompressor's history, and the compressor's two
     * DEFL_WSIZE half-windows.  See gzip.h.
     */
#ifdef DYN_ALLOC
    window = (uch*) fcalloc(WSIZE, sizeof(uch));
    if (window == NULL) error("insufficient memory for window");
#endif

    if (file_count != 0) {
	if (to_stdout && !test) {
	    SET_BINARY_MODE(fileno(stdout));
	}
        while (optind < argc) {
	    treat_file(argv[optind++]);
	}
    } else {  /* Standard input */
	treat_stdin();
    }
    exit(exit_code);
}

/* ========================================================================
 * Compress or decompress stdin
 */
local void treat_stdin()
{
    if (isatty(fileno(decompress ? stdin : stdout))) {
	/* Do not send compressed data to the terminal or read it from
	 * the terminal. We get here when user invoked the program
	 * without parameters, so be helpful.
	 */
	usage();
	fprintf(stderr,"For more help, type: %s -h\n", progname);
	exit(1);
    }
    SET_BINARY_MODE(fileno(stdin));
    if (!test) SET_BINARY_MODE(fileno(stdout));

    strcpy(ifname, "stdin");
    strcpy(ofname, "stdout");

    /* Get the time stamp on the input file */
    if (fstat(fileno(stdin), &istat) != 0) {
	error("fstat(stdin)");
    }
    ifile_size = -1L; /* convention for unknown size */
    time_stamp = istat.st_mtime;

    clear_bufs(); /* clear input and output buffers */
    to_stdout = 1;

    if (decompress) {
	method = get_method(ifd);
	if (method == -1) {
	    exit(exit_code); /* error message already emitted */
	}
    }

    /* Actually do the compression/decompression. If testing only and
     * input file is in LZW format (which has no CRC) skip it. Loop over
     * zipped members.
     */
    for (;;) {
	if (!test || method != COMPRESSED) {
	    (*work)(fileno(stdin), fileno(stdout));
	}
	if (!decompress || single_member || inptr == insize) break;
	/* end of file */

	method = get_method(ifd);
	if (method == -1) return; /* error message already emitted */
	bytes_out = 0; /* required for length check */
    }

    if (verbose) {
	if (test) {
	    fprintf(stderr,
		    method == COMPRESSED ? " cannot be tested" : " OK");

	} else if (!decompress) {
	    fprintf(stderr, "Compression: ");
	    display_ratio(bytes_in-bytes_out-overhead, bytes_in);
	}
	fprintf(stderr, "\n");
    }
}

/* ========================================================================
 * Compress or decompress the given file
 */
local void treat_file(iname)
    char *iname;
{
    unsigned imode;    /* input file kind */

    /* Check if the input file is present, set ifname and istat: */
    if (get_istat(iname, &istat) != 0) return;

    /* If the input name is that of a directory, recurse or ignore: */
    imode = istat.st_mode;
    if (S_ISDIR (imode)) {
#ifndef NO_DIR
	if (recursive) {
	    treat_dir(iname);
	    /* Warning: ifname is now garbage */
	} else
#endif
	if (verbose) {
	    fprintf(stderr,"%s is a directory -- ignored\n", ifname);
	}
	return;
    }
    if (!S_ISREG (imode)) {
	fprintf(stderr,"%s is not a directory or a regular file - ignored\n",
		ifname);
	return;
    }
    if (istat.st_nlink > 1 && !to_stdout && !force) {
	fprintf(stderr, "%s has %d other link%c -- unchanged\n", ifname,
		(int)istat.st_nlink - 1, istat.st_nlink > 2 ? 's' : ' ');
	exit_code = 1;
	return;
    }

    ifile_size = istat.st_size;
    time_stamp = istat.st_mtime;

    /* Generate output file name */
    if (to_stdout) {
	strcpy(ofname, "stdout");

    } else if (make_ofname() != 0) {
	return;
    }

    /* Open the input file and determine compression method */
    ifd = open(ifname, O_RDONLY | O_BINARY);
    if (ifd == -1) {
	perror(ifname);
	exit_code = 1;
	return;
    }
    clear_bufs(); /* clear input and output buffers */

    if (decompress) {
	method = get_method(ifd); /* updates ofname if original given */
	if (method == -1) return; /* error message already emitted */
    }

    /* If compressing to a file, check if ofname is not ambigous
     * because the operating system truncates names. Otherwise, generate
     * a new ofname and save the original name in the compressed file.
     */
    if (to_stdout) {
	ofd = fileno(stdout);
	/* keep remove_ofname as zero */
    } else {
	if (create_outfile() == -1) return;

	if (save_orig_name && !verbose && !force) {
	    fprintf(stderr, "%s compressed to %s\n", ifname, ofname);
	}
    }
    if (verbose) {
	fprintf(stderr, "%s:\t%s", ifname, (int)strlen(ifname) >= 15 ? 
		"" : ((int)strlen(ifname) >= 7 ? "\t" : "\t\t"));
    }

    /* Actually do the compression/decompression. If testing only and
     * input file is in LZW format (which has no CRC) skip it. Loop over
     * zipped members.
     */
    for (;;) {
	if (!test || method != COMPRESSED) {
	    (*work)(ifd, ofd);
	}
	if (!decompress || single_member || inptr == insize) break;
	/* end of file */

	method = get_method(ifd);
	if (method == -1) return; /* error message already emitted */
	bytes_out = 0; /* required for length check */
    }

    close(ifd);
    if (!to_stdout && close(ofd)) {
	write_error();
    }
    /* Display statistics */
    if(verbose) {
	if (!decompress) {
	    display_ratio(bytes_in-bytes_out-overhead, bytes_in);
	}
	if (test) {
	    fprintf(stderr, method == COMPRESSED ? " untested" : " OK");
	} else if (!to_stdout) {
	    fprintf(stderr, " -- replaced with %s", ofname);
	}
	fprintf(stderr, "\n");
    }
    /* Copy modes, times, ownership */
    if (!to_stdout) {
	copy_stat(&istat);
    }
}

/* ========================================================================
 * Create the output file. Try twice if ofname is exactly one beyond the
 * name limit, to avoid creating a compressed file of name "1234567890123."
 * We could actually loop more than once if the user gives an extra long
 * name, but I prefer generating an error then. (Posix forbids the system
 * to truncate names.) The error message is generated by check_ofname()
 * in this case.
 * IN assertion: the input file has already been open (ifd is set) and
 * ofname has already been updated if there was an original name.
 */
local int create_outfile()
{
    struct stat	ostat; /* stat for ofname */
    int n;             /* loop counter */

    for (n = 1; n <= 2; n++) {
	if (check_ofname() == -1) {
	    close(ifd);
	    return -1;
	}
	/* Create the output file */
	ofd = open(ofname, O_WRONLY|O_CREAT|O_EXCL|O_BINARY, RW_USER);
	if (ofd == -1) {
	    perror(ofname);
	    close(ifd);
	    exit_code = 1;
	    return -1;
	}
	remove_ofname = 1;

	/* Check for name truncation on new file (1234567890123.z) */
	if (fstat(ofd, &ostat) != 0) {
	    perror(ofname);
	    fprintf(stderr, " fstat failed\n");
	    close(ifd); close(ofd);
	    unlink(ofname);
	    exit_code = 1;
	    return -1;
	}
	if (!name_too_long(ofname, &ostat)) return 0;

	if (decompress) {
	    /* name might be too long if an original name was saved */
	    fprintf(stderr, " warning, name truncated: %s\n", ofname);
	    return 0;
	} else {
#ifdef NO_MULTIPLE_DOTS
	    /* Should never happen, see check_ofname() */
	    fprintf(stderr, "ERROR: name too long: %s\n", ofname);
	    exit(1);
#else
	    close(ofd);
	    unlink(ofname);
	    save_orig_name = 1;
	    strcpy(ofname+strlen(ofname)-Z_SUFLEN-1, Z_SUFFIX);
            /* 1234567890123.gz -> 123456789012.gz */
#endif
	} /* decompress ? */
    } /* for (n) */

    close(ifd);
    fprintf(stderr, " name too long: %s\n", ofname);
    exit_code = 1;
    return -1;
}

/* ========================================================================
 * Return the length of the compressed-file suffix on name, or 0 if the
 * name carries none.  ".gz", ".z" and ".Z" are all recognized.
 */
local int has_z_suffix(name, len)
    char *name;
    int len;
{
    if (len > (int)Z_SUFLEN && strcmp(name+len-Z_SUFLEN, Z_SUFFIX) == 0) {
	return (int)Z_SUFLEN;
    }
    if (len > 2 && (strcmp(name+len-2, ".z") == 0
		 || strcmp(name+len-2, ".Z") == 0)) {
	return 2;
    }
    return 0;
}

/* ========================================================================
 * Use lstat if available, except for -c or -f. Use stat otherwise.
 * This allows links when not removing the original file.
 */
local int do_stat(name, sbuf)
    char *name;
    struct stat *sbuf;
{
#if (defined(S_IFLNK) || defined (S_ISLNK)) && !defined(NO_SYMLINK)
    if (!to_stdout && !force) {
	return lstat(name, sbuf);
    }
#endif
    return stat(name, sbuf);
}

/* ========================================================================
 * Set ifname to the input file name (with .z appended if necessary)
 * and istat to its stats. Return 0 if ok, -1 if error.
 */
local int get_istat(iname, sbuf)
    char *iname;
    struct stat *sbuf;
{
    int iexists; /* set if iname exists */
    int ilen = strlen(iname);

    strcpy(ifname, iname);
    errno = 0;

    /* If input file exists, return OK. */
    if (do_stat(ifname, sbuf) == 0) return 0;

    if (!decompress || errno != ENOENT) {
	perror(ifname);
	exit_code = 1;
	return -1;
    }
    /* file.ext is absent: try file.ext.gz, file.ext.z and file.ext.Z */
    if (has_z_suffix(ifname, ilen) == 0) {
	static char *suffixes[] = {Z_SUFFIX, ".z", ".Z", 0};
	char **sp;

	iexists = 0;
	for (sp = suffixes; *sp != (char *)0; sp++) {
	    strcpy(ifname, iname);
	    strcat(ifname, *sp);
	    errno = 0;
	    iexists = !do_stat(ifname, sbuf);
	    if (iexists) break;
	}
	if (!iexists) {
	    strcpy(ifname, iname);
	    strcat(ifname, Z_SUFFIX);
	    perror(ifname);
	    exit_code = 1;
	    return -1;
	}
	if (!S_ISREG (sbuf->st_mode)) {
	    fprintf(stderr, "%s: Not a regular file.\n", ifname);
	    exit_code = 1;
	    return -1;
	}
 	return 0; /* ok */
    } /* try file.gz */

    perror(ifname); /* neither ifname nor ifname.gz exists */
    exit_code = 1;
    return -1;
}

/* ========================================================================
 * Generate ofname given ifname. Return 0 if ok, -1 if file must be skipped.
 * Initializes save_orig_name.
 */
local int make_ofname()
{
    int iflen = strlen(ifname);
    int suflen = has_z_suffix(ifname, iflen);
    int zip_suffix = iflen > 4 && (strcmp(ifname+iflen-4, ".zip") == 0
			  || strcmp(ifname+iflen-4, ".ZIP") == 0);

    if (decompress) {
	/* Be tolerant for "zcat foo.tar-z | tar xf -" but do not
         * complain for "gunzip -r *". In other words, force a compressed
         * suffix for gunzip but not zcat.
         */
	if (!to_stdout && suflen == 0 && !zip_suffix) {
	    if (verbose) {
		fprintf(stderr,"%s -- no gz suffix, ignored\n", ifname);
	    }
	    return -1;
	}
	strcpy(ofname, ifname);
	if (suflen != 0) {
	    ofname[iflen - suflen] = '\0';
	} else if (zip_suffix) {
	    ofname[iflen - 4] = '\0'; /* Remove the .zip suffix */
	}
        /* ofname might be changed later if infile contains an original name */

    } else { /* compress */
	if (suflen != 0) {
	    /* Avoid annoying messages with -r (see treat_dir()) */
	    if (verbose || !recursive) {
		fprintf(stderr,"%s already has %s suffix -- unchanged\n",
			ifname, ifname+iflen-suflen);
	    }
	    return -1;
	}
	if (zip_suffix) {
	    fprintf(stderr,"%s already has .zip suffix -- unchanged\n",
		    ifname);
	    return -1;
	}
	strcpy(ofname, ifname);
        save_orig_name = 0;

	strcat(ofname, do_lzw ? ".Z" : Z_SUFFIX);

    } /* decompress ? */
    return 0;
}


/* ========================================================================
 * Check the magic number of the input file and update ofname if an
 * original name was given and to_stdout is not set.
 * Return the compression method or -1 for error.
 * Set inptr to the offset of the next byte to be processed.
 * This function may be called repeatedly for an input file consisting
 * of several contiguous gzip'ed members.
 * IN assertions: there is at least one remaining compressed member.
 *   If the member is a zip file, it must be the only one.
 */
local int get_method(in)
    int in;        /* input file descriptor */
{
    uch flags;
    char magic[2]; /* magic header */

    magic[0] = get_byte();
    magic[1] = get_byte();

    time_stamp = istat.st_mtime; /* may be modified later for some methods */
    method = -1;                 /* unknown yet */
    single_member = 0;           /* assume multiple members in gzip file */

    if (memcmp(magic, GZIP_MAGIC, 2) == 0
        || memcmp(magic, OLD_GZIP_MAGIC, 2) == 0) {

	work = unzip;
	method = get_byte();
	flags  = get_byte();
	if ((flags & ENCRYPTED) != 0) {
	    fprintf(stderr, "%s is encrypted -- get newer version of gzip\n",
		    ifname);
	    exit_code = 1;
	    return -1;
	}
	if ((flags & CONTINUATION) != 0) {
	    fprintf(stderr,
	       "%s is a a multi-part gzip file -- get newer version of gzip\n",
		    ifname);
	    exit_code = 1;
	    if (force <= 1) return -1;
	}
	if ((flags & RESERVED) != 0) {
	    fprintf(stderr, "%s has flags 0x%x -- get newer version of gzip\n",
		    ifname, flags);
	    exit_code = 1;
	    if (force <= 1) return -1;
	}
	time_stamp  = get_byte();
	time_stamp |= get_byte()<<8;
	time_stamp |= ((ulg)get_byte()) << 16;
	time_stamp |= ((ulg)get_byte()) << 24;

	(void)get_byte();  /* Ignore extra flags for the moment */
	(void)get_byte();  /* Ignore OS type for the moment */

	if ((flags & CONTINUATION) != 0) {
	    unsigned part = get_byte();
	    part |= get_byte()<<8;
	    if (verbose) {
		fprintf(stderr,"%s: part number %u\n",
			ifname, part);
	    }
	}
	if ((flags & EXTRA_FIELD) != 0) {
	    unsigned len = get_byte();
	    len |= get_byte()<<8;
	    if (verbose) {
		fprintf(stderr,"%s: extra field of %u bytes ignored\n",
			ifname, len);
	    }
	    while (len--) (void)get_byte();
	}

	/* Get original file name if it was truncated */
	if ((flags & ORIG_NAME) != 0) {
	    if (to_stdout) {
		/* Discard the old name */
		while (get_byte() != '\0') /* null */ ;
	    } else {
		/* Copy the base name. Keep a directory prefix intact. */
		char *p = basename(ofname);
		for (;;) {
		    *p = get_byte();
		    if (*p++ == '\0') break;
		    if (p >= ofname+sizeof(ofname)) {
			error("corrupted input -- file name too large");
		    }
		}
	    } /* to_stdout */
	} /* orig_name */

	/* Discard file comment if any */
	if ((flags & COMMENT) != 0) {
	    while (get_byte() != '\0') /* null */ ;
	}

    } else if (memcmp(magic, PKZIP_MAGIC, 2) == 0 && inptr == 2
	    && memcmp(inbuf, PKZIP_MAGIC, 4) == 0) {
	/* To simplify the code, we support a zip file when alone only.
         * We are thus guaranteed that the entire local header fits in inbuf.
         */
        inptr = 0;
	work = unzip;
	if (check_zipfile(in) == -1) return -1;
	/* check_zipfile may get ofname from the local header */
	single_member = 1;

    } else if (memcmp(magic, PACK_MAGIC, 2) == 0) {
	work = unpack;
	method = PACKED;
    } else if (memcmp(magic, LZW_MAGIC, 2) == 0) {
#ifdef NO_LZW
	/* The LZW decoder wants a 128K code table, which no single object on
	 * this target can hold.  uncompress(1) reads these files.
	 */
	fprintf(stderr, "%s is in compress .Z format -- use uncompress\n",
		ifname);
	exit_code = 1;
	return -1;
#else
	work = unlzw;
	method = COMPRESSED;
	single_member = 1;
#endif
    }
    if (method == -1) {
	fprintf(stderr, "%s is not in gzip format\n", ifname);
	exit_code = 1;
	return -1;
    }
    return method;
}

/* ========================================================================
 * Return true if the two stat structures correspond to the same file.
 */
local int same_file(stat1, stat2)
    struct stat *stat1;
    struct stat *stat2;
{
    return stat1->st_mode  == stat2->st_mode
	&& stat1->st_ino   == stat2->st_ino
	&& stat1->st_dev   == stat2->st_dev
	&& stat1->st_uid   == stat2->st_uid
	&& stat1->st_gid   == stat2->st_gid
	&& stat1->st_size  == stat2->st_size
	&& stat1->st_atime == stat2->st_atime
	&& stat1->st_mtime == stat2->st_mtime
	&& stat1->st_ctime == stat2->st_ctime;
}

/* ========================================================================
 * Return true if a file name is ambigous because the operating system
 * truncates file names.
 */
local int name_too_long(name, statb)
    char *name;           /* file name to check */
    struct stat *statb;   /* stat buf for this file name */
{
    int s = strlen(name);
    char c = name[s-1];
    struct stat	tstat; /* stat for truncated name */
    int res;

    tstat = *statb;      /* Just in case OS does not fill all fields */
    name[s-1] = '\0';
    res = stat(name, &tstat) == 0 && same_file(statb, &tstat);
    name[s-1] = c;
    return res;
}

/* ========================================================================
 * If compressing to a file, check if ofname is not ambigous
 * because the operating system truncates names. Otherwise, generate
 * a new ofname and save the original name in the compressed file.
 * If the compressed file already exists, ask for confirmation.
 *    The check for name truncation is made dynamically, because different
 * file systems on the same OS might use different truncation rules (on SVR4
 * s5 truncates to 14 chars and ufs does not truncate).
 *    This function returns -1 if the file must be skipped, and
 * updates save_orig_name if necessary.
 * IN assertions: save_orig_name is already set if ofname has been
 * already truncated because of NO_MULTIPLE_DOTS. The input file has
 * already been open and istat is set.
 */
local int check_ofname()
{
    int s = strlen(ofname);
    struct stat	ostat; /* stat for ofname */

    if (stat(ofname, &ostat) != 0) return 0;

    /* Check for name truncation on existing file: */
#ifdef NO_MULTIPLE_DOTS
    if (!decompress && name_too_long(ofname, &ostat)) {
#else
    if (!decompress && s > 8 && name_too_long(ofname, &ostat)) {
#endif
	save_orig_name = 1;
#ifdef NO_MULTIPLE_DOTS
	strcpy(ofname+s-2, "z");  /* f.extz -> f.exz  */
#else
	strcpy(ofname+s-Z_SUFLEN-2, Z_SUFFIX);
	/* 12345678901234.gz -> 123456789012.gz */
#endif
	if (stat(ofname, &ostat) != 0) return 0;
    } /* !decompress && name_too_long */

    /* Check that the input and output files are different (could be
     * the same by name truncation or links).
     */
    if (same_file(&istat, &ostat)) {
	fprintf(stderr, "error: %s and %s are the same file\n",
		ifname, ofname);
	exit_code = 1;
	return -1;
    }
    /* Ask permission to overwrite the existing file */
    if (!force && isatty(fileno(stdin))) {
	char response[80];
	strcpy(response,"n");
	fprintf(stderr, "%s already exists;", ofname);
	if (foreground) {
	    fprintf(stderr, " do you wish to overwrite (y or n)? ");
	    fflush(stderr);
	    (void)read(fileno(stdin), response, sizeof(response));
	}
	if (tolow(*response) != 'y') {
	    fprintf(stderr, "\tnot overwritten\n");
	    return -1;
	}
    }
    if (unlink(ofname)) {
	fprintf(stderr, "Can't remove old output file\n");
	perror(ofname);
	exit_code = 1;
	return -1;
    }
    return 0;
}


/* ========================================================================
 * Copy modes, times, ownership.
 * IN assertion: to_stdout is false.
 */
local void copy_stat(ifstat)
    struct stat *ifstat;
{
#ifndef NO_UTIME
    struct utimbuf	timep;

    /* Copy the time stamp */
    timep.actime = ifstat->st_atime;
    timep.modtime = ifstat->st_mtime;

    if (decompress && timep.modtime != time_stamp && time_stamp != 0) {
	timep.modtime = time_stamp;
	if (verbose) {
	    fprintf(stderr, " (time stamp restored)\n");
	}
    }
    if (utime(ofname, &timep)) {
	fprintf(stderr, "\nutime error (ignored) ");
	perror(ofname);
	exit_code = 1;
    }
#endif
    /* Copy the protection modes */
    if (chmod(ofname, ifstat->st_mode & 07777)) {
	fprintf(stderr, "\nchmod error (ignored) ");
	perror(ofname);
	exit_code = 1;
    }
#ifndef NO_CHOWN
    chown(ofname, ifstat->st_uid, ifstat->st_gid);  /* Copy ownership */
#endif
    remove_ofname = 0;
    /* It's now safe to remove the input file: */
    if (unlink(ifname)) {
	fprintf(stderr, "\nunlink error (ignored) ");
	perror(ifname);
	exit_code = 1;
    }
}

#ifndef NO_DIR

/* ========================================================================
 * Recurse through the given directory. This code is taken from ncompress.
 */
local void treat_dir(dir)
    char *dir;
{
    dir_type *dp;
    DIR      *dirp;
    char     nbuf[MAX_PATH_LEN];

    dirp = opendir(dir);
    
    if (dirp == NULL) {
	fprintf(stderr, "%s unreadable\n", dir);
	return ;
    }
    /*
     ** WARNING: the following algorithm could occasionally cause
     ** compress to produce error warnings of the form "<filename>.z
     ** already has .z suffix - ignored". This occurs when the
     ** .z output file is inserted into the directory below
     ** readdir's current pointer.
     ** These warnings are harmless but annoying, so they are suppressed
     ** with option -r (except when -v is on). An alternative
     ** to allowing this would be to store the entire directory
     ** list in memory, then compress the entries in the stored
     ** list. Given the depth-first recursive algorithm used here,
     ** this could use up a tremendous amount of memory. I don't
     ** think it's worth it. -- Dave Mack
     ** (An other alternative might be two passes to avoid depth-first.)
     */
    
    while ((dp = readdir(dirp)) != NULL) {

	if (dp->d_ino == 0) {
	    continue;
	}
	if (strcmp(dp->d_name,".") == 0 || strcmp(dp->d_name,"..") == 0) {
	    continue;
	}
	if (((int)strlen(dir) + NLENGTH(dp) + 1) < (MAX_PATH_LEN - 1)) {
	    strcpy(nbuf,dir);
	    strcat(nbuf,"/");
	    strcat(nbuf,dp->d_name);
	    treat_file(nbuf);
	} else {
	    fprintf(stderr,"Pathname too long: %s/%s\n", dir, dp->d_name);
	}
    }
    closedir(dirp);
}
#endif /* ? NO_DIR */

/* ========================================================================
 * Signal and error handler.
 */
RETSIGTYPE abort_gzip()
{
   if (remove_ofname) {
       unlink (ofname);
   }
   exit(1);
}
