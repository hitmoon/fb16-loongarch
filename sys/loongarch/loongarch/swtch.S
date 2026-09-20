/*-
 * Copyright (c) 2015-2017 Ruslan Bukin <br@bsdpad.com>
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

#include "assym.inc"
#include "opt_sched.h"

#include <machine/param.h>
#include <machine/asm.h>
#include <machine/loongarchreg.h>
#include <machine/pte.h>

#define RSIZE 8

.macro	strel_mb
	dbar	0x12
.endm

.macro	ldacq_mb
	dbar	0x14
.endm

.macro EX inst, reg, src, offset
	li.d	$t0, \offset
	\inst \reg, \src, $t0
.endm

.macro __fpe_state_save p

	/* Store registers */
	EX fstx.d, $fa0, \p, (PCB_F + 0 * RSIZE)
	EX fstx.d, $fa1, \p, (PCB_F + 1 * RSIZE)
	EX fstx.d, $fa2, \p, (PCB_F + 2 * RSIZE)
	EX fstx.d, $fa3, \p, (PCB_F + 3 * RSIZE)
	EX fstx.d, $fa4, \p, (PCB_F + 4 * RSIZE)
	EX fstx.d, $fa5, \p, (PCB_F + 5 * RSIZE)
	EX fstx.d, $fa6, \p, (PCB_F + 6 * RSIZE)
	EX fstx.d, $fa7, \p, (PCB_F + 7 * RSIZE)

	EX fstx.d, $ft0, \p, (PCB_F + 8 * RSIZE)
	EX fstx.d, $ft1, \p, (PCB_F + 9 * RSIZE)
	EX fstx.d, $ft2, \p, (PCB_F + 10 * RSIZE)
	EX fstx.d, $ft3, \p, (PCB_F + 11 * RSIZE)
	EX fstx.d, $ft4, \p, (PCB_F + 12 * RSIZE)
	EX fstx.d, $ft5, \p, (PCB_F + 13 * RSIZE)
	EX fstx.d, $ft6, \p, (PCB_F + 14 * RSIZE)
	EX fstx.d, $ft7, \p, (PCB_F + 15 * RSIZE)
	EX fstx.d, $ft8, \p, (PCB_F + 16 * RSIZE)
	EX fstx.d, $ft9, \p, (PCB_F + 17 * RSIZE)
	EX fstx.d, $ft10, \p, (PCB_F + 18 * RSIZE)
	EX fstx.d, $ft11, \p, (PCB_F + 19 * RSIZE)
	EX fstx.d, $ft12, \p, (PCB_F + 20 * RSIZE)
	EX fstx.d, $ft13, \p, (PCB_F + 21 * RSIZE)
	EX fstx.d, $ft14, \p, (PCB_F + 22 * RSIZE)
	EX fstx.d, $ft15, \p, (PCB_F + 23 * RSIZE)

	EX fstx.d, $fs0, \p, (PCB_F + 24 * RSIZE)
	EX fstx.d, $fs1, \p, (PCB_F + 25 * RSIZE)
	EX fstx.d, $fs2, \p, (PCB_F + 26 * RSIZE)
	EX fstx.d, $fs3, \p, (PCB_F + 27 * RSIZE)
	EX fstx.d, $fs4, \p, (PCB_F + 28 * RSIZE)
	EX fstx.d, $fs5, \p, (PCB_F + 29 * RSIZE)
	EX fstx.d, $fs6, \p, (PCB_F + 30 * RSIZE)
	EX fstx.d, $fs7, \p, (PCB_F + 31 * RSIZE)

	movfcsr2gr  $t1, $fcsr0
	li.d	$t0, (PCB_F + 32 * RSIZE)	/* Push the FCSR0 register. */
	stx.d	$t1, \p, $t0

.endm

