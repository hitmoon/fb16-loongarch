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

#include "assym.inc"

#include <sys/syscall.h>
#include <machine/asm.h>
#include <machine/param.h>
#include <machine/intr.h>
#include <machine/loongarchreg.h>
#include <machine/pte.h>

	.globl	kernbase
	.set	kernbase, KERNBASE

	.text


.macro setup_page_table bsp
	/* setup STLB page size */
	li.w		$t0, PAGE_SHIFT
	csrwr		$t0, LOONGARCH_CSR_STLBPGSIZE

	/* set PWCL */
	li.d		$t0, PWCL
	csrwr		$t0, LOONGARCH_CSR_PWCTL0

	/* set PWCH */
	li.d		$t0, PWCH
	csrwr		$t0, LOONGARCH_CSR_PWCTL1

	/* bsp or ap */
	li.d		$t4, \bsp

	/* setup PGD */
	la.pcrel	$t0, pagetable_l0
	beqz		$t4, $2f
	la.pcrel	$t1, pagetable_l1
	li.d		$t2, KERNBASE
	srli.d		$t2, $t2, L0_SHIFT
	andi		$t2, $t2, L0_ADDR_MASK
	li.d		$t3, PTE_SIZE
	mul.d		$t3, $t2, $t3		/* l0 slot */
	stx.d		$t1, $t0, $t3		/* l1 entry */
2:
	csrwr		$t0, LOONGARCH_CSR_PGDH		/* high half */

	beqz		$t4, $3f
	la.pcrel	$t0, pagetable_id_l0
	la.pcrel	$t1, pagetable_id_l1
	csrrd		$t2, LOONGARCH_CSR_KS0
	srli.d		$t2, $t2, L0_SHIFT
	andi		$t2, $t2, L0_ADDR_MASK
	li.d		$t3, PTE_SIZE
	mul.d		$t3, $t2, $t3		/* l0 slot */
	stx.d		$t1, $t0, $t3		/* l1 entry */
	csrwr		$t0, LOONGARCH_CSR_PGDL		/* low half identity map*/
3:
.endm

/*
 * Main entry point. This routine is marked as the ELF entry, and is where
 * loader(8) will enter the kernel. Arguments are as follows:
 *  - a0 = modulep
 *  - a1 = kernel load phys address
 *  - a2 = kernel end
 *
 * It is expected that only a single CPU will enter here.
 */
	.globl _start
_start:
	csrwr		$a1, LOONGARCH_CSR_KS0
	/*
	 * Zero a1 to indicate that we have no DTB pointer. It is already
	 * included in the loader(8) metadata.
	 */
	move		$a1, $zero

	/* get kernel size */
	li.d		$t7, KERNBASE
	sub.d		$t7, $a2, $t7
	li.d		$t6, (L2_SIZE - 1)  /* L2 Align */
	add.d		$t7, $t7, $t6
	andn		$t7, $t7, $t6
	li.d		$t6, LOCORE_EXTRA_MAP_LEN
	add.d		$t7, $t7, $t6

	/*
	 *  a0 - modulep or zero
	 *  a1 - zero or dtbp
	 */

	/* disable PG */
	li.w            $t0, 0xa8               # PLV=0, IE=0, PG=0, DA=1
	csrwr		$t0, LOONGARCH_CSR_CRMD

