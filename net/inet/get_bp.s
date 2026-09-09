/
/ get_bp.s -- read the Z8001 frame-base register, for panic stack traces.
/
/ stacktrace() walks the frame chain from the frame it is itself running on, and
/ this returns that frame's pointer.  On this target the compiler keeps the frame
/ pointer in R13, addressing the BOTTOM of the frame (prologue: `DEC R15,#res' or
/ `SUB R15,#res', then the register save, then `LD R13,R15'), and a 16-bit int is
/ returned in R1.
/
/ There is deliberately NO prologue here: this routine must not build a frame of
/ its own, because that would leave R13 addressing that frame and the trace would
/ start one level too low.  Entering with R13 untouched, it still holds the
/ caller's frame pointer, which is exactly what is wanted.
/
/ The value is a 16-bit offset, not a full address: the stack lives in segment 0
/ (the compiler addresses locals at small positive displacements off R13), so
/ the near offset alone identifies a frame and stacktrace() can cast it back to
/ a pointer.
/

.globl	get_bp_

get_bp_:
	ld	r1, r13		/ caller's frame pointer -> int return register
	ret