.macro __fpe_state_load p

	/* Restore registers */
	EX fldx.d, $fa0, \p, (PCB_F + 0 * RSIZE)
	EX fldx.d, $fa1, \p, (PCB_F + 1 * RSIZE)
	EX fldx.d, $fa2, \p, (PCB_F + 2 * RSIZE)
	EX fldx.d, $fa3, \p, (PCB_F + 3 * RSIZE)
	EX fldx.d, $fa4, \p, (PCB_F + 4 * RSIZE)
	EX fldx.d, $fa5, \p, (PCB_F + 5 * RSIZE)
	EX fldx.d, $fa6, \p, (PCB_F + 6 * RSIZE)
	EX fldx.d, $fa7, \p, (PCB_F + 7 * RSIZE)

	EX fldx.d, $ft0, \p, (PCB_F + 8 * RSIZE)
	EX fldx.d, $ft1, \p, (PCB_F + 9 * RSIZE)
	EX fldx.d, $ft2, \p, (PCB_F + 10 * RSIZE)
	EX fldx.d, $ft3, \p, (PCB_F + 11 * RSIZE)
	EX fldx.d, $ft4, \p, (PCB_F + 12 * RSIZE)
	EX fldx.d, $ft5, \p, (PCB_F + 13 * RSIZE)
	EX fldx.d, $ft6, \p, (PCB_F + 14 * RSIZE)
	EX fldx.d, $ft7, \p, (PCB_F + 15 * RSIZE)
	EX fldx.d, $ft8, \p, (PCB_F + 16 * RSIZE)
	EX fldx.d, $ft9, \p, (PCB_F + 17 * RSIZE)
	EX fldx.d, $ft10, \p, (PCB_F + 18 * RSIZE)
	EX fldx.d, $ft11, \p, (PCB_F + 19 * RSIZE)
	EX fldx.d, $ft12, \p, (PCB_F + 20 * RSIZE)
	EX fldx.d, $ft13, \p, (PCB_F + 21 * RSIZE)
	EX fldx.d, $ft14, \p, (PCB_F + 22 * RSIZE)
	EX fldx.d, $ft15, \p, (PCB_F + 23 * RSIZE)

	EX fldx.d, $fs0, \p, (PCB_F + 24 * RSIZE)
	EX fldx.d, $fs1, \p, (PCB_F + 25 * RSIZE)
	EX fldx.d, $fs2, \p, (PCB_F + 26 * RSIZE)
	EX fldx.d, $fs3, \p, (PCB_F + 27 * RSIZE)
	EX fldx.d, $fs4, \p, (PCB_F + 28 * RSIZE)
	EX fldx.d, $fs5, \p, (PCB_F + 29 * RSIZE)
	EX fldx.d, $fs6, \p, (PCB_F + 30 * RSIZE)
	EX fldx.d, $fs7, \p, (PCB_F + 31 * RSIZE)

	li.d $t0, (PCB_F + 32 * RSIZE)
	ldx.d $t0, \p, $t0
	movgr2fcsr  $fcsr0, $t0	/* Pop the fcsr0 register. */

.endm

/*
 * void
 * fpe_state_save(struct thread *td)
 */
ENTRY(fpe_state_save)
	/*
	 * Only save FPU state if FPU is enabled.  fpe_state_save is
	 * called from fill_fpregs (ptrace) and get_fpcontext (signal
	 * delivery).  After a context switch the hardware FPEN may have
	 * been disabled by another thread even though PCB_FP_STARTED is
	 * set for this thread.  Accessing FPU registers with FPU disabled
	 * raises exception 0xf, which is fatal in kernel mode.
	 */
	csrrd	$t0, LOONGARCH_CSR_EUEN
	andi	$t0, $t0, CSR_EUEN_FPEN
	beqz	$t0, 1f
	ld.d	$a0, $a0, TD_PCB
	__fpe_state_save $a0
1:
	jr	$ra
END(fpe_state_save)

/*
 * void
 * fpe_state_clear(void)
 */
