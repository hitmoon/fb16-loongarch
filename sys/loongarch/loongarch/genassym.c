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

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/assym.h>
#include <sys/proc.h>
#include <sys/mbuf.h>
#include <sys/vmmeter.h>
#include <sys/bus.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <machine/loongarchreg.h>
#include <machine/frame.h>
#include <machine/pcb.h>
#include <machine/cpu.h>
#include <machine/proc.h>
#include <machine/cpufunc.h>
#include <machine/pte.h>
#include <machine/intr.h>
#include <machine/machdep.h>
#include <machine/vmparam.h>

ASSYM(KERNBASE, KERNBASE);
ASSYM(VM_MAXUSER_ADDRESS, VM_MAXUSER_ADDRESS);
ASSYM(VM_MAX_KERNEL_ADDRESS, VM_MAX_KERNEL_ADDRESS);
ASSYM(VM_EARLY_DTB_ADDRESS, VM_EARLY_DTB_ADDRESS);
ASSYM(PMAP_MAPDEV_EARLY_SIZE, PMAP_MAPDEV_EARLY_SIZE);

ASSYM(PCB_ONFAULT, offsetof(struct pcb, pcb_onfault));
ASSYM(PCB_SIZE, sizeof(struct pcb));
ASSYM(PCB_A0, offsetof(struct pcb, pcb_a0));
ASSYM(PCB_S, offsetof(struct pcb, pcb_regs[23]));
ASSYM(PCB_RA, offsetof(struct pcb, pcb_regs[1]));
ASSYM(PCB_SP, offsetof(struct pcb, pcb_regs[3]));
ASSYM(PCB_TP, offsetof(struct pcb, pcb_regs[2]));
ASSYM(PCB_FP, offsetof(struct pcb, pcb_regs[22]));
ASSYM(PCB_F, offsetof(struct pcb, pcb_fregs));

ASSYM(SF_UC, offsetof(struct sigframe, sf_uc));

ASSYM(PC_CURPCB, offsetof(struct pcpu, pc_curpcb));
ASSYM(PC_CURTHREAD, offsetof(struct pcpu, pc_curthread));
ASSYM(PC_KERNEL_SP, offsetof(struct pcpu, pc_kernel_sp));

ASSYM(TD_PCB, offsetof(struct thread, td_pcb));
ASSYM(TD_FLAGS, offsetof(struct thread, td_flags));
ASSYM(TD_AST, offsetof(struct thread, td_ast));
ASSYM(TD_PROC, offsetof(struct thread, td_proc));
ASSYM(TD_FRAME, offsetof(struct thread, td_frame));
ASSYM(TD_MD, offsetof(struct thread, td_md));
ASSYM(TD_LOCK, offsetof(struct thread, td_lock));

ASSYM(TF_SIZE, roundup2(sizeof(struct trapframe), STACKALIGNBYTES + 1));
ASSYM(TF_REGS, offsetof(struct trapframe, tf_regs));
ASSYM(TF_R0, offsetof(struct trapframe, tf_regs[0]));
ASSYM(TF_R1, offsetof(struct trapframe, tf_regs[1]));
ASSYM(TF_R2, offsetof(struct trapframe, tf_regs[2]));
ASSYM(TF_R3, offsetof(struct trapframe, tf_regs[3]));
ASSYM(TF_R4, offsetof(struct trapframe, tf_regs[4]));
ASSYM(TF_R5, offsetof(struct trapframe, tf_regs[5]));
ASSYM(TF_R6, offsetof(struct trapframe, tf_regs[6]));
ASSYM(TF_R7, offsetof(struct trapframe, tf_regs[7]));
ASSYM(TF_R8, offsetof(struct trapframe, tf_regs[8]));
ASSYM(TF_R9, offsetof(struct trapframe, tf_regs[9]));
ASSYM(TF_R10, offsetof(struct trapframe, tf_regs[10]));
ASSYM(TF_R11, offsetof(struct trapframe, tf_regs[11]));
ASSYM(TF_R12, offsetof(struct trapframe, tf_regs[12]));
ASSYM(TF_R13, offsetof(struct trapframe, tf_regs[13]));
ASSYM(TF_R14, offsetof(struct trapframe, tf_regs[14]));
ASSYM(TF_R15, offsetof(struct trapframe, tf_regs[15]));
ASSYM(TF_R16, offsetof(struct trapframe, tf_regs[16]));
ASSYM(TF_R17, offsetof(struct trapframe, tf_regs[17]));
ASSYM(TF_R18, offsetof(struct trapframe, tf_regs[18]));
ASSYM(TF_R19, offsetof(struct trapframe, tf_regs[19]));
ASSYM(TF_R20, offsetof(struct trapframe, tf_regs[20]));
ASSYM(TF_R21, offsetof(struct trapframe, tf_regs[21]));
ASSYM(TF_R22, offsetof(struct trapframe, tf_regs[22]));
ASSYM(TF_R23, offsetof(struct trapframe, tf_regs[23]));
ASSYM(TF_R24, offsetof(struct trapframe, tf_regs[24]));
ASSYM(TF_R25, offsetof(struct trapframe, tf_regs[25]));
ASSYM(TF_R26, offsetof(struct trapframe, tf_regs[26]));
ASSYM(TF_R27, offsetof(struct trapframe, tf_regs[27]));
ASSYM(TF_R28, offsetof(struct trapframe, tf_regs[28]));
ASSYM(TF_R29, offsetof(struct trapframe, tf_regs[29]));
ASSYM(TF_R30, offsetof(struct trapframe, tf_regs[30]));
ASSYM(TF_R31, offsetof(struct trapframe, tf_regs[31]));

