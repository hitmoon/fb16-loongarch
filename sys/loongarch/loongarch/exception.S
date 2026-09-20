/*-
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
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
#include "assym.inc"

#include <machine/trap.h>
#include <machine/loongarchreg.h>

.altmacro
.macro save_registers mode

.if \mode == 0	/* We came from userspace. */

	move	$t0, $sp		/* user sp */
	csrrd	$t1, PERCPU_BASE_KS	/* percpu */
	/* Switch to kernel sp. */
	ld.d	$sp, $t1, PC_KERNEL_SP
	addi.d	$sp, $sp, -(TF_SIZE)

	/* Save user sp. */
	st.d	$t0, $sp, TF_SP
	/* Save user r21. */
	st.d	$r21, $sp, TF_R21

	/* Load r21 from KS4. */
	move	$r21, $t1

.else
	move	$t0, $sp		/* already kernel sp */
	addi.d	$sp, $sp, -(TF_SIZE)
	/* Save kernel sp. */
	st.d	$t0, $sp, TF_SP

	/* Load from KS4. */
	csrrd	$r21, PERCPU_BASE_KS
	st.d	$r21, $sp, TF_R21
.endif

	/* Restore t0, t1. */
	csrrd	$t0, EXCEPTION_KS0
	csrrd	$t1, EXCEPTION_KS1

	/* Push GP registers. */
	st.d	$zero, $sp, TF_R0
	st.d	$ra, $sp, TF_RA
	st.d	$tp, $sp, TF_TP
	st.d	$a0, $sp, TF_R4
	st.d	$a1, $sp, TF_R5
	st.d	$a2, $sp, TF_R6
	st.d	$a3, $sp, TF_R7
	st.d	$a4, $sp, TF_R8
	st.d	$a5, $sp, TF_R9
	st.d	$a6, $sp, TF_R10
	st.d	$a7, $sp, TF_R11

	st.d	$t0, $sp, TF_R12
	st.d	$t1, $sp, TF_R13
	st.d	$t2, $sp, TF_R14
	st.d	$t3, $sp, TF_R15
	st.d	$t4, $sp, TF_R16
	st.d	$t5, $sp, TF_R17
	st.d	$t6, $sp, TF_R18
	st.d	$t7, $sp, TF_R19
	st.d	$t8, $sp, TF_R20
	st.d	$fp, $sp, TF_R22

	/* save static regiter */
	st.d	$s0, $sp, TF_R23
	st.d	$s1, $sp, TF_R24
	st.d	$s2, $sp, TF_R25
	st.d	$s3, $sp, TF_R26
	st.d	$s4, $sp, TF_R27
	st.d	$s5, $sp, TF_R28
	st.d	$s6, $sp, TF_R29
	st.d	$s7, $sp, TF_R30
	st.d	$s8, $sp, TF_R31

	/* Push CSR registers. */
	csrrd	$t0, LOONGARCH_CSR_CRMD
	st.d	$t0, $sp, TF_CRMD
	csrrd	$t0, LOONGARCH_CSR_PRMD
	st.d	$t0, $sp, TF_PRMD
	csrrd	$t0, LOONGARCH_CSR_EUEN
	st.d	$t0, $sp, TF_EUEN
	csrrd	$t0, LOONGARCH_CSR_MISC
	st.d	$t0, $sp, TF_MISC
	csrrd	$t0, LOONGARCH_CSR_ECFG
	st.d	$t0, $sp, TF_ECFG
	csrrd	$t0, LOONGARCH_CSR_ESTAT
	st.d	$t0, $sp, TF_ESTAT
	csrrd	$t0, LOONGARCH_CSR_ERA
	st.d	$t0, $sp, TF_ERA
	csrrd	$t0, LOONGARCH_CSR_BADV
	st.d	$t0, $sp, TF_BADVADDR

	/* Push FP registers. */
	csrrd	$t0, LOONGARCH_CSR_EUEN
	andi	$t0, $t0, CSR_EUEN_FPEN
	beqz	$t0, PushRegDone_\@

	fst.d  $fa0, $sp, TF_FR0
	fst.d  $fa1, $sp, TF_FR1
	fst.d  $fa2, $sp, TF_FR2
	fst.d  $fa3, $sp, TF_FR3
	fst.d  $fa4, $sp, TF_FR4
	fst.d  $fa5, $sp, TF_FR5
	fst.d  $fa6, $sp, TF_FR6
	fst.d  $fa7, $sp, TF_FR7
	fst.d  $ft0, $sp, TF_FR8
	fst.d  $ft1, $sp, TF_FR9
	fst.d  $ft2, $sp, TF_FR10
	fst.d  $ft3, $sp, TF_FR11
	fst.d  $ft4, $sp, TF_FR12
	fst.d  $ft5, $sp, TF_FR13
	fst.d  $ft6, $sp, TF_FR14
	fst.d  $ft7, $sp, TF_FR15
	fst.d  $ft8, $sp, TF_FR16
	fst.d  $ft9, $sp, TF_FR17
	fst.d  $ft10, $sp, TF_FR18
	fst.d  $ft11, $sp, TF_FR19
	fst.d  $ft12, $sp, TF_FR20
	fst.d  $ft13, $sp, TF_FR21
	fst.d  $ft14, $sp, TF_FR22
	fst.d  $ft15, $sp, TF_FR23
	fst.d  $fs0, $sp, TF_FR24
	fst.d  $fs1, $sp, TF_FR25
	fst.d  $fs2, $sp, TF_FR26
	fst.d  $fs3, $sp, TF_FR27
	fst.d  $fs4, $sp, TF_FR28
	fst.d  $fs5, $sp, TF_FR29
	fst.d  $fs6, $sp, TF_FR30
	fst.d  $fs7, $sp, TF_FR31

	movfcsr2gr  $t3, $fcsr0
	st.d	$t3, $sp, TF_FCSR0	/* Push the FCSR0 register. */

	/* Push the fcc0-fcc7 registers. */
	movcf2gr    $t3, $fcc0
	or          $t2, $t3, $zero
	movcf2gr    $t3, $fcc1
	bstrins.d   $t2, $t3, 0xf, 0x8
	movcf2gr    $t3, $fcc2
	bstrins.d   $t2, $t3, 0x17, 0x10
	movcf2gr    $t3, $fcc3
	bstrins.d   $t2, $t3, 0x1f, 0x18
	movcf2gr    $t3, $fcc4
	bstrins.d   $t2, $t3, 0x27, 0x20
	movcf2gr    $t3, $fcc5
	bstrins.d   $t2, $t3, 0x2f, 0x28
	movcf2gr    $t3, $fcc6
	bstrins.d   $t2, $t3, 0x37, 0x30
	movcf2gr    $t3, $fcc7
	bstrins.d   $t2, $t3, 0x3f, 0x38
	st.d        $t2, $sp, TF_FCC

	/*
	 * Save vector registers when LSX/LASX is enabled.  Each tf_vregs
	 * slot is 32 bytes; vst writes the low 16 (LSX view), xvst the
	 * full 32 (LASX view).  No-op until the LSXDIS/LASXDIS handlers
	 * enable the units.
	 */
	csrrd	$t0, LOONGARCH_CSR_EUEN
	andi	$t1, $t0, (CSR_EUEN_LSXEN | CSR_EUEN_LASXEN)
	beqz	$t1, PushRegDone_\@
	andi	$t0, $t0, CSR_EUEN_LASXEN
	bnez	$t0, PushVecLasx_\@
	.irp i, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
	vst	$vr\i, $sp, (TF_VREGS + \i * 32)
	.endr
	b	PushRegDone_\@