ENTRY(fpe_state_clear)
	/*
	 * Only clear FPU registers if FPU is enabled.  fpe_state_clear
	 * may be called from the FPDIS trap handler before FPU is enabled
	 * for the current thread.  Accessing FPU registers with FPU
	 * disabled raises exception 0xf.
	 */
	csrrd	$t1, LOONGARCH_CSR_EUEN
	andi	$t1, $t1, CSR_EUEN_FPEN
	beqz	$t1, 1f

	li.w	$t0, 0
	movgr2fr.d $fa0, $t0
	movgr2fr.d $fa1, $t0
	movgr2fr.d $fa2, $t0
	movgr2fr.d $fa3, $t0
	movgr2fr.d $fa4, $t0
	movgr2fr.d $fa5, $t0
	movgr2fr.d $fa6, $t0
	movgr2fr.d $fa7, $t0

	movgr2fr.d $ft0, $t0
	movgr2fr.d $ft1, $t0
	movgr2fr.d $ft2, $t0
	movgr2fr.d $ft3, $t0
	movgr2fr.d $ft4, $t0
	movgr2fr.d $ft5, $t0
	movgr2fr.d $ft6, $t0
	movgr2fr.d $ft7, $t0
	movgr2fr.d $ft8, $t0
	movgr2fr.d $ft9, $t0
	movgr2fr.d $ft10, $t0
	movgr2fr.d $ft11, $t0
	movgr2fr.d $ft12, $t0
	movgr2fr.d $ft13, $t0
	movgr2fr.d $ft14, $t0
	movgr2fr.d $ft15, $t0

	movgr2fr.d $fs0, $t0
	movgr2fr.d $fs1, $t0
	movgr2fr.d $fs2, $t0
	movgr2fr.d $fs3, $t0
	movgr2fr.d $fs4, $t0
	movgr2fr.d $fs5, $t0
	movgr2fr.d $fs6, $t0
	movgr2fr.d $fs7, $t0

1:
	jr	$ra
END(fpe_state_clear)

/*
 * void cpu_throw(struct thread *old __unused, struct thread *new)
 */
ENTRY(cpu_throw)
	/* Activate the new thread's pmap. */
	move	$s0, $a1
	move	$a0, $a1
	bl	_C_LABEL(pmap_activate_sw)
	move	$a0, $s0

	/* Store the new curthread */
	st.d	$a0, $r21, PC_CURTHREAD
	/* And the new pcb */
	ld.d	$a3, $a0, TD_PCB
	st.d	$a3, $r21, PC_CURPCB

	/* s[0-8] */
	ld.d	$s0, $a3, (PCB_S + 0 * 8)
	ld.d	$s1, $a3, (PCB_S + 1 * 8)
	ld.d	$s2, $a3, (PCB_S + 2 * 8)
	ld.d	$s3, $a3, (PCB_S + 3 * 8)
	ld.d	$s4, $a3, (PCB_S + 4 * 8)
	ld.d	$s5, $a3, (PCB_S + 5 * 8)
	ld.d	$s6, $a3, (PCB_S + 6 * 8)
	ld.d	$s7, $a3, (PCB_S + 7 * 8)
	ld.d	$s8, $a3, (PCB_S + 8 * 8)

	/* Load registers */
	ld.d	$ra, $a3, PCB_RA
	ld.d	$fp, $a3, PCB_FP
	ld.d	$tp, $a3, PCB_TP
	ld.d	$sp, $a3, PCB_SP

	jr	$ra
END(cpu_throw)

/*
 * void cpu_switch(struct thread *old, struct thread *new, struct mtx *mtx)
 *
 * a0 = old
 * a1 = new
 * a2 = mtx
 */
ENTRY(cpu_switch)
	/* Save the old context. */
	ld.d	$a3, $a0, TD_PCB

	/* Store ra, sp, fp and the callee-saved registers */
	st.d	$ra, $a3, PCB_RA
	st.d	$sp, $a3, PCB_SP
        st.d    $tp, $a3, PCB_TP
	st.d	$fp, $a3, PCB_FP

	/* s[0-8] */
	st.d	$s0, $a3, (PCB_S + 0 * 8)
	st.d	$s1, $a3, (PCB_S + 1 * 8)
	st.d	$s2, $a3, (PCB_S + 2 * 8)
	st.d	$s3, $a3, (PCB_S + 3 * 8)
	st.d	$s4, $a3, (PCB_S + 4 * 8)
	st.d	$s5, $a3, (PCB_S + 5 * 8)
	st.d	$s6, $a3, (PCB_S + 6 * 8)
	st.d	$s7, $a3, (PCB_S + 7 * 8)
	st.d	$s8, $a3, (PCB_S + 8 * 8)

	/* Activate the new thread's pmap */
	move	$s0, $a0
	move	$s1, $a1
	move	$s2, $a2
	move	$a0, $a1
	bl	_C_LABEL(pmap_activate_sw)
	move	$a1, $s1

	/* Release the old thread */
	strel_mb
	st.d	$s2, $s0, TD_LOCK
