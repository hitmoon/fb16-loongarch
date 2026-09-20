/*-
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2019 Mitchell Horne
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asm.h>
#include <machine/loongarchreg.h>
#include <sys/errno.h>

#include "assym.inc"

/*
 * Fault handler for the copy{in,out} functions below.
 */
ENTRY(copyio_fault)
	SET_FAULT_HANDLER($zero, $a1) /* Clear the handler */
	EXIT_USER_ACCESS($a1)
copyio_fault_nopcb:
	li.d	$a0, EFAULT
	jirl 	$zero, $ra, 0
END(copyio_fault)

/*
 * copycommon - common copy routine
 *
 * a0 - Source address
 * a1 - Destination address
 * a2 - Size of copy
 */
	.macro copycommon
	la.pcrel	$a6, _C_LABEL(copyio_fault)	/* Get the handler address */
	SET_FAULT_HANDLER($a6, $a7)	/* Set the handler */
	ENTER_USER_ACCESS($a7)

	li.d	$t2, GRLEN_BYTES
	blt	$a2, $t2, 4f		/* Byte-copy if len < GRLEN_BYTES */

	/*
	 * Compare lower bits of src and dest.
	 * If they are aligned with each other, we can do word copy.
	 */
	li.d	$t2, (GRLEN_BYTES-1)
	and	$t0, $a0, $t2			/* Low bits of src */
	and	$t1, $a1, $t2			/* Low bits of dest */
	bne	$t0, $t1, 4f		/* Misaligned. Go to byte copy */
	li.d	$t2, GRLEN_BYTES
	beqz	$t0, 2f			/* Already word-aligned, skip ahead */

	/* Byte copy until the first word-aligned address */
1:	ld.b	$a4, $a0, 0		/* Load byte from src */
	addi.d	$a0, $a0, 1
	st.b	$a4, $a1, 0		/* Store byte in dest */
	addi.d	$a1, $a1, 1
	addi.d	$a2, $a2, -1		/* len-- */
	li.d	$t1, (GRLEN_BYTES-1)
	and	$t0, $a0, $t1
	bnez	$t0, $1b
	la.pcrel	$t3, 3f
	jr	$t3

	/* Copy words */
2:	ld.d	$a4, $a0, 0		/* Load word from src */
	addi.d	$a0, $a0, GRLEN_BYTES
	st.d	$a4, $a1, 0		/* Store word in dest */
	addi.d	$a1, $a1, GRLEN_BYTES
	addi.d	$a2, $a2, -GRLEN_BYTES	/* len -= GRLEN_BYTES */
3:	bgeu	$a2, $t2, 2b		/* Again if len >= GRLEN_BYTES */

	/* Check if we're finished */
	beqz	$a2, 5f

	/* Copy any remaining bytes */
4:	ld.b	$a4, $a0, 0		/* Load byte from src */
	addi.d	$a0, $a0, 1
	st.b	$a4, $a1, 0		/* Store byte in dest */
	addi.d	$a1, $a1, 1
	addi.d	$a2, $a2, -1		/* len-- */
	bnez	$a2, 4b

5:	EXIT_USER_ACCESS($a7)
	SET_FAULT_HANDLER($zero, $a7)	/* Clear the handler */
	.endm

/*
 * Copies from a kernel to user address
 *
 * int copyout(const void *kaddr, void *udaddr, size_t len)
 */
ENTRY(copyout)
	beqz	$a2, copyout_end	/* If len == 0 then skip loop */
	add.d	$a3, $a1, $a2
	li.d	$a4, VM_MAXUSER_ADDRESS
	bgeu	$a3, $a4, copyio_fault_nopcb

	copycommon

copyout_end:
	li.d	$a0, 0		/* return 0 */
	jirl	$zero, $ra, 0
END(copyout)

/*
 * Copies from a user to kernel address
 *
 * int copyin(const void *uaddr, void *kaddr, size_t len)
 */
ENTRY(copyin)
	beqz	$a2, copyin_end	/* If len == 0 then skip loop */
	add.d	$a3, $a0, $a2
	li.d	$a4, VM_MAXUSER_ADDRESS
	bgeu	$a3, $a4, copyio_fault_nopcb

	copycommon

copyin_end:
	li.d	$a0, 0		/* return 0 */
	jirl	$zero, $ra, 0
END(copyin)

/*
 * Copies a string from a user to kernel address
 *
 * int copyinstr(const void *udaddr, void *kaddr, size_t len, size_t *done)
 */
ENTRY(copyinstr)
	move	$a5, $zero	/* count = 0 */
	beqz	$a2, 3f		/* If len == 0 then skip loop */

	la.pcrel	$a6, _C_LABEL(copyio_fault) /* Get the handler address */
	SET_FAULT_HANDLER($a6, $a7) /* Set the handler */
	ENTER_USER_ACCESS($a7)

	li.d	$a7, VM_MAXUSER_ADDRESS
1:	bgeu	$a0, $a7, copyio_fault
	ld.b	$a4, $a0, 0	/* Load from uaddr */
	addi.d	$a0, $a0, 1
	st.b	$a4, $a1, 0	/* Store in kaddr */
	addi.d	$a1, $a1, 1
	beqz	$a4, 2f
	addi.d	$a2, $a2, -1	/* len-- */
	addi.d	$a5, $a5, 1	/* count++ */
	bnez	$a2, 1b

2:	EXIT_USER_ACCESS($a7)
	SET_FAULT_HANDLER($zero, $a7) /* Clear the handler */

3:	beqz	$a3, 4f		/* Check if done != NULL */
	addi.d	$a5, $a5, 1	/* count++ */
	st.d	$a5, $a3, 0	/* done = count */

4:	move	$a0, $zero	/* return 0 */
	beqz	$a4, 5f
	li.d	$a0, ENAMETOOLONG
5:
	jirl	$zero, $ra, 0
END(copyinstr)