pagetables:
	/*
         * map region starting at KERNBASE using L2 hugepage
	 */
	li.d		$t5, PMD_KERN_HUGE
	csrwr		$t5, LOONGARCH_CSR_KS1

	la.pcrel	$t0, pagetable_l1
	la.pcrel	$t1, pagetable_l2
	li.d		$t2, KERNBASE
	csrrd		$t3, LOONGARCH_CSR_KS0
	move		$t4, $t7
	bl		create_pagetables_to_l2

	/*
         * map region starting at 0 using L2 hugepage, Lower half
	 */
	la.pcrel	$t0, pagetable_id_l1
	la.pcrel	$t1, pagetable_id_l2
	csrrd		$t2, LOONGARCH_CSR_KS0	/* virtual load address, id mmap */
	csrrd		$t3, LOONGARCH_CSR_KS0  /* phys load address */
	move		$t4, $t7
	bl		create_pagetables_to_l2

	/*
	 * Map the early devmap window so the console UART is reachable in
	 * initloongarch() before cninit()/pmap_bootstrap.  create_pagetables_to_l2
	 * links pagetable_l2_devmap into the kernel pagetable_l1 at the devmap
	 * VA's L1 slot -- the devmap VA shares the kernel's L0 slot, so it must
	 * use the kernel L1 (not a separate one) -- and fills it with 2M huge
	 * pages with SUC (uncached) attributes mapping the Loongson config/UART
	 * region at 0x1fe00000.
	 */

	li.d		$t5, PMD_KERN_SUC_HUGE
	csrwr		$t5, LOONGARCH_CSR_KS1

	la.pcrel	$t0, pagetable_l1		/* kernel L1 (devmap shares its L0 slot) */
	la.pcrel	$t1, pagetable_l2_devmap	/* L2 table to fill */
	li.d		$t2, (VM_MAX_KERNEL_ADDRESS - PMAP_MAPDEV_EARLY_SIZE) /* VA */
	li.d		$t3, 0x1fe00000		/* PA: Loongson UART config base */
	li.d		$t4, PMAP_MAPDEV_EARLY_SIZE /* size: 16M (8 x 2M pages) */
	bl		create_pagetables_to_l2

	/* setup pagetable settings */
	setup_page_table 1

	/* setup tlbrentry */
	la.pcrel	$t0, handle_tlbrefill
	csrwr		$t0, LOONGARCH_CSR_TLBRENTRY

	/* setup exception */
	csrrd		$t0, LOONGARCH_CSR_ECFG
	li.d		$t1, 0x70000
	andn		$t0, $t0, $t1
	/* enable timer and ipi */
	li.d		$t1, 0x1
	slli.d		$t1, $t1, INT_TI
	li.d		$t2, 0x1
	slli.d		$t2, $t2, INT_IPI
	or		$t1, $t1, $t2
	or		$t0, $t0, $t1
	csrwr		$t0, LOONGARCH_CSR_ECFG

	/* flush tlb (global) */
	invtlb		0, $zero, $zero

	/* record virtdone virt addr*/
	la.pcrel	$t1, Lvirtdone
	ld.d		$t5, $t1, 0

	/* record kernel pgd */
	la.pcrel	$s2, pagetable_l0

	/* Enable PG */
        li.w            $t0, 0xb0                # PLV=0, IE=0, PG=1
        csrwr           $t0, LOONGARCH_CSR_CRMD

	/* jump to virtual address space */
	jirl		$zero, $t5, 0

virtdone:

	li.d            $t0, 0x8000000000000001 /* CSR_DMW0_INIT: DMW0 VSEG=0x8000, PLV0 */
	csrwr           $t0, LOONGARCH_CSR_DMWIN0

	la.pcrel	$t0, Lbss
        ld.d		$t0, $t0, 0         # clear .bss
        st.d            $zero, $t0, 0
	la.pcrel	$t1, Lend
	ld.d		$t1, $t1, 0
1:
        addi.d          $t0, $t0, 8
        st.d            $zero, $t0, 0
        bne             $t0, $t1, 1b

        /* KSave3 used for percpu base, initialized as 0 */
        csrwr           $zero, PERCPU_BASE_KS
        /* GPR21 used for percpu base (runtime), initialized as 0 */
        move            $r21, $zero

	la.pcrel	$tp, initstack

	/* Initialize stack pointer */
	la.pcrel	$sp, initstack_end

	/* Allocate space for thread0 PCB and bootparams*/
	li.d		$t0, (-(PCB_SIZE + LOONGARCH_BOOTPARAMS_SIZE) & ~STACKALIGNBYTES)
	add.d		$sp, $sp, $t0

	/* fill bootparams */
	la.pcrel	$t0, initstack
	st.d		$t0, $sp, LOONGARCH_BOOTPARAMS_KERN_STACK
	csrrd		$s1, LOONGARCH_CSR_KS0
	st.d		$s1, $sp, LOONGARCH_BOOTPARAMS_KERN_PHYS
	st.d		$s2, $sp, LOONGARCH_BOOTPARAMS_KERN_PGD
	st.d		$a0, $sp, LOONGARCH_BOOTPARAMS_MODULEP

	/* disable id map */
	li.d		$t0, PAGE_SIZE
	sub.d		$s2, $s2, $t0
	csrwr		$s2, LOONGARCH_CSR_PGDL		/* disable low half identity map*/

	move	$a0, $sp
	bl	_C_LABEL(initloongarch) /* Off we go */
	bl	_C_LABEL(mi_startup)

	/* We should never reach here, but if so just hang. */
