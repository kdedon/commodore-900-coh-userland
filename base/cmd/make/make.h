/*
 * Copyright (c) 1977-1995 Robert Swartz.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Definitions and declarations for make.
 * Created due to the offended sensitivities of all MWC, 1-2-85.
 */

/* Compiler-portability names.  COHERENT 4.2 took these from <sys/compat.h>,
 * which reaches <common/ccompat.h> and is not part of this system's headers.
 * USE_PROTO 0 selects the K&R function definitions throughout make.c.
 */
#define	USE_PROTO	0
#define	CONST
#define	VOID		char
#define	NO_RETURN	void

/* Types. */
#define	T_UNKNOWN	0
#define	T_NODETAIL	1
#define	T_DETAIL	2

/* Other manifest constants. */
#define	EOS	256
#define	NBACKUP	4096
#define	NMACRONAME	48
#define	NTOKBUF	100

/* Macros. */
#define	Streq(a,b)	(strcmp(a,b) == 0)

/* Structures. */
typedef	struct token {
	struct token *next;
	char *value;
} TOKEN;

typedef struct macro {
	struct macro *next;
	char *value;
	char *name;
	int flags;
} MACRO;

typedef struct sym {
	struct sym *next;
	char *action;
	char *name;
	char *filename;
	struct dep *deplist;
	char	type;
	char	done;
	time_t moddate;
} SYM;

typedef struct dep {
	struct dep *next;
	char *action;
	struct sym *symbol;
} DEP;

/* System dependencies. */
#if	MSDOS || GEMDOS
#define ACTIONFILE	"mactions"
#define MACROFILE	"mmacros"
#define TDELIM		" \t\n=;"
#endif

#if	__COHERENT__
#define ACTIONFILE	"makeactions"
#define MACROFILE	"makemacros"
#define TDELIM		" \t\n=:;"
#endif
