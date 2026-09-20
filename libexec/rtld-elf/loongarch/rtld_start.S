/*-
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2024 Xiaoqiang Zhao <zhaoxiaoqiang007@gmail.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFCL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * This software was developed by the University of Cambridge Computer
 * Laboratory as part of the CTSRT Project, with support from the UK Higher
 * Education Innovation Fund (HEIF).
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
 * HOWEVER CAUSED ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asm.h>
/*
 * func_ptr_type
 * _rtld(Elf_Addr *sp, func_ptr_type *exit_proc, Obj_Entry **objp)
 */

ENTRY(.rtld_start)
	.cfi_undefined 1	/* Do not attempt to unwind any further. */
	move	$s0, $a0		/* Put ps_strings in a callee-saved register */
	move	$s1, $sp		/* And the stack pointer */

	addi.d	$sp, $sp, -16		/* Make room for obj_main & exit proc */

	move	$a1, $sp		/* exit_proc */
	addi.d	$a2, $a1, 8		/* obj_main */
	la.local $ra, _rtld
	jirl	$ra, $ra, 0		/* Call the loader */
	move	$t0, $a0		/* Backup the entry point */

	ld.d	$a2, $sp, 0		/* Load cleanup */
	ld.d	$a1, $sp, 8		/* Load obj_main */
	move	$a0, $s0		/* Restore ps_strings */
	move	$sp, $s1		/* Restore the stack pointer */
	jr	$t0			/* Jump to the entry point */
END(.rtld_start)

/*
 * t0 = obj pointer
 * t1 = reloc offset
 */
ENTRY(_rtld_bind_start)
	/* Save the arguments, ra, and callee-saved registers */
	addi.d	$sp, $sp, -(8 * 20)
	st.d	$a0, $sp, (8 * 0)
	st.d	$a1, $sp, (8 * 1)
	st.d	$a2, $sp, (8 * 2)
	st.d	$a3, $sp, (8 * 3)
	st.d	$a4, $sp, (8 * 4)
	st.d	$a5, $sp, (8 * 5)
	st.d	$a6, $sp, (8 * 6)
	st.d	$a7, $sp, (8 * 7)
	st.d	$ra, $sp, (8 * 8)
	st.d	$fp, $sp, (8 * 9)
	st.d	$s0, $sp, (8 * 10)
	st.d	$s1, $sp, (8 * 11)
	st.d	$s2, $sp, (8 * 12)
	st.d	$s3, $sp, (8 * 13)
	st.d	$s4, $sp, (8 * 14)
	st.d	$s5, $sp, (8 * 15)
	st.d	$s6, $sp, (8 * 16)
	st.d	$s7, $sp, (8 * 17)
	st.d	$s8, $sp, (8 * 18)

	/* Reloc offset is 3x of the .got.plt offset */
	slli.d	$a1, $t1, 1		/* Mult items by 2 */
	add.d	$a1, $a1, $t1		/* Plus item */

	/* Load obj */
	move	$a0, $t0

	/* Call into rtld */
	la.local $ra, _rtld_bind
	jirl	$ra, $ra, 0

	/* Backup the address to branch to */
	move	$t0, $a0

	/* Restore the arguments, ra, and callee-saved registers */
	ld.d	$a0, $sp, (8 * 0)
	ld.d	$a1, $sp, (8 * 1)
	ld.d	$a2, $sp, (8 * 2)
	ld.d	$a3, $sp, (8 * 3)
	ld.d	$a4, $sp, (8 * 4)
	ld.d	$a5, $sp, (8 * 5)
	ld.d	$a6, $sp, (8 * 6)
	ld.d	$a7, $sp, (8 * 7)
	ld.d	$ra, $sp, (8 * 8)
	ld.d	$fp, $sp, (8 * 9)
	ld.d	$s0, $sp, (8 * 10)
	ld.d	$s1, $sp, (8 * 11)
	ld.d	$s2, $sp, (8 * 12)
	ld.d	$s3, $sp, (8 * 13)
	ld.d	$s4, $sp, (8 * 14)
	ld.d	$s5, $sp, (8 * 15)
	ld.d	$s6, $sp, (8 * 16)
	ld.d	$s7, $sp, (8 * 17)
	ld.d	$s8, $sp, (8 * 18)

	addi.d	$sp, $sp, (8 * 20)

	/* Call into the correct function */
	jr	$t0
END(_rtld_bind_start)
