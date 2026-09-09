/ Load sentinels into R6..R12, call one string routine, report the first
/ register that came back changed (0 = all intact).

	.globl	clstrlen_, clstrcpy_, clindex_, clstrcmp_
	.globl	strlen_, strcpy_, index_, strcmp_
	.globl	SS

clstrlen_:
	ldl	rr2, SS|4(r15)
	pushl	(rr14), rr2
	calr	setup
	call	strlen_
	add	r15, $4
	jr	un, check

clindex_:
	ld	r0, SS|8(r15)
	ldl	rr2, SS|4(r15)
	push	(rr14), r0
	pushl	(rr14), rr2
	calr	setup
	call	index_
	add	r15, $6
	jr	un, check

clstrcpy_:
	ldl	rr4, SS|8(r15)
	ldl	rr2, SS|4(r15)
	pushl	(rr14), rr4
	pushl	(rr14), rr2
	calr	setup
	call	strcpy_
	add	r15, $8
	jr	un, check

clstrcmp_:
	ldl	rr4, SS|8(r15)
	ldl	rr2, SS|4(r15)
	pushl	(rr14), rr4
	pushl	(rr14), rr2
	calr	setup
	call	strcmp_
	add	r15, $8
	jr	un, check

setup:
	ld	r6, $0x6666
	ld	r7, $0x7777
	ld	r8, $0x8888
	ld	r9, $0x9999
	ld	r10, $0xaaaa
	ld	r11, $0xbbbb
	ld	r12, $0xcccc
	ret

check:
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
