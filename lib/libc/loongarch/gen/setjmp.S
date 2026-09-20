/*-
 * Copyright (c) 2015-2016 Ruslan Bukin <br@bsdpad.com>
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

ENTRY(setjmp)
	addi.d	$sp, $sp, -(2 * 8)
	st.d	$a0, $sp, 0
	st.d	$ra, $sp, 8

	/* Store the signal mask */
	addi.d	$a2, $a0, (_JB_SIGMASK * 8)	/* oset */
	li.d	$a1, 0				/* set */
	li.d	$a0, 1				/* SIG_BLOCK */
	bl	_C_LABEL(sigprocmask)

	ld.d	$a0, $sp, 0
	ld.d	$ra, $sp, 8
	addi.d	$sp, $sp, (2 * 8)

	/* Store the magic value and stack pointer */
	la.pcrel	$t0, .Lmagic
	ld.d	$t0, $t0, 0
	st.d	$t0, $a0, (0 * 8)
	st.d	$sp, $a0, (1 * 8)
	addi.d	$a0, $a0, (2 * 8)

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
	addi.d	$a0, $a0, (11 * 8)

#ifdef __loongarch_double_float
	/* Store the fpe registers */
	fst.d	$fs0, $a0, (0 * 8)
	fst.d	$fs1, $a0, (1 * 8)
	fst.d	$fs2, $a0, (2 * 8)
	fst.d	$fs3, $a0, (3 * 8)
	fst.d	$fs4, $a0, (4 * 8)
	fst.d	$fs5, $a0, (5 * 8)
	fst.d	$fs6, $a0, (6 * 8)
	fst.d	$fs7, $a0, (7 * 8)
	addi.d	$a0, $a0, (8 * 8)

	/* Save fcsr0 (FP control/status register) */
	movfcsr2gr	$t0, $fcsr0
	st.d	$t0, $a0, 0
#endif

	/* Return value */
	li.d	$a0, 0
	jr	$ra
	.align	3
.Lmagic:
	.quad	_JB_MAGIC_SETJMP
END(setjmp)

ENTRY(longjmp)
	/* Check the magic value */
	ld.d	$t0, $a0, 0
	la.pcrel	$t1, .Lmagic
	ld.d	$t1, $t1, 0
	bne	$t0, $t1, botch

	addi.d	$sp, $sp, -(4 * 8)
	st.d	$a0, $sp, (0 * 8)
	st.d	$ra, $sp, (1 * 8)
	st.d	$a1, $sp, (2 * 8)

	/* Restore the signal mask */
	li.d	$a2, 0				/* oset */
	addi.d	$a1, $a0, (_JB_SIGMASK * 8)	/* set */
	li.d	$a0, 3				/* SIG_BLOCK */
	bl	_C_LABEL(sigprocmask)

	ld.d	$a1, $sp, (2 * 8)
	ld.d	$ra, $sp, (1 * 8)
	ld.d	$a0, $sp, (0 * 8)
	addi.d	$sp, $sp, (4 * 8)

	/* Restore the stack pointer */
	ld.d	$t0, $a0, 8
	move	$sp, $t0
	addi.d	$a0, $a0, (2 * 8)

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
	addi.d	$a0, $a0, (11 * 8)

#ifdef __loongarch_double_float
	/* Restore the fpe registers */
	fld.d	$fs0, $a0, (0 * 8)
	fld.d	$fs1, $a0, (1 * 8)
	fld.d	$fs2, $a0, (2 * 8)
	fld.d	$fs3, $a0, (3 * 8)
	fld.d	$fs4, $a0, (4 * 8)
	fld.d	$fs5, $a0, (5 * 8)
	fld.d	$fs6, $a0, (6 * 8)
	fld.d	$fs7, $a0, (7 * 8)
	addi.d	$a0, $a0, (8 * 8)

	/* Restore fcsr0 */
	ld.d	$t0, $a0, 0
	movgr2fcsr	$fcsr0, $t0
#endif

	/* Load the return value */
	move	$a0, $a1
	bnez	$a1, 1f
	li.d	$a0, 1
1:
	jr	$ra

botch:
	bl	_C_LABEL(longjmperror)
	bl	_C_LABEL(abort)
END(longjmp)
