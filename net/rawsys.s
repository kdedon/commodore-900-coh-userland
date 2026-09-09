/
/ rawsys.s -- raw read/write/close syscall stubs.
/
/ libsocket overrides the C read/write/close so they work on socket handles.
/ For a real fd those overrides must reach the kernel, but they cannot call
/ read/write/close (that is themselves).  These stubs are byte-identical to
/ libc's read_/write_/close_ (sys 3/4/6) under private _raw* names -- private so
/ they never clash with a future libc _read, and so the overrides fall through
/ to them.  (C _rawread -> asm _rawread_.)
/

.globl	_rawread_
_rawread_:
	sys	3
	ret

.globl	_rawwrite_
_rawwrite_:
	sys	4
	ret

.globl	_rawclose_
_rawclose_:
	sys	6
	ret

/
/ open and ioctl join them for the /dev/tcp shim: libsocket turns an open of a
/ network device into a daemon channel and routes that channel's NWIO ioctls
/ over it, so those two names are overridden as well and need the same
/ fall-through.  (sys 5 = open, sys 54 = ioctl.)
/

.globl	_rawopen_
_rawopen_:
	sys	5
	ret

.globl	_rawioctl_
_rawioctl_:
	sys	066		/54
	ret