ASSYM(TF_A, offsetof(struct trapframe, tf_regs[4]));
ASSYM(TF_T, offsetof(struct trapframe, tf_regs[12]));
ASSYM(TF_S, offsetof(struct trapframe, tf_regs[23]));
ASSYM(TF_RA, offsetof(struct trapframe, tf_regs[1]));
ASSYM(TF_TP, offsetof(struct trapframe, tf_regs[2]));
ASSYM(TF_SP, offsetof(struct trapframe, tf_regs[3]));

ASSYM(TF_A0, offsetof(struct trapframe, tf_a0));
ASSYM(TF_ERA, offsetof(struct trapframe, tf_era));
ASSYM(TF_BADVADDR, offsetof(struct trapframe, tf_badvaddr));
ASSYM(TF_CRMD, offsetof(struct trapframe, tf_crmd));
ASSYM(TF_PRMD, offsetof(struct trapframe, tf_prmd));
ASSYM(TF_EUEN, offsetof(struct trapframe, tf_euen));
ASSYM(TF_MISC, offsetof(struct trapframe, tf_misc));
ASSYM(TF_ECFG, offsetof(struct trapframe, tf_ecfg));
ASSYM(TF_ESTAT, offsetof(struct trapframe, tf_estat));

ASSYM(TF_FR0, offsetof(struct trapframe, tf_fregs[0]));
ASSYM(TF_FR1, offsetof(struct trapframe, tf_fregs[1]));
ASSYM(TF_FR2, offsetof(struct trapframe, tf_fregs[2]));
ASSYM(TF_FR3, offsetof(struct trapframe, tf_fregs[3]));
ASSYM(TF_FR4, offsetof(struct trapframe, tf_fregs[4]));
ASSYM(TF_FR5, offsetof(struct trapframe, tf_fregs[5]));
ASSYM(TF_FR6, offsetof(struct trapframe, tf_fregs[6]));
ASSYM(TF_FR7, offsetof(struct trapframe, tf_fregs[7]));
ASSYM(TF_FR8, offsetof(struct trapframe, tf_fregs[8]));
ASSYM(TF_FR9, offsetof(struct trapframe, tf_fregs[9]));
ASSYM(TF_FR10, offsetof(struct trapframe, tf_fregs[10]));
ASSYM(TF_FR11, offsetof(struct trapframe, tf_fregs[11]));
ASSYM(TF_FR12, offsetof(struct trapframe, tf_fregs[12]));
ASSYM(TF_FR13, offsetof(struct trapframe, tf_fregs[13]));
ASSYM(TF_FR14, offsetof(struct trapframe, tf_fregs[14]));
ASSYM(TF_FR15, offsetof(struct trapframe, tf_fregs[15]));
ASSYM(TF_FR16, offsetof(struct trapframe, tf_fregs[16]));
ASSYM(TF_FR17, offsetof(struct trapframe, tf_fregs[17]));
ASSYM(TF_FR18, offsetof(struct trapframe, tf_fregs[18]));
ASSYM(TF_FR19, offsetof(struct trapframe, tf_fregs[19]));
ASSYM(TF_FR20, offsetof(struct trapframe, tf_fregs[20]));
ASSYM(TF_FR21, offsetof(struct trapframe, tf_fregs[21]));
ASSYM(TF_FR22, offsetof(struct trapframe, tf_fregs[22]));
ASSYM(TF_FR23, offsetof(struct trapframe, tf_fregs[23]));
ASSYM(TF_FR24, offsetof(struct trapframe, tf_fregs[24]));
ASSYM(TF_FR25, offsetof(struct trapframe, tf_fregs[25]));
ASSYM(TF_FR26, offsetof(struct trapframe, tf_fregs[26]));
ASSYM(TF_FR27, offsetof(struct trapframe, tf_fregs[27]));
ASSYM(TF_FR28, offsetof(struct trapframe, tf_fregs[28]));
ASSYM(TF_FR29, offsetof(struct trapframe, tf_fregs[29]));
ASSYM(TF_FR30, offsetof(struct trapframe, tf_fregs[30]));
ASSYM(TF_FR31, offsetof(struct trapframe, tf_fregs[31]));
ASSYM(TF_FCSR0, offsetof(struct trapframe, uf.fcsr0));
ASSYM(TF_FCC, offsetof(struct trapframe, uf.fcc));
ASSYM(TF_VREGS, offsetof(struct trapframe, tf_vregs));

ASSYM(LOONGARCH_BOOTPARAMS_SIZE, sizeof(struct loongarch_bootparams));
ASSYM(LOONGARCH_BOOTPARAMS_KERN_PGD,
    offsetof(struct loongarch_bootparams, kern_pgd));
ASSYM(LOONGARCH_BOOTPARAMS_KERN_PHYS,
    offsetof(struct loongarch_bootparams, kern_phys));
ASSYM(LOONGARCH_BOOTPARAMS_KERN_STACK, offsetof(struct loongarch_bootparams,
    kern_stack));
ASSYM(LOONGARCH_BOOTPARAMS_DTBP_VIRT,
    offsetof(struct loongarch_bootparams, dtbp_virt));
ASSYM(LOONGARCH_BOOTPARAMS_DTBP_PHYS,
    offsetof(struct loongarch_bootparams, dtbp_phys));
ASSYM(LOONGARCH_BOOTPARAMS_MODULEP,
    offsetof(struct loongarch_bootparams, modulep));
