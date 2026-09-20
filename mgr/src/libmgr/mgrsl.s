/ mgrsl.s -- the absolutes a shared libmgr needs of its own.
/
/ SS and errno_ are the same in every process; a program gets them from its
/ start-off, which a library does not link.

	.globl	SS
	.globl	errno_

SS = 0x0000

errno_ = 0x0000FFFE		/ SS|0xFFFE
