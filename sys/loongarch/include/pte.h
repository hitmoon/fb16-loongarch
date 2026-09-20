/*-
 * Copyright (c) 2014 Andrew Turner
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

#ifndef _MACHINE_PTE_H_
#define	_MACHINE_PTE_H_

#ifndef LOCORE
typedef	uint64_t	pd_entry_t;	/* page directory entry */
typedef	uint64_t	pt_entry_t;	/* page table entry */
typedef	uint64_t	pn_t;		/* page number */
#endif

/*
 * Uncomment TLB_LIFECYCLE_DEBUG to trace the full TLB lifecycle:
 * invalidation, installation (tlbfill / tlbwr), and stale-hit
 * diagnostics during page faults.
 */
#ifdef TLB_LIFECYCLE_DEBUG
#define TLB_DBG(...)	printf(__VA_ARGS__)
#else
#define TLB_DBG(...)	do {} while (0)
#endif

/* extra 8M */
#define LOCORE_EXTRA_MAP_LEN	(8 * 1024 * 1024)

#if PAGE_SIZE == PAGE_SIZE_4K
#define L0_SHIFT	39
#define L1_SHIFT	30
#define L2_SHIFT	21
#define L3_SHIFT	12
#elif PAGE_SIZE == PAGE_SIZE_16K
#define L0_SHIFT	47
#define L1_SHIFT	36
#define L2_SHIFT	25
#define L3_SHIFT	14
#else
#error Unsupported page size
#endif

/* Level 0 table, 512GiB/128TiB per entry */
#define	L0_SIZE		(1UL << L0_SHIFT)
#define	L0_OFFSET	(L0_SIZE - 1)

/* Level 1 table, 1GiB/64GiB per entry */
#define	L1_SIZE 	(1UL << L1_SHIFT)
#define	L1_OFFSET 	(L1_SIZE - 1)

/* Level 2 table, 2MiB/32MiB per entry */
#define	L2_SIZE 	(1UL << L2_SHIFT)
#define	L2_OFFSET 	(L2_SIZE - 1)

/* Level 3 table, 4KiB/16KiB per entry */
#define	L3_SIZE 	(1UL << L3_SHIFT)
#define	L3_OFFSET 	(L3_SIZE - 1)

#if PAGE_SIZE == PAGE_SIZE_4K
#define	L0_ENTRIES_SHIFT	9
#define	Ln_ENTRIES_SHIFT	9
#elif PAGE_SIZE == PAGE_SIZE_16K
#define	L0_ENTRIES_SHIFT	1
#define	Ln_ENTRIES_SHIFT	11
#else
#error Unsupported page size
#endif

#define	L0_ENTRIES	(1 << L0_ENTRIES_SHIFT)
#define	L0_ADDR_MASK	(L0_ENTRIES - 1)

#define	Ln_ENTRIES	(1 << Ln_ENTRIES_SHIFT)
#define	Ln_ADDR_MASK	(Ln_ENTRIES - 1)

/* Bits 11:9 are reserved */
#define	PTE_D		(1 << 1) /* Dirty */
#define	PTE_G		(1 << 6) /* Global */
#define PTE_K   (0 << 2) /* kernel, PLV 0 */
#define	PTE_U		(0x3 << 2) /* User, PLV 3 */
#define PTE_P   (1 << 7)   /* Present */
#define PTE_CC  (1 << 4)   /* coherent cached */
#define PTE_WUC (2 << 4)   /* weakly-order uncached */
#define PTE_SUC (0 << 4)   /* strongly-order uncached */
#define	PTE_MAT_MASK	(PTE_CC | PTE_WUC) /* memory attribute (MAT) field */
#define	PTE_NX		(1UL << 62) /* Non Execute */
#define	PTE_NR		(1UL << 61) /* Non Read */
#define	PTE_W		(1 << 8) /* Write */
#define	PTE_V		(1 << 0) /* Valid */
#define PTE_A		PTE_V
#define PTE_HUGE_G	(1 << 12) /* Huge page G bit */
#define PTE_HUGE		(1 << 6)  /* Huge page marker */
#define PTE_READABLE	(PTE_V)
#define PTE_WRITEABLE	(PTE_D | PTE_W)

#define PTE_SW_WIRED    (1 << 9)
#define PTE_SW_MANAGED  (1 << 10)

#define PTE_RX(pte)  (((pte & PTE_NR) == 0) || ((pte & PTE_NX) == 0))
#define PTE_RWX(pte) (PTE_RX(pte) || (((pte) & PTE_W) != 0))

#define PMD_KERN (PTE_K | PTE_READABLE | PTE_WRITEABLE | PTE_CC | PTE_P)
#define PMD_KERN_SUC (PTE_K | PTE_READABLE | PTE_WRITEABLE | PTE_SUC | PTE_P)
#define PMD_KERN_HUGE (PMD_KERN | PTE_HUGE | PTE_HUGE_G)
#define PMD_KERN_SUC_HUGE (PMD_KERN_SUC | PTE_HUGE | PTE_HUGE_G)


#define PTE_KERN (PTE_K | PTE_READABLE | PTE_WRITEABLE | PTE_CC | PTE_P | PTE_G)
#define PTE_KERN_SUC (PTE_K | PTE_READABLE | PTE_WRITEABLE | \
				PTE_SUC | PTE_P | PTE_G)
#define PTE_HI_MASK			0xffff000000000000ULL

#define PTE_PROMOTE (PTE_V | PTE_D | PTE_G | PTE_U | PTE_SW_MANAGED | \
				PTE_SW_WIRED | PTE_W | PTE_NX)

#define PPNS						L3_SHIFT
#define PAGE_PFN_SHIFT	L3_SHIFT

#define	PTE_SIZE		8
#define PTE_WIDTH		0		/* 64 bit */

#define PT_BASE			L3_SHIFT
#define PT_WIDTH		Ln_ENTRIES_SHIFT
#define DIR1_BASE		L2_SHIFT
#define DIR1_WIDTH	Ln_ENTRIES_SHIFT
#define DIR2_BASE		L1_SHIFT
#define DIR2_WIDTH	Ln_ENTRIES_SHIFT
#define DIR3_BASE		L0_SHIFT
#define DIR3_WIDTH	L0_ENTRIES_SHIFT
#define DIR4_BASE		48 /* 48 bit address */
#define DIR4_WIDTH	0

/* PWCL */
#define PT_BASE_SHIFT			0
#define PT_WIDTH_SHIFT		5
#define DIR1_BASE_SHIFT		10
#define DIR1_WIDTH_SHIFT	15
#define DIR2_BASE_SHIFT		20
#define DIR2_WIDTH_SHIFT	25
#define PTE_WIDTH_SHIFT		30

/* PWCH */
#define DIR3_BASE_SHIFT		0
#define DIR3_WIDTH_SHIFT	6
#define DIR4_BASE_SHIFT		12
#define DIR4_WIDTH_SHIFT	18

#define PWCL \
((PTE_WIDTH << PTE_WIDTH_SHIFT)|(DIR2_WIDTH << DIR2_WIDTH_SHIFT) \
|(DIR2_BASE << DIR2_BASE_SHIFT)|(DIR1_WIDTH << DIR1_WIDTH_SHIFT) \
|(DIR1_BASE << DIR1_BASE_SHIFT)|(PT_WIDTH << PT_WIDTH_SHIFT) \
|PT_BASE << PT_BASE_SHIFT)

#define PWCH \
((DIR3_WIDTH << DIR3_WIDTH_SHIFT)|DIR3_BASE << DIR3_BASE_SHIFT)

#endif /* !_MACHINE_PTE_H_ */

/* End of pte.h */