PushVecLasx_\@:
	.irp i, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
	xvst	$xr\i, $sp, (TF_VREGS + \i * 32)
	.endr
	/* Push exception context down. */
PushRegDone_\@:

.endm

.macro load_registers mode
.if \mode == 1
	/*
	 * Disable interrupts for supervisor mode
	 */
	li.d	$t0, 1
	li.d	$t1, CSR_CRMD_IE
	csrxchg	$t0, $t1, LOONGARCH_CSR_CRMD
.endif

	ld.d	$t1, $sp, TF_EUEN
	csrrd	$t0, LOONGARCH_CSR_EUEN
	li.d	$t2, (CSR_EUEN_FPEN | CSR_EUEN_LSXEN | CSR_EUEN_LASXEN)
	and	$t0, $t0, $t2
	and	$t1, $t1, $t2
	beq	$t0, $t1, PopRegs_\@

	/* Restore saved EUEN to enable/disable FPU as needed */
	ld.d	$t0, $sp, TF_EUEN
	csrwr	$t0, LOONGARCH_CSR_EUEN

PopRegs_\@:

	/* Pop CSR registers: just restore PRMD and ERA. */
	ld.d    $t0, $sp, TF_PRMD
	csrwr   $t0, LOONGARCH_CSR_PRMD
	ld.d    $t0, $sp, TF_ERA
	csrwr   $t0, LOONGARCH_CSR_ERA

	/* If the FPE is not set, only pop the GP registers. */
	beqz	$t1, PopGP_\@

	/* Pop FP registers. */
	fld.d  $fa0, $sp, TF_FR0
	fld.d  $fa1, $sp, TF_FR1
	fld.d  $fa2, $sp, TF_FR2
	fld.d  $fa3, $sp, TF_FR3
	fld.d  $fa4, $sp, TF_FR4
	fld.d  $fa5, $sp, TF_FR5
	fld.d  $fa6, $sp, TF_FR6
	fld.d  $fa7, $sp, TF_FR7
	fld.d  $ft0, $sp, TF_FR8
	fld.d  $ft1, $sp, TF_FR9
	fld.d  $ft2, $sp, TF_FR10
	fld.d  $ft3, $sp, TF_FR11
	fld.d  $ft4, $sp, TF_FR12
	fld.d  $ft5, $sp, TF_FR13
	fld.d  $ft6, $sp, TF_FR14
	fld.d  $ft7, $sp, TF_FR15
	fld.d  $ft8, $sp, TF_FR16
	fld.d  $ft9, $sp, TF_FR17
	fld.d  $ft10, $sp, TF_FR18
	fld.d  $ft11, $sp, TF_FR19
	fld.d  $ft12, $sp, TF_FR20
	fld.d  $ft13, $sp, TF_FR21
	fld.d  $ft14, $sp, TF_FR22
	fld.d  $ft15, $sp, TF_FR23
	fld.d  $fs0, $sp, TF_FR24
	fld.d  $fs1, $sp, TF_FR25
	fld.d  $fs2, $sp, TF_FR26
	fld.d  $fs3, $sp, TF_FR27
	fld.d  $fs4, $sp, TF_FR28
	fld.d  $fs5, $sp, TF_FR29
	fld.d  $fs6, $sp, TF_FR30
	fld.d  $fs7, $sp, TF_FR31

	ld.d        $t0, $sp, TF_FCSR0
	movgr2fcsr  $fcsr0, $t0	/* Pop the fcsr0 register. */

	/* Pop the fcc0-fcc7 registers. */
	ld.d        $t0, $sp, TF_FCC
	bstrpick.d  $t1, $t0, 7, 0
	movgr2cf    $fcc0, $t1
	bstrpick.d  $t1, $t0, 15, 8
	movgr2cf    $fcc1, $t1
	bstrpick.d  $t1, $t0, 23, 16
	movgr2cf    $fcc2, $t1
	bstrpick.d  $t1, $t0, 31, 24
	movgr2cf    $fcc3, $t1
	bstrpick.d  $t1, $t0, 39, 32
	movgr2cf    $fcc4, $t1
	bstrpick.d  $t1, $t0, 47, 40
	movgr2cf    $fcc5, $t1
	bstrpick.d  $t1, $t0, 55, 48
	movgr2cf    $fcc6, $t1
	bstrpick.d  $t1, $t0, 63, 56
	movgr2cf    $fcc7, $t1

	/*
	 * Restore vector registers if LSX/LASX was enabled in the saved
	 * EUEN (written to the CSR above, so vld/xvld are legal here).
	 */
	ld.d	$t1, $sp, TF_EUEN
	andi	$t0, $t1, (CSR_EUEN_LSXEN | CSR_EUEN_LASXEN)
	beqz	$t0, PopGP_\@
	andi	$t0, $t1, CSR_EUEN_LASXEN
	bnez	$t0, PopVecLasx_\@
	.irp i, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
	vld	$vr\i, $sp, (TF_VREGS + \i * 32)
	.endr
	b	PopGP_\@
