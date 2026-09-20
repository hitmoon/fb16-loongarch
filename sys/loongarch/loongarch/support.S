/*-
 * Copyright (c) 2015-2020 Ruslan Bukin <br@bsdpad.com>
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
#include <machine/setjmp.h>
#include <machine/loongarchreg.h>

#include "assym.inc"

/*
 * One of the fu* or su* functions failed, return -1.
 */
ENTRY(fsu_fault)
	SET_FAULT_HANDLER($zero, $a1)	/* Reset the handler function */
	EXIT_USER_ACCESS($a1)
fsu_fault_nopcb:
	li.d	$a0, -1
	jr	$ra
END(fsu_fault)

/*
 * int casueword32(volatile uint32_t *, uint32_t, uint32_t *, uint32_t)
 */
ENTRY(casueword32)
	li.d	$a4, (VM_MAXUSER_ADDRESS-3)
	bgeu	$a0, $a4, fsu_fault_nopcb
	la.pcrel	$a6, _C_LABEL(fsu_fault)		/* Load the fault handler */
	SET_FAULT_HANDLER($a6, $a4)	/* And set it */
	ENTER_USER_ACCESS($a4)
	move	$t0, $a3		/* save newval: sc's rd is both value and result */
	li.d	$a5, 1			/* rv = 1 (CAS did not apply) by default */
1:	ll.w	$a4, $a0, 0		/* a4 = observed old value */
	bne	$a4, $a1, 2f		/* mismatch: leave rv = 1 */
	move	$a3, $t0		/* reload newval before sc (sc clobbers rd) */
	sc.w	$a3, $a0, 0		/* store newval; a3 = 1 (ok) / 0 (lost race) */
	beqz	$a3, 1b			/* lost the ll/sc race -> retry */
	li.d	$a5, 0			/* matched and stored: rv = 0 (success) */
2:	EXIT_USER_ACCESS($a6)
	SET_FAULT_HANDLER($zero, $a6)	/* Reset the fault handler */
	st.w	$a4, $a2, 0		/* Store the read data */
	move	$a0, $a5		/* 0 = success, 1 = CAS did not apply */
	jr	$ra			/* Return */
END(casueword32)

/*
 * int casueword(volatile u_long *, u_long, u_long *, u_long)
 */
ENTRY(casueword)
	li.d	$a4, (VM_MAXUSER_ADDRESS-7)
	bgeu	$a0, $a4, fsu_fault_nopcb
	la.pcrel	$a6, _C_LABEL(fsu_fault)		/* Load the fault handler */
	SET_FAULT_HANDLER($a6, $a4)	/* And set it */
	ENTER_USER_ACCESS($a4)
	move	$t0, $a3		/* save newval: sc's rd is both value and result */
	li.d	$a5, 1			/* rv = 1 (CAS did not apply) by default */
1:	ll.d	$a4, $a0, 0		/* a4 = observed old value */
	bne	$a4, $a1, 2f		/* mismatch: leave rv = 1 */
	move	$a3, $t0		/* reload newval before sc (sc clobbers rd) */
	sc.d	$a3, $a0, 0		/* store newval; a3 = 1 (ok) / 0 (lost race) */
	beqz	$a3, 1b			/* lost the ll/sc race -> retry */
	li.d	$a5, 0			/* matched and stored: rv = 0 (success) */
2:	EXIT_USER_ACCESS($a6)
	SET_FAULT_HANDLER($zero, $a6)	/* Reset the fault handler */
	st.d	$a4, $a2, 0		/* Store the read data */
	move	$a0, $a5		/* 0 = success, 1 = CAS did not apply */
	jr	$ra			/* Return */
END(casueword)

/*
 * int fubyte(volatile const void *)
 */
