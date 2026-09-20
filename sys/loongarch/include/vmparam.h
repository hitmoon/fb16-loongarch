/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 * Copyright (c) 1994 John S. Dyson
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)vmparam.h     5.9 (Berkeley) 5/12/91
 *	from: FreeBSD: src/sys/i386/include/vmparam.h,v 1.33 2000/03/30
 */

#ifndef	_MACHINE_VMPARAM_H_
#define	_MACHINE_VMPARAM_H_

/*
 * Virtual memory related constants, all in bytes
 */
#ifndef MAXTSIZ
#define	MAXTSIZ	(1*1024*1024*1024)	/* max text size */
#endif
#ifndef DFLDSIZ
#define	DFLDSIZ	(128*1024*1024) /* initial data size limit */
#endif
#ifndef MAXDSIZ
#define	MAXDSIZ	(1*1024*1024*1024)	/* max data size */
#endif
#ifndef DFLSSIZ
#define	DFLSSIZ	(128*1024*1024) /* initial stack size limit */
#endif
#ifndef MAXSSIZ
#define	MAXSSIZ	(1*1024*1024*1024)	/* max stack size */
#endif
#ifndef SGROWSIZ
#define	SGROWSIZ	(128*1024)	/* amount to grow stack */
#endif

/*
 * The physical address space is sparsely populated.
 */
#define	VM_PHYSSEG_SPARSE

/*
 * The number of PHYSSEG entries.
 */
#define	VM_PHYSSEG_MAX		64

/*
 * Create two free page pools: VM_FREEPOOL_DEFAULT is the default pool
 * from which physical pages are allocated and VM_FREEPOOL_DIRECT is
 * the pool from which physical pages for small UMA objects are
 * allocated.
 */
#define	VM_NFREEPOOL		2
#define	VM_FREEPOOL_DEFAULT	0
#define	VM_FREEPOOL_DIRECT	1

/*
 * Create one free page list: VM_FREELIST_DEFAULT is for all physical
 * pages.
 */
#define	VM_NFREELIST		1
#define	VM_FREELIST_DEFAULT	0

/*
 * An allocation size of 16MB is supported in order to optimize the
 * use of the direct map by UMA.  Specifically, a cache line contains
 * at most four TTEs, collectively mapping 16MB of physical memory.
 * By reducing the number of distinct 16MB "pages" that are used by UMA,
 * the physical memory allocator reduces the likelihood of both 4MB
 * page TLB misses and cache misses caused by 4MB page TLB misses.
 */
#define	VM_NFREEORDER		12

/*
 * Enable superpage reservations: 1 level.
 */
#ifndef	VM_NRESERVLEVEL
#define	VM_NRESERVLEVEL		1
#endif

/*
 * Level 0 reservations consist of 512 pages.
 */
#ifndef	VM_LEVEL_0_ORDER
#define	VM_LEVEL_0_ORDER	9
#endif

/**
 * Address space layout.
 */

#define	VM_MIN_ADDRESS		(0x0000000000000000UL)
#define	VM_MAX_ADDRESS		(0xffffffffffffffffUL)

/* 128G kernel space */
#define	VM_MIN_KERNEL_ADDRESS	(0xffff800000000000UL)
#define	VM_MAX_KERNEL_ADDRESS	(0xffff802000000000UL)

/*
 * Kernel modules must load within +/-2 GB of the kernel's symbol range:
 * LoongArch pcalau12i/pcaddu12i (PCALA/GOT) reach only signed si20<<12, and
 * the short 2-instruction forms the compiler emits (e.g. __stack_chk_guard)
 * can't be extended at load time.
 *
 * The low 1 GB (LA_KMOD_VA_SKIP) is the first PUD region, which holds the
 * kernel image and its bootstrap-allocated L2 page tables.  Those L2 pages
 * have no vm_page, so pmap_alloc_l2()'s PHYS_TO_VM_PAGE() on the PUD entry
 * returns NULL and faults.  Both kmem (kva_import) and modules therefore
 * start above it.  Modules get the next LA_KMOD_VA_SIZE window (within reach);
 * kmem follows.  Window size < 1 GB so the window's far edge stays well under
 * the 2 GB reach from VM_MIN_KERNEL_ADDRESS.
 */
#define	LA_KMOD_VA_SKIP 0x40000000UL /* 1 GB: kernel + bootstrap PT */
#define	LA_KMOD_VA_SIZE  0x30000000UL /* 768 MB module window */

/* 256G direct map */
#define	DMAP_MIN_ADDRESS	(0xffffc04000000000UL)
#define	DMAP_MAX_ADDRESS	(0xffffc08000000000UL)

#define	DMAP_MIN_PHYSADDR	(dmap_phys_base)
#define	DMAP_MAX_PHYSADDR	(dmap_phys_max)

/* True if pa is in the dmap range */
#define	PHYS_IN_DMAP(pa)	((pa) >= DMAP_MIN_PHYSADDR && \
    (pa) < DMAP_MAX_PHYSADDR)
/* True if va is in the dmap range */
#define	VIRT_IN_DMAP(va)	((va) >= DMAP_MIN_ADDRESS && \
    (va) < (dmap_max_addr))

