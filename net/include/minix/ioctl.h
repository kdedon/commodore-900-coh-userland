/*	minix/ioctl.h - Ioctl helper definitions.	Author: Kees J. Bot
 *
 * COHERENT osdep port: adapted from Minix 2.0.4.  The command-encoding
 * macros gate on _WORD_SIZE (set to 2 for the Z8001 by minix/config.h)
 * rather than the ACK-specific _EM_WSIZE, and the ioctl() declaration is
 * K&R so cc0 accepts it.
 */
#ifndef _M_IOCTL_H
#define _M_IOCTL_H

#ifndef _TYPES_H
#include <sys/types.h>
#endif

#if _WORD_SIZE >= 4
/* Ioctls have the command encoded in the low-order word, and the size
 * of the parameter in the high-order word. The 3 high bits of the high-
 * order word are used to encode the in/out/void status of the parameter.
 */
#define _IOCPARM_MASK	0x1FFF
#define _IOC_VOID	0x20000000
#define _IOCTYPE_MASK	0xFFFF
#define _IOC_IN		0x40000000
#define _IOC_OUT	0x80000000
#define _IOC_INOUT	(_IOC_IN | _IOC_OUT)

#define _IO(x,y)	((x << 8) | y | _IOC_VOID)
#define _IOR(x,y,t)	((x << 8) | y | ((sizeof(t) & _IOCPARM_MASK) << 16) |\
				_IOC_OUT)
#define _IOW(x,y,t)	((x << 8) | y | ((sizeof(t) & _IOCPARM_MASK) << 16) |\
				_IOC_IN)
#define _IORW(x,y,t)	((x << 8) | y | ((sizeof(t) & _IOCPARM_MASK) << 16) |\
				_IOC_INOUT)
#else
/* No fancy encoding on a 16-bit machine: there is no room for the size and
 * direction bits, so a command is just the type letter and the number.
 *
 * THE TYPE OF THESE MACROS IS int (16 bits here), and this header is K&R -- no
 * prototype widens an argument at a call site.  So every function parameter and
 * struct field that carries one of these codes must be declared int, not long.
 * Getting that wrong does not warn: the caller pushes 2 bytes, the callee reads
 * 4, and the code arrives shifted left 16 bits with the following arguments
 * pulled out of the wrong stack words.  That is how ifconfig died with "Bad
 * system call" -- ichan_ioctl declared `long req', so the payload pointer it
 * handed to write(2) was garbage and the kernel's EFAULT became SIGSYS.
 */

#define _IO(x,y)	((x << 8) | y)
#define _IOR(x,y,t)	_IO(x,y)
#define _IOW(x,y,t)	_IO(x,y)
#define _IORW(x,y,t)	_IO(x,y)
#endif

int ioctl();

#endif /* _M_IOCTL_H */