PopVecLasx_\@:
	.irp i, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
	xvld	$xr\i, $sp, (TF_VREGS + \i * 32)
	.endr
PopGP_\@:
	/* Pop GP registers. */
	ld.d    $ra, $sp, TF_RA
	ld.d    $tp, $sp, TF_TP
	ld.d    $a0, $sp, TF_R4
	ld.d    $a1, $sp, TF_R5
	ld.d    $a2, $sp, TF_R6
	ld.d    $a3, $sp, TF_R7
	ld.d    $a4, $sp, TF_R8
	ld.d    $a5, $sp, TF_R9
	ld.d    $a6, $sp, TF_R10
	ld.d    $a7, $sp, TF_R11
	ld.d    $t0, $sp, TF_R12
	ld.d    $t1, $sp, TF_R13
	ld.d    $t2, $sp, TF_R14
	ld.d    $t3, $sp, TF_R15
	ld.d    $t4, $sp, TF_R16
	ld.d    $t5, $sp, TF_R17
	ld.d    $t6, $sp, TF_R18
	ld.d    $t7, $sp, TF_R19
	ld.d    $t8, $sp, TF_R20
	ld.d    $fp, $sp, TF_R22
	ld.d    $s0, $sp, TF_R23
	ld.d    $s1, $sp, TF_R24
	ld.d    $s2, $sp, TF_R25
	ld.d    $s3, $sp, TF_R26
	ld.d    $s4, $sp, TF_R27
	ld.d    $s5, $sp, TF_R28
	ld.d    $s6, $sp, TF_R29
	ld.d    $s7, $sp, TF_R30
	ld.d    $s8, $sp, TF_R31

