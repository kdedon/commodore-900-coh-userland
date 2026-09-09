/*
 * A debugger.
 * Tables for the Zilog 8001.
 */
#include <stdio.h>
#include <types.h>
#include <machine.h>
#include <n.out.h>
#include "trace.h"
#include "z8001.h"

/*
 * Global variables.
 */
int	cacdata;
long	cacaddr;
int	cacsegn;
int	sysflag;
BIN	sin;
REG	reg;
int	slflag;

/*
 * Breakpoint instruction
 */
BIN	bin ={
	0x7F, 0x81
};

/*
 * Table containing format strings.
 */
char *formtab[4][4] ={
	"%4d",				/* 'b', 'd' */
	"%3u",				/* 'b', 'u' */
	"%04o",				/* 'b', 'o' */
	"%02x",				/* 'b', 'x' */
	"%6d",				/* 'w', 'd' */
	"%5u",				/* 'w', 'u' */
	"%07o",				/* 'w', 'o' */
	"%04x",				/* 'w', 'x' */
	"%10ld",			/* 'l', 'd' */
	"%11lu",			/* 'l', 'u' */
	"%012lo",			/* 'l', 'o' */
	"%08lx",			/* 'l', 'x' */
	"%8ld",				/* 'v', 'd' */
	"%8lu",				/* 'v', 'd' */
	"%09lo",			/* 'v', 'o' */
	"%06lx"				/* 'v', 'x' */
};

/*
 * Register indices in user area.
 */
int rintab[] ={
	offset(ureg, ur_r0),
	offset(ureg, ur_r1),
	offset(ureg, ur_r2),
	offset(ureg, ur_r3),
	offset(ureg, ur_r4),
	offset(ureg, ur_r5),
	offset(ureg, ur_r6),
	offset(ureg, ur_r7),
	offset(ureg, ur_r8),
	offset(ureg, ur_r9),
	offset(ureg, ur_r10),
	offset(ureg, ur_r11),
	offset(ureg, ur_r12),
	offset(ureg, ur_r13),
	offset(ureg, ur_r14),
	offset(ureg, ur_r15)
};

/*
 * Machine dependent system calls.
 */
char *sysdtab[NMDCALL] ={
	"sgrow",
	"bpt",
	"halt"
};

/*
 * Fault types.
 */
char *signame[] ={
	"Hangup",
	"Interrupt",
	"Quit",
	"Alarm clock",
	"Termination signal",
	"Restart",
	"Bad argument to system call",
	"Write on open pipe",
	"Kill",
	"Breakpoint",
	"Segmentation violation",
	"Unimplemented instruction",
	"Privileged instruction",
	"NVI/Step trap"
};

/*
 * Table of condition codes
 */
char *ccdtab[] ={
	"nun",
	"lt",
	"le",
	"ule",
	"ov",
	"mi",
	"eq",
	"ult",
	"un",
	"ge",
	"gt",
	"ugt",
	"nov",
	"pl",
	"ne",
	"nc"
};

/*
 * Control registers.
 */
char *ctltab[] ={
	"?",
	"FLAGS",
	"FCW",
	"REFRESH",
	"PSAPSEG",
	"PSAPOFF",
	"NSPREG",
	"NSPOFF"
};

/*
 * Interrupt flags.
 */
char *inttab[] ={
	"VI, NVI",
	"VI",
	"NVI",
	""
};

/*
 * Machine independent system calls.
 */
char *sysitab[NMICALL] ={
	NULL,
	"exit",
	"fork",
	"read",
	"write",
	"open",
	"close",
	"wait",
	"creat",
	"link",
	"unlink",
	"exece",
	"chdir",
	NULL,
	"mknod",
	"chmod",
	"chown",
	"brk",
	"stat",
	"lseek",
	"getpid",
	"mount",
	"umount",
	"setuid",
	"getuid",
	"stime",
	"ptrace",
	"alarm",
	"fstat",
	"pause",
	"utime",
	NULL,
	NULL,
	"access",
	"nice",
	"ftime",
	"sync",
	"kill",
	NULL,
	NULL,
	NULL,
	"dup",
	"pipe",
	"times",
	"profil",
	"unique",
	"setgid",
	"getgid",
	"signal",
	NULL,
	NULL,
	"acct",
	NULL,
	NULL,
	"ioctl",
	NULL,
	"getegid",
	"geteuid",
	NULL,
	NULL,
	"umask",
	"chroot",
	NULL,
	NULL,
	"sload",
	"suload"
};

/*
 * Instruction table.
 */