2:
	idle	0
	b	2b

/*
 * Create L1 pagetables
 * @parms
 * t0: l1 address
 * t1: l2 address
 * t2: virtual adress
 */
create_pagetables_to_l1:
	/* level 1 */
	srli.d		$a2, $t1, PPNS

	srli.d		$a3, $t2, L1_SHIFT
	andi		$a3, $a3, Ln_ADDR_MASK	/* L1 index */

	slli.d		$t5, $a2, PPNS		/* L1 pte */

	li.d		$t6, PTE_SIZE
	mul.d		$a3, $a3, $t6		/* L1 slot */
	stx.d		$t5, $t0, $a3		/* save L1 pte */
	jr		$ra

/*
 * Create L1 and L2 pagetables, L2 is huge page
 * @parms
 * t0: l1 address
 * t1: l2 address
 * t2: virtual adress
 * t3: physical address
 * t4: map size
 * KS1: memory attribute
 */
create_pagetables_to_l2:
	/* level 1 */
	srli.d		$a2, $t1, PPNS

	srli.d		$a3, $t2, L1_SHIFT
	andi		$a3, $a3, Ln_ADDR_MASK	/* L1 index */

	slli.d		$t5, $a2, PPNS		/* L1 pte */

	li.d		$t6, PTE_SIZE
	mul.d		$a3, $a3, $t6		/* L1 slot */
	stx.d		$t5, $t0, $a3		/* save L1 pte */

	/* level 2 huge page  */
	srli.d		$t5, $t2, L2_SHIFT
	andi		$t5, $t5, Ln_ADDR_MASK	/* L2 index */
	srli.d		$t6, $t4, L2_SHIFT	/* L2 count */
	add.d		$t6, $t6, $t5		/* t6 => map end idex */

	csrrd		$a2, LOONGARCH_CSR_KS1	/* memory atrribute */
	li.d		$a3, PTE_SIZE
	mul.d		$a4, $t5, $a3		/* L2 slot */
	srli.d		$t4, $t3, L2_SHIFT
1:
	slli.d		$a3, $t4, L2_SHIFT
	or		$a3, $a3, $a2		/* meke L2 pte */
	stx.d		$a3, $t1, $a4		/* save L2 pte */

	addi.d		$a4, $a4, PTE_SIZE	/* update l2 slot */
	addi.d		$t4, $t4, 1		/* update physical address */
	addi.d		$t5, $t5, 1		/* update l2 index */
	bltu		$t5, $t6, 1b		/* build all L2 entries */
	jr		$ra


	.align 12
handle_tlbrefill:
	csrwr	$t0, LOONGARCH_CSR_TLBRSAVE
	csrrd	$t0, LOONGARCH_CSR_PGD
	lddir	$t0, $t0, 3
	lddir	$t0, $t0, 2
	lddir	$t0, $t0, 1
	ldpte	$t0, 0
	ldpte	$t0, 1
	tlbfill
	csrrd	$t0, LOONGARCH_CSR_TLBRSAVE
	ertn

	.align  4
initstack:
	.space  (PAGE_SIZE * KSTACK_PAGES)
initstack_end:

ENTRY(sigcode)
	move    $a0, $sp
	addi.d  $a0, $a0, SF_UC

1:
        li.d    $a7, SYS_sigreturn
        syscall	0

        /* sigreturn failed, exit */
        li.d    $a7, SYS_exit
        syscall	0

        b       1b
END(sigcode)
        /* This may be copied to the stack, keep it 16-byte aligned */
        .align  3
esigcode:

        .data
        .align  3
        .global szsigcode
szsigcode:
        .quad   esigcode - sigcode

        .align  12
pagetable_invalid:
	.space	PAGE_SIZE
pagetable_l0:
	.space	PAGE_SIZE
pagetable_l1:
        .space  PAGE_SIZE