.if \mode == 0	/* restore userspace r21. */
	/* save kernel sp */
	addi.d	$t0, $sp, TF_SIZE
	st.d	$t0, $r21, PC_KERNEL_SP
	/* Fixup t0. */
	ld.d	$t0, $sp, TF_R12

	ld.d	$r21, $sp, TF_R21
	/* restore user sp */
	ld.d	$sp, $sp, TF_SP
.else
	csrrd	$r21, PERCPU_BASE_KS
	/* restore kernel sp */
	ld.d	$sp, $sp, TF_SP
.endif
.endm

	.align 12
ENTRY(cpu_exception_handler)
	/* Backup t0, t1. */
	csrwr	$t0, EXCEPTION_KS0
	csrwr	$t1, EXCEPTION_KS1
	/*
	 * Stash the raw ERA in KS2 and KS3 before any code that might
	 * trigger a nested exception (TLB refill, etc.) so that
	 * do_trap_supervisor can compare it against frame->tf_era
	 * to detect stack/TLB corruption.
	 */
	csrrd	$t0, EXCEPTION_KS2
	csrwr	$t0, EXCEPTION_KS3

	csrrd	$t0, LOONGARCH_CSR_ERA
	csrwr	$t0, EXCEPTION_KS2

	csrrd	$t0, LOONGARCH_CSR_PRMD
	andi	$t0, $t0, 0x3
	beqz	$t0, 1f
	b	cpu_exception_handler_user
1:
	b	cpu_exception_handler_supervisor
END(cpu_exception_handler)

ENTRY(cpu_exception_handler_supervisor)
	save_registers 1

	move	$a0, $sp
	bl	_C_LABEL(do_trap_supervisor)

	load_registers 1
	ertn
END(cpu_exception_handler_supervisor)

ENTRY(cpu_exception_handler_user)
	save_registers 0
	move	$a0, $sp
	bl	_C_LABEL(do_trap_user)

	/*
	 * Check for pending ASTs (signals, etc.) before returning to
	 * user.  do_trap_user may have queued signals via
	 * call_trapsignal() which must be delivered via ast() before
	 * resuming user execution.
	 *
	 * Sp still points to the trapframe after the C call (callee-
	 * saved per the calling convention), so use it directly as
	 * ARM64 does.
	 */
1:
	/*
	 * Disable interrupts
	 */
	li.d	$t0, 1
	li.d	$t1, CSR_CRMD_IE
	csrxchg	$t0, $t1, LOONGARCH_CSR_CRMD

	ld.d	$t0, $r21, PC_CURTHREAD
	ld.d	$t0, $t0, TD_AST
	beqz	$t0, 2f			/* no ASTs pending */

	/*
	 * Enable interrupts
	 */
	li.d	$t0, CSR_CRMD_IE
	li.d	$t1, CSR_CRMD_IE
	csrxchg	$t0, $t1, LOONGARCH_CSR_CRMD

	move	$a0, $sp
	bl	_C_LABEL(ast)
	b	1b			/* re-check for new ASTs */
2:
	load_registers 0
	ertn
END(cpu_exception_handler_user)
