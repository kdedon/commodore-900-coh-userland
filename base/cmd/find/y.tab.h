/*
 * Copyright (c) 1977-1995 Robert Swartz.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * y.tab.h -- the token values and value-stack type of find's grammar, find.y.
 * This header is source, maintained by hand.  It must agree with find.y's
 * %union and with the token numbers Coherent yacc assigns to find.y's %token
 * declarations.  No source in this directory includes it: find is built from
 * find.y alone, and the parser generated from it includes none of this.
 */
typedef union {
	NODE  *nodeptr;
	} YYSTYPE;
#define OR 256
#define AND 257
#define NAME 258
#define PERM 259
#define TYPE 260
#define LINKS 261
#define USER 262
#define GROUP 263
#define SIZE 264
#define INUM 265
#define ATIME 266
#define CTIME 267
#define MTIME 268
#define EXEC 269
#define OK 270
#define PRINT 271
#define NEWER 272
#define FUN 273
#define NOP 274
#ifdef YYTNAMES
extern struct yytname
{
	char	*tn_name;
	int	tn_val;
} yytnames[];
#endif
extern	YYSTYPE	yylval;
