/*
 * Copyright (c) 2026 Kevin Dedon.
 * SPDX-License-Identifier: MIT
 */
/*
 * Constants.
 */
#define ISBPT	0x7F81			/* Breakpoint instruction */
#define ICALL	0x5f00			/* Call instruction */

/*
 * Simple functions.
 */
#define bound(n)	((n+0xFFFFL)&~0xFFFFL)

/*
 * Register offset.
 */
#define UREGOFF	(04000-(sizeof (struct ureg)))

/*
 * Registers from the Users area after a core dump.
 */
struct ureg {
	int	ur_r14;		/* Register 14 */
	int	ur_r15;		/* Register 15 */
	int	ur_r0;		/* Register 0 */
	int	ur_r1;		/* Register 1 */
	int	ur_r2;		/* Register 2 */
	int	ur_r3;		/* Register 3 */
	int	ur_r4;		/* Register 4 */
	int	ur_r5;		/* Register 5 */
	int	ur_r6;		/* register 6 */
	int	ur_r7;		/* register 7 */
	int	ur_r8;		/* Register 8 */
	int	ur_r9;		/* Register 9 */
	int	ur_r10;		/* Register 10 */
	int	ur_r11;		/* Register 11 */
	int	ur_r12;		/* Register 12 */
	int	ur_r13;		/* Register 13 */
	int	ur_yy[4];	/* skip ID, type, S15, S14 */
	int	ur_fcw;		/* Flag control word */
	long	ur_pc;		/* Program (no me) counter */
};

/*
 * Indices for registers in register structure.
 */
#define R0	0
#define R1	1
#define R2	2
#define R3	3
#define R4	4
#define R5	5
#define R6	6
#define R7	7
#define R8	8
#define R9	9
#define R10	10
#define R11	11
#define R12	12
#define R13	13
#define R14	14
#define R15	15

/*
 * Registers.
 */
typedef	struct reg {
	unsigned r_rn[16];		/* General registers */
	unsigned long r_pc;		/* Program counter */
	unsigned r_fcw;			/* Flags control word */
} REG;

/*
 * Instruction table.
 */
typedef struct {
	unsigned i_code;		/* Code */
	unsigned i_mask;		/* Mask */
	char	 *i_name;		/* Name */
} INS;

/*
 * Functions.
 */
char	*putifmt();
char	*index();
vaddr_t	getsp(), getfp(), getpc();
MAP	*lshrseg(), *lpriseg();

/*
 * Global symbols.
 */
extern	int	cacdata;		/* Current word in cache */
extern	long	cacaddr;		/* Address of word in cache */
extern	int	cacsegn;		/* Segment number of word in cache */
extern	int	sysflag;		/* Executing a system call */
extern	BIN	sin;			/* Instruction after sys call */
extern	REG	reg;			/* General registers */
extern	int	slflag;			/* l.out refs shared libraries */

/*
 * Tables.
 */
extern	int	rintab[];		/* Register index table */
extern	char	*sysitab[NMICALL];	/* Independent system calls */
extern	char	*sysdtab[NMDCALL];	/* Dependent system calls */
extern	char	*sigtab[];		/* Fault type table */
extern	char	*ccdtab[];		/* Table of condition codes */
extern	char	*ctltab[];		/* Control registers */
extern	char	*inttab[];		/* Interrupt flags */
extern	char	*formtab[4][4];		/* Format table */
extern	INS	instab[];		/* Table of instructions */

/*
 *  Compatability definitions for use with common db code.
 */
#define	printk	printr
#define	printn	printx
#define	putn	putx