INS instab[] ={
	0xb400,	0xfe00,	"adc%t\t%0r,%1r",
	0x0000,	0x3e00,	"add%t\t%0r,%1A",
	0x1600,	0x3f00,	"addl%lm\t%0r,%1A",
	0x0600,	0x3e00,	"and%t\t%0r,%1A",
	0x2600,	0x3e00,	"bit%t\t%1z0B,$%e4a",
	0x2600,	0xfef0,	"bit%t\t%n6r,%w0r",
	0x1f00,	0xbf0f,	"call\t%1B",
	0xd000,	0xf000,	"calr\t%ecucd+",
	0x0c08,	0x3e0f,	"clr%t\t%1B",
	0x0c00,	0x3e0f,	"com%t\t%1B",
	0x8d05,	0xff0f,	"comflg %1f",
	0x0a00,	0x3e00,	"cp%t\t%0r,%1A",
	0x1000,	0x3f00,	"cpl%lm\t%l0r,%1A",
	0x0c01,	0xbe0f,	"cp%t\t%1B,$%o",
	0xba00,	0xfe01,	"cp%0y1s*%y3di%y2r*%t\t%n5r,(%x1r),%w6r,%4c",
	0xb000,	0xff00,	"dab\t%bm%1r",
	0x2a00,	0x3e00,	"dec%t\t%1B,$%e4i+a",
	0x7c00,	0xfffc,	"di\t%v",
	0x1b00,	0x3f00,	"div\t%l0r,%1A",
	0x1a00,	0x3f00,	"divl%lm\t%q0r,%1A",
	0xf080,	0xf080,	"djnz\t%2r,%e7d-",
	0xf000,	0xf080,	"dbjnz%bm %2r,%e7d-",
	0x7c04,	0xfffc,	"ei\t%v",
	0x2c00,	0x3e00,	"ex%t\t%0r,%1B",
	0xb100,	0xff0f,	"extsb %1r",
	0xb10a,	0xff0f,	"exts%lm\t%1r",
	0xb107,	0xff0f,	"extsl%qm %1r",
	0x7a00,	0xffff,	"halt",
	0x3c00,	0xfe00,	"in%t\t%0r,(%x1r)",
	0x3a04,	0xfe0f,	"in%t\t%1r,%na",
	0x2800,	0x3e00,	"inc%t\t%1B,$%e4i+a",
	0x3a00,	0xfe07,	"in%0y3di%n4y3*r%t\t(%w5r),(%1r),%w6r",
	0x7b00,	0xffff,	"iret",
	0x1e00,	0xbf00,	"jp\t%0c,%1B",
	0xe000,	0xf000,	"jr\t%2c,%e8u8d+",
	0x2000,	0x3e00,	"ld%t\t%0r,%1A",
	0x1400,	0x3f00,	"ldl%lm\t%0r,%1A",
	0xc000,	0xf000,	"ldb%bm\t%2r,$%e8a",
	0x3000,	0xfe00,	"ld%t\t%0r,%1z0xr(%nwa)",
	0x3500,	0xff00,	"ldl%lm\t%0r,%1z0xr(%nwa)",
	0x7000,	0xfe00,	"ld%t\t%0r,%1z1xr(%nw6r)",
	0x7500,	0xff00,	"ldl%lm\t%0r,%1z1xr(%nw6r)",
	0x2e00,	0xbe00,	"ld%t\t%1B,%0r",
	0x1d00,	0xbf00,	"ldl%lm\t%1B,%0r",
	0x0c05,	0xbe0f,	"ld%t\t%1B,$%o",
	0x3200,	0xfe00,	"ld%t\t%1z0xr(%nwa),%0r",
	0x3700,	0xff00,	"ldl%lm\t%1z0xr(%nwa),%0r",
	0x7200,	0xfe00,	"ld%t\t%1z1xr(%nw6),%0r",
	0x7700,	0xff00,	"ldl%lm\t%1z1xr(%nw6),%0r",
	0x7600,	0xff00,	"lda\tr%0r,%1B",
	0x3400,	0xff00,	"lda\tr%0r,%1z0xr(%nwa)",
	0x7400,	0xff00,	"lda\tr%0r,%1z1xr(%nw6r)",
	0x3400,	0xfff0,	"ldar\tr%0r,%nd.",
	0x8c09,	0xff0f,	"ldctl %0k,%1r",
	0x8c01,	0xff0f,	"ldctl %1r,%0k",
	0x7d08,	0xff08,	"ldctl %0k,%1r",
	0x7d00,	0xff08,	"ldctl %1r,%0k",
	0xba01,	0xfe07,	"ld%0y3di%n4y3*r%t\t(%x5r),(%x1r),%w6r",
	0xbd00,	0xff00,	"ldk\t%1r,$%e4a",
	0x1c01,	0xbf0f,	"ldm\t%nw6r,%1B,$%e4i+a",
	0x1c09,	0xbf0f,	"ldm\t%n1B,%w6r,$%e4i+a",
	0x3900,	0xbf0f,	"ldps\t%1B",
	0x3000,	0xfef0,	"ldr%t\t%0r,%nd.",
	0x3500,	0xfff0,	"ldrl%lm\t%0r,%nd.",
	0x3200,	0xfef0,	"ldr%t\t%nd.,%0r",
	0x3700,	0xfff0,	"ldrl%lm\t%nd.,%0r",
	0x7b0a,	0xffff,	"mbit",
	0x7b0d,	0xff0f,	"mreq\t%1r",
	0x7b09,	0xffff,	"mres",
	0x7b08,	0xffff,	"mset",
	0x1900,	0x3f00,	"mult\t%l0r,%w1A",
	0x1800,	0x3f00,	"multl%lm %q0r,%1A",
	0x0c02,	0x3e0f,	"neg\t%1B",
	0x8d07,	0xffff,	"nop",
	0x0400,	0x3e00,	"or%t\t%0r,%1A",
	0x3a08,	0xfe07,	"ot%0y3dir%t\t(%xn5r),(%x1r),%w6r",
	0x3e00,	0xfe00,	"out%t\t(%x1r),%0r",
	0x3a06,	0xfe0f,	"out%t\t%na,%1r",
	0x3a02,	0xfe07,	"out%0y3di%t\t(%xn5r),(%x1r),%w6r",
	0x1700,	0x3f00,	"pop\t%0B,(%x1r)",
	0x1500,	0x3f00,	"popl%lm\t%l0B,(%x1r)",
	0x1300,	0x3f00,	"push\t(%x1r),%w0B",
	0x1100,	0x3f00,	"pushl%lm (%x1r),%0B",
	0x0d09,	0xff0f,	"push\t(%x1r),$%o",
	0x2200,	0x3e00,	"res%t\t%1z0B,$%e4a",
	0x2200,	0xfef0,	"res%t\t%n6r,%0r",
	0x8d03,	0xff0f,	"resflg %1f",
	0x9e08,	0xffff,	"ret",
	0x9e00,	0xfff0,	"ret\t%0c",
	0xb200,	0xfe01,	"r%0y2rl%y3c*%t\t%1r,$%0y121",
	0xbc00,	0xfd00,	"r%2y1lrdb\t%b0r,%1r",
	0xb600,	0xbe00,	"sbc%t\t%0r,%1r",
	0x7f00,	0xff00,	"sys\t%s",
	0xb203,	0xfe07,	"sd%0y3al%t\t%1r,%n6r",
	0xb30f,	0xff0f,	"sd%0y3all\t%l1r,%n6r",
	0xb201,	0xfe07,	"s%n7y3rl%0y3al%t\t%1r,$%i|a",
	0xb305,	0xff07,	"s%n7y3rl%0y3all%lm\t%1r,$%i|a",
	0x2400,	0x3e00,	"set%t\t%1z0B,$%e4a",
	0x2400,	0xfef0,	"set%t\t%n6r,%w0r",
	0x8d01,	0xff0f,	"setflg %1f",
	0x3a05,	0xfe0f,	"sin%t\t%1r,%na",
	0x3a01,	0xfe07,	"sin%0y3di%n4y3*r%t\t(%x5r),(%x1r),%w6r",
	0x3a07,	0xfe0f,	"sout%t\t%na,%1r",
	0x3a03,	0xfe07,	"so%n4y3u*t%0y3di%4y3*r%t\t(%x5r),(%x1r),%w6r",
	0x0200,	0x3e00,	"sub%t\t%0r,%1A",
	0x1200,	0x3f00,	"subl%lm\t%0r,%1A",
	0xae00,	0xfe00,	"tcc%t\t%0c,%1r",
	0x0c04,	0x3e0f,	"test%t\t%1B",
	0x1c08,	0x3f0f,	"testl%lm %1B",
	0xb800,	0xff01,	"tr%0y1t*%y3di%y2r*b\t(%x1r),(%nx5r),%w6r",
	0x0c06,	0x3e0f,	"tset%t\t%1B",
	0x0800,	0x3e00,	"xor%t\t%0r,%1A",
	0x0000,	0x0000,	NULL
};