#if defined(SCHED_ULE) && defined(SMP)
	/* Spin if TD_LOCK points to a blocked_lock */
	la.pcrel	$s2, _C_LABEL(blocked_lock)
1:
	ld.d	$t0, $a1, TD_LOCK
	ldacq_mb
	beq	$t0, $s2, 1b
#endif

	/* Store the new curthread */
	st.d	$a1, $r21, PC_CURTHREAD
	/* And the new pcb */
	ld.d	$a3, $a1, TD_PCB
	st.d	$a3, $r21, PC_CURPCB

	/*
	 * Restore the saved context.
	 */

	/* s[0-8] */
	ld.d	$s0, $a3, (PCB_S + 0 * 8)
	ld.d	$s1, $a3, (PCB_S + 1 * 8)
	ld.d	$s2, $a3, (PCB_S + 2 * 8)
	ld.d	$s3, $a3, (PCB_S + 3 * 8)
	ld.d	$s4, $a3, (PCB_S + 4 * 8)
	ld.d	$s5, $a3, (PCB_S + 5 * 8)
	ld.d	$s6, $a3, (PCB_S + 6 * 8)
	ld.d	$s7, $a3, (PCB_S + 7 * 8)
	ld.d	$s8, $a3, (PCB_S + 8 * 8)

	/* Restore the registers */
	ld.d	$ra, $a3, PCB_RA
	ld.d	$fp, $a3, PCB_FP
	ld.d	$tp, $a3, PCB_TP
	ld.d	$sp, $a3, PCB_SP
	jr	$ra
END(cpu_switch)

/*
 * fork_exit(void (*callout)(void *, struct trapframe *), void *arg,
 *  struct trapframe *frame)
 */

ENTRY(fork_trampoline)

	move	$a0, $s0
	move	$a1, $s1
	move	$a2, $sp
	bl	_C_LABEL(fork_exit)

	/*
	 * Disable interrupts IMMEDIATELY after fork_exit returns, before any
	 * register restore.
	 */
	li.d	$t0, 1
	li.d	$t1, CSR_CRMD_IE
	csrxchg	$t0, $t1, LOONGARCH_CSR_CRMD

	/* Restore exception program counter */
	ld.d	$t0, $sp, TF_ERA
	csrwr	$t0, LOONGARCH_CSR_ERA

	/* Restore PRMD */
	ld.d	$t0, $sp, TF_PRMD
	csrwr	$t0, LOONGARCH_CSR_PRMD

	/* Restore EUEN if FPEN changed */
	ld.d	$t1, $sp, TF_EUEN
	csrrd	$t0, LOONGARCH_CSR_EUEN
	andi	$t0, $t0, CSR_EUEN_FPEN
	andi	$t1, $t1, CSR_EUEN_FPEN
	beq	$t0, $t1, 1f
	ld.d	$t0, $sp, TF_EUEN
	csrwr	$t0, LOONGARCH_CSR_EUEN