ENTRY(fubyte)
	li.d	$a1, VM_MAXUSER_ADDRESS
	bgeu	$a0, $a1, fsu_fault_nopcb
	la.pcrel	$a6, _C_LABEL(fsu_fault)		/* Load the fault handler */
	SET_FAULT_HANDLER($a6, $a1)	/* And set it */
	ENTER_USER_ACCESS($a1)
	ld.bu	$a0, $a0, 0		/* Try loading the data */
	EXIT_USER_ACCESS($a1)
	SET_FAULT_HANDLER($zero, $a1)	/* Reset the fault handler */
	jr	$ra			/* Return */
END(fubyte)

/*
 * int fuword(volatile const void *)
 */
ENTRY(fuword16)
	li.d	$a1, (VM_MAXUSER_ADDRESS-1)
	bgeu	$a0, $a1, fsu_fault_nopcb
	la.pcrel	$a6, _C_LABEL(fsu_fault)		/* Load the fault handler */
	SET_FAULT_HANDLER($a6, $a1)	/* And set it */
	ENTER_USER_ACCESS($a1)
	ld.hu	$a0, $a0, 0		/* Try loading the data */
	EXIT_USER_ACCESS($a1)
	SET_FAULT_HANDLER($zero, $a1)	/* Reset the fault handler */
	jr	$ra			/* Return */
END(fuword16)

/*
 * int32_t fueword32(volatile const void *, int32_t *)
 */
ENTRY(fueword32)
	li.d	$a2, (VM_MAXUSER_ADDRESS-3)
	bgeu	$a0, $a2, fsu_fault_nopcb
	la.pcrel	$a6, _C_LABEL(fsu_fault)		/* Load the fault handler */
	SET_FAULT_HANDLER($a6, $a2)	/* And set it */
	ENTER_USER_ACCESS($a2)
	ld.w	$a0, $a0, 0		/* Try loading the data */
	EXIT_USER_ACCESS($a2)
	SET_FAULT_HANDLER($zero, $a2)	/* Reset the fault handler */
	st.w	$a0, $a1, 0		/* Save the data in kernel space */
	li.d	$a0, 0			/* Success */
	jr	$ra			/* Return */
END(fueword32)

/*
 * long fueword(volatile const void *, int64_t *)
 * int64_t fueword64(volatile const void *, int64_t *)
 */
ENTRY(fueword)
EENTRY(fueword64)
	li.d	$a2, (VM_MAXUSER_ADDRESS-7)
	bgeu	$a0, $a2, fsu_fault_nopcb
	la.pcrel	$a6, _C_LABEL(fsu_fault)		/* Load the fault handler */
	SET_FAULT_HANDLER($a6, $a2)	/* And set it */
	ENTER_USER_ACCESS($a2)
	ld.d	$a0, $a0, 0		/* Try loading the data */
	EXIT_USER_ACCESS($a2)
	SET_FAULT_HANDLER($zero, $a2)	/* Reset the fault handler */
	st.d	$a0, $a1, 0		/* Save the data in kernel space */
	li.d	$a0, 0			/* Success */
	jr	$ra			/* Return */
EEND(fueword64)
END(fueword)

/*
 * int subyte(volatile void *, int)
 */
ENTRY(subyte)
	li.d	$a2, VM_MAXUSER_ADDRESS
	bgeu	$a0, $a2, fsu_fault_nopcb
	la.pcrel	$a6, _C_LABEL(fsu_fault)		/* Load the fault handler */
	SET_FAULT_HANDLER($a6, $a2)	/* And set it */
	ENTER_USER_ACCESS($a2)
	st.b	$a1, $a0, 0		/* Try storing the data */
	EXIT_USER_ACCESS($a2)
	SET_FAULT_HANDLER($zero, $a2)	/* Reset the fault handler */
	li.d	$a0, 0			/* Success */
	jr	$ra			/* Return */
END(subyte)

/*
 * int suword16(volatile void *, int)
 */
