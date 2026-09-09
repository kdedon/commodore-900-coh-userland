/ Sentinel R6..R12 across the double runtime helpers, which save the pair set
/ with LDM to a stack hole rather than PUSHL.

	.globl	cldmul_, cldadd_, clddiv_
	.globl	dlmul, dladd, dldiv
	.globl	SS

cldmul_:
	calr	pushargs
	call	dlmul
	add	r15, $12
	jr	un, dcheck

cldadd_:
	calr	pushargs
	call	dladd
	add	r15, $12
	jr	un, dcheck

clddiv_:
	calr	pushargs
	call	dldiv
	add	r15, $12
	jr	un, dcheck

/ (a, b) at 4(r15)/8(r15) of the caller frame, which pushargs sees at +8/+12
pushargs:
	ldl	rr2, SS|12(r15)
	ldl	rr4, SS|16(r15)
	ldm	r8, (rr2), $4
	popl	rr2, (rr14)
	pushl	(rr14), rr4
	push	(rr14), r11
	push	(rr14), r10
	push	(rr14), r9
	push	(rr14), r8
	ld	r6, $0x6666
	ld	r7, $0x7777
	ld	r8, $0x8888
	ld	r9, $0x9999
	ld	r10, $0xaaaa
	ld	r11, $0xbbbb
	ld	r12, $0xcccc
	jp	(rr2)

dcheck:
	ld	r1, $6
	cp	r6, $0x6666
	jr	ne, 9f
	ld	r1, $7
	cp	r7, $0x7777
	jr	ne, 9f
	ld	r1, $8
	cp	r8, $0x8888
	jr	ne, 9f
	ld	r1, $9
	cp	r9, $0x9999
	jr	ne, 9f
	ld	r1, $10
	cp	r10, $0xaaaa
	jr	ne, 9f
	ld	r1, $11
	cp	r11, $0xbbbb
	jr	ne, 9f
	ld	r1, $12
	cp	r12, $0xcccc
	jr	ne, 9f
	ld	r1, $0
9:
	ret
