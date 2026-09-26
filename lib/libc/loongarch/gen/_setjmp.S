/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Xiaoqiang Zhao <zhaoxiaoqiang007@gmail.com>
 *
 */

#include <machine/asm.h>
#include <machine/setjmp.h>

ENTRY(_setjmp)
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

#if !defined(_STANDALONE) && defined(__loongarch_double_float)
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

#endif

	/* Return value */
	li.d	$a0, 0
	jr	$ra
	.align	3
.Lmagic:
	.quad	_JB_MAGIC__SETJMP
END(_setjmp)

ENTRY(_longjmp)
	/* Check the magic value */
	ld.d	$t0, $a0, 0
	la.pcrel	$t1, .Lmagic
	ld.d	$t1, $t1, 0
	bne	$t0, $t1, botch

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

#if !defined(_STANDALONE) && defined(__loongarch_double_float)
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

#endif

	/* Load the return value */
	move	$a0, $a1
	bnez	$a1, 1f
	li.d	$a0, 1
1:
	jr	$ra

botch:
#ifdef _STANDALONE
	b	botch
#else
	bl	_C_LABEL(longjmperror)
	bl	_C_LABEL(abort)
#endif
END(_longjmp)