ENTRY(suword16)
	li.d	$a2, (VM_MAXUSER_ADDRESS-1)
	bgeu	$a0, $a2, fsu_fault_nopcb
	la.pcrel	$a6, _C_LABEL(fsu_fault)		/* Load the fault handler */
	SET_FAULT_HANDLER($a6, $a2)	/* And set it */
	ENTER_USER_ACCESS($a2)
	st.h	$a1, $a0, 0		/* Try storing the data */
	EXIT_USER_ACCESS($a2)
	SET_FAULT_HANDLER($zero, $a2)	/* Reset the fault handler */
	li.d	$a0, 0			/* Success */
	jr	$ra			/* Return */
END(suword16)

/*
 * int suword32(volatile void *, int)
 */
ENTRY(suword32)
	li.d	$a2, (VM_MAXUSER_ADDRESS-3)
	bgeu	$a0, $a2, fsu_fault_nopcb
	la.pcrel	$a6, _C_LABEL(fsu_fault)		/* Load the fault handler */
	SET_FAULT_HANDLER($a6, $a2)	/* And set it */
	ENTER_USER_ACCESS($a2)
	st.w	$a1, $a0, 0		/* Try storing the data */
	EXIT_USER_ACCESS($a2)
	SET_FAULT_HANDLER($zero, $a2)	/* Reset the fault handler */
	li.d	$a0, 0			/* Success */
	jr	$ra			/* Return */
END(suword32)

/*
 * int suword(volatile void *, long)
 */
ENTRY(suword)
EENTRY(suword64)
	li.d	$a2, (VM_MAXUSER_ADDRESS-7)
	bgeu	$a0, $a2, fsu_fault_nopcb
	la.pcrel	$a6, _C_LABEL(fsu_fault)		/* Load the fault handler */
	SET_FAULT_HANDLER($a6, $a2)	/* And set it */
	ENTER_USER_ACCESS($a2)
	st.d	$a1, $a0, 0		/* Try storing the data */
	EXIT_USER_ACCESS($a2)
	SET_FAULT_HANDLER($zero, $a2)	/* Reset the fault handler */
	li.d	$a0, 0			/* Success */
	jr	$ra			/* Return */
EEND(suword64)
END(suword)

ENTRY(setjmp)
	/* Store the stack pointer */
	st.d	$sp, $a0, 0
	addi.d	$a0, $a0, 8

	/* Store the general purpose registers and ra */
	st.d	$fp, $a0, (0 * 8)
	st.d	$s0, $a0, (1 * 8)
	st.d	$s1, $a0, (2 * 8)
	st.d	$s2, $a0, (3 * 8)
	st.d	$s3, $a0, (4 * 8)
	st.d	$s4, $a0, (5 * 8)
	st.d	$s5, $a0, (6 * 8)
	st.d	$s6, $a0, (7 * 8)
	st.d	$s7, $a0, (8 * 8)
	st.d	$s8, $a0, (9 * 8)
	st.d	$ra, $a0, (10 * 8)

	/* Return value */
	li.d	$a0, 0
	jr	$ra
END(setjmp)

ENTRY(longjmp)
	/* Restore the stack pointer */
	ld.d	$sp, $a0, 0
	addi.d	$a0, $a0, 8

	/* Restore the general purpose registers and ra */
	ld.d	$fp, $a0, (0 * 8)
	ld.d	$s0, $a0, (1 * 8)
	ld.d	$s1, $a0, (2 * 8)
	ld.d	$s2, $a0, (3 * 8)
	ld.d	$s3, $a0, (4 * 8)
	ld.d	$s4, $a0, (5 * 8)
	ld.d	$s5, $a0, (6 * 8)
	ld.d	$s6, $a0, (7 * 8)
	ld.d	$s7, $a0, (8 * 8)
	ld.d	$s8, $a0, (9 * 8)
	ld.d	$ra, $a0, (10 * 8)

	/* Load the return value */
	move	$a0, $a1
	jr	$ra
END(longjmp)