pagetable_l2:
        .space  PAGE_SIZE
pagetable_l2_devmap:
        .space  PAGE_SIZE
pagetable_id_l0:
	.space	PAGE_SIZE
pagetable_id_l1:
	.space	PAGE_SIZE
pagetable_id_l2:
	.space	PAGE_SIZE
pagetable_dmap:
	.space	PAGE_SIZE

        .align 3
Lvirtdone:
	.quad	virtdone
LSvirtdone:
	.quad	svirtdone

Lbss:
	.quad	__bss_start
Lend:
	.quad	_end

	.globl l1_pt_va
l1_pt_va:
	.quad pagetable_l1
	.globl l2_pt_va
l2_pt_va:
	.quad pagetable_l2	/* XXX: Keep page tables VA */

	.globl dmap_pt_va	/* for kernel dmap */
dmap_pt_va:
	.quad pagetable_dmap

#ifndef SMP
ENTRY(mpentry)
1:
	idle 	0
	j	1b
END(mpentry)
#else
/*
 * mpentry(unsigned long)
 *
 * Called by a core when it is being brought online.
 */
ENTRY(mpentry)

	/* disable PG */
	li.w            $t0, 0xa8               # PLV=0, IE=0, PG=0, DA=1
	csrwr		$t0, LOONGARCH_CSR_CRMD

	/* setup kernel pagetable */
	setup_page_table 0

	/* setup cached DMWIN0 */
	pcaddi	$t0, 0
	li.d	$t1, 0xFFFF000000000000
	and	$t0, $t0, $t1
	ori	$t0, $t0, (1 << 4 | 1)
	csrwr	$t0, LOONGARCH_CSR_DMWIN0

	/* setup tlbrentry */
	la.pcrel	$t0, handle_tlbrefill
	csrwr		$t0, LOONGARCH_CSR_TLBRENTRY

	/*
	 * Configure ECFG to enable timer and IPI interrupts.
	 * Global interrupts are disabled (CRMD.IE=0) so these will not
	 * fire until enabled in init_secondary().
	 */
	csrrd		$t0, LOONGARCH_CSR_ECFG
	li.d		$t1, 0x70000
	andn		$t0, $t0, $t1
	li.d		$t1, 0x1
	slli.d		$t1, $t1, INT_TI
	li.d		$t2, 0x1
	slli.d		$t2, $t2, INT_IPI
	or		$t1, $t1, $t2
	or		$t0, $t0, $t1
	csrwr		$t0, LOONGARCH_CSR_ECFG

	/* flush tlb (global) */
	invtlb		0, $zero, $zero

	/* record virtdone virt addr*/
	la.pcrel        $t1, LSvirtdone
	ld.d            $t5, $t1, 0

	/* record l0 phy address */
	la.pcrel	$s1, pagetable_l0

	/* Enable PG */
        li.w            $t0, 0xb0                # PLV=0, IE=0, PG=1
        csrwr           $t0, LOONGARCH_CSR_CRMD

	/* jump to virtual address space */
	jirl		$zero, $t5, 0

svirtdone:

	/* remove DMW0 */
	li.d		$t0, 0
	csrwr		$t0, LOONGARCH_CSR_DMWIN0

	/* for uart */
	li.d            $t0, 0x8000000000000001 /* CSR_DMW0_INIT: DMW0 VSEG=0x8000, PLV0 */
	csrwr           $t0, LOONGARCH_CSR_DMWIN0

	/* get cpuid, pass to init_secondary */
	csrrd	$a0, LOONGARCH_CSR_CPUID
	andi	$a0, $a0, 0x3ff

	/*
	 * Calculate the offset to __loongarch_boot_ap
	 * for the current core, cpuid is in a0.
	 */
	li.w	$t1, 4
	mul.w	$t1, $t1, $a0
	/* Get the pointer */
	la.pcrel	$t0, __loongarch_boot_ap
	add.d	$t0, $t0, $t1

1:
	/* Wait the kernel to be ready */
	ld.w	$t1, $t0, 0
	beqz	$t1, $1b

	/* Setup stack pointer */
	la.pcrel	$t0, bootstack
	ld.d	$sp, $t0, 0

	bl	init_secondary
END(mpentry)
#endif