#define	PMAP_HAS_DMAP	1
#define	PHYS_TO_DMAP_ADDR(pa)					\
({								\
	KASSERT(PHYS_IN_DMAP(pa),				\
	    ("%s: PA out of range, PA: 0x%lx", __func__,	\
	    (vm_paddr_t)(pa)));					\
	((pa) - dmap_phys_base) + DMAP_MIN_ADDRESS;		\
})
#define PHYS_TO_DMAP(x)		((void*)PHYS_TO_DMAP_ADDR(x))

#define	DMAP_TO_PHYS(va)				\
({							\
	uintptr_t _va = (uintptr_t)(va);	\
	KASSERT(VIRT_IN_DMAP(_va),			\
	    ("%s: VA out of range, VA: 0x%lx", __func__,	\
	    (vm_offset_t)(va)));			\
	(_va - DMAP_MIN_ADDRESS) + dmap_phys_base;	\
})

/*
 * DMW0: the uncached direct-map window (CSR.DMW0, VSEG=0x8000) configured in
 * locore.S.  Unlike the page-table-backed DMAP, DMW0 is a hardware window: a
 * VA whose bits [63:48] equal 0x8000 maps 1:1 to the low 48-bit physical
 * address with no page-table entry.  It is used for early/uncached MMIO such
 * as the console UART.  Because there is no page-table entry, pmap_kextract()
 * and pmap_extract() must translate these VAs directly instead of walking the
 * page table (which would hit the VIRT_IS_VALID KASSERT in pmap_l1()).
 */
#define	DMW0_BASE_ADDRESS	(0x8000000000000000UL)
#define	VA_IN_DMW0(va)		(((va) & 0xffff000000000000UL) == \
				    DMW0_BASE_ADDRESS)
#define	DMW0_TO_PHYS(va) ((vm_paddr_t)((va) & 0x0000ffffffffffffUL))

/* 512 G of user space */
#define	VM_MIN_USER_ADDRESS		(0x00000000000UL)
#define	VM_MAX_USER_ADDRESS		(0x08000000000UL)

#define	VM_MINUSER_ADDRESS	(VM_MIN_USER_ADDRESS)
#define	VM_MAXUSER_ADDRESS	(VM_MAX_USER_ADDRESS)

#define	KERNBASE		(VM_MIN_KERNEL_ADDRESS)
#define	SHAREDPAGE		(VM_MAX_USER_ADDRESS - PAGE_SIZE)
#define	USRSTACK		SHAREDPAGE
#define	PS_STRINGS		(USRSTACK - sizeof(struct ps_strings))

#define VM_EARLY_DTB_ADDRESS	(VM_MAX_KERNEL_ADDRESS - (2 * L2_SIZE))

/*
 * How many physical pages per kmem arena virtual page.
 */
#ifndef VM_KMEM_SIZE_SCALE
#define	VM_KMEM_SIZE_SCALE	(1)
#endif

/*
 * Optional ceiling (in bytes) on the size of the kmem arena: 60% of the
 * kernel map.
 */
#ifndef VM_KMEM_SIZE_MAX
#define	VM_KMEM_SIZE_MAX	((VM_MAX_KERNEL_ADDRESS - \
    VM_MIN_KERNEL_ADDRESS + 1) * 3 / 5)
#endif

/*
 * Initial pagein size of beginning of executable file.
 */
#ifndef	VM_INITIAL_PAGEIN
#define	VM_INITIAL_PAGEIN	16
#endif

/*
 * LoongArch has a valid direct map (DMW0 window, see DMAP_MIN_ADDRESS and
 * PHYS_TO_DMAP in this file), so single-page UMA slabs are served from the
 * DMAP via the MI uma_small_alloc/free (uma_core.c), matching arm64/riscv.
 * Without UMA_USE_DMAP, PCPU zones created after booted >= BOOT_KVA fall
 * through to pcpu_page_alloc, which needs kva/pmap plumbing not ready that
 * early and hangs the boot (e.g. counter_u64_sysinit at SI_SUB_COUNTER).
 */
#define	UMA_USE_DMAP

#ifndef LOCORE
extern vm_paddr_t dmap_phys_base;
extern vm_paddr_t dmap_phys_max;
extern vm_offset_t dmap_max_addr;
extern vm_offset_t vm_max_kernel_address;
extern vm_offset_t l1_pt_va;
extern vm_offset_t l2_pt_va;
extern vm_offset_t dmap_pt_va;
#endif

#define	ZERO_REGION_SIZE	(64 * 1024)	/* 64KB */

#define	DEVMAP_MAX_VADDR	VM_MAX_KERNEL_ADDRESS

/*
 * Helpers needed by subr_devmap.c for 1MB-aligned device mappings.
 */
#define	L1_S_OFFSET	((1 << 20) - 1)
#define	trunc_1mpage(x)	((x) & ~L1_S_OFFSET)
#define	PMAP_MAPDEV_EARLY_SIZE	(L2_SIZE * 8)	/* 16M */

/*
 * No non-transparent large page support in the pmap.
 */
#define	PMAP_HAS_LARGEPAGES	0

/*
 * Need a page dump array for minidump.
 */
#define MINIDUMP_PAGE_TRACKING	1
#define MINIDUMP_STARTUP_PAGE_TRACKING 1

#endif /* !_MACHINE_VMPARAM_H_ */