1:

	/* Restore the registers */
	/* t0 and t1 will be used later */
	ld.d	$t2, $sp, (TF_T + 2 * RSIZE)
	ld.d	$t3, $sp, (TF_T + 3 * RSIZE)
	ld.d	$t4, $sp, (TF_T + 4 * RSIZE)
	ld.d	$t5, $sp, (TF_T + 5 * RSIZE)
	ld.d	$t6, $sp, (TF_T + 6 * RSIZE)
	ld.d	$t7, $sp, (TF_T + 7 * RSIZE)
	ld.d	$t8, $sp, (TF_T + 8 * RSIZE)

	ld.d	$s0, $sp, (TF_S + 0 * RSIZE)
	ld.d	$s1, $sp, (TF_S + 1 * RSIZE)
	ld.d	$s2, $sp, (TF_S + 2 * RSIZE)
	ld.d	$s3, $sp, (TF_S + 3 * RSIZE)
	ld.d	$s4, $sp, (TF_S + 4 * RSIZE)
	ld.d	$s5, $sp, (TF_S + 5 * RSIZE)
	ld.d	$s6, $sp, (TF_S + 6 * RSIZE)
	ld.d	$s7, $sp, (TF_S + 7 * RSIZE)
	ld.d	$s8, $sp, (TF_S + 8 * RSIZE)

	ld.d	$a0, $sp, (TF_A + 0 * RSIZE)
	ld.d	$a1, $sp, (TF_A + 1 * RSIZE)
	ld.d	$a2, $sp, (TF_A + 2 * RSIZE)
	ld.d	$a3, $sp, (TF_A + 3 * RSIZE)
	ld.d	$a4, $sp, (TF_A + 4 * RSIZE)
	ld.d	$a5, $sp, (TF_A + 5 * RSIZE)
	ld.d	$a6, $sp, (TF_A + 6 * RSIZE)
	ld.d	$a7, $sp, (TF_A + 7 * RSIZE)

	addi.d	$t0, $sp, TF_SIZE
	/* Save kernel stack so we can use it doing a user trap */
	st.d	$t0, $r21, PC_KERNEL_SP

	/* Fixup t0, t1. */
	ld.d	$t0, $sp, (TF_T + 0 * RSIZE)
	ld.d	$t1, $sp, (TF_T + 1 * RSIZE)

	/* Load user ra */
	ld.d	$ra, $sp, TF_RA

	/* Restore user tp, fp, r21 from trapframe */
	ld.d	$tp, $sp, TF_TP
	ld.d	$fp, $sp, TF_R22
	ld.d	$r21, $sp, TF_R21

	/* Load user stack */
	ld.d	$sp, $sp, TF_SP

	ertn
END(fork_trampoline)

ENTRY(savectx)
	/* Store ra, sp tp, fp and the callee-saved registers */
	st.d	$ra, $a0, (PCB_RA)
	st.d	$sp, $a0, (PCB_SP)
	st.d	$tp, $a0, (PCB_TP)
	st.d	$fp, $a0, (PCB_FP)

	/* s[0-8] */
	st.d	$s0, $a0, (PCB_S + 0 * RSIZE)
	st.d	$s1, $a0, (PCB_S + 1 * RSIZE)
	st.d	$s2, $a0, (PCB_S + 2 * RSIZE)
	st.d	$s3, $a0, (PCB_S + 3 * RSIZE)
	st.d	$s4, $a0, (PCB_S + 4 * RSIZE)
	st.d	$s5, $a0, (PCB_S + 5 * RSIZE)
	st.d	$s6, $a0, (PCB_S + 6 * RSIZE)
	st.d	$s7, $a0, (PCB_S + 7 * RSIZE)
	st.d	$s8, $a0, (PCB_S + 8 * RSIZE)

	/*
	 * Only save FPU state if FPU is enabled.  savectx is called from
	 * IPI_STOP and dump_savectx() which may fire while FPU is disabled
	 * (e.g. on an AP that hasn't entered a FPU-using thread yet).
	 * Accessing FPU registers with FPU disabled raises exception 0xf,
	 * which is fatal in an interrupt context.
	 */
	ld.d	$t0, $r21, PC_CURTHREAD
	ld.d	$t0, $t0, TD_FRAME
	ld.d	$t1, $t0, TF_EUEN
	andi	$t2, $t1, CSR_EUEN_FPEN
	beqz	$t2, 1f

	__fpe_state_save $a0
1:
	jr	$ra
END(savectx)
