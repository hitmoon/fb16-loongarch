/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 * Copyright (c) 1994 John S. Dyson
 * All rights reserved.
 * Copyright (c) 1994 David Greenman
 * All rights reserved.
 * Copyright (c) 2003 Peter Wemm
 * All rights reserved.
 * Copyright (c) 2005-2010 Alan L. Cox <alc@cs.rice.edu>
 * All rights reserved.
 * Copyright (c) 2014 Andrew Turner
 * All rights reserved.
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and William Jolitz of UUNET Technologies Inc.
 *
 * Portions of this software were developed by Andrew Turner under
 * sponsorship from The FreeBSD Foundation.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 */
/*-
 * Copyright (c) 2003 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Jake Burkholder,
 * Safeport Network Services, and Network Associates Laboratories, the
 * Security Research Division of Network Associates, Inc. under
 * DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA
 * CHATS research program.
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
/*
 *	Manages physical address maps.
 *
 *	Since the information managed by this module is
 *	also stored by the logical address mapping module,
 *	this module may throw away valid virtual-to-physical
 *	mappings at almost any time.  However, invalidations
 *	of virtual-to-physical mappings must be done as
 *	requested.
 *
 *	In order to cope with hardware architectures which
 *	make virtual-to-physical map invalidates expensive,
 *	this module may delay invalidate or reduced protection
 *	operations until such time as they are actually
 *	necessary.  This module is given full information as
 *	to which processors are currently using which maps,
 *	and to when physical maps must be made correct.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitstring.h>
#include <sys/bus.h>
#include <sys/cpuset.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/msgbuf.h>
#include <sys/mutex.h>
#include <sys/physmem.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/sbuf.h>
#include <sys/sx.h>
#include <sys/vmem.h>
#include <sys/vmmeter.h>
#include <sys/sched.h>
#include <sys/sysctl.h>
#include <sys/smp.h>
#include <machine/smp.h>
#include <machine/tlb.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/vm_phys.h>
#include <vm/vm_radix.h>
#include <vm/vm_reserv.h>
#include <vm/vm_dumpset.h>
#include <vm/uma.h>

#include <machine/machdep.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/pte.h>
#include <machine/watch.h>
#include <machine/tlb.h>

/*
 * Boundary values for the page table page index space:
 *
 * L3 pages: [0, NUL2E)
 * L2 pages: [NUL2E, NUL2E + NUL1E)
 * L1 pages: [NUL2E + NUL1E, NUL2E + NUL1E + NUL0E)
 * L0 pages: [NUL2E + NUL1E + NUL0E, ...)
 */
#define	NUL0E		L0_ENTRIES
#define	NUL1E		(Ln_ENTRIES * NUL0E)
#define	NUL2E		(Ln_ENTRIES * NUL1E)

#ifdef PV_STATS
#define PV_STAT(x)	do { x ; } while (0)
#define	__pv_stat_used
#else
#define PV_STAT(x)	do { } while (0)
#define	__pv_stat_used	__unused
#endif

#define	pmap_l1_pindex(v)	(NUL2E + ((v) >> L1_SHIFT))
#define	pmap_l2_pindex(v)	((v) >> L2_SHIFT)
#define	pa_index(pa)		((pa) >> L2_SHIFT)
#define	pa_to_pvh(pa)		(&pv_table[pa_index(pa)])

#define	NPV_LIST_LOCKS	MAXCPU

#define	PHYS_TO_PV_LIST_LOCK(pa)	\
			(&pv_list_locks[pa_index(pa) % NPV_LIST_LOCKS])

#define	CHANGE_PV_LIST_LOCK_TO_PHYS(lockp, pa)	do {	\
	struct rwlock **_lockp = (lockp);		\
	struct rwlock *_new_lock;			\
							\
	_new_lock = PHYS_TO_PV_LIST_LOCK(pa);		\
	if (_new_lock != *_lockp) {			\
		if (*_lockp != NULL)			\
			rw_wunlock(*_lockp);		\
		*_lockp = _new_lock;			\
		rw_wlock(*_lockp);			\
	}						\
} while (0)

#define	CHANGE_PV_LIST_LOCK_TO_VM_PAGE(lockp, m)	\
			CHANGE_PV_LIST_LOCK_TO_PHYS(lockp, VM_PAGE_TO_PHYS(m))

#define	RELEASE_PV_LIST_LOCK(lockp)		do {	\
	struct rwlock **_lockp = (lockp);		\
							\
	if (*_lockp != NULL) {				\
		rw_wunlock(*_lockp);			\
		*_lockp = NULL;				\
	}						\
} while (0)

#define	VM_PAGE_TO_PV_LIST_LOCK(m)	\
			PHYS_TO_PV_LIST_LOCK(VM_PAGE_TO_PHYS(m))

static SYSCTL_NODE(_vm, OID_AUTO, pmap, CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
    "VM/pmap parameters");

enum pmap_mode __read_frequently pmap_mode = PMAP_MODE_LA48;
SYSCTL_INT(_vm_pmap, OID_AUTO, mode, CTLFLAG_RDTUN | CTLFLAG_NOFETCH,
    &pmap_mode, 0,
    "translation mode = LA48");

struct pmap kernel_pmap_store;
vm_offset_t kernel_load_phys;

vm_offset_t virtual_avail; /* VA of first avail page (after kernel bss) */
vm_offset_t virtual_end;	/* VA of last avail page (end of kernel AS) */
vm_offset_t kernel_vm_end = 0;

vm_paddr_t dmap_phys_base;	/* The start of the dmap region */
vm_paddr_t dmap_phys_max;	/* The limit of the dmap region */
vm_offset_t dmap_max_addr;	/* The virtual address limit of the dmap */

static int pmap_growkernel_panic = 0;
SYSCTL_INT(_vm_pmap, OID_AUTO, growkernel_panic, CTLFLAG_RDTUN,
    &pmap_growkernel_panic, 0,
    "panic on failure to allocate kernel page table page");

/* FIXME */
#if 0
/* This code assumes all L1 DMAP entries will be used */
CTASSERT((DMAP_MIN_ADDRESS  & ~L1_OFFSET) == DMAP_MIN_ADDRESS);
CTASSERT((DMAP_MAX_ADDRESS  & ~L1_OFFSET) == DMAP_MAX_ADDRESS);

/*
 * This code assumes that the early DEVMAP is L2_SIZE aligned and is fully
 * contained within a single L2 entry. The early DTB is mapped immediately
 * before the devmap L2 entry.
 */
CTASSERT((PMAP_MAPDEV_EARLY_SIZE & L2_OFFSET) == 0);
CTASSERT((VM_EARLY_DTB_ADDRESS & L2_OFFSET) == 0);
CTASSERT(VM_EARLY_DTB_ADDRESS <
    (VM_MAX_KERNEL_ADDRESS - PMAP_MAPDEV_EARLY_SIZE));
#endif

static struct rwlock_padalign pvh_global_lock;

static int __read_frequently superpages_enabled = 1;
SYSCTL_INT(_vm_pmap, OID_AUTO, superpages_enabled,
    CTLFLAG_RDTUN, &superpages_enabled, 0,
    "Enable support for transparent superpages");

static SYSCTL_NODE(_vm_pmap, OID_AUTO, l2, CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
    "2MB page mapping counters");

static u_long pmap_l2_demotions;
SYSCTL_ULONG(_vm_pmap_l2, OID_AUTO, demotions, CTLFLAG_RD,
    &pmap_l2_demotions, 0,
    "2MB page demotions");

static u_long pmap_l2_mappings;
SYSCTL_ULONG(_vm_pmap_l2, OID_AUTO, mappings, CTLFLAG_RD,
    &pmap_l2_mappings, 0,
    "2MB page mappings");

/*
 * Threshold (in pages) above which pmap_remove stops issuing one
 * pmap_invalidate_range shootdown per contiguous run and instead defers
 * them all to a single pmap_invalidate_all at the end -- trading one
 * full-pmap flush for N range IPIs on a large munmap.  0 disables
 * coalescing (always per-range).
 */
int pmap_tlb_ipi_threshold = 32;
SYSCTL_INT(_vm_pmap, OID_AUTO, tlb_ipi_threshold, CTLFLAG_RWTUN,
    &pmap_tlb_ipi_threshold, 0,
    "Page count above which pmap_remove coalesces TLB invalidations");

static u_long pmap_l2_p_failures;
SYSCTL_ULONG(_vm_pmap_l2, OID_AUTO, p_failures, CTLFLAG_RD,
    &pmap_l2_p_failures, 0,
    "2MB page promotion failures");

static u_long pmap_l2_promotions;
SYSCTL_ULONG(_vm_pmap_l2, OID_AUTO, promotions, CTLFLAG_RD,
    &pmap_l2_promotions, 0,
    "2MB page promotions");

static SYSCTL_NODE(_vm_pmap, OID_AUTO, l1, CTLFLAG_RD | CTLFLAG_MPSAFE, 0,
    "L1 (1GB) page mapping counters");

static COUNTER_U64_DEFINE_EARLY(pmap_l1_demotions);
SYSCTL_COUNTER_U64(_vm_pmap_l1, OID_AUTO, demotions, CTLFLAG_RD,
    &pmap_l1_demotions, "L1 (1GB) page demotions");

/*
 * Data for the pv entry allocation mechanism
 */
static TAILQ_HEAD(pch, pv_chunk) pv_chunks = TAILQ_HEAD_INITIALIZER(pv_chunks);
static struct mtx pv_chunks_mutex;
static struct rwlock pv_list_locks[NPV_LIST_LOCKS];
static struct md_page *pv_table;
static struct md_page pv_dummy;

/*
 * Internal flags for pmap_enter()'s helper functions.
 */
#define	PMAP_ENTER_NORECLAIM 0x1000000 /* Don't reclaim PV entries. */
#define	PMAP_ENTER_NOREPLACE 0x2000000 /* Don't replace mappings. */

static void	free_pv_chunk(struct pv_chunk *pc);
static void	free_pv_entry(pmap_t pmap, pv_entry_t pv);
static pv_entry_t get_pv_entry(pmap_t pmap, struct rwlock **lockp);
static vm_page_t reclaim_pv_chunk(pmap_t locked_pmap, struct rwlock **lockp);
static void	pmap_pvh_free(struct md_page *pvh, pmap_t pmap,
		    vm_offset_t va);
static pv_entry_t pmap_pvh_remove(struct md_page *pvh, pmap_t pmap,
		    vm_offset_t va);
static bool	pmap_demote_l1(pmap_t pmap, pd_entry_t *l1, vm_offset_t va);
static bool	pmap_demote_l2(pmap_t pmap, pd_entry_t *l2, vm_offset_t va);
static bool	pmap_demote_l2_locked(pmap_t pmap, pd_entry_t *l2,
		    vm_offset_t va, struct rwlock **lockp);
static int	pmap_enter_l2(pmap_t pmap, vm_offset_t va, pd_entry_t new_l2,
		    u_int flags, vm_page_t m, struct rwlock **lockp);
static vm_page_t pmap_enter_quick_locked(pmap_t pmap, vm_offset_t va,
    vm_page_t m, vm_prot_t prot, vm_page_t mpte, struct rwlock **lockp);
static int pmap_remove_l3(pmap_t pmap, pt_entry_t *l3, vm_offset_t sva,
    pd_entry_t ptepde, struct spglist *free, struct rwlock **lockp,
    bool invalidate);
static bool pmap_try_insert_pv_entry(pmap_t pmap, vm_offset_t va,
    vm_page_t m, struct rwlock **lockp);

static vm_page_t _pmap_alloc_l3(pmap_t pmap, vm_pindex_t ptepindex,
		struct rwlock **lockp);

static void _pmap_unwire_ptp(pmap_t pmap, vm_offset_t va, vm_page_t m,
    struct spglist *free);
static int pmap_unuse_pt(pmap_t, vm_offset_t, pd_entry_t, struct spglist *);

static int pmap_change_attr_locked(void *va, vm_size_t size, int mode);

static pd_entry_t invalid_pgd_table; /* fwd decl: defined and set below */

#ifdef INVARIANTS
/*
 * pmap_inv_active gates the bare-phys wired-child write-watch.  It is set at
 * the end of pmap_bootstrap -- before that, the kernel page tables are built
 * by locore using pages that are not tracked as wired vm_pages, so the check
 * would false-fire.
 */
static bool pmap_inv_active;

/*
 * Two invariants, checked at every PTE store:
 *
 * 1. A 0 store must target a leaf entry, never a directory entry.  Directory
 *    entries are emptied to the sentinel, not 0.  Detect a directory page by
 *    the sentinel in its empty slots.
 *
 * 2. A bare-phys value (page-aligned, non-zero, non-sentinel, no V or HUGE
 *    bits) stored as a directory child pointer must point at a wired PT page.
 *    A stray store that writes a data page's phys into a directory slot --
 *    the corrupt-directory-entry bug -- is caught here at the WRITE, with the
 *    writer's backtrace.
 */
static void
pmap_pte_sync_invariant(volatile pt_entry_t *pte)
{
	pd_entry_t val, *pg;
	int j, nsent;

	val = *(volatile pt_entry_t *)pte;

	if (val == 0) {
		/* Invariant 1: 0 must not be stored into a directory page. */
		pg = (pd_entry_t *)((uintptr_t)pte & ~PAGE_MASK);
		nsent = 0;
		for (j = 0; j < 16; j++)
			if (*(volatile pd_entry_t *)&pg[j] == invalid_pgd_table)
				nsent++;
		KASSERT(nsent <= 8,
		    ("pmap: 0 stored into directory page %#lx slot %lu",
		    (unsigned long)(uintptr_t)pg,
		    (unsigned long)(((uintptr_t)pte & PAGE_MASK) / PTE_SIZE)));
		return;
	}

	/*
	 * Invariant 2: a bare-phys directory child must point at a wired PT
	 * page. Filter: page-aligned (bits [11:0] = 0), no NX/NR high bits,
	 * non-zero, non-sentinel.  This uniquely identifies a directory-child
	 * bare-phys pointer -- leaf PTEs always have at least one low flag bit
	 * (V, D, PLV, G, MAT) or a high bit (NX, NR) set.
	 */
	if (pmap_inv_active && invalid_pgd_table != 0 &&
	    val != invalid_pgd_table &&
	    val != 0 && (val & (0xfffUL | PTE_NX | PTE_NR)) == 0) {
		vm_paddr_t cphys = ((val & ~PTE_HI_MASK) >> PAGE_PFN_SHIFT) <<
		    PAGE_SHIFT;
		vm_page_t m;

		if (cphys == 0)
			return;
		m = PHYS_TO_VM_PAGE(cphys);
		KASSERT(vm_page_wired(m),
		    ("pmap: bare phys %#lx stored as directory child -- "
		    "not wired (pindex=%#lx ref_count=%d) -- corrupt dir "
		    "entry?",
		    (unsigned long)val, (unsigned long)m->pindex,
		    m->ref_count));
	}
}
#endif

static __inline void
pmap_pte_sync(volatile pt_entry_t *pte)
{

	c_sync();
#ifdef INVARIANTS
	pmap_pte_sync_invariant(pte);
#endif
}

#define	pmap_clear(pte)			pmap_store(pte, 0)
#define	pmap_clear_bits(pte, bits)	do { \
	atomic_clear_64(pte, bits); \
	pmap_pte_sync(pte); \
	} while (0)
#define	pmap_load_store(pte, entry)	({ \
	pt_entry_t _o; \
	_o = atomic_swap_64(pte, entry); \
	pmap_pte_sync(pte); \
	_o; \
	})
#define	pmap_load_clear(pte)		pmap_load_store(pte, 0)
#define	pmap_load(pte)			atomic_load_64(pte)
#define	pmap_store(pte, entry)		do { \
	atomic_store_64(pte, entry); \
	pmap_pte_sync(pte); \
	} while (0)
#define	pmap_store_bits(pte, bits)	do { \
	atomic_set_64(pte, bits); \
	pmap_pte_sync(pte); \
	} while (0)
#define pmap_cmpset(pte, old, new) ({ \
	bool _r; \
	_r = atomic_fcmpset_long(pte, old, new); \
	pmap_pte_sync(pte); \
	_r; \
	})

/* will be initialized in pmap_bootstrap */
static pd_entry_t invalid_pgd_table = 0;
static pd_entry_t invalid_pud_table = 0;
static pd_entry_t invalid_pmd_table = 0;

static inline bool
pmap_pgd_valid(pd_entry_t *pgd)
{
	return (pmap_load(pgd) != (pd_entry_t)invalid_pgd_table);
}

static inline bool
pmap_pud_valid(pd_entry_t *pud)
{
	return (pmap_load(pud) != (pd_entry_t)invalid_pud_table);
}

static inline bool
pmap_pmd_valid(pd_entry_t *pmd)
{
	return (pmap_load(pmd) != (pd_entry_t)invalid_pmd_table);
}

static inline bool
pmap_pte_valid(pd_entry_t *pte)
{
	return ((pmap_load(pte) & PTE_P) != 0);
}

static inline void
pmap_init_pd(pd_entry_t *pd)
{
	int i;

	for (i = 0; i < Ln_ENTRIES; i++)
		pmap_store(&pd[i], invalid_pmd_table);
}

static inline uint32_t
pmap_current_asid(void)
{
	return (csr_read32(LOONGARCH_CSR_ASID) & CSR_ASID_ASID);
}

#define pmap_pgd_entry(x) (pmap_load(x))
#define pmap_pud_entry(x) (pmap_load(x))
#define pmap_pmd_entry(x) (pmap_load(x))
#define pmap_pte_entry(x) (pmap_load(x))

/********************/
/* Inline functions */
/********************/

static inline void
set_pgd_asid(uint32_t asid, vm_offset_t pgdl)
{
	__asm__ __volatile__(
    "csrwr %[pgdl_val], %[pgdl_reg] \n\t"
    "csrwr %[asid_val], %[asid_reg] \n\t"
    : [asid_val] "+r" (asid), [pgdl_val] "+r" (pgdl)
    : [asid_reg] "i" (LOONGARCH_CSR_ASID), [pgdl_reg] "i" (LOONGARCH_CSR_PGDL)
    : "memory"
    );
}

static __inline void
pagecopy(void *s, void *d)
{

	memcpy(d, s, PAGE_SIZE);
}

static __inline void
pagezero(void *p)
{

	bzero(p, PAGE_SIZE);
}

#define	pmap_l0_index(va)	(((va) >> L0_SHIFT) & L0_ADDR_MASK)
#define	pmap_l1_index(va)	(((va) >> L1_SHIFT) & Ln_ADDR_MASK)
#define	pmap_l2_index(va)	(((va) >> L2_SHIFT) & Ln_ADDR_MASK)
#define	pmap_l3_index(va)	(((va) >> L3_SHIFT) & Ln_ADDR_MASK)

#define	PTE_TO_PHYS(pte) \
    ((((pte) & ~PTE_HI_MASK) >> PAGE_PFN_SHIFT) << PAGE_SHIFT)

#define	L2PTE_TO_PHYS(l2) \
    ((((l2) & ~PTE_HI_MASK) >> L2_SHIFT) << L2_SHIFT)

#define PTE_TO_VM_PAGE(pte)	PHYS_TO_VM_PAGE(PTE_TO_PHYS(pte))

#ifdef INVARIANTS
/*
 * Every page-table page is allocated VM_ALLOC_WIRED.  A directory entry that
 * points at a recycled *data* page (corruption) leads the HW walker to read
 * that page's contents as page-table entries -- the "V=1, impossible PFN"
 * garbage TLB entries.  Verify, on the SW walk, that a directory entry's child
 * is actually a wired page-table page.
 */
static __inline void
pmap_ptchild_assert_wired(vm_paddr_t phys, vm_offset_t va, const char *fn)
{
	vm_page_t m;

	if (phys == 0)
		return;
	m = PHYS_TO_VM_PAGE(phys);
	KASSERT(vm_page_wired(m),
	    ("%s: page-table page: %p child phys %#lx (va %#lx) not wired -- "
	    "pindex=%#lx ref_count=%d -- corrupt dir entry or wire-count bug?",
	    fn, m, (unsigned long)phys, va,
	    (unsigned long)m->pindex, m->ref_count));
}
#endif

static __inline pd_entry_t *
pmap_l0(pmap_t pmap, vm_offset_t va)
{
	KASSERT(VIRT_IS_VALID(va),
	    ("%s: malformed virtual address %#lx", __func__, va));
	return (&pmap->pm_top[pmap_l0_index(va)]);
}

static __inline pd_entry_t *
pmap_l0_to_l1(pd_entry_t *l0, vm_offset_t va)
{
	vm_paddr_t phys;
	pd_entry_t *l1;

	phys = PTE_TO_PHYS(pmap_pgd_entry(l0));
#ifdef INVARIANTS
	if (va < VM_MAXUSER_ADDRESS && pmap_pgd_entry(l0) != invalid_pgd_table)
		pmap_ptchild_assert_wired(phys, va, __func__);
#endif
	l1 = (pd_entry_t *)PHYS_TO_DMAP(phys);
	return (&l1[pmap_l1_index(va)]);
}

static __inline pd_entry_t *
pmap_l1(pmap_t pmap, vm_offset_t va)
{
	pd_entry_t *l0;

	KASSERT(VIRT_IS_VALID(va),
	    ("%s: malformed virtual address %#lx", __func__, va));

	l0 = pmap_l0(pmap, va);
	if (!pmap_pgd_valid(l0))
		return NULL;

	if (pmap_pgd_entry(l0) & PTE_HUGE)
		return NULL;

	return (pmap_l0_to_l1(l0, va));
}

static __inline pd_entry_t *
pmap_l1_to_l2(pd_entry_t *l1, vm_offset_t va)
{
	vm_paddr_t phys;
	pd_entry_t *l2;

	phys = PTE_TO_PHYS(pmap_pud_entry(l1));
#ifdef INVARIANTS
	if (va < VM_MAXUSER_ADDRESS && pmap_pud_entry(l1) != invalid_pgd_table)
		pmap_ptchild_assert_wired(phys, va, __func__);
#endif
	l2 = (pd_entry_t *)PHYS_TO_DMAP(phys);

	return (&l2[pmap_l2_index(va)]);
}

static __inline pd_entry_t *
pmap_l2(pmap_t pmap, vm_offset_t va)
{
	pd_entry_t *l1;

	l1 = pmap_l1(pmap, va);
	if (l1 == NULL || !pmap_pud_valid(l1))
		return (NULL);
	if ((pmap_pud_entry(l1) & PTE_HUGE) != 0)
		return (NULL);
	return (pmap_l1_to_l2(l1, va));
}

static __inline pt_entry_t *
pmap_l2_to_l3(pd_entry_t *l2, vm_offset_t va)
{
	vm_paddr_t phys;
	pt_entry_t *l3;

	phys = PTE_TO_PHYS(pmap_pmd_entry(l2));
#ifdef INVARIANTS
	if (va < VM_MAXUSER_ADDRESS && pmap_pmd_entry(l2) != invalid_pgd_table)
		pmap_ptchild_assert_wired(phys, va, __func__);
#endif
	l3 = (pd_entry_t *)PHYS_TO_DMAP(phys);

	return (&l3[pmap_l3_index(va)]);
}

static __inline pt_entry_t *
pmap_l3(pmap_t pmap, vm_offset_t va)
{
	pd_entry_t *l2;
	pd_entry_t l2e;

	l2 = pmap_l2(pmap, va);
	if (l2 == NULL)
		return (NULL);
	l2e = pmap_pmd_entry(l2);
	if (!pmap_pmd_valid(l2) || (l2e & PTE_HUGE) != 0)
		return (NULL);

	return (pmap_l2_to_l3(l2, va));
}

static __inline void
pmap_resident_count_inc(pmap_t pmap, int count)
{

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	pmap->pm_stats.resident_count += count;
}

static __inline void
pmap_resident_count_dec(pmap_t pmap, int count)
{

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	KASSERT(pmap->pm_stats.resident_count >= count,
	    ("pmap %p resident count underflow %ld %d", pmap,
	    pmap->pm_stats.resident_count, count));
	pmap->pm_stats.resident_count -= count;
}

static pt_entry_t *
pmap_early_page_idx(vm_offset_t l1pt, vm_offset_t va, u_int *l1_slot,
    u_int *l2_slot)
{
	pt_entry_t *l2;
	pd_entry_t *l1 __diagused;

	l1 = (pd_entry_t *)l1pt;
	*l1_slot = (va >> L1_SHIFT) & Ln_ADDR_MASK;

	/* Check locore has used a table L1 map */
	KASSERT(!(l1[*l1_slot] & PTE_HUGE),
		("Invalid bootstrap L1 table"));

	/* Find the address of the L2 table */
	l2 = (pt_entry_t *)l2_pt_va;
	*l2_slot = pmap_l2_index(va);

	return (l2);
}

static vm_paddr_t
pmap_early_vtophys(vm_offset_t l1pt, vm_offset_t va)
{
	u_int l1_slot, l2_slot;
	pt_entry_t *l2;
	vm_paddr_t ret;

	l2 = pmap_early_page_idx(l1pt, va, &l1_slot, &l2_slot);

	/* Check locore has used L2 superpages */
	KASSERT((l2[l2_slot] & PTE_HUGE),
		("Invalid bootstrap L2 table"));

	/* L2 is superpages */
	ret = L2PTE_TO_PHYS(l2[l2_slot]);
	ret += (va & L2_OFFSET);

	return (ret);
}

static void
pmap_bootstrap_dmap(vm_paddr_t min_pa, vm_paddr_t max_pa)
{
	vm_offset_t va;
	vm_paddr_t pa;
	pd_entry_t *l1;
	u_int l0_slot, l1_slot;
	pt_entry_t entry;
	pn_t pn;
	vm_offset_t dmap_pt_phys;

	pa = dmap_phys_base = min_pa & ~L1_OFFSET;
	va = DMAP_MIN_ADDRESS;
	l0_slot = pmap_l0_index(DMAP_MIN_ADDRESS);

	/* Fixup the L0 entry. */
	dmap_pt_phys = dmap_pt_va - KERNBASE + kernel_load_phys;
	pmap_store(&kernel_pmap->pm_top[l0_slot], dmap_pt_phys);

	/* DMAP page-table page. */
	l1 = (pd_entry_t *)dmap_pt_va;
	l1_slot = pmap_l1_index(DMAP_MIN_ADDRESS);

	for (; va < DMAP_MAX_ADDRESS && pa < max_pa;
	    pa += L1_SIZE, va += L1_SIZE, l1_slot++) {
		KASSERT(l1_slot < Ln_ENTRIES, ("Invalid L1 index"));

		/* 1G page */
		pn = pa >> PAGE_PFN_SHIFT;
		entry = PMD_KERN_HUGE;
		entry |= (pn << L3_SHIFT);
		pmap_store(&l1[l1_slot], entry);
	}

	/* Set the upper limit of the DMAP region */
	dmap_phys_max = pa;
	dmap_max_addr = va;

	invtlb_all(INVTLB_CURRENT_ALL);
}

static vm_offset_t
pmap_bootstrap_l3(vm_offset_t l1pt, vm_offset_t va, vm_offset_t l3_start)
{
	vm_offset_t l3pt;
	pt_entry_t entry;
	pd_entry_t *l2;
	vm_paddr_t pa;
	u_int l2_slot;
	pn_t pn;

	KASSERT((va & L2_OFFSET) == 0, ("Invalid virtual address"));

	l2 = pmap_l2(kernel_pmap, va);
	l2 = (pd_entry_t *)((uintptr_t)l2 & ~(PAGE_SIZE - 1));
	l2_slot = pmap_l2_index(va);
	l3pt = l3_start;

	printf("l2: %p, l2_slot: 0x%x\n", l2, l2_slot);

	for (; va < VM_MAX_KERNEL_ADDRESS; l2_slot++, va += L2_SIZE) {
		KASSERT(l2_slot < Ln_ENTRIES, ("Invalid L2 index"));

		pa = pmap_early_vtophys(l1pt, l3pt);
		pn = pa >> PAGE_PFN_SHIFT;
		entry = pn << L3_SHIFT;
		pmap_store(&l2[l2_slot], entry);
		l3pt += PAGE_SIZE;
	}

	/* Clean the L3 page table */
	memset((void *)l3_start, 0, l3pt - l3_start);

	return (l3pt);
}

/*
 * Map a range of physical addresses into kernel virtual address space,
 * assigning the given attributes.  Used by subr_devmap for static device
 * mappings.  Currently unused on loongarch (no static devmap table is
 * registered), but must exist to satisfy the link.
 */
void
pmap_preboot_map_attr(vm_paddr_t pa, vm_offset_t va, vm_size_t size,
    vm_prot_t prot, vm_memattr_t attr)
{

	panic("%s: not implemented", __func__);
}

/*
 *	Bootstrap the system enough to run with virtual memory.
 */
void
pmap_bootstrap(vm_offset_t pgd, vm_paddr_t kernstart, vm_size_t kernlen)
{
	vm_paddr_t physmap[PHYS_AVAIL_ENTRIES];
	vm_offset_t dpcpu, freemempos, msgbufpv;
	vm_paddr_t max_pa, min_pa;
	u_int l1_slot, l2_slot;
	u_int physmap_idx;
	int i;
	vm_offset_t l1, va;
	pd_entry_t *l0_table, *l1_table, *l2_table, *sentinel_page;
	int l2_end;

	printf("pmap_bootstrap %lx %lx %lx\n", pgd, kernstart, kernlen);

	kernel_load_phys = kernstart;

	/* Set this early so we can use the pagetable walking functions */
	/* pgd is physical address here !! */
	kernel_pmap_store.pm_top = (pd_entry_t *)(KERNBASE + pgd - kernstart);

	/* point to invalid L0 */
	invalid_pgd_table = pgd - PAGE_SIZE;
	invalid_pud_table = invalid_pgd_table;
	invalid_pmd_table = invalid_pgd_table;

	/*
	 * Fill the sentinel page (at invalid_pgd_table, i.e. pgd - PAGE_SIZE)
	 * with the sentinel value itself.  The LoongArch HW walker follows
	 * bare-phys directory entries unconditionally -- there is no V bit on
	 * directory entries -- so if the sentinel page is all-zero, the walker
	 * follows 0x00 entries to phys 0 and reads its contents as PTEs,
	 * producing garbage TLB entries (V=1, bogus PFN).  By filling the
	 * sentinel page with invalid_pgd_table (page-aligned, bit 0 = 0), the
	 * walker descends through this page at each remaining level and, when
	 * it reaches the leaf PTE level, reads the sentinel value as a PTE with
	 * V=0 -> TLB miss -> clean page fault.
	 */
	sentinel_page = (pd_entry_t *)
	    (KERNBASE + invalid_pgd_table - kernstart);
	for (i = 0; i < Ln_ENTRIES; i++)
		pmap_store(&sentinel_page[i], invalid_pgd_table);

	kernel_pmap_store.pm_pgdl = invalid_pgd_table;
	kernel_pmap_store.pm_pgdh = pgd;

	/* reinitialize kernel page table */
	l0_table = (pd_entry_t*)kernel_pmap_store.pm_top;
	for (i = 0; i < L0_ENTRIES; i++) {
		if (l0_table[i] == 0)
			pmap_store(&l0_table[i], invalid_pgd_table);
	}

	l1 = (vm_offset_t)l1_pt_va;
	l1_table = (pd_entry_t*)l1;
	l2_table = pmap_early_page_idx(l1, KERNBASE, &l1_slot, &l2_slot);
	for (i = l1_slot + 1; i < Ln_ENTRIES; i++) {
		if (l1_table[i] == 0)
			pmap_store(&l1_table[i], invalid_pud_table);
	}

	l2_end = l2_slot + ((kernlen + L2_SIZE - 1) / L2_SIZE);

	for (i = l2_end; i < Ln_ENTRIES; i++) {
		if (l2_table[i] == 0)
			pmap_store(&l2_table[i], invalid_pmd_table);
	}

	mtx_init(&kernel_pmap->pm_mtx, "kernel pmap", NULL, MTX_DEF);
	TAILQ_INIT(&kernel_pmap->pm_pvchunk);
	vm_radix_init(&kernel_pmap->pm_root);

	rw_init(&pvh_global_lock, "pmap pv global");

	/*
	 * Set the current CPU as active in the kernel pmap. Secondary cores
	 * will add themselves later in init_secondary().
	*/
	CPU_SET(PCPU_GET(cpuid), &kernel_pmap->pm_active);

	/* Assume the address we were loaded to is a valid physical address. */
	min_pa = max_pa = kernstart;

	physmap_idx = physmem_avail(physmap, nitems(physmap));
	physmap_idx /= 2;

	/*
	 * Find the minimum physical address. physmap is sorted,
	 * but may contain empty ranges.
	 */
	for (i = 0; i < physmap_idx * 2; i += 2) {
		if (physmap[i] == physmap[i + 1])
			continue;
		if (physmap[i] <= min_pa)
			min_pa = physmap[i];
		if (physmap[i + 1] > max_pa)
			max_pa = physmap[i + 1];
	}
	printf("physmap_idx %u\n", physmap_idx);
	printf("min_pa %lx\n", min_pa);
	printf("max_pa %lx\n", max_pa);

	/* Create a direct map region early so we can use it for pa -> va */
	pmap_bootstrap_dmap(min_pa, max_pa);

	/* Sanity check the index, KERNBASE should be the first VA */
	KASSERT(l2_slot == 0, ("The L2 index is non-zero"));

	freemempos = roundup2(KERNBASE + kernlen, PAGE_SIZE);

	/* Create the l3 tables for the early devmap */
	freemempos = pmap_bootstrap_l3(l1,
	    VM_MAX_KERNEL_ADDRESS - PMAP_MAPDEV_EARLY_SIZE, freemempos);

#if 0
	/*
	 * Invalidate the mapping we created for the DTB. At this point a copy
	 * has been created, and we no longer need it. We want to avoid the
	 * possibility of an aliased mapping in the future.
	 */
	l2p = pmap_l2(kernel_pmap, VM_EARLY_DTB_ADDRESS);
	if ((pmap_pmd_entry(l2p) & PTE_V) != 0)
		pmap_clear(l2p);

#endif

	/* invalid kernel pgdl */
	csr_write64(kernel_pmap->pm_pgdl, LOONGARCH_CSR_PGDL);

#define alloc_pages(var, np)						\
	(var) = freemempos;						\
	freemempos += (np * PAGE_SIZE);					\
	memset((char *)(var), 0, ((np) * PAGE_SIZE));

	/* Allocate dynamic per-cpu area. */
	alloc_pages(dpcpu, DPCPU_SIZE / PAGE_SIZE);
	dpcpu_init((void *)dpcpu, 0);

	/* Allocate memory for the msgbuf, e.g. for /sbin/dmesg */
	alloc_pages(msgbufpv, round_page(msgbufsize) / PAGE_SIZE);
	msgbufp = (void *)msgbufpv;

	virtual_avail = KERNBASE + LA_KMOD_VA_SKIP;

	/* clear 2M mappings after kernel image setup by locore */
	for (va = roundup2(KERNBASE + kernlen, L2_SIZE);
			va < virtual_avail; va += L2_SIZE) {
		pd_entry_t *l2 = pmap_l2(kernel_pmap, va);
		if (l2 != NULL && (pmap_pmd_entry(l2) & PTE_HUGE) != 0)
			pmap_store(l2, invalid_pmd_table);
	}

	virtual_end = VM_MAX_KERNEL_ADDRESS - PMAP_MAPDEV_EARLY_SIZE;
	kernel_vm_end = virtual_avail;

	physmem_exclude_region(kernstart,
	    pmap_early_vtophys(l1, freemempos) - kernstart, EXFLAG_NOALLOC);

	/* Initialize pv chunk mutex early -- UMA (uma_startup1) runs before
	 * pmap_init() and may trigger pmap_enter/pmap_kenter which need
	 * pv_chunks_mutex. */
	mtx_init(&pv_chunks_mutex, "pmap pv chunk list", NULL, MTX_DEF);

	invtlb_all(INVTLB_CURRENT_ALL);

#ifdef INVARIANTS
	pmap_inv_active = true;
#endif
}

/*
 *	Initialize a vm_page's machine-dependent fields.
 */
void
pmap_page_init(vm_page_t m)
{

	TAILQ_INIT(&m->md.pv_list);
	m->md.pv_memattr = VM_MEMATTR_WRITE_BACK;
}

/*
 *	Initialize the pmap module.
 *
 *	Called by vm_mem_init(), to initialize any structures that the pmap
 *	system needs to map virtual memory.
 */
void
pmap_init(void)
{
	vm_size_t s;
	int i, pv_npg;

	/*
	 * Initialize the pool of pv list locks.
	 */
	for (i = 0; i < NPV_LIST_LOCKS; i++)
		rw_init(&pv_list_locks[i], "pmap pv list");

	/*
	 * Calculate the size of the pv head table for superpages.
	 */
	pv_npg = howmany(vm_phys_segs[vm_phys_nsegs - 1].end, L2_SIZE);

	/*
	 * Allocate memory for the pv head table for superpages.
	 */
	s = (vm_size_t)(pv_npg * sizeof(struct md_page));
	s = round_page(s);
	pv_table = kmem_malloc(s, M_WAITOK | M_ZERO);
	for (i = 0; i < pv_npg; i++)
		TAILQ_INIT(&pv_table[i].pv_list);
	TAILQ_INIT(&pv_dummy.pv_list);

	if (superpages_enabled)
		pagesizes[1] = L2_SIZE;
}

#ifdef SMP
/*
 * For SMP, each invalidation builds a single shootdown request handled by
 * smp_targeted_tlb_shootdown(): it computes the remote mask, sends one IPI,
 * flushes locally, and (when interrupts are enabled) waits for every target
 * to flush.  pmap_invalidate_range is now a single shootdown instead of a
 * per-page IPI loop.
 */
static void
pmap_invalidate_page(pmap_t pmap, vm_offset_t va)
{

	sched_pin();
	smp_targeted_tlb_shootdown(pmap, va, 0, LA_TLB_OP_PAGE);
	sched_unpin();
}

static void
pmap_invalidate_range(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{

	sched_pin();
	smp_targeted_tlb_shootdown(pmap, sva, eva, LA_TLB_OP_RANGE);
	sched_unpin();
}

static void
pmap_invalidate_all(pmap_t pmap)
{

	sched_pin();
	smp_targeted_tlb_shootdown(pmap, 0, 0, LA_TLB_OP_ALL);
	sched_unpin();
}

#else
/*
 * Normal, non-SMP, invalidation functions.
 * We inline these within pmap.c for speed.
 */
static __inline void
pmap_invalidate_page(pmap_t pmap, vm_offset_t va)
{
	if (pmap == kernel_pmap)
		invtlb(INVTLB_ADDR_GTRUE_OR_ASID, pmap_current_asid(), va);
	else
		invtlb(INVTLB_ADDR_GFALSE_AND_ASID,
		    pmap_get_asid(pmap, PCPU_GET(cpuid)), va);
}

static __inline void
pmap_invalidate_range(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	if (pmap == kernel_pmap) {
		uint32_t curasid = pmap_current_asid();
		while (sva < eva) {
			invtlb(INVTLB_ADDR_GTRUE_OR_ASID, curasid, sva);
			sva += PAGE_SIZE;
		}
	} else {
		uint32_t asid = pmap_get_asid(pmap, PCPU_GET(cpuid));
		while (sva < eva) {
			invtlb(INVTLB_ADDR_GFALSE_AND_ASID, asid, sva);
			sva += PAGE_SIZE;
		}
	}
}

static __inline void
pmap_invalidate_all(pmap_t pmap)
{
	TLB_DBG("TLB inv: cpu%d %s ALL\n",
	    PCPU_GET(cpuid),
	    pmap == kernel_pmap ? "kernel" : "user");
	if (pmap == kernel_pmap)
		invtlb_all(INVTLB_CURRENT_ALL);
	else
		flush_tlb_local(pmap);
}
#endif

/*
 *	Routine:	pmap_extract
 *	Function:
 *		Extract the physical page address associated
 *		with the given map/virtual_address pair.
 */
vm_paddr_t
pmap_extract(pmap_t pmap, vm_offset_t va)
{
	pd_entry_t *l2p, l2;
	pt_entry_t *l3p;
	vm_paddr_t pa;

	/*
	 * DMW0 is a hardware direct-map window with no page-table entry; the
	 * physical address is just the low 48 bits of the VA.  Handle it
	 * before touching the page table (pmap_l1() would KASSERT on these
	 * VAs, which are outside the page-table-managed kernel range).
	 */
	if (VA_IN_DMW0(va))
		return (DMW0_TO_PHYS(va));

	pa = 0;

	/*
	 * Start with an L2 lookup, L1 superpages are currently not implemented.
	 */
	PMAP_LOCK(pmap);
	l2p = pmap_l2(pmap, va);
	if (l2p != NULL && pmap_pmd_valid(l2p))  {
		l2 = pmap_pmd_entry(l2p);
		if (!(l2 & PTE_HUGE)) {
			l3p = pmap_l2_to_l3(l2p, va);
			pa = PTE_TO_PHYS(pmap_pte_entry(l3p));
			pa |= (va & L3_OFFSET);
		} else {
			/* L2 is a superpage mapping. */
			pa = L2PTE_TO_PHYS(l2);
			pa |= (va & L2_OFFSET);
		}
	}
	PMAP_UNLOCK(pmap);
	return (pa);
}

/*
 *	Routine:	pmap_extract_and_hold
 *	Function:
 *		Atomically extract and hold the physical page
 *		with the given pmap and virtual address pair
 *		if that mapping permits the given protection.
 */
vm_page_t
pmap_extract_and_hold(pmap_t pmap, vm_offset_t va, vm_prot_t prot)
{
	pd_entry_t *l2p, l2;
	pt_entry_t *l3p, l3;
	vm_page_t m;

	m = NULL;
	PMAP_LOCK(pmap);
	l2p = pmap_l2(pmap, va);
	if (l2p == NULL || !pmap_pmd_valid(l2p)) {
		PMAP_UNLOCK(pmap);
		return (NULL);
	}

	l2 = pmap_pmd_entry(l2p);
	if ((l2 & PTE_HUGE))  {
		if ((l2 & PTE_W) != 0 || (prot & VM_PROT_WRITE) == 0) {
			m = PHYS_TO_VM_PAGE(L2PTE_TO_PHYS(l2) +
				(va & L2_OFFSET));
		}
	} else {
		l3p = pmap_l2_to_l3(l2p, va);
		if (pmap_pte_valid(l3p)) {
			l3 = pmap_pte_entry(l3p);
			if ((l3 & PTE_W) != 0 || (prot & VM_PROT_WRITE) == 0)
				m = PTE_TO_VM_PAGE(l3);
		}
	}

	if (m != NULL && !vm_page_wire_mapped(m))
		m = NULL;

	PMAP_UNLOCK(pmap);
	return (m);
}

vm_paddr_t
pmap_kextract(vm_offset_t va)
{
	pd_entry_t *l2, l2e;
	pt_entry_t *l3;
	vm_paddr_t pa;

	if (va >= DMAP_MIN_ADDRESS && va < DMAP_MAX_ADDRESS) {
		pa = DMAP_TO_PHYS(va);
	} else if (VA_IN_DMW0(va)) {
		/* DMW0 hardware direct-map window: no page-table entry. */
		pa = DMW0_TO_PHYS(va);
	} else {
		l2 = pmap_l2(kernel_pmap, va);
		if (l2 == NULL)
			panic("pmap_kextract: No l2");
		l2e = pmap_pmd_entry(l2);
		/*
		 * Beware of concurrent promotion and demotion! We must
		 * use l2e rather than loading from l2 multiple times to
		 * ensure we see a consistent state, including the
		 * implicit load in pmap_l2_to_l3.  It is, however, safe
		 * to use an old l2e because the L3 page is preserved by
		 * promotion.
		 */
		if (pmap_pmd_valid(l2) && (l2e & PTE_HUGE)) {
			/* superpages */
			pa = L2PTE_TO_PHYS(l2e);
			pa |= (va & L2_OFFSET);
			return (pa);
		}

		l3 = pmap_l2_to_l3(&l2e, va);
		if (l3 == NULL)
			panic("pmap_kextract: No l3...");
		pa = PTE_TO_PHYS(pmap_pte_entry(l3));
		pa |= (va & PAGE_MASK);
	}
	return (pa);
}

/***************************************************
 * Low level mapping routines.....
 ***************************************************/

void
pmap_kenter(vm_offset_t sva, vm_size_t size, vm_paddr_t pa, int mode)
{
	pt_entry_t entry;
	pt_entry_t *l3;
	vm_offset_t va, eva;
	pn_t pn;

	KASSERT((pa & L3_OFFSET) == 0,
	   ("pmap_kenter_device: Invalid physical address"));
	KASSERT((sva & L3_OFFSET) == 0,
	   ("pmap_kenter_device: Invalid virtual address"));
	KASSERT((size & PAGE_MASK) == 0,
	    ("pmap_kenter_device: Mapping is not page-sized"));

	va = sva;
	while (size != 0) {
		pd_entry_t *l2;

		l2 = pmap_l2(kernel_pmap, va);
		if (l2 != NULL && (pmap_pmd_entry(l2) & PTE_HUGE) != 0) {
			PMAP_LOCK(kernel_pmap);
			(void)pmap_demote_l2(kernel_pmap, l2,
			    va & ~L2_OFFSET);
			PMAP_UNLOCK(kernel_pmap);
		}

		l3 = pmap_l3(kernel_pmap, va);
		KASSERT(l3 != NULL, ("Invalid page table, va: 0x%lx", va));

		pn = pa >> PAGE_PFN_SHIFT;
		if (mode == VM_MEMATTR_DEVICE ||
		    mode == VM_MEMATTR_UNCACHEABLE ||
		    mode == VM_MEMATTR_WRITE_COMBINING)
			entry = PTE_KERN_SUC;
		else
			entry = PTE_KERN;
		entry |= pn << L3_SHIFT;
		pmap_store(l3, entry);

		va += PAGE_SIZE;
		pa += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	pmap_invalidate_range(kernel_pmap, sva, va);

	eva = va;
	for (va = sva; va < eva; va += PAGE_SIZE)
		pmap_update_tlb(kernel_pmap, va);
}

void
pmap_kenter_device(vm_offset_t sva, vm_size_t size, vm_paddr_t pa)
{
	pmap_kenter(sva, size, pa, VM_MEMATTR_DEVICE);
}

/*
 * Remove a page from the kernel pagetables.
 * Note: not SMP coherent.
 */
void
pmap_kremove(vm_offset_t va)
{
	pt_entry_t *l3;

	l3 = pmap_l3(kernel_pmap, va);
	KASSERT(l3 != NULL, ("pmap_kremove: Invalid address"));

	pmap_clear(l3);

	pmap_invalidate_page(kernel_pmap, va);
}

void
pmap_kremove_device(vm_offset_t sva, vm_size_t size)
{
	pt_entry_t *l3;
	vm_offset_t va;

	KASSERT((sva & L3_OFFSET) == 0,
	   ("pmap_kremove_device: Invalid virtual address"));
	KASSERT((size & PAGE_MASK) == 0,
	    ("pmap_kremove_device: Mapping is not page-sized"));

	va = sva;
	while (size != 0) {
		pd_entry_t *l2;

		l2 = pmap_l2(kernel_pmap, va);
		if (l2 != NULL && (pmap_pmd_entry(l2) & PTE_HUGE) != 0) {
			PMAP_LOCK(kernel_pmap);
			(void)pmap_demote_l2(kernel_pmap, l2,
			    va & ~L2_OFFSET);
			PMAP_UNLOCK(kernel_pmap);
		}

		l3 = pmap_l3(kernel_pmap, va);
		KASSERT(l3 != NULL, ("Invalid page table, va: 0x%lx", va));
		pmap_clear(l3);

		va += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	pmap_invalidate_range(kernel_pmap, sva, va);
}

/*
 *	Used to map a range of physical addresses into kernel
 *	virtual address space.
 *
 *	The value passed in '*virt' is a suggested virtual address for
 *	the mapping. Architectures which can support a direct-mapped
 *	physical to virtual region can return the appropriate address
 *	within that region, leaving '*virt' unchanged. Other
 *	architectures should map the pages starting at '*virt' and
 *	update '*virt' with the first usable address after the mapped
 *	region.
 */
void *
pmap_map(vm_offset_t *virt, vm_paddr_t start, vm_paddr_t end, int prot)
{

	return (PHYS_TO_DMAP(start));
}

/*
 * Add a list of wired pages to the kva
 * this routine is only used for temporary
 * kernel mappings that do not need to have
 * page modification or references recorded.
 * Note that old mappings are simply written
 * over.  The page *must* be wired.
 * Note: SMP coherent.  Uses a ranged shootdown IPI.
 */
void
pmap_qenter(void *sva, vm_page_t *ma, int count)
{
	pt_entry_t *l3, pa;
	vm_offset_t va, eva;
	vm_page_t m;
	pt_entry_t entry;
	pn_t pn;
	int i;

	va = (vm_offset_t)sva;
	for (i = 0; i < count; i++) {
		pt_entry_t *l2;
		m = ma[i];
		pa = VM_PAGE_TO_PHYS(m);
		pn = pa >> PAGE_PFN_SHIFT;

		/* 2M page must be demoted to 4K */
		l2 = pmap_l2(kernel_pmap, va);
		if (l2 != NULL && (pmap_pmd_entry(l2) & PTE_HUGE) != 0) {
			PMAP_LOCK(kernel_pmap);
			(void)pmap_demote_l2(kernel_pmap, l2,
			    va & ~L2_OFFSET);
			PMAP_UNLOCK(kernel_pmap);
		}

		l3 = pmap_l3(kernel_pmap, va);

		entry = PTE_KERN;
		entry |= (pn << L3_SHIFT);
		pmap_store(l3, entry);

		va += L3_SIZE;
	}

	pmap_invalidate_range(kernel_pmap, (vm_offset_t)sva, va);

	eva = va;
	for (va = (vm_offset_t)sva; va < eva; va += L3_SIZE)
		pmap_update_tlb(kernel_pmap, va);
}

/*
 * This routine tears out page mappings from the
 * kernel -- it is meant only for temporary mappings.
 * Note: SMP coherent.  Uses a ranged shootdown IPI.
 */
void
pmap_qremove(void *sva, int count)
{
	pt_entry_t *l3;
	vm_offset_t va;

	KASSERT((vm_offset_t)sva >= VM_MIN_KERNEL_ADDRESS,
	    ("usermode va %lx", (vm_offset_t)sva));

	for (va = (vm_offset_t)sva; count-- > 0; va += PAGE_SIZE) {
		l3 = pmap_l3(kernel_pmap, va);
		KASSERT(l3 != NULL, ("pmap_kremove: Invalid address"));
		pmap_clear(l3);
	}
	pmap_invalidate_range(kernel_pmap, (vm_offset_t)sva, va);
}

bool
pmap_ps_enabled(pmap_t pmap __unused)
{

	return (superpages_enabled);
}

/***************************************************
 * Page table page management routines.....
 ***************************************************/
/*
 * Schedule the specified unused page table page to be freed.  Specifically,
 * add the page to the specified list of pages that will be released to the
 * physical memory manager after the TLB has been updated.
 */
static __inline void
pmap_add_delayed_free_list(vm_page_t m, struct spglist *free, bool set_PG_ZERO)
{

	if (set_PG_ZERO)
		m->flags |= PG_ZERO;
	else
		m->flags &= ~PG_ZERO;
	SLIST_INSERT_HEAD(free, m, plinks.s.ss);
}

/*
 * Inserts the specified page table page into the specified pmap's collection
 * of idle page table pages.  Each of a pmap's page table pages is responsible
 * for mapping a distinct range of virtual addresses.  The pmap's collection is
 * ordered by this virtual address range.
 *
 * If "promoted" is false, then the page table page "ml3" must be zero filled.
 * mpte's valid filed will be set to 1.
 *
 * If promoted and l3_all_PTE_A are both true, then mpte must contain valid
 * mappings with identical attributes include PTE_A, mpte's valid field will
 * be set to VM_PAGE_BITS_ALL.
 */
static __inline int
pmap_insert_pt_page(pmap_t pmap, vm_page_t mpte, bool promoted,
	bool l3_all_PTE_A)
{

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	KASSERT(promoted || !l3_all_PTE_A,
		("a zero-filled PTP can't have PTE_A set in every PTE"));
	mpte->valid = promoted ? (l3_all_PTE_A ? VM_PAGE_BITS_ALL : 1) : 0;
	return (vm_radix_insert(&pmap->pm_root, mpte));
}

/*
 * Removes the page table page mapping the specified virtual address from the
 * specified pmap's collection of idle page table pages, and returns it.
 * Otherwise, returns NULL if there is no page table page corresponding to the
 * specified virtual address.
 */
static __inline vm_page_t
pmap_remove_pt_page(pmap_t pmap, vm_offset_t va)
{

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	return (vm_radix_remove(&pmap->pm_root, pmap_l2_pindex(va)));
}

/*
 * Decrements a page table page's reference count, which is used to record the
 * number of valid page table entries within the page.  If the reference count
 * drops to zero, then the page table page is unmapped.  Returns true if the
 * page table page was unmapped and false otherwise.
 */
static inline bool
pmap_unwire_ptp(pmap_t pmap, vm_offset_t va, vm_page_t m, struct spglist *free)
{
	KASSERT(m != NULL, ("%s: m is NULL va %#lx", __func__, va));
	KASSERT(m->ref_count > 0,
	    ("%s: page %p ref count underflow", __func__, m));

	--m->ref_count;
	if (m->ref_count == 0) {
		_pmap_unwire_ptp(pmap, va, m, free);
		return (true);
	} else {
		return (false);
	}
}

static void
_pmap_unwire_ptp(pmap_t pmap, vm_offset_t va, vm_page_t m, struct spglist *free)
{
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	if (m->pindex >= NUL2E + NUL1E) {
		pd_entry_t *l0;
		l0 = pmap_l0(pmap, va);
		pmap_store(l0, invalid_pgd_table);
	} else if (m->pindex >= NUL2E) {
		pd_entry_t *l1;
		l1 = pmap_l1(pmap, va);
		pmap_store(l1, invalid_pud_table);
	} else {
		pd_entry_t *l2;
		l2 = pmap_l2(pmap, va);
#ifdef INVARIANTS
		/*
		 * Verify the L2 entry we're about to clear actually points at
		 * this page.  If it points at a different page (or is already
		 * the sentinel), we'd be clearing the WRONG L2 entry — the
		 * real one would survive, pointing at the freed page → the
		 * freed-while-linked corruption.
		 */
		KASSERT(l2 != NULL && pmap_pmd_valid(l2) &&
		    PTE_TO_PHYS(pmap_pmd_entry(l2)) == VM_PAGE_TO_PHYS(m),
		    ("_pmap_unwire_ptp: L2 entry for va %#lx doesn't point at "
		    "this page (l2e=%#lx page phys %#lx) — wrong L2 cleared",
		    va, l2 ? pmap_pmd_entry(l2) : 0, VM_PAGE_TO_PHYS(m)));
#endif
		pmap_store(l2, invalid_pmd_table);
	}
	pmap_resident_count_dec(pmap, 1);
	if (m->pindex < NUL2E) {
		pd_entry_t *l1;
		vm_page_t pdpg;

		l1 = pmap_l1(pmap, va);
		pdpg = PTE_TO_VM_PAGE(pmap_pud_entry(l1));
		pmap_unwire_ptp(pmap, va, pdpg, free);
	} else if (m->pindex < NUL2E + NUL1E) {
		pd_entry_t *l0;
		vm_page_t pdpg;

		l0 = pmap_l0(pmap, va);
		pdpg = PTE_TO_VM_PAGE(pmap_pgd_entry(l0));
		pmap_unwire_ptp(pmap, va, pdpg, free);
	}
	pmap_invalidate_page(pmap, va);

	vm_wire_sub(1);

	/*
	 * Put page on a list so that it is released after *ALL* TLB shootdown
	 * is done.  Only a leaf (L3) page-table page is actually zeroed at this
	 * point: non-leaf (L2/L1) pages still hold the shared
	 * invalid-page-table sentinels (invalid_pmd_table / invalid_pud_table)
	 * in their slots, so they are NOT zeroed and must not be marked
	 * PG_ZERO.  Claiming PG_ZERO on them made vm_page_alloc(VM_ALLOC_ZERO)
	 * hand them out unzeroed, panicking UMA ctors under INVARIANTS ("lock
	 * already initialized").
	 */
	pmap_add_delayed_free_list(m, free, m->pindex < NUL2E);
}

/*
 * After removing a page table entry, this routine is used to
 * conditionally free the page, and manage the reference count.
 */
static int
pmap_unuse_pt(pmap_t pmap, vm_offset_t va, pd_entry_t ptepde,
    struct spglist *free)
{
	vm_page_t mpte;

	if (va >= VM_MAXUSER_ADDRESS)
		return (0);
	KASSERT(ptepde != 0, ("pmap_unuse_pt: ptepde != 0"));
	mpte = PTE_TO_VM_PAGE(ptepde);
	KASSERT(mpte >= vm_page_array &&
	    mpte < &vm_page_array[vm_page_array_size],
	    ("pmap_unuse_pt: ptepde %#lx pte_to_phys %#llx mpte %p va %#lx",
	    ptepde, PTE_TO_PHYS(ptepde), mpte, va));
	return (pmap_unwire_ptp(pmap, va, mpte, free));
}

void
pmap_pinit0(pmap_t pmap)
{
	PMAP_LOCK_INIT(pmap);
	bzero(&pmap->pm_stats, sizeof(pmap->pm_stats));
	bzero(&pmap->asid, sizeof(pmap->asid));
	pmap->pm_top = kernel_pmap->pm_top;
	pmap->pm_pgdl = kernel_pmap->pm_pgdl;
	pmap->pm_pgdh = kernel_pmap->pm_pgdh;
	CPU_ZERO(&pmap->pm_active);
	TAILQ_INIT(&pmap->pm_pvchunk);
	vm_radix_init(&pmap->pm_root);
	pmap_activate_boot(pmap);
}

int
pmap_pinit(pmap_t pmap)
{
	vm_paddr_t topphys;
	vm_page_t mtop;

	mtop = vm_page_alloc_noobj(VM_ALLOC_WIRED | VM_ALLOC_ZERO |
	    VM_ALLOC_WAITOK);

	topphys = VM_PAGE_TO_PHYS(mtop);
	pmap->pm_top = (pd_entry_t *)PHYS_TO_DMAP(topphys);
	pmap->pm_pgdl = topphys;
	pmap->pm_pgdh = kernel_pmap->pm_pgdh;

	pmap_init_pd(pmap->pm_top);

	bzero(&pmap->pm_stats, sizeof(pmap->pm_stats));
	bzero(&pmap->asid, sizeof(pmap->asid));

	CPU_ZERO(&pmap->pm_active);

	TAILQ_INIT(&pmap->pm_pvchunk);
	vm_radix_init(&pmap->pm_root);

	return (1);
}

/*
 * This routine is called if the desired page table page does not exist.
 *
 * If page table page allocation fails, this routine may sleep before
 * returning NULL.  It sleeps only if a lock pointer was given.
 *
 * Note: If a page allocation fails at page table level two or three,
 * one or two pages may be held during the wait, only to be released
 * afterwards.  This conservative approach is easily argued to avoid
 * race conditions.
 */
static vm_page_t
_pmap_alloc_l3(pmap_t pmap, vm_pindex_t ptepindex, struct rwlock **lockp)
{
	vm_page_t m, pdpg;
	pt_entry_t entry;
	vm_paddr_t phys;
	pn_t pn;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	/*
	 * Allocate a page table page.
	 */
	m = vm_page_alloc_noobj(VM_ALLOC_WIRED | VM_ALLOC_ZERO);
	if (m == NULL) {
		if (lockp != NULL) {
			RELEASE_PV_LIST_LOCK(lockp);
			PMAP_UNLOCK(pmap);
			rw_runlock(&pvh_global_lock);
			vm_wait(NULL);
			rw_rlock(&pvh_global_lock);
			PMAP_LOCK(pmap);
		}

		/*
		 * Indicate the need to retry.  While waiting, the page table
		 * page may have been allocated.
		 */
		return (NULL);
	}
	m->pindex = ptepindex;

	/*
	 * Map the pagetable page into the process address space, if
	 * it isn't already there.
	 */
	pn = VM_PAGE_TO_PHYS(m) >> PAGE_PFN_SHIFT;
	if (ptepindex >= NUL2E + NUL1E) {
		pd_entry_t *l0;
		vm_pindex_t l0index;

		pmap_init_pd(VM_PAGE_TO_DMAP(m));

		KASSERT(pmap_mode == PMAP_MODE_LA48,
		    ("%s: pindex %#lx not in LA48 mode", __func__, ptepindex));
		KASSERT(ptepindex < NUL2E + NUL1E + NUL0E,
		    ("%s: pindex %#lx out of range", __func__, ptepindex));

		l0index = ptepindex - (NUL2E + NUL1E);
		l0 = &pmap->pm_top[l0index];
		KASSERT(!(pmap_pgd_valid(l0)),
		    ("%s: L0 entry %#lx is valid",
		    __func__, pmap_pgd_entry(l0)));

		entry = pn << L3_SHIFT;
		pmap_store(l0, entry);
	} else if (ptepindex >= NUL2E) {
		pd_entry_t *l0, *l1;
		vm_pindex_t l0index, l1index;

		l1index = ptepindex - NUL2E;
		pmap_init_pd(VM_PAGE_TO_DMAP(m));

		l0index = l1index >> Ln_ENTRIES_SHIFT;
		l0 = &pmap->pm_top[l0index];
		if (!pmap_pgd_valid(l0)) {
			/* Recurse to allocate the L1 page. */
			if (_pmap_alloc_l3(pmap,
				NUL2E + NUL1E + l0index, lockp) == NULL)
				goto fail;
			phys = PTE_TO_PHYS(pmap_pgd_entry(l0));
		} else {
			phys = PTE_TO_PHYS(pmap_pgd_entry(l0));
			pdpg = PHYS_TO_VM_PAGE(phys);
			pdpg->ref_count++;
		}
		l1 = (pd_entry_t *)PHYS_TO_DMAP(phys);
		l1 = &l1[ptepindex & Ln_ADDR_MASK];

		KASSERT(!pmap_pud_valid(l1),
		    ("%s: L1 entry %#lx is valid",
		    __func__, pmap_pud_entry(l1)));

		entry = pn << L3_SHIFT;
		pmap_store(l1, entry);
	} else {
		vm_pindex_t l0index, l1index;
		pd_entry_t *l0, *l1, *l2;

		l1index = ptepindex >> (L1_SHIFT - L2_SHIFT);
		l0index = l1index >> Ln_ENTRIES_SHIFT;
		l0 = &pmap->pm_top[l0index];
		if (!pmap_pgd_valid(l0)) {
			/* Recurse to allocate the L1 entry. */
			if (_pmap_alloc_l3(pmap, NUL2E + l1index,
				lockp) == NULL)
				goto fail;
			phys = PTE_TO_PHYS(pmap_pgd_entry(l0));
			l1 = (pd_entry_t *)PHYS_TO_DMAP(phys);
			l1 = &l1[l1index & Ln_ADDR_MASK];
		} else {
			phys = PTE_TO_PHYS(pmap_pgd_entry(l0));
			l1 = (pd_entry_t *)PHYS_TO_DMAP(phys);
			l1 = &l1[l1index & Ln_ADDR_MASK];
			if (!pmap_pud_valid(l1)) {
				/* Recurse to allocate the L2 page. */
				if (_pmap_alloc_l3(pmap,
					NUL2E + l1index, lockp) == NULL)
					goto fail;
			} else {
				pdpg = PTE_TO_VM_PAGE(pmap_pud_entry(l1));
				pdpg->ref_count++;
			}
		}

		phys = PTE_TO_PHYS(pmap_pud_entry(l1));
		l2 = (pd_entry_t *)PHYS_TO_DMAP(phys);
		l2 = &l2[ptepindex & Ln_ADDR_MASK];
		KASSERT(!pmap_pmd_valid(l2),
		    ("%s: L2 entry %#lx is valid",
		    __func__, pmap_pmd_entry(l2)));

		entry = (pn << L3_SHIFT);
		pmap_store(l2, entry);
	}

	pmap_resident_count_inc(pmap, 1);

	return (m);

fail:
	vm_page_unwire_noq(m);
	/*
	 * m may be a non-leaf page-table page whose slots were filled with the
	 * shared invalid-page-table sentinels by pmap_init_pd() above (the L1
	 * branch reaches here after that call), so it is not necessarily
	 * zeroed.  Use vm_page_free(), not vm_page_free_zero(), to avoid
	 * falsely marking it PG_ZERO.
	 */
	vm_page_free(m);
	return (NULL);
}

static vm_page_t
pmap_alloc_l2(pmap_t pmap, vm_offset_t va, struct rwlock **lockp)
{
	pd_entry_t *l1;
	vm_page_t l2pg;
	vm_pindex_t pindex;

retry:
	l1 = pmap_l1(pmap, va);
	if (l1 != NULL && pmap_pud_valid(l1)) {
		KASSERT(!(pmap_pud_entry(l1) & PTE_HUGE),
		    ("%s: L1 entry %#lx for VA %#lx is a leaf", __func__,
		    pmap_pud_entry(l1), va));
		/* Add a reference to the L2 page. */
		l2pg = PHYS_TO_VM_PAGE(PTE_TO_PHYS(pmap_pud_entry(l1)));
		l2pg->ref_count++;
	} else {
		/* Allocate a L2 page. */
		pindex = pmap_l1_pindex(va);
		l2pg = _pmap_alloc_l3(pmap, pindex, lockp);
		if (l2pg == NULL && lockp != NULL)
			goto retry;
	}
	return (l2pg);
}

static vm_page_t
pmap_alloc_l3(pmap_t pmap, vm_offset_t va, struct rwlock **lockp)
{
	vm_pindex_t ptepindex;
	pd_entry_t *l2;
	vm_paddr_t phys;
	vm_page_t m;

	/*
	 * Calculate pagetable page index
	 */
	ptepindex = pmap_l2_pindex(va);
retry:
	/*
	 * Get the page directory entry
	 */
	l2 = pmap_l2(pmap, va);

	/*
	 * If the page table page is mapped, we just increment the
	 * hold count, and activate it.
	 */
	if (l2 != NULL && pmap_pmd_valid(l2)) {
		phys = PTE_TO_PHYS(pmap_pmd_entry(l2));
		m = PHYS_TO_VM_PAGE(phys);
		m->ref_count++;
	} else {
		/*
		 * Here if the pte page isn't mapped, or if it has been
		 * deallocated.
		 */
		m = _pmap_alloc_l3(pmap, ptepindex, lockp);
		if (m == NULL && lockp != NULL)
			goto retry;
	}
	return (m);
}

/***************************************************
 * Pmap allocation/deallocation routines.
 ***************************************************/

/*
 * Reconcile pm_stats.resident_count against an actual walk of the user page
 * table, run from pmap_release when the count is non-zero.  By now
 * pmap_remove_pages() (PV-tracked) and pmap_remove() (via vm_map_delete) have
 * torn the tables down, so both the count and the walk should be 0.
 *
 * The LoongArch resident_count model sums: each valid L3 leaf (+1), each L3
 * page-table page (+1), and each 2MB superpage (+L2_SIZE/PAGE_SIZE).  A wired
 * superpage's shadow PT page is also +1 but lives only in the PT-page trie,
 * not in the page table, so it is not counted here.  Comparing the walked
 * total to resident_count localizes the leak:
 *   expected=0, count>0  -> phantom inc (an inc with no matching dec)
 *   ptpages>0            -> L3 pages whose L2 slot was never reverted to the
 *                           sentinel (the _pmap_unwire_ptp path was skipped)
 *   leaves>0             -> real mappings that survived teardown (e.g. an
 *                           unmanaged special page outside every vm_map range)
 * Every level is read through the DMAP window so the probe never depends on
 * the (already-torn-down) user mappings.
 */
static void
pmap_release_audit(pmap_t pmap)
{
	pd_entry_t *l0, *l1, *l2, *l3t, e;
	vm_paddr_t phys, phys_max;
	int i0, i1, i2, i3;
	uint64_t leaves = 0, ptpages = 0, superpages = 0, expected;

	phys_max = dmap_phys_max;
	l0 = pmap->pm_top;
	for (i0 = 0; i0 < Ln_ENTRIES; i0++) {
		e = pmap_load(&l0[i0]);
		if (e == invalid_pgd_table || e == 0)
			continue;
		phys = PTE_TO_PHYS(e);
		if (phys == 0 || phys >= phys_max)
			continue;
		l1 = (pd_entry_t *)PHYS_TO_DMAP(phys);
		for (i1 = 0; i1 < Ln_ENTRIES; i1++) {
			e = pmap_load(&l1[i1]);
			if (e == invalid_pud_table || e == 0)
				continue;
			if ((e & PTE_HUGE) != 0) {
				superpages += L1_SIZE / PAGE_SIZE;
				continue;
			}
			phys = PTE_TO_PHYS(e);
			if (phys == 0 || phys >= phys_max)
				continue;
			l2 = (pd_entry_t *)PHYS_TO_DMAP(phys);
			for (i2 = 0; i2 < Ln_ENTRIES; i2++) {
				e = pmap_load(&l2[i2]);
				if (e == invalid_pmd_table || e == 0)
					continue;
				if ((e & PTE_HUGE) != 0) {
					superpages++;
					continue;
				}
				ptpages++;	/* L2 points at an L3 table */
				phys = PTE_TO_PHYS(e);
				if (phys == 0 || phys >= phys_max)
					continue;
				l3t = (pd_entry_t *)PHYS_TO_DMAP(phys);
				for (i3 = 0; i3 < Ln_ENTRIES; i3++) {
					e = pmap_load(&l3t[i3]);
					if ((e & PTE_V) != 0)
						leaves++;
				}
			}
		}
	}
	expected = leaves + ptpages + superpages * (L2_SIZE / PAGE_SIZE);
	printf("pmap_release_audit: resident_count=%ld expected=%lu "
	    "(leaves=%lu ptpages=%lu superpages=%lu)\n",
	    pmap->pm_stats.resident_count, expected, leaves, ptpages,
	    superpages);
}

/*
 * Release any resources held by the given physical map.
 * Called when a pmap initialized by pmap_pinit is being released.
 * Should only be called if the map contains no valid mappings.
 */
void
pmap_release(pmap_t pmap)
{
	vm_page_t m;

	if (pmap != kernel_pmap && pmap->pm_stats.resident_count != 0)
		pmap_release_audit(pmap);

	KASSERT(pmap->pm_stats.resident_count == 0,
	    ("pmap_release: pmap resident count %ld != 0",
	    pmap->pm_stats.resident_count));
	KASSERT(CPU_EMPTY(&pmap->pm_active),
	    ("releasing active pmap %p", pmap));

	m = PHYS_TO_VM_PAGE(DMAP_TO_PHYS((vm_offset_t)pmap->pm_top));
	vm_page_unwire_noq(m);
	vm_page_free(m);
}

static int
kvm_size(SYSCTL_HANDLER_ARGS)
{
	unsigned long ksize = VM_MAX_KERNEL_ADDRESS - VM_MIN_KERNEL_ADDRESS;

	return sysctl_handle_long(oidp, &ksize, 0, req);
}
SYSCTL_PROC(_vm, OID_AUTO, kvm_size, CTLTYPE_LONG | CTLFLAG_RD | CTLFLAG_MPSAFE,
    0, 0, kvm_size, "LU",
    "Size of KVM");

static int
kvm_free(SYSCTL_HANDLER_ARGS)
{
	unsigned long kfree = VM_MAX_KERNEL_ADDRESS - kernel_vm_end;

	return sysctl_handle_long(oidp, &kfree, 0, req);
}
SYSCTL_PROC(_vm, OID_AUTO, kvm_free, CTLTYPE_LONG | CTLFLAG_RD | CTLFLAG_MPSAFE,
    0, 0, kvm_free, "LU",
    "Amount of KVM free");

/*
 * grow the number of kernel page table entries, if needed
 */
static int
pmap_growkernel_nopanic(vm_offset_t addr)
{
	vm_paddr_t paddr;
	vm_page_t nkpg;
	pd_entry_t *l1, *l2;
	pt_entry_t entry;
	pn_t pn;

	mtx_assert(&kernel_map->system_mtx, MA_OWNED);

	addr = roundup2(addr, L2_SIZE);
	if (addr - 1 >= vm_map_max(kernel_map))
		addr = vm_map_max(kernel_map);
	while (kernel_vm_end < addr) {
		l1 = pmap_l1(kernel_pmap, kernel_vm_end);
		if (!pmap_pud_valid(l1)) {
			/* We need a new PDP entry */
			nkpg = vm_page_alloc_noobj(VM_ALLOC_INTERRUPT |
			    VM_ALLOC_NOFREE | VM_ALLOC_WIRED | VM_ALLOC_ZERO);
			if (nkpg == NULL)
				return (KERN_RESOURCE_SHORTAGE);

			nkpg->pindex = pmap_l1_pindex(kernel_vm_end);
			pmap_init_pd(VM_PAGE_TO_DMAP(nkpg));
			paddr = VM_PAGE_TO_PHYS(nkpg);

			pn = paddr >> PAGE_PFN_SHIFT;
			entry = 0;
			entry |= (pn << L3_SHIFT);
			pmap_store(l1, entry);
			continue; /* try again */
		}
		l2 = pmap_l1_to_l2(l1, kernel_vm_end);
		if (pmap_pmd_valid(l2)) {
			kernel_vm_end = (kernel_vm_end + L2_SIZE) & ~L2_OFFSET;
			if (kernel_vm_end - 1 >= vm_map_max(kernel_map)) {
				kernel_vm_end = vm_map_max(kernel_map);
				break;
			}
			continue;
		}

		nkpg = vm_page_alloc_noobj(VM_ALLOC_INTERRUPT |
		    VM_ALLOC_NOFREE | VM_ALLOC_WIRED | VM_ALLOC_ZERO);
		if (nkpg == NULL)
			return (KERN_RESOURCE_SHORTAGE);
		nkpg->pindex = pmap_l2_pindex(kernel_vm_end);
		/*
		 * This is an L3 (leaf PTE) page, not a directory page: it must
		 * read as zero/empty, never the directory sentinel.
		 * VM_ALLOC_ZERO already zeroed it, so do NOT pmap_init_pd()
		 * here -- doing so filled every empty PTE slot with
		 * invalid_p*d_table, which the HW walker read as a (V=0) PTE
		 * and which also tripped the "0 stored into a directory page"
		 * watch on pmap_qremove().
		 */
		paddr = VM_PAGE_TO_PHYS(nkpg);

		/*
		 * Store the L3 page table physical address in the L2
		 * directory entry.  LoongArch hardware does not check
		 * flags in directory entries during page table walks, so
		 * only the physical address is needed — no mapping flags.
		 */
		pn = paddr >> PAGE_PFN_SHIFT;
		entry = pn << L3_SHIFT;
		pmap_store(l2, entry);

		pmap_invalidate_page(kernel_pmap, kernel_vm_end);

		kernel_vm_end = (kernel_vm_end + L2_SIZE) & ~L2_OFFSET;
		if (kernel_vm_end - 1 >= vm_map_max(kernel_map)) {
			kernel_vm_end = vm_map_max(kernel_map);
			break;                       
		}
	}
	return (KERN_SUCCESS);
}


/***************************************************
 * page management routines.
 ***************************************************/

static const uint64_t pc_freemask[_NPCM] = {
	[0 ... _NPCM - 2] = PC_FREEN,
	[_NPCM - 1] = PC_FREEL
};

#ifdef PV_STATS
static int pc_chunk_count, pc_chunk_allocs, pc_chunk_frees, pc_chunk_tryfail;

SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_count, CTLFLAG_RD, &pc_chunk_count, 0,
	"Current number of pv entry chunks");
SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_allocs, CTLFLAG_RD, &pc_chunk_allocs, 0,
	"Current number of pv entry chunks allocated");
SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_frees, CTLFLAG_RD, &pc_chunk_frees, 0,
	"Current number of pv entry chunks frees");
SYSCTL_INT(_vm_pmap, OID_AUTO, pc_chunk_tryfail, CTLFLAG_RD,
	&pc_chunk_tryfail, 0,
	"Number of times tried to get a chunk page but failed.");

static long pv_entry_frees, pv_entry_allocs, pv_entry_count;
static int pv_entry_spare;

SYSCTL_LONG(_vm_pmap, OID_AUTO, pv_entry_frees, CTLFLAG_RD, &pv_entry_frees, 0,
	"Current number of pv entry frees");
SYSCTL_LONG(_vm_pmap, OID_AUTO, pv_entry_allocs, CTLFLAG_RD,
	&pv_entry_allocs, 0,
	"Current number of pv entry allocs");
SYSCTL_LONG(_vm_pmap, OID_AUTO, pv_entry_count, CTLFLAG_RD, &pv_entry_count, 0,
	"Current number of pv entries");
SYSCTL_INT(_vm_pmap, OID_AUTO, pv_entry_spare, CTLFLAG_RD, &pv_entry_spare, 0,
	"Current number of spare pv entries");
#endif

/*
 * We are in a serious low memory condition.  Resort to
 * drastic measures to free some pages so we can allocate
 * another pv entry chunk.
 *
 * Returns NULL if PV entries were reclaimed from the specified pmap.
 *
 * We do not, however, unmap 2mpages because subsequent accesses will
 * allocate per-page pv entries until repromotion occurs, thereby
 * exacerbating the shortage of free pv entries.
 */
static vm_page_t
reclaim_pv_chunk(pmap_t locked_pmap, struct rwlock **lockp)
{

	panic("LoongArch Todo: reclaim_pv_chunk");
}

/*
 * free the pv_entry back to the free list
 */
static void
free_pv_entry(pmap_t pmap, pv_entry_t pv)
{
	struct pv_chunk *pc;
	int idx, field, bit;

	rw_assert(&pvh_global_lock, RA_LOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	PV_STAT(atomic_add_long(&pv_entry_frees, 1));
	PV_STAT(atomic_add_int(&pv_entry_spare, 1));
	PV_STAT(atomic_subtract_long(&pv_entry_count, 1));
	pc = pv_to_chunk(pv);
	idx = pv - &pc->pc_pventry[0];
	field = idx / 64;
	bit = idx % 64;
	pc->pc_map[field] |= 1ul << bit;
	if (!pc_is_free(pc)) {
		/* 98% of the time, pc is already at the head of the list. */
		if (__predict_false(pc != TAILQ_FIRST(&pmap->pm_pvchunk))) {
			TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
			TAILQ_INSERT_HEAD(&pmap->pm_pvchunk, pc, pc_list);
		}
		return;
	}
	TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
	free_pv_chunk(pc);
}

static void
free_pv_chunk(struct pv_chunk *pc)
{
	vm_page_t m;

	mtx_lock(&pv_chunks_mutex);
 	TAILQ_REMOVE(&pv_chunks, pc, pc_lru);
	mtx_unlock(&pv_chunks_mutex);
	PV_STAT(atomic_subtract_int(&pv_entry_spare, _NPCPV));
	PV_STAT(atomic_subtract_int(&pc_chunk_count, 1));
	PV_STAT(atomic_add_int(&pc_chunk_frees, 1));
	/* entire chunk is free, return it */
	m = DMAP_TO_VM_PAGE(pc);
	dump_drop_page(m->phys_addr);
	vm_page_unwire_noq(m);
	vm_page_free(m);
}

/*
 * Returns a new PV entry, allocating a new PV chunk from the system when
 * needed.  If this PV chunk allocation fails and a PV list lock pointer was
 * given, a PV chunk is reclaimed from an arbitrary pmap.  Otherwise, NULL is
 * returned.
 *
 * The given PV list lock may be released.
 */
static pv_entry_t
get_pv_entry(pmap_t pmap, struct rwlock **lockp)
{
	int bit, field;
	pv_entry_t pv;
	struct pv_chunk *pc;
	vm_page_t m;

	rw_assert(&pvh_global_lock, RA_LOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	PV_STAT(atomic_add_long(&pv_entry_allocs, 1));
retry:
	pc = TAILQ_FIRST(&pmap->pm_pvchunk);
	if (pc != NULL) {
		for (field = 0; field < _NPCM; field++) {
			if (pc->pc_map[field]) {
				bit = ffsl(pc->pc_map[field]) - 1;
				break;
			}
		}
		if (field < _NPCM) {
			pv = &pc->pc_pventry[field * 64 + bit];
			pc->pc_map[field] &= ~(1ul << bit);
			/* If this was the last item, move it to tail */
			if (pc_is_full(pc)) {
				TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
				TAILQ_INSERT_TAIL(&pmap->pm_pvchunk, pc,
				    pc_list);
			}
			PV_STAT(atomic_add_long(&pv_entry_count, 1));
			PV_STAT(atomic_subtract_int(&pv_entry_spare, 1));
			return (pv);
		}
	}
	/* No free items, allocate another chunk */
	m = vm_page_alloc_noobj(VM_ALLOC_WIRED);
	if (m == NULL) {
		if (lockp == NULL) {
			PV_STAT(pc_chunk_tryfail++);
			return (NULL);
		}
		m = reclaim_pv_chunk(pmap, lockp);
		if (m == NULL)
			goto retry;
	}
	PV_STAT(atomic_add_int(&pc_chunk_count, 1));
	PV_STAT(atomic_add_int(&pc_chunk_allocs, 1));
	dump_add_page(m->phys_addr);
	pc = VM_PAGE_TO_DMAP(m);
	pc->pc_pmap = pmap;
	pc->pc_map[0] = PC_FREEN & ~1ul;	/* preallocated bit 0 */
	pc->pc_map[1] = PC_FREEN;
	pc->pc_map[2] = PC_FREEL;
	mtx_lock(&pv_chunks_mutex);
	TAILQ_INSERT_TAIL(&pv_chunks, pc, pc_lru);
	mtx_unlock(&pv_chunks_mutex);
	pv = &pc->pc_pventry[0];
	TAILQ_INSERT_HEAD(&pmap->pm_pvchunk, pc, pc_list);
	PV_STAT(atomic_add_long(&pv_entry_count, 1));
	PV_STAT(atomic_add_int(&pv_entry_spare, _NPCPV - 1));
	return (pv);
}

/*
 * Ensure that the number of spare PV entries in the specified pmap meets or
 * exceeds the given count, "needed".
 *
 * The given PV list lock may be released.
 */
static void
reserve_pv_entries(pmap_t pmap, int needed, struct rwlock **lockp)
{
	struct pch new_tail;
	struct pv_chunk *pc;
	vm_page_t m;
	int avail, free;
	bool reclaimed;

	rw_assert(&pvh_global_lock, RA_LOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	KASSERT(lockp != NULL, ("reserve_pv_entries: lockp is NULL"));

	/*
	 * Newly allocated PV chunks must be stored in a private list until
	 * the required number of PV chunks have been allocated.  Otherwise,
	 * reclaim_pv_chunk() could recycle one of these chunks.  In
	 * contrast, these chunks must be added to the pmap upon allocation.
	 */
	TAILQ_INIT(&new_tail);
retry:
	avail = 0;
	TAILQ_FOREACH(pc, &pmap->pm_pvchunk, pc_list) {
		bit_count((bitstr_t *)pc->pc_map, 0,
		    sizeof(pc->pc_map) * NBBY, &free);
		if (free == 0)
			break;
		avail += free;
		if (avail >= needed)
			break;
	}
	for (reclaimed = false; avail < needed; avail += _NPCPV) {
		m = vm_page_alloc_noobj(VM_ALLOC_WIRED);
		if (m == NULL) {
			m = reclaim_pv_chunk(pmap, lockp);
			if (m == NULL)
				goto retry;
			reclaimed = true;
		}
		PV_STAT(atomic_add_int(&pc_chunk_count, 1));
		PV_STAT(atomic_add_int(&pc_chunk_allocs, 1));
		dump_add_page(m->phys_addr);
		pc = VM_PAGE_TO_DMAP(m);
		pc->pc_pmap = pmap;
		pc->pc_map[0] = PC_FREEN;
		pc->pc_map[1] = PC_FREEN;
		pc->pc_map[2] = PC_FREEL;
		TAILQ_INSERT_HEAD(&pmap->pm_pvchunk, pc, pc_list);
		TAILQ_INSERT_TAIL(&new_tail, pc, pc_lru);
		PV_STAT(atomic_add_int(&pv_entry_spare, _NPCPV));

		/*
		 * The reclaim might have freed a chunk from the current pmap.
		 * If that chunk contained available entries, we need to
		 * re-count the number of available entries.
		 */
		if (reclaimed)
			goto retry;
	}
	if (!TAILQ_EMPTY(&new_tail)) {
		mtx_lock(&pv_chunks_mutex);
		TAILQ_CONCAT(&pv_chunks, &new_tail, pc_lru);
		mtx_unlock(&pv_chunks_mutex);
	}
}

/*
 * First find and then remove the pv entry for the specified pmap and virtual
 * address from the specified pv list.  Returns the pv entry if found and NULL
 * otherwise.  This operation can be performed on pv lists for either 4KB or
 * 2MB page mappings.
 */
static __inline pv_entry_t
pmap_pvh_remove(struct md_page *pvh, pmap_t pmap, vm_offset_t va)
{
	pv_entry_t pv;

	rw_assert(&pvh_global_lock, RA_LOCKED);
	TAILQ_FOREACH(pv, &pvh->pv_list, pv_next) {
		if (pmap == PV_PMAP(pv) && va == pv->pv_va) {
			TAILQ_REMOVE(&pvh->pv_list, pv, pv_next);
			pvh->pv_gen++;
			break;
		}
	}
	return (pv);
}

/*
 * First find and then destroy the pv entry for the specified pmap and virtual
 * address.  This operation can be performed on pv lists for either 4KB or 2MB
 * page mappings.
 */
static void
pmap_pvh_free(struct md_page *pvh, pmap_t pmap, vm_offset_t va)
{
	pv_entry_t pv;

	pv = pmap_pvh_remove(pvh, pmap, va);

	KASSERT(pv != NULL, ("pmap_pvh_free: pv not found for %#lx", va));
	free_pv_entry(pmap, pv);
}

/*
 * Conditionally create the PV entry for a 4KB page mapping if the required
 * memory can be allocated without resorting to reclamation.
 */
static bool
pmap_try_insert_pv_entry(pmap_t pmap, vm_offset_t va, vm_page_t m,
    struct rwlock **lockp)
{
	pv_entry_t pv;

	rw_assert(&pvh_global_lock, RA_LOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	/* Pass NULL instead of the lock pointer to disable reclamation. */
	if ((pv = get_pv_entry(pmap, NULL)) != NULL) {
		pv->pv_va = va;
		CHANGE_PV_LIST_LOCK_TO_VM_PAGE(lockp, m);
		TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_next);
		m->md.pv_gen++;
		return (true);
	} else
		return (false);
}

/*
 * After demotion from a 2MB page mapping to 512 4KB page mappings,
 * destroy the pv entry for the 2MB page mapping and reinstantiate the pv
 * entries for each of the 4KB page mappings.
 */
static void __unused
pmap_pv_demote_l2(pmap_t pmap, vm_offset_t va, vm_paddr_t pa,
    struct rwlock **lockp)
{
	struct md_page *pvh;
	struct pv_chunk *pc;
	pv_entry_t pv;
	vm_page_t m;
	vm_offset_t va_last;
	int bit, field;

	rw_assert(&pvh_global_lock, RA_LOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	CHANGE_PV_LIST_LOCK_TO_PHYS(lockp, pa);

	/*
	 * Transfer the 2mpage's pv entry for this mapping to the first
	 * page's pv list.  Once this transfer begins, the pv list lock
	 * must not be released until the last pv entry is reinstantiated.
	 */
	pvh = pa_to_pvh(pa);
	va &= ~L2_OFFSET;
	pv = pmap_pvh_remove(pvh, pmap, va);
	KASSERT(pv != NULL, ("pmap_pv_demote_l2: pv not found"));
	m = PHYS_TO_VM_PAGE(pa);
	TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_next);
	m->md.pv_gen++;
	/* Instantiate the remaining 511 pv entries. */
	PV_STAT(atomic_add_long(&pv_entry_allocs, Ln_ENTRIES - 1));
	va_last = va + L2_SIZE - PAGE_SIZE;
	for (;;) {
		pc = TAILQ_FIRST(&pmap->pm_pvchunk);
		KASSERT(!pc_is_full(pc), ("pmap_pv_demote_l2: missing spare"));
		for (field = 0; field < _NPCM; field++) {
			while (pc->pc_map[field] != 0) {
				bit = ffsl(pc->pc_map[field]) - 1;
				pc->pc_map[field] &= ~(1ul << bit);
				pv = &pc->pc_pventry[field * 64 + bit];
				va += PAGE_SIZE;
				pv->pv_va = va;
				m++;
				KASSERT((m->oflags & VPO_UNMANAGED) == 0,
			    ("pmap_pv_demote_l2: page %p is not managed", m));
				TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_next);
				m->md.pv_gen++;
				if (va == va_last)
					goto out;
			}
		}
		TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
		TAILQ_INSERT_TAIL(&pmap->pm_pvchunk, pc, pc_list);
	}
out:
	if (pc_is_full(pc)) {
		TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
		TAILQ_INSERT_TAIL(&pmap->pm_pvchunk, pc, pc_list);
	}
	PV_STAT(atomic_add_long(&pv_entry_count, Ln_ENTRIES - 1));
	PV_STAT(atomic_add_int(&pv_entry_spare, -(Ln_ENTRIES - 1)));
}

#if VM_NRESERVLEVEL > 0
static void
pmap_pv_promote_l2(pmap_t pmap, vm_offset_t va, vm_paddr_t pa,
    struct rwlock **lockp)
{
	struct md_page *pvh;
	pv_entry_t pv;
	vm_page_t m;
	vm_offset_t va_last;

	rw_assert(&pvh_global_lock, RA_LOCKED);
	KASSERT((pa & L2_OFFSET) == 0,
	    ("pmap_pv_promote_l2: misaligned pa %#lx", pa));

	CHANGE_PV_LIST_LOCK_TO_PHYS(lockp, pa);

	m = PHYS_TO_VM_PAGE(pa);
	va = va & ~L2_OFFSET;
	pv = pmap_pvh_remove(&m->md, pmap, va);
	KASSERT(pv != NULL, ("pmap_pv_promote_l2: pv for %#lx not found", va));
	pvh = pa_to_pvh(pa);
	TAILQ_INSERT_TAIL(&pvh->pv_list, pv, pv_next);
	pvh->pv_gen++;

	va_last = va + L2_SIZE - PAGE_SIZE;
	do {
		m++;
		va += PAGE_SIZE;
		pmap_pvh_free(&m->md, pmap, va);
	} while (va < va_last);
}
#endif /* VM_NRESERVLEVEL > 0 */

/*
 * Create the PV entry for a 2MB page mapping.  Always returns true unless the
 * flag PMAP_ENTER_NORECLAIM is specified.  If that flag is specified, returns
 * false if the PV entry cannot be allocated without resorting to reclamation.
 */
static bool
pmap_pv_insert_l2(pmap_t pmap, vm_offset_t va, pd_entry_t l2e, u_int flags,
    struct rwlock **lockp)
{
	struct md_page *pvh;
	pv_entry_t pv;
	vm_paddr_t pa;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	/* Pass NULL instead of the lock pointer to disable reclamation. */
	if ((pv = get_pv_entry(pmap, (flags & PMAP_ENTER_NORECLAIM) != 0 ?
	    NULL : lockp)) == NULL)
		return (false);
	pv->pv_va = va;
	pa = PTE_TO_PHYS(l2e);
	CHANGE_PV_LIST_LOCK_TO_PHYS(lockp, pa);
	pvh = pa_to_pvh(pa);
	TAILQ_INSERT_TAIL(&pvh->pv_list, pv, pv_next);
	pvh->pv_gen++;
	return (true);
}

static void
pmap_remove_kernel_l2(pmap_t pmap, pt_entry_t *l2, vm_offset_t va)
{
	pt_entry_t newl2, oldl2 __diagused;
	vm_page_t ml3;
	vm_paddr_t ml3pa;

	KASSERT(!VIRT_IN_DMAP(va), ("removing direct mapping of %#lx", va));
	KASSERT(pmap == kernel_pmap, ("pmap %p is not kernel_pmap", pmap));
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	ml3 = pmap_remove_pt_page(pmap, va);
	if (ml3 == NULL) {
		/*
		 * No tracked L3 page-table page: this 2MB kernel mapping was
		 * entered directly by pmap_enter_l2() -- a fresh superpage that
		 * was never promoted from 4KB pages, so there is no PT page
		 * kept for demotion.  pmap_remove_l2() already cleared the L2
		 * entry and invalidated the TLB; nothing more to do.  This is
		 * the same "no PT page" case that pmap_demote_l2_locked()
		 * anticipates in its success path (it allocates a fresh PT page
		 * there).  Without this, removing/demoting a direct kernel
		 * superpage panicked.
		 */
		static int diag;
		if (diag < 4)
			printf("pmap_remove_kernel_l2: direct 2MB superpage at "
			    "va=%#lx (no tracked PT page) [%d]\n", va, ++diag);
		return;
	}

	ml3pa = VM_PAGE_TO_PHYS(ml3);
	newl2 = (ml3pa >> L3_SHIFT) << L3_SHIFT;

	/*
	 * If this page table page was unmapped by a promotion, then it
	 * contains valid mappings.  Zero it to invalidate those mappings.
	 */
	if (vm_page_any_valid(ml3))
		pagezero(PHYS_TO_DMAP(ml3pa));

	/*
	 * Demote the mapping.
	 */
	oldl2 = pmap_load_store(l2, newl2);
	KASSERT(oldl2 == invalid_pmd_table,
	    ("%s: found existing mapping at %p: %#lx",
	    __func__, l2, oldl2));
}

/*
 * pmap_remove_l2: Do the things to unmap a level 2 superpage.
 */
static int
pmap_remove_l2(pmap_t pmap, pt_entry_t *l2, vm_offset_t sva,
    pd_entry_t l1e, struct spglist *free, struct rwlock **lockp)
{
	struct md_page *pvh;
	pt_entry_t oldl2;
	vm_offset_t eva, va;
	vm_page_t m, ml3;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	KASSERT((sva & L2_OFFSET) == 0, ("pmap_remove_l2: sva is not aligned"));
	oldl2 = pmap_pmd_entry(l2);
	pmap_store(l2, invalid_pmd_table);
	KASSERT((oldl2 & PTE_HUGE),
	    ("pmap_remove_l2: L2e %lx is not a superpage mapping", oldl2));

	/*
	 * An invtlb specifying a single address within a superpage mapping is
	 * sufficient.  However, since we do not perform any invalidation upon
	 * promotion, TLBs may still be caching 4KB mappings within the
	 * superpage, so we must invalidate the entire range.
	 */
	pmap_invalidate_range(pmap, sva, sva + L2_SIZE);

	if ((oldl2 & PTE_SW_WIRED) != 0)
		pmap->pm_stats.wired_count -= L2_SIZE / PAGE_SIZE;
	pmap_resident_count_dec(pmap, L2_SIZE / PAGE_SIZE);

	if ((oldl2 & PTE_SW_MANAGED) != 0) {
		CHANGE_PV_LIST_LOCK_TO_PHYS(lockp, PTE_TO_PHYS(oldl2));
		pvh = pa_to_pvh(PTE_TO_PHYS(oldl2));
		pmap_pvh_free(pvh, pmap, sva);
		eva = sva + L2_SIZE;
		for (va = sva, m = PTE_TO_VM_PAGE(oldl2);
		    va < eva; va += PAGE_SIZE, m++) {
			if ((oldl2 & PTE_D) != 0)
				vm_page_dirty(m);
			if ((oldl2 & PTE_A) != 0)
				vm_page_aflag_set(m, PGA_REFERENCED);
			if (TAILQ_EMPTY(&m->md.pv_list) &&
			    TAILQ_EMPTY(&pvh->pv_list))
				vm_page_aflag_clear(m, PGA_WRITEABLE);
		}
	}
	if (pmap == kernel_pmap) {
		pmap_remove_kernel_l2(pmap, l2, sva);
	} else {
		ml3 = pmap_remove_pt_page(pmap, sva);
		if (ml3 != NULL) {
			KASSERT(vm_page_any_valid(ml3),
			    ("pmap_remove_l2: l3 page not promoted"));
			pmap_resident_count_dec(pmap, 1);
			KASSERT(ml3->ref_count == Ln_ENTRIES,
			    ("pmap_remove_l2: l3 page ref count error"));
			ml3->ref_count = 1;
			vm_page_unwire_noq(ml3);
			pmap_add_delayed_free_list(ml3, free, false);
		}
	}
	return (pmap_unuse_pt(pmap, sva, l1e, free));
}

/*
 * pmap_remove_l3: do the things to unmap a page in a process
 */
static int
pmap_remove_l3(pmap_t pmap, pt_entry_t *l3, vm_offset_t va,
    pd_entry_t l2e, struct spglist *free, struct rwlock **lockp,
    bool invalidate)
{
	struct md_page *pvh;
	pt_entry_t old_l3;
	vm_page_t m;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	old_l3 = pmap_load_clear(l3);
	/*
	 * Caller-driven: pmap_remove batches invalidation across the whole run
	 * (pmap_invalidate_range/all in its outer loop), so it passes false to
	 * avoid a redundant whole-ASID shootdown per page.  pmap_enter_l2 has
	 * no surrounding batch, so it passes true to invalidate each evicted
	 * 4KB entry here.
	 */
	if (invalidate)
		pmap_invalidate_page(pmap, va);

	if (old_l3 & PTE_SW_WIRED)
		pmap->pm_stats.wired_count -= 1;
	pmap_resident_count_dec(pmap, 1);

	if (old_l3 & PTE_SW_MANAGED) {
		m = PTE_TO_VM_PAGE(old_l3);
		if ((old_l3 & PTE_D) != 0)
			vm_page_dirty(m);
		if (old_l3 & PTE_A)
			vm_page_aflag_set(m, PGA_REFERENCED);
		CHANGE_PV_LIST_LOCK_TO_VM_PAGE(lockp, m);
		pmap_pvh_free(&m->md, pmap, va);
		if (TAILQ_EMPTY(&m->md.pv_list) &&
		    (m->flags & PG_FICTITIOUS) == 0) {
			pvh = pa_to_pvh(VM_PAGE_TO_PHYS(m));
			if (TAILQ_EMPTY(&pvh->pv_list))
				vm_page_aflag_clear(m, PGA_WRITEABLE);
		}
	}

	return (pmap_unuse_pt(pmap, va, l2e, free));
}

/*
 *	Remove the given range of addresses from the specified map.
 *
 *	It is assumed that the start and end are properly
 *	rounded to the page size.
 */
void
pmap_remove(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	struct spglist free;
	struct rwlock *lock;
	vm_offset_t va, va_next;
	pd_entry_t *l0, *l1, *l2, l2e;
	pt_entry_t *l3;
	int nflush;
	bool flush_all;

	/*
	 * Perform an unsynchronized read.  This is, however, safe.
	 */
	if (pmap->pm_stats.resident_count == 0)
		return;

	SLIST_INIT(&free);

	rw_rlock(&pvh_global_lock);
	PMAP_LOCK(pmap);

	lock = NULL;
	nflush = 0;
	flush_all = false;
	for (; sva < eva; sva = va_next) {
		if (pmap->pm_stats.resident_count == 0)
			break;

		l0 = pmap_l0(pmap, sva);
		if (!pmap_pgd_valid(l0)) {
			va_next = (sva + L0_SIZE) & ~L0_OFFSET;
			if (va_next < sva)
				va_next = eva;
			continue;
		}

		l1 = pmap_l0_to_l1(l0, sva);

		if (!pmap_pud_valid(l1)) {
			va_next = (sva + L1_SIZE) & ~L1_OFFSET;
			if (va_next < sva)
				va_next = eva;
			continue;
		}

		/*
		 * Calculate index for next page table.
		 */
		va_next = (sva + L2_SIZE) & ~L2_OFFSET;
		if (va_next < sva)
			va_next = eva;

		l2 = pmap_l1_to_l2(l1, sva);
		if (!pmap_pmd_valid(l2))
			continue;
		l2e = pmap_pmd_entry(l2);
		if (l2e & PTE_HUGE) {
			if (sva + L2_SIZE == va_next && eva >= va_next) {
				(void)pmap_remove_l2(pmap, l2, sva,
				    pmap_pud_entry(l1), &free, &lock);
				continue;
			} else if (!pmap_demote_l2_locked(pmap, l2, sva,
			    &lock)) {
				/*
				 * The large page mapping was destroyed.
				 */
				continue;
			}
			l2e = pmap_pmd_entry(l2);
		}

		/*
		 * Limit our scan to either the end of the va represented
		 * by the current page table page, or to the end of the
		 * range being removed.
		 */
		if (va_next > eva)
			va_next = eva;

		va = va_next;
		for (l3 = pmap_l2_to_l3(l2, sva); sva != va_next; l3++,
		    sva += L3_SIZE) {
			if (!pmap_pte_valid(l3)) {
				if (va != va_next) {
					nflush += (sva - va) / L3_SIZE;
					if (pmap_tlb_ipi_threshold > 0 &&
					    nflush >= pmap_tlb_ipi_threshold)
						flush_all = true;
					if (!flush_all)
						pmap_invalidate_range(pmap,
						    va, sva);
					va = va_next;
				}
				continue;
			}
			if (va == va_next)
				va = sva;
			if (pmap_remove_l3(pmap, l3, sva, l2e, &free, &lock,
			    false)) {
				sva += L3_SIZE;
				break;
			}
		}
		if (va != va_next) {
			nflush += (sva - va) / L3_SIZE;
			if (pmap_tlb_ipi_threshold > 0 &&
			    nflush >= pmap_tlb_ipi_threshold)
				flush_all = true;
			if (!flush_all)
				pmap_invalidate_range(pmap, va, sva);
		}
	}
	if (flush_all)
		pmap_invalidate_all(pmap);
	if (lock != NULL)
		rw_wunlock(lock);
	rw_runlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
	vm_page_free_pages_toq(&free, false);
}

/*
 *	Routine:	pmap_remove_all
 *	Function:
 *		Removes this physical page from
 *		all physical maps in which it resides.
 *		Reflects back modify bits to the pager.
 *
 *	Notes:
 *		Original versions of this routine were very
 *		inefficient because they iteratively called
 *		pmap_remove (slow...)
 */

void
pmap_remove_all(vm_page_t m)
{
	struct spglist free;
	struct md_page *pvh;
	pmap_t pmap;
	pt_entry_t *l3, l3e;
	pd_entry_t *l2, l2e __diagused;
	pv_entry_t pv;
	vm_offset_t va;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_remove_all: page %p is not managed", m));
	SLIST_INIT(&free);
	pvh = (m->flags & PG_FICTITIOUS) != 0 ? &pv_dummy :
	    pa_to_pvh(VM_PAGE_TO_PHYS(m));

	rw_wlock(&pvh_global_lock);
	while ((pv = TAILQ_FIRST(&pvh->pv_list)) != NULL) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		va = pv->pv_va;
		l2 = pmap_l2(pmap, va);
		(void)pmap_demote_l2(pmap, l2, va);
		PMAP_UNLOCK(pmap);
	}
	while ((pv = TAILQ_FIRST(&m->md.pv_list)) != NULL) {
		pmap = PV_PMAP(pv);
		PMAP_LOCK(pmap);
		pmap_resident_count_dec(pmap, 1);
		l2 = pmap_l2(pmap, pv->pv_va);
		KASSERT(l2 != NULL, ("pmap_remove_all: no l2 table found"));
		l2e = pmap_pmd_entry(l2);

		KASSERT(!(l2e & PTE_HUGE),
		    ("pmap_remove_all: found a superpage in %p's pv list", m));

		l3 = pmap_l2_to_l3(l2, pv->pv_va);
		l3e = pmap_load_clear(l3);
		pmap_invalidate_page(pmap, pv->pv_va);
		if (l3e & PTE_SW_WIRED)
			pmap->pm_stats.wired_count--;
		if ((l3e & PTE_A) != 0)
			vm_page_aflag_set(m, PGA_REFERENCED);

		/*
		 * Update the vm_page_t clean and reference bits.
		 */
		if ((l3e & PTE_D) != 0)
			vm_page_dirty(m);
		pmap_unuse_pt(pmap, pv->pv_va, pmap_pmd_entry(l2), &free);
		TAILQ_REMOVE(&m->md.pv_list, pv, pv_next);
		m->md.pv_gen++;
		free_pv_entry(pmap, pv);
		PMAP_UNLOCK(pmap);
	}
	vm_page_aflag_clear(m, PGA_WRITEABLE);
	rw_wunlock(&pvh_global_lock);
	vm_page_free_pages_toq(&free, false);
}

/*
 *	Set the physical protection on the
 *	specified range of this map as requested.
 */
void
pmap_protect(pmap_t pmap, vm_offset_t sva, vm_offset_t eva, vm_prot_t prot)
{
	pd_entry_t *l0, *l1, *l2, l2e;
	pt_entry_t *l3, l3e, mask, setmask;
	vm_page_t m, mt;
	vm_paddr_t pa;
	vm_offset_t va_next;
	bool anychanged, pv_lists_locked;

	if ((prot & VM_PROT_READ) == VM_PROT_NONE) {
		pmap_remove(pmap, sva, eva);
		return;
	}

	if ((prot & (VM_PROT_WRITE | VM_PROT_EXECUTE)) ==
	    (VM_PROT_WRITE | VM_PROT_EXECUTE))
		return;

	anychanged = false;
	pv_lists_locked = false;
	mask = 0;
	setmask = 0;
	if ((prot & VM_PROT_WRITE) == 0)
		mask |= PTE_W | PTE_D;
	if ((prot & VM_PROT_EXECUTE) == 0)
		setmask |= PTE_NX;
	else
		mask |= PTE_NX;
resume:
	PMAP_LOCK(pmap);
	for (; sva < eva; sva = va_next) {
		l0 = pmap_l0(pmap, sva);
		if (!pmap_pgd_valid(l0)) {
			va_next = (sva + L0_SIZE) & ~L0_OFFSET;
			if (va_next < sva)
				va_next = eva;
			continue;
		}

		l1 = pmap_l0_to_l1(l0, sva);

		if (!pmap_pud_valid(l1)) {
			va_next = (sva + L1_SIZE) & ~L1_OFFSET;
			if (va_next < sva)
				va_next = eva;
			continue;
		}

		va_next = (sva + L2_SIZE) & ~L2_OFFSET;
		if (va_next < sva)
			va_next = eva;

		l2 = pmap_l1_to_l2(l1, sva);
		if (!pmap_pmd_valid(l2))
			continue;
		l2e = pmap_pmd_entry(l2);
		if (l2e & PTE_HUGE) {
			if (sva + L2_SIZE == va_next && eva >= va_next) {
retryl2:
				if ((prot & VM_PROT_WRITE) == 0 &&
				    (l2e & (PTE_SW_MANAGED | PTE_D)) ==
				    (PTE_SW_MANAGED | PTE_D)) {
					pa = PTE_TO_PHYS(l2e);
					m = PHYS_TO_VM_PAGE(pa);
					for (mt = m; mt < &m[Ln_ENTRIES]; mt++)
						vm_page_dirty(mt);
				}
				if (!pmap_cmpset(l2, &l2e,
				    (l2e & ~mask) | setmask))
					goto retryl2;
				anychanged = true;
				continue;
			} else {
				if (!pv_lists_locked) {
					pv_lists_locked = true;
					if (!rw_try_rlock(&pvh_global_lock)) {
						if (anychanged)
							pmap_invalidate_all(
							    pmap);
						PMAP_UNLOCK(pmap);
						rw_rlock(&pvh_global_lock);
						goto resume;
					}
				}
				if (!pmap_demote_l2(pmap, l2, sva)) {
					/*
					 * The large page mapping was destroyed.
					 */
					continue;
				}
			}
		}

		if (va_next > eva)
			va_next = eva;

		for (l3 = pmap_l2_to_l3(l2, sva); sva != va_next; l3++,
		    sva += L3_SIZE) {
			l3e = pmap_pte_entry(l3);
retryl3:
			if (!pmap_pte_valid(l3))
				continue;
			if ((prot & VM_PROT_WRITE) == 0 &&
			    (l3e & (PTE_SW_MANAGED | PTE_D)) ==
			    (PTE_SW_MANAGED | PTE_D)) {
				m = PTE_TO_VM_PAGE(l3e);
				vm_page_dirty(m);
			}
			if (!pmap_cmpset(l3, &l3e, (l3e & ~mask) | setmask))
				goto retryl3;
			anychanged = true;
		}
	}
	if (anychanged)
		pmap_invalidate_all(pmap);
	if (pv_lists_locked)
		rw_runlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
}

/*
 * Install the leaf PTE for va directly into the TLB, bypassing the hardware
 * page walker.
 * Most callers (pmap_enter, pmap_fault) hold PMAP_LOCK.  pmap_kenter and
 * pmap_qenter do not; they rely on the caller guaranteeing exclusive access
 * to the VA range being mapped.
 */
bool
pmap_update_tlb(pmap_t pmap, vm_offset_t va)
{
	pd_entry_t *leaf;
	pt_entry_t elo0, elo1;
	uint64_t ehi, idx, ps;
	register_t flags;

	if (pmap != kernel_pmap && pmap != PCPU_GET(curpmap))
		goto out;

	/*
	 * Walk to the faulting leaf and validate IT -- not its even partner. An
	 * odd-page fault frequently has the even partner unmapped; validating
	 * the partner would make us return without installing anything while
	 * pmap_fault() reports the fault resolved, so the access re-faults
	 * forever (observed as a hang at init).  After validating the faulting
	 * leaf, step back to the even partner so tlbfill installs the correct
	 * even/odd pair (the partner slot may legitimately be invalid).
	 */
	leaf = pmap_l1(pmap, va);
	if (leaf != NULL && pmap_pud_valid(leaf) &&
	    (pmap_pud_entry(leaf) & PTE_HUGE) != 0) {
		ps = 30;
		if ((va & (1UL << 30)) != 0)
			leaf--;
	} else {
		leaf = pmap_l2(pmap, va);
		if (leaf == NULL || !pmap_pmd_valid(leaf))
			goto out;
		if ((pmap_pmd_entry(leaf) & PTE_HUGE) != 0) {
			ps = 21;
			if ((va & (1UL << 21)) != 0)
				leaf--;
		} else {
			leaf = pmap_l3(pmap, va);
			if (leaf == NULL || !pmap_pte_valid(leaf))
				goto out;
			ps = 12;
			if ((va & L3_SIZE) != 0)
				leaf--;
		}
	}
	elo0 = pmap_load(leaf);
	elo1 = pmap_load(leaf + 1);
	if (ps > 12) {
		if ((elo0 & PTE_HUGE_G) == 0)
			elo0 &= ~PTE_G;

		if ((elo1 & PTE_HUGE_G) == 0)
			elo1 &= ~PTE_G;
	}

	flags = intr_disable();
	ehi = va & ~((2UL << ps) - 1);
	__asm __volatile("csrwr %0, %1" : "+r"(ehi) :
	    "i"(LOONGARCH_CSR_TLBEHI));
	__asm __volatile("tlbsrch");
	__asm __volatile("csrrd %0, %1" : "=r"(idx) :
	    "i"(LOONGARCH_CSR_TLBIDX));
	idx = (idx & ~CSR_TLBIDX_PS) | (ps << CSR_TLBIDX_PS_SHIFT);
	__asm __volatile("csrwr %0, %1" : "+r"(idx) :
	    "i"(LOONGARCH_CSR_TLBIDX));
	__asm __volatile("csrwr %0, %1" : "+r"(elo0) :
	    "i"(LOONGARCH_CSR_TLBELO0));
	__asm __volatile("csrwr %0, %1" : "+r"(elo1) :
	    "i"(LOONGARCH_CSR_TLBELO1));
	if ((idx & CSR_TLBIDX_EHINV) != 0) {
		__asm __volatile("tlbfill");
		TLB_DBG("pmap_update_tlb: cpu%d pid=%d (%s) tlbfill"
		    " va=%#lx ps=%lu ELO0=%#lx ELO1=%#lx\n",
		    PCPU_GET(cpuid), curproc->p_pid, curproc->p_comm,
		    va, ps, (unsigned long)elo0, (unsigned long)elo1);
	} else {
		__asm __volatile("tlbwr");
		TLB_DBG("pmap_update_tlb: cpu%d pid=%d (%s) tlbwr"
		    " va=%#lx ps=%lu ELO0=%#lx ELO1=%#lx\n",
		    PCPU_GET(cpuid), curproc->p_pid, curproc->p_comm,
		    va, ps, (unsigned long)elo0, (unsigned long)elo1);
	}
	intr_restore(flags);
	return (true);

out:
	/* PTE gone — evict any stale TLB entry for va.
	 * invtlb's op (sub-code) is an asm "i" immediate, so the kernel/user
	 * op must be a compile-time constant -- branch in C, not via ?: . */
	if (pmap == kernel_pmap)
		invtlb(INVTLB_ADDR_GTRUE_OR_ASID, pmap_current_asid(), va);
	else
		invtlb(INVTLB_ADDR_GFALSE_AND_ASID,
		    pmap_get_asid(pmap, PCPU_GET(cpuid)), va);
	return (false);
}

int
pmap_fault(pmap_t pmap, vm_offset_t va, vm_prot_t ftype)
{
	pd_entry_t *l1, *l2, l2e;
	pt_entry_t bits, *pte, oldpte;
	int rv;

	KASSERT(VIRT_IS_VALID(va), ("pmap_fault: invalid va %#lx", va));

	rv = 0;
	PMAP_LOCK(pmap);
	l1 = pmap_l1(pmap, va);
	if (l1 != NULL && pmap_pud_valid(l1) &&
	    (pmap_pud_entry(l1) & PTE_HUGE) != 0) {
		/* 1GB leaf at L1 (e.g. the DMAP). */
		pte = l1;
		oldpte = pmap_pud_entry(l1);
	} else {
		l2 = pmap_l2(pmap, va);
		if (l2 == NULL || !pmap_pmd_valid(l2))
			goto done;
		l2e = pmap_pmd_entry(l2);
		if (!(l2e & PTE_HUGE)) {
			pte = pmap_l2_to_l3(l2, va);
			oldpte = pmap_pte_entry(pte);
			if (!pmap_pte_valid(pte))
				goto done;
		} else {
			pte = l2;
			oldpte = l2e;
		}
	}

	if ((pmap != kernel_pmap && (oldpte & PTE_U) == 0) ||
	    (ftype == VM_PROT_WRITE && (oldpte & PTE_W) == 0) ||
	    (ftype == VM_PROT_EXECUTE && (oldpte & PTE_NX) == PTE_NX) ||
	    (ftype == VM_PROT_READ && (oldpte & PTE_NR) == PTE_NR))
		goto done;

	bits = PTE_A;
	if (ftype == VM_PROT_WRITE)
		bits |= PTE_D;

	/*
	 * Spurious faults can occur if the implementation caches invalid
	 * entries in the TLB, or if simultaneous accesses on multiple CPUs
	 * race with each other.
	 */
	if ((oldpte & bits) != bits)
		pmap_store_bits(pte, bits);

	pmap_update_tlb(pmap, va);

	rv = 1;
done:
	PMAP_UNLOCK(pmap);
	/*
	 * Sync the i-cache on all cpus after updating the PTE
	 * if the new PTE is executable.  Must be done outside the
	 * PMAP lock to avoid deadlock with remote CPUs that may be
	 * spinning on PV list locks with interrupts disabled.
	 */
	if (ftype & VM_PROT_EXECUTE)
		pmap_sync_icache(pmap, va, PAGE_SIZE);
	return (rv);
}

/*
 *	Demote the specified L1 page to separate L2 pages.
 *	Currently only used for DMAP entries.
 */
static bool
pmap_demote_l1(pmap_t pmap, pd_entry_t *l1, vm_offset_t va)
{
	vm_page_t m;
	pt_entry_t *l2, oldl1, newl2;
	pd_entry_t newl1;
	vm_paddr_t l2phys;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	oldl1 = pmap_pud_entry(l1);
	KASSERT((oldl1 & PTE_HUGE) != 0,
	    ("pmap_demote_l1: oldl1 is not a leaf PTE"));
	KASSERT((oldl1 & PTE_A) != 0,
	    ("pmap_demote_l1: oldl1 is missing PTE_A"));
	KASSERT((oldl1 & (PTE_D | PTE_W)) != PTE_W,
	    ("pmap_demote_l1: not dirty!"));
	KASSERT((oldl1 & PTE_SW_MANAGED) == 0,
	    ("pmap_demote_l1: L1 table shouldn't be managed"));
	KASSERT(VIRT_IN_DMAP(va),
	    ("pmap_demote_l1: is unsupported for non-DMAP va=%#lx", va));

	/* Demoting L1 means we need to allocate a new page-table page. */
	m = vm_page_alloc_noobj(VM_ALLOC_INTERRUPT | VM_ALLOC_WIRED);
	if (m == NULL) {
		CTR2(KTR_PMAP, "pmap_demote_l1: failure for va %#lx in pmap %p",
		    va, pmap);
		return (false);
	}

	l2phys = VM_PAGE_TO_PHYS(m);
	l2 = (pt_entry_t *)PHYS_TO_DMAP(l2phys);

	/*
	 * Create new entries, relying on the fact that only the low bits
	 * (index) of the physical address are changing.
	 */
	newl2 = oldl1;
	for (int i = 0; i < Ln_ENTRIES; i++)
		pmap_store(&l2[i], newl2 | (i << L2_SHIFT));

	/*
	 * And update the L1 entry.
	 *
	 * NB: flushing the TLB is the responsibility of the caller. Cached
	 * translations are still "correct" for demoted mappings until some
	 * subset of the demoted range is modified.
	 */
	newl1 = ((l2phys >> PAGE_PFN_SHIFT) << L3_SHIFT);
	pmap_store(l1, newl1);

	counter_u64_add(pmap_l1_demotions, 1);
	CTR2(KTR_PMAP, "pmap_demote_l1: success for va %#lx in pmap %p",
	    va, pmap);
	return (true);
}

static bool
pmap_demote_l2(pmap_t pmap, pd_entry_t *l2, vm_offset_t va)
{
	struct rwlock *lock;
	bool rv;

	lock = NULL;
	rv = pmap_demote_l2_locked(pmap, l2, va, &lock);
	if (lock != NULL)
		rw_wunlock(lock);
	return (rv);
}

/*
 * Tries to demote a 2MB page mapping.  If demotion fails, the 2MB page
 * mapping is invalidated.
 */
static bool
pmap_demote_l2_locked(pmap_t pmap, pd_entry_t *l2, vm_offset_t va,
    struct rwlock **lockp)
{
	struct spglist free;
	vm_page_t mpte;
	pd_entry_t newl2, oldl2;
	pt_entry_t *firstl3, newl3;
	vm_paddr_t mptepa;
	int i;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	oldl2 = pmap_pmd_entry(l2);
	KASSERT((oldl2 & PTE_HUGE),
	    ("pmap_demote_l2_locked: oldl2 is not a leaf entry"));
	if ((oldl2 & PTE_A) == 0 ||
	    (mpte = pmap_remove_pt_page(pmap, va)) == NULL) {
		KASSERT((oldl2 & PTE_SW_WIRED) == 0,
			("pmap_demote_l2_locked: page table page for a "
			"wired mapping is missing"));
		if ((oldl2 & PTE_A) == 0 || (mpte = vm_page_alloc_noobj(
		    (VIRT_IN_DMAP(va) ? VM_ALLOC_INTERRUPT : 0) |
		    VM_ALLOC_WIRED)) == NULL) {
			SLIST_INIT(&free);
			pmap_remove_l2(pmap, l2, va & ~L2_OFFSET,
			    pmap_pud_entry(pmap_l1(pmap, va)), &free, lockp);
			vm_page_free_pages_toq(&free, true);
			CTR2(KTR_PMAP, "pmap_demote_l2_locked: "
			    "failure for va %#lx in pmap %p", va, pmap);
			return (false);
		}
		mpte->pindex = pmap_l2_pindex(va);
		if (va < VM_MAXUSER_ADDRESS) {
			mpte->ref_count = Ln_ENTRIES;
			pmap_resident_count_inc(pmap, 1);
		}
	}
	mptepa = VM_PAGE_TO_PHYS(mpte);
	firstl3 = (pt_entry_t *)PHYS_TO_DMAP(mptepa);
	newl2 = ((mptepa >> PAGE_PFN_SHIFT) << L3_SHIFT);
	KASSERT((oldl2 & PTE_A) != 0,
	    ("pmap_demote_l2_locked: oldl2 is missing PTE_A"));
	KASSERT((oldl2 & (PTE_D | PTE_W)) != PTE_W,
	    ("pmap_demote_l2_locked: oldl2 is missing PTE_D"));

	newl3 = oldl2;
	newl3 &= ~PTE_HUGE;
	newl3 &= ~PTE_HUGE_G;
	if (oldl2 & PTE_HUGE_G) {
		newl3 |= PTE_G;
	}

	/*
	 * If the page table page is not leftover from an earlier promotion,
	 * initialize it.
	 */
	if (!vm_page_all_valid(mpte)) {
		for (i = 0; i < Ln_ENTRIES; i++)
			pmap_store(firstl3 + i, newl3 + (i << L3_SHIFT));
	}
	KASSERT(PTE_TO_PHYS(pmap_pte_entry(firstl3)) == PTE_TO_PHYS(newl3),
	    ("pmap_demote_l2_locked: firstl3 and newl3 map different physical "
	    "addresses"));

	/*
	 * If the mapping has changed attributes, update the PTEs.
	 */
	if ((pmap_pte_entry(firstl3) & PTE_PROMOTE) != (newl3 & PTE_PROMOTE))
		for (i = 0; i < Ln_ENTRIES; i++)
			pmap_store(firstl3 + i, newl3 + (i << L3_SHIFT));

	/*
	 * The spare PV entries must be reserved prior to demoting the
	 * mapping, that is, prior to changing the L2 entry.  Otherwise, the
	 * state of the L2 entry and the PV lists will be inconsistent, which
	 * can result in reclaim_pv_chunk() attempting to remove a PV entry from
	 * the wrong PV list and pmap_pv_demote_l2() failing to find the
	 * expected PV entry for the 2MB page mapping that is being demoted.
	 */

	if ((oldl2 & PTE_SW_MANAGED) != 0)
		reserve_pv_entries(pmap, Ln_ENTRIES - 1, lockp);

	/*
	 * Demote the mapping.  Unlike arm64, we cannot use break-before-
	 * make here: LoongArch directory entries are bare physical
	 * addresses with no valid bit, so writing the sentinel makes the
	 * HW page walker descend into the sentinel page and fault.  If
	 * the faulting address is a kernel mapping whose pmap lock we
	 * already hold, that fault deadlocks.  Instead, atomically replace
	 * the L2 entry and invalidate the stale superpage TLB afterwards.
	 * The new L3 table was fully populated above, so any concurrent
	 * walker observes a correct 4KB mapping once it reloads the L2.
	 */
	pmap_store(l2, newl2);
	pmap_invalidate_range(pmap, va & ~L2_OFFSET,
	    (va & ~L2_OFFSET) + L2_SIZE);

	/*
	 * Demote the PV entry.
	 */
	if ((oldl2 & PTE_SW_MANAGED) != 0)
		pmap_pv_demote_l2(pmap, va, PTE_TO_PHYS(oldl2), lockp);

	atomic_add_long(&pmap_l2_demotions, 1);
	CTR2(KTR_PMAP, "pmap_demote_l2_locked: success for va %#lx in pmap %p",
	    va, pmap);
	return (true);
}

#if VM_NRESERVLEVEL > 0
static bool
pmap_promote_l2(pmap_t pmap, pd_entry_t *l2, vm_offset_t va, vm_page_t ml3,
    struct rwlock **lockp)
{
	pt_entry_t *firstl3, firstl3e, *l3, l3e;
	pt_entry_t newl2, l3_all_PTE_A;
	vm_paddr_t pa;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	if (!pmap_ps_enabled(pmap))
		return (false);

	KASSERT(!(pmap_pmd_entry(l2) & PTE_HUGE),
	    ("pmap_promote_l2: invalid l2 entry %p", l2));

	/*
	 * Examine the first L3E in the specified PTP.  Abort if this L3E is
	 * ineligible for promotion or does not map the first 4KB physical page
	 * within a 2MB page.
	 */
	firstl3 = (pt_entry_t *)PHYS_TO_DMAP(PTE_TO_PHYS(pmap_pmd_entry(l2)));
	firstl3e = pmap_pte_entry(firstl3);
	pa = PTE_TO_PHYS(firstl3e);
	if ((pa & L2_OFFSET) != 0) {
		CTR2(KTR_PMAP, "pmap_promote_l2: failure for va %#lx pmap %p",
		    va, pmap);
		atomic_add_long(&pmap_l2_p_failures, 1);
		return (false);
	}

	/*
	 * Downgrade a clean, writable mapping to read-only to ensure that the
	 * hardware does not set PTE_D while we are comparing PTEs.
	 *
	 * Upon a write access to a clean mapping, the implementation will
	 * either atomically check protections and set PTE_D, or raise a page
	 * fault.  In the latter case, the pmap lock provides atomicity.  Thus,
	 * we do not issue an invtlb here and instead rely on pmap_fault()
	 * to do so lazily.
	 */
	while ((firstl3e & (PTE_W | PTE_D)) == PTE_W) {
		if (pmap_cmpset(firstl3, &firstl3e, firstl3e & ~PTE_W)) {
			firstl3e &= ~PTE_W;
			break;
		}
	}

	pa += L2_SIZE - PAGE_SIZE;
	l3_all_PTE_A = firstl3e & PTE_A;
	for (l3 = firstl3 + Ln_ENTRIES - 1; l3 > firstl3; l3--) {
		l3e = pmap_pte_entry(l3);
		if (PTE_TO_PHYS(l3e) != pa) {
			CTR2(KTR_PMAP,
			    "pmap_promote_l2: failure for va %#lx pmap %p",
			    va, pmap);
			atomic_add_long(&pmap_l2_p_failures, 1);
			return (false);
		}
		while ((l3e & (PTE_W | PTE_D)) == PTE_W) {
			if (pmap_cmpset(l3, &l3e, l3e & ~PTE_W)) {
				l3e &= ~PTE_W;
				break;
			}
		}
		if ((l3e & PTE_PROMOTE) != (firstl3e & PTE_PROMOTE)) {
			CTR2(KTR_PMAP,
			    "pmap_promote_l2: failure for va %#lx pmap %p",
			    va, pmap);
			atomic_add_long(&pmap_l2_p_failures, 1);
			return (false);
		}
		pa -= PAGE_SIZE;
		l3_all_PTE_A &= l3e;
	}

	/*
	 * Unless all PTEs have PTE_A set, clear it from the superpage
	 * mapping, so that promotions triggered by speculative mappings,
	 * such as pmap_enter_quick(), don't automatically mark the
	 * underlying pages as referenced.
	 */
	firstl3e &= ~PTE_A | l3_all_PTE_A;

	/*
	 * Save the page table page in its current state until the L2
	 * mapping the superpage is demoted by pmap_demote_l2() or
	 * destroyed by pmap_remove_l3().
	 */
	if (ml3 == NULL)
		ml3 = PTE_TO_VM_PAGE(pmap_pmd_entry(l2));
	KASSERT(ml3->pindex == pmap_l2_pindex(va),
	    ("pmap_promote_l2: page table page's pindex is wrong"));
	if (pmap_insert_pt_page(pmap, ml3, true, l3_all_PTE_A != 0)) {
		CTR2(KTR_PMAP, "pmap_promote_l2: failure for va %#lx pmap %p",
		    va, pmap);
		atomic_add_long(&pmap_l2_p_failures, 1);
		return (false);
	}

	if ((firstl3e & PTE_SW_MANAGED) != 0)
		pmap_pv_promote_l2(pmap, va, PTE_TO_PHYS(firstl3e), lockp);

	/*
	 * Promote an L3 leaf PTE into a 2MiB L2 leaf.
	 * LoongArch's L2-leaf marker (PTE_HUGE, bit 6) is NOT present
	 * on L3 entries, so it must be added here; otherwise the
	 * promoted L2 entry is mistaken for a page-table pointer.
	 * Convert the L3 global bit (PTE_G) to the huge-page global
	 * marker (PTE_HUGE_G), the inverse of pmap_demote_l2_locked.
	 */

	newl2 = firstl3e;

	if ((firstl3e & PTE_G) != 0) {
		newl2 &= ~PTE_G;
		newl2 |= PTE_HUGE_G;
		KASSERT(va >= VM_MIN_KERNEL_ADDRESS,
			("user pmap with G bit, pmap: %p, va: 0x%lx\n",
			pmap, va));
	}
	newl2 |= PTE_HUGE;

	pmap_store(l2, newl2);
	KASSERT((pmap_pmd_entry(l2) & PTE_HUGE) != 0,
		("pmap_promote_l2: promoted L2 leaf missing PTE_HUGE, "
		"va %#lx l2e %#lx", va, pmap_pmd_entry(l2)));

	pmap_invalidate_range(pmap, va & ~L2_OFFSET,
	    (va & ~L2_OFFSET) + L2_SIZE);

	atomic_add_long(&pmap_l2_promotions, 1);
	CTR2(KTR_PMAP, "pmap_promote_l2: success for va %#lx in pmap %p", va,
	    pmap);

	return (true);
}
#endif

/*
 *	Insert the given physical page (p) at
 *	the specified virtual address (v) in the
 *	target physical map with the protection requested.
 *
 *	If specified, the page will be wired down, meaning
 *	that the related pte can not be reclaimed.
 *
 *	NB:  This is the only routine which MAY NOT lazy-evaluate
 *	or lose information.  That is, this routine must actually
 *	insert this page into the given map NOW.
 */

int
pmap_enter(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot,
    u_int flags, int8_t psind)
{
	struct rwlock *lock;
	pd_entry_t *l2;
	pt_entry_t new_l3, orig_l3;
	pt_entry_t *l3;
	pv_entry_t pv;
	vm_paddr_t opa, pa;
	vm_page_t mpte, om;
	pn_t pn;
	int rv;
	bool nosleep;

	va = trunc_page(va);
	if ((m->oflags & VPO_UNMANAGED) == 0)
		VM_PAGE_OBJECT_BUSY_ASSERT(m);
	pa = VM_PAGE_TO_PHYS(m);
	pn = pa >> PAGE_PFN_SHIFT;

	new_l3 = PTE_V | PTE_P | PTE_CC | PTE_NX | PTE_K;
	if (prot & VM_PROT_EXECUTE)
		new_l3 &= ~PTE_NX;
	if (flags & VM_PROT_WRITE)
		new_l3 |= PTE_D;
	if (prot & VM_PROT_WRITE)
		new_l3 |= PTE_W;
	if (va < VM_MAX_USER_ADDRESS)
		new_l3 |= PTE_U;

	/* for kernel mapping, add G bit */
	if (pmap == kernel_pmap)
		new_l3 |= PTE_G;

	new_l3 |= (pn << L3_SHIFT);
	if ((flags & PMAP_ENTER_WIRED) != 0)
		new_l3 |= PTE_SW_WIRED;

	/*
	 * Set modified bit gratuitously for writeable mappings if
	 * the page is unmanaged. We do not want to take a fault
	 * to do the dirty bit accounting for these mappings.
	 */
	if ((m->oflags & VPO_UNMANAGED) != 0) {
		if (prot & VM_PROT_WRITE)
			new_l3 |= PTE_D;
	} else
		new_l3 |= PTE_SW_MANAGED;

	CTR2(KTR_PMAP, "pmap_enter: %.16lx -> %.16lx", va, pa);

	lock = NULL;
	mpte = NULL;
	rw_rlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	if (psind == 1) {
		/* Assert the required virtual and physical alignment. */
		KASSERT((va & L2_OFFSET) == 0,
		    ("pmap_enter: va %#lx unaligned", va));
		KASSERT(m->psind > 0, ("pmap_enter: m->psind < psind"));
		rv = pmap_enter_l2(pmap, va, new_l3, flags, m, &lock);
		goto out;
	}

	l2 = pmap_l2(pmap, va);
	if (l2 != NULL && pmap_pmd_valid(l2) &&
	    (!(pmap_pmd_entry(l2) & PTE_HUGE) ||
	    pmap_demote_l2_locked(pmap, l2, va, &lock))) {
		l3 = pmap_l2_to_l3(l2, va);
		if (va < VM_MAXUSER_ADDRESS) {
			mpte = PTE_TO_VM_PAGE(pmap_pmd_entry(l2));
			mpte->ref_count++;
		}
	} else if (va < VM_MAXUSER_ADDRESS) {
		nosleep = (flags & PMAP_ENTER_NOSLEEP) != 0;
		mpte = pmap_alloc_l3(pmap, va, nosleep ? NULL : &lock);
		if (mpte == NULL && nosleep) {
			CTR0(KTR_PMAP, "pmap_enter: mpte == NULL");
			if (lock != NULL)
				rw_wunlock(lock);
			rw_runlock(&pvh_global_lock);
			PMAP_UNLOCK(pmap);
			return (KERN_RESOURCE_SHORTAGE);
		}
		l3 = pmap_l3(pmap, va);
	} else {
		panic("pmap_enter: missing L3 table for kernel va %#lx", va);
	}

	if (l3 == NULL) {
		panic("L3 is null: va: 0x%lx\n", va);
	}
	orig_l3 = pmap_pte_entry(l3);
	opa = PTE_TO_PHYS(orig_l3);
	pv = NULL;

	/*
	 * Is the specified virtual address already mapped?
	 */
	if (pmap_pte_valid(l3)) {
		/*
		 * Wiring change, just update stats. We don't worry about
		 * wiring PT pages as they remain resident as long as there
		 * are valid mappings in them. Hence, if a user page is wired,
		 * the PT page will be also.
		 */
		if ((flags & PMAP_ENTER_WIRED) != 0 &&
			(orig_l3 & PTE_SW_WIRED) == 0)
			pmap->pm_stats.wired_count++;
		else if ((flags & PMAP_ENTER_WIRED) == 0 &&
			(orig_l3 & PTE_SW_WIRED) != 0)
			pmap->pm_stats.wired_count--;

		/*
		 * Remove the extra PT page reference.
		 */
		if (mpte != NULL) {
			mpte->ref_count--;
			KASSERT(mpte->ref_count > 0,
			    ("pmap_enter: missing reference to page table page,"
			     " va: 0x%lx", va));
		}

		/*
		 * Has the physical page changed?
		 */
		if (opa == pa) {
			/*
			 * No, might be a protection or wiring change.
			 */
			if ((orig_l3 & PTE_SW_MANAGED) != 0 &&
				(new_l3 & PTE_W) != 0)
				vm_page_aflag_set(m, PGA_WRITEABLE);
			goto validate;
		}

		/*
		 * The physical page has changed.  Temporarily invalidate
		 * the mapping.  This ensures that all threads sharing the
		 * pmap keep a consistent view of the mapping, which is
		 * necessary for the correct handling of COW faults.  It
		 * also permits reuse of the old mapping's PV entry,
		 * avoiding an allocation.
		 *
		 * For consistency, handle unmanaged mappings the same way.
		 */
		orig_l3 = pmap_load_clear(l3);
		KASSERT(PTE_TO_PHYS(orig_l3) == opa,
		    ("pmap_enter: unexpected pa update for %#lx", va));
		if ((orig_l3 & PTE_SW_MANAGED) != 0) {
			om = PHYS_TO_VM_PAGE(opa);

			/*
			 * The pmap lock is sufficient to synchronize with
			 * concurrent calls to pmap_page_test_mappings() and
			 * pmap_ts_referenced().
			 */
			if ((orig_l3 & PTE_D) != 0)
				vm_page_dirty(om);
			if ((orig_l3 & PTE_A) != 0)
				vm_page_aflag_set(om, PGA_REFERENCED);
			CHANGE_PV_LIST_LOCK_TO_PHYS(&lock, opa);
			pv = pmap_pvh_remove(&om->md, pmap, va);
			KASSERT(pv != NULL,
			    ("pmap_enter: no PV entry for %#lx", va));
			if ((new_l3 & PTE_SW_MANAGED) == 0)
				free_pv_entry(pmap, pv);
			if ((om->a.flags & PGA_WRITEABLE) != 0 &&
			    TAILQ_EMPTY(&om->md.pv_list) &&
			    ((om->flags & PG_FICTITIOUS) != 0 ||
			    TAILQ_EMPTY(&pa_to_pvh(opa)->pv_list)))
				vm_page_aflag_clear(om, PGA_WRITEABLE);
		}
		pmap_invalidate_page(pmap, va);
		orig_l3 = 0;
	} else {
		/*
		 * Increment the counters.
		 */
		if ((new_l3 & PTE_SW_WIRED) != 0)
			pmap->pm_stats.wired_count++;
		pmap_resident_count_inc(pmap, 1);
	}
	/*
	 * Enter on the PV list if part of our managed memory.
	 */
	if ((new_l3 & PTE_SW_MANAGED) != 0) {
		if (pv == NULL) {
			pv = get_pv_entry(pmap, &lock);
			pv->pv_va = va;
		}
		CHANGE_PV_LIST_LOCK_TO_PHYS(&lock, pa);
		TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_next);
		m->md.pv_gen++;
		if ((new_l3 & PTE_W) != 0)
			vm_page_aflag_set(m, PGA_WRITEABLE);
	}

validate:
	/*
	 * Update the L3 entry.
	 */
	if (pmap_pte_valid(l3)) {
		orig_l3 = pmap_load_store(l3, new_l3);
		pmap_invalidate_page(pmap, va);
		KASSERT(PTE_TO_PHYS(orig_l3) == pa,
		    ("pmap_enter: invalid update"));
		if ((orig_l3 & (PTE_D | PTE_SW_MANAGED)) ==
		    (PTE_D | PTE_SW_MANAGED))
			vm_page_dirty(m);
	} else {
		pmap_store(l3, new_l3);
		pmap_invalidate_page(pmap, va);
	}

#if VM_NRESERVLEVEL > 0
	if (mpte != NULL && mpte->ref_count == Ln_ENTRIES &&
	    (m->flags & PG_FICTITIOUS) == 0 &&
	    vm_reserv_level_iffullpop(m) == 0)
		(void)pmap_promote_l2(pmap, l2, va, mpte, &lock);
#endif

	rv = KERN_SUCCESS;
out:
	if (lock != NULL)
		rw_wunlock(lock);
	rw_runlock(&pvh_global_lock);

	/*
	 * Install the TLB entry while holding PMAP_LOCK.  If this runs
	 * outside the lock, a concurrent pmap_enter on another CPU can
	 * modify the PTE and shoot down this CPU between the PTE read
	 * (pmap_load) and the tlbfill/tlbwr — installing a stale PA.
	 */
	pmap_update_tlb(pmap, va);

	PMAP_UNLOCK(pmap);

	/*
	 * Sync the i-cache on all cpus after updating the PTE
	 * if the new PTE is executable.  Must be done outside the
	 * PMAP lock to avoid deadlock with remote CPUs that may be
	 * spinning on PV list locks with interrupts disabled.
	 */
	if (prot & VM_PROT_EXECUTE)
		pmap_sync_icache(pmap, va, PAGE_SIZE);
	return (rv);
}

/*
 * Release a page table page reference after a failed attempt to create a
 * mapping.
 */
static void
pmap_abort_ptp(pmap_t pmap, vm_offset_t va, vm_page_t l2pg)
{
	struct spglist free;

	SLIST_INIT(&free);
	if (pmap_unwire_ptp(pmap, va, l2pg, &free)) {
		/*
		 * Although "va" is not mapped, paging-structure
		 * caches could nonetheless have entries that
		 * refer to the freed page table pages.
		 * Invalidate those entries.
		 */
		pmap_invalidate_page(pmap, va);
		vm_page_free_pages_toq(&free, true);
	}
}

/*
 * Tries to create a read- and/or execute-only 2MB page mapping.  Returns
 * KERN_SUCCESS if the mapping was created.  Otherwise, returns an error
 * value.  See pmap_enter_l2() for the possible error values when "no sleep",
 * "no replace", and "no reclaim" are specified.
 */
static int
pmap_enter_2mpage(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot,
    struct rwlock **lockp)
{
	pd_entry_t new_l2;
	pn_t pn;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	pn = VM_PAGE_TO_PHYS(m) >> PAGE_PFN_SHIFT;
	new_l2 = (pd_entry_t)((pn << L3_SHIFT) |
	    PTE_V | PTE_CC | PTE_P | PTE_NX);
	if ((m->oflags & VPO_UNMANAGED) == 0)
		new_l2 |= PTE_SW_MANAGED;
	if ((prot & VM_PROT_EXECUTE) != 0)
		new_l2 &= ~PTE_NX;
	if (va < VM_MAXUSER_ADDRESS)
		new_l2 |= PTE_U;
	if (pmap == kernel_pmap)
		new_l2 |= PTE_G;
	return (pmap_enter_l2(pmap, va, new_l2, PMAP_ENTER_NOSLEEP |
	    PMAP_ENTER_NOREPLACE | PMAP_ENTER_NORECLAIM, NULL, lockp));
}

/*
 * Returns true if every page table entry in the specified page table is
 * zero.
 */
static bool
pmap_every_pte_zero(vm_paddr_t pa)
{
	pt_entry_t *pt_end, *pte;

	KASSERT((pa & PAGE_MASK) == 0, ("pa is misaligned"));
	pte = (pt_entry_t *)PHYS_TO_DMAP(pa);
	for (pt_end = pte + Ln_ENTRIES; pte < pt_end; pte++) {
		if (*pte != 0)
			return (false);
	}
	return (true);
}

/*
 * Tries to create the specified 2MB page mapping.  Returns KERN_SUCCESS if
 * the mapping was created, and one of KERN_FAILURE, KERN_NO_SPACE, or
 * KERN_RESOURCE_SHORTAGE otherwise.  Returns KERN_FAILURE if
 * PMAP_ENTER_NOREPLACE was specified and a 4KB page mapping already exists
 * within the 2MB virtual address range starting at the specified virtual
 * address.  Returns KERN_NO_SPACE if PMAP_ENTER_NOREPLACE was specified and a
 * 2MB page mapping already exists at the specified virtual address.  Returns
 * KERN_RESOURCE_SHORTAGE if either (1) PMAP_ENTER_NOSLEEP was specified and a
 * page table page allocation failed or (2) PMAP_ENTER_NORECLAIM was specified
 * and a PV entry allocation failed.
 *
 * The parameter "m" is only used when creating a managed, writeable mapping.
 */
static int
pmap_enter_l2(pmap_t pmap, vm_offset_t va, pd_entry_t new_l2, u_int flags,
    vm_page_t m, struct rwlock **lockp)
{
	struct spglist free;
	pd_entry_t *l2, *l3, oldl2;
	vm_offset_t sva;
	vm_page_t l2pg, mt;
	vm_page_t uwptpg;

	PMAP_LOCK_ASSERT(pmap, MA_OWNED);

	if ((l2pg = pmap_alloc_l2(pmap, va, (flags & PMAP_ENTER_NOSLEEP) != 0 ?
	    NULL : lockp)) == NULL) {
		CTR2(KTR_PMAP, "pmap_enter_l2: failed to allocate PT page"
		    " for va %#lx in pmap %p", va, pmap);
		return (KERN_RESOURCE_SHORTAGE);
	}

	l2 = VM_PAGE_TO_DMAP(l2pg);
	l2 = &l2[pmap_l2_index(va)];
	if (pmap_pmd_valid(l2)) {
		oldl2 = pmap_pmd_entry(l2);
		KASSERT(l2pg->ref_count > 1,
		    ("pmap_enter_l2: l2pg's ref count is too low"));
		if ((flags & PMAP_ENTER_NOREPLACE) != 0) {
			if (oldl2 & PTE_HUGE) {
				l2pg->ref_count--;
				CTR2(KTR_PMAP,
				    "pmap_enter_l2: no space for va %#lx"
				    " in pmap %p", va, pmap);
				return (KERN_NO_SPACE);
			} else if (va < VM_MAXUSER_ADDRESS ||
			    !pmap_every_pte_zero(L2PTE_TO_PHYS(oldl2))) {
				l2pg->ref_count--;
				CTR2(KTR_PMAP, "pmap_enter_l2:"
				    " failed to replace existing mapping"
				    " for va %#lx in pmap %p", va, pmap);
				return (KERN_FAILURE);
			}
		}
		SLIST_INIT(&free);
		if (oldl2 & PTE_HUGE)
			(void)pmap_remove_l2(pmap, l2, va,
			    pmap_pud_entry(pmap_l1(pmap, va)), &free, lockp);
		else
			for (sva = va; sva < va + L2_SIZE; sva += PAGE_SIZE) {
				l3 = pmap_l2_to_l3(l2, sva);
				if (pmap_pte_valid(l3) &&
				    pmap_remove_l3(pmap, l3, sva, oldl2, &free,
				    lockp, true) != 0)
					break;
			}
		vm_page_free_pages_toq(&free, true);
		if (va >= VM_MAXUSER_ADDRESS) {
			/*
			 * Both pmap_remove_l2() and pmap_remove_l3() will
			 * leave the kernel page table page zero filled.
			 */
			mt = PTE_TO_VM_PAGE(pmap_pmd_entry(l2));
			if (pmap_insert_pt_page(pmap, mt, false, false))
				panic("pmap_enter_l2: trie insert failed");
		} else
			KASSERT(!pmap_pmd_valid(l2),
			    ("pmap_enter_l2: non-zero L2 entry %p", l2));
	}

	/*
	 * Allocate leaf ptpage for wired userspace pages.
	 */
	uwptpg = NULL;
	if ((new_l2 & PTE_SW_WIRED) != 0 && pmap != kernel_pmap) {
		uwptpg = vm_page_alloc_noobj(VM_ALLOC_WIRED);
		if (uwptpg == NULL) {
			pmap_abort_ptp(pmap, va, l2pg);
			return (KERN_RESOURCE_SHORTAGE);
		}
		uwptpg->pindex = pmap_l2_pindex(va);
		if (pmap_insert_pt_page(pmap, uwptpg, true, false)) {
			vm_page_unwire_noq(uwptpg);
			vm_page_free(uwptpg);
			pmap_abort_ptp(pmap, va, l2pg);
			return (KERN_RESOURCE_SHORTAGE);
		}
		pmap_resident_count_inc(pmap, 1);
		uwptpg->ref_count = Ln_ENTRIES;
	}

	if ((new_l2 & PTE_SW_MANAGED) != 0) {
		/*
		 * Abort this mapping if its PV entry could not be created.
		 */
		if (!pmap_pv_insert_l2(pmap, va, new_l2, flags, lockp)) {
			pmap_abort_ptp(pmap, va, l2pg);
			if (uwptpg != NULL) {
				mt = pmap_remove_pt_page(pmap, va);
				KASSERT(mt == uwptpg,
				    ("removed pt page %p, expected %p", mt,
				    uwptpg));
				pmap_resident_count_dec(pmap, 1);
				uwptpg->ref_count = 1;
				vm_page_unwire_noq(uwptpg);
				vm_page_free(uwptpg);
			}
			CTR2(KTR_PMAP,
			    "pmap_enter_l2: failed to create PV entry"
			    " for va %#lx in pmap %p", va, pmap);
			return (KERN_RESOURCE_SHORTAGE);
		}
		if ((new_l2 & PTE_W) != 0)
			for (mt = m; mt < &m[L2_SIZE / PAGE_SIZE]; mt++)
				vm_page_aflag_set(mt, PGA_WRITEABLE);
	}

	/*
	 * Increment counters.
	 */
	if ((new_l2 & PTE_SW_WIRED) != 0)
		pmap->pm_stats.wired_count += L2_SIZE / PAGE_SIZE;
	pmap->pm_stats.resident_count += L2_SIZE / PAGE_SIZE;

	/*
	 * Map the superpage.
	 *
	 * PTE_HUGE (bit 6) is the marker that tells the LoongArch HW page
	 * walker that this L2 entry is a 2MB leaf, not a directory pointer to
	 * an L3 table.  Without it, the walker follows the PFN as a bare-phys
	 * directory pointer -- reading the data page's contents as L3 PTEs,
	 * producing the garbage-TLB corruption.
	 */

	if (new_l2 & PTE_G) {
		new_l2 &= ~PTE_G;
		new_l2 |= PTE_HUGE_G;
		KASSERT(va >= VM_MIN_KERNEL_ADDRESS,
			("user pmap with G bit, pmap: %p, va: 0x%lx\n",
			pmap, va));
	}
	new_l2 |= PTE_HUGE;

	pmap_store(l2, new_l2);
	pmap_invalidate_page(pmap, va);

	atomic_add_long(&pmap_l2_mappings, 1);
	CTR2(KTR_PMAP, "pmap_enter_l2: success for va %#lx in pmap %p",
	    va, pmap);

	return (KERN_SUCCESS);
}

/*
 * Maps a sequence of resident pages belonging to the same object.
 * The sequence begins with the given page m_start.  This page is
 * mapped at the given virtual address start.  Each subsequent page is
 * mapped at a virtual address that is offset from start by the same
 * amount as the page is offset from m_start within the object.  The
 * last page in the sequence is the page with the largest offset from
 * m_start that can be mapped at a virtual address less than the given
 * virtual address end.  Not every virtual page between start and end
 * is mapped; only those for which a resident page exists with the
 * corresponding offset from m_start are mapped.
 */
void
pmap_enter_object(pmap_t pmap, vm_offset_t start, vm_offset_t end,
    vm_page_t m_start, vm_prot_t prot)
{
	struct pctrie_iter pages;
	struct rwlock *lock;
	vm_offset_t va;
	vm_page_t m, mpte;
	int rv;

	VM_OBJECT_ASSERT_LOCKED(m_start->object);

	mpte = NULL;
	vm_page_iter_limit_init(&pages, m_start->object,
	    m_start->pindex + atop(end - start));
	m = vm_radix_iter_lookup(&pages, m_start->pindex);
	lock = NULL;
	rw_rlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	while (m != NULL) {
		va = start + ptoa(m->pindex - m_start->pindex);
		if ((va & L2_OFFSET) == 0 && va + L2_SIZE <= end &&
		    m->psind == 1 && pmap_ps_enabled(pmap) &&
		    ((rv = pmap_enter_2mpage(pmap, va, m, prot, &lock)) ==
		    KERN_SUCCESS || rv == KERN_NO_SPACE)) {
			m = vm_radix_iter_jump(&pages, L2_SIZE / PAGE_SIZE);
		} else {
			mpte = pmap_enter_quick_locked(pmap, va, m, prot, mpte,
			    &lock);
			m = vm_radix_iter_step(&pages);
		}
	}
	if (lock != NULL)
		rw_wunlock(lock);
	rw_runlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
}

/*
 * this code makes some *MAJOR* assumptions:
 * 1. Current pmap & pmap exists.
 * 2. Not wired.
 * 3. Read access.
 * 4. No page table pages.
 * but is *MUCH* faster than pmap_enter...
 */

void
pmap_enter_quick(pmap_t pmap, vm_offset_t va, vm_page_t m, vm_prot_t prot)
{
	struct rwlock *lock;

	lock = NULL;
	rw_rlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	(void)pmap_enter_quick_locked(pmap, va, m, prot, NULL, &lock);
	if (lock != NULL)
		rw_wunlock(lock);
	rw_runlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
	if (prot & VM_PROT_EXECUTE)
		pmap_sync_icache(pmap, va, PAGE_SIZE);
}

static vm_page_t
pmap_enter_quick_locked(pmap_t pmap, vm_offset_t va, vm_page_t m,
    vm_prot_t prot, vm_page_t mpte, struct rwlock **lockp)
{
	struct spglist free;
	pd_entry_t *l2;
	pt_entry_t *l3, newl3;

	KASSERT(!VA_IS_CLEANMAP(va) ||
	    (m->oflags & VPO_UNMANAGED) != 0,
	    ("pmap_enter_quick_locked: managed mapping within the "
	    "clean submap"));
	rw_assert(&pvh_global_lock, RA_LOCKED);
	PMAP_LOCK_ASSERT(pmap, MA_OWNED);
	l2 = NULL;

	CTR2(KTR_PMAP, "pmap_enter_quick_locked: %p %lx", pmap, va);
	/*
	 * In the case that a page table page is not
	 * resident, we are creating it here.
	 */
	if (va < VM_MAXUSER_ADDRESS) {
		vm_pindex_t l2pindex;

		/*
		 * Calculate pagetable page index
		 */
		l2pindex = pmap_l2_pindex(va);
		if (mpte && (mpte->pindex == l2pindex)) {
			mpte->ref_count++;
		} else {
			/*
			 * Get the l2 entry
			 */
			l2 = pmap_l2(pmap, va);

			/*
			 * If the page table page is mapped, we just increment
			 * the hold count, and activate it.  Otherwise, we
			 * attempt to allocate a page table page.  If this
			 * attempt fails, we don't retry.  Instead, we give up.
			 */
			if (l2 != NULL && pmap_pmd_valid(l2)) {
				if (pmap_pmd_entry(l2) & PTE_HUGE)
					return (NULL);
				mpte = PTE_TO_VM_PAGE(pmap_pmd_entry(l2));
				mpte->ref_count++;
			} else {
				/*
				 * Pass NULL instead of the PV list lock
				 * pointer, because we don't intend to sleep.
				 */
				mpte = _pmap_alloc_l3(pmap, l2pindex, NULL);
				if (mpte == NULL)
					return (mpte);
			}
		}
		l3 = VM_PAGE_TO_DMAP(mpte);
		l3 = &l3[pmap_l3_index(va)];
	} else {
		mpte = NULL;
		l3 = pmap_l3(kernel_pmap, va);
	}
	if (l3 == NULL)
		panic("pmap_enter_quick_locked: No l3");
	if (pmap_pte_valid(l3)) {
		if (mpte != NULL)
			mpte->ref_count--;
		return (NULL);
	}

	/*
	 * Enter on the PV list if part of our managed memory.
	 */
	if ((m->oflags & VPO_UNMANAGED) == 0 &&
	    !pmap_try_insert_pv_entry(pmap, va, m, lockp)) {
		if (mpte != NULL) {
			SLIST_INIT(&free);
			if (pmap_unwire_ptp(pmap, va, mpte, &free))
				vm_page_free_pages_toq(&free, false);
		}
		return (NULL);
	}

	/*
	 * Increment counters
	 */
	pmap_resident_count_inc(pmap, 1);

	newl3 = ((VM_PAGE_TO_PHYS(m) / PAGE_SIZE) << L3_SHIFT) |
	    PTE_V | PTE_CC | PTE_P | PTE_NX;
	if ((prot & VM_PROT_EXECUTE) != 0)
		newl3 &= ~PTE_NX;
	if ((m->oflags & VPO_UNMANAGED) == 0)
		newl3 |= PTE_SW_MANAGED;
	if (va < VM_MAX_USER_ADDRESS)
		newl3 |= PTE_U;
	else
		newl3 |= PTE_G;

	pmap_store(l3, newl3);
	pmap_invalidate_page(pmap, va);
	pmap_update_tlb(pmap, va);

#if VM_NRESERVLEVEL > 0
	/*
	 * If both the PTP and the reservation are fully populated, then attempt
	 * promotion.
	 */
	if ((prot & VM_PROT_NO_PROMOTE) == 0 &&
		(mpte == NULL || mpte->ref_count == Ln_ENTRIES) &&
			(m->flags & PG_FICTITIOUS) == 0 &&
			vm_reserv_level_iffullpop(m) == 0) {
		if (l2 == NULL)
			l2 = pmap_l2(pmap, va);

		/*
		 * If promotion succeeds, then the next call to this function
		 * should not be given the unmapped PTP as a hint.
		 */
		if (pmap_promote_l2(pmap, l2, va, mpte, lockp))
			mpte = NULL;
	}
#endif

	return (mpte);
}

/*
 * This code maps large physical mmap regions into the
 * processor address space.  Note that some shortcuts
 * are taken, but the code works.
 */
void
pmap_object_init_pt(pmap_t pmap, vm_offset_t addr, vm_object_t object,
    vm_pindex_t pindex, vm_size_t size)
{

	VM_OBJECT_ASSERT_WLOCKED(object);
	KASSERT(object->type == OBJT_DEVICE || object->type == OBJT_SG,
	    ("pmap_object_init_pt: non-device object"));
}

/*
 *	Clear the wired attribute from the mappings for the specified range of
 *	addresses in the given pmap.  Every valid mapping within that range
 *	must have the wired attribute set.  In contrast, invalid mappings
 *	cannot have the wired attribute set, so they are ignored.
 *
 *	The wired attribute of the page table entry is not a hardware feature,
 *	so there is no need to invalidate any TLB entries.
 */
void
pmap_unwire(pmap_t pmap, vm_offset_t sva, vm_offset_t eva)
{
	vm_offset_t va_next;
	pd_entry_t *l0, *l1, *l2, l2e;
	pt_entry_t *l3, l3e;
	bool pv_lists_locked;

	pv_lists_locked = false;
retry:
	PMAP_LOCK(pmap);
	for (; sva < eva; sva = va_next) {

		l0 = pmap_l0(pmap, sva);
		if (!pmap_pgd_valid(l0)) {
			va_next = (sva + L0_SIZE) & ~L0_OFFSET;
			if (va_next < sva)
				va_next = eva;
			continue;
		}
		l1 = pmap_l0_to_l1(l0, sva);

		if (!pmap_pud_valid(l1)) {
			va_next = (sva + L1_SIZE) & ~L1_OFFSET;
			if (va_next < sva)
				va_next = eva;
			continue;
		}

		va_next = (sva + L2_SIZE) & ~L2_OFFSET;
		if (va_next < sva)
			va_next = eva;

		l2 = pmap_l1_to_l2(l1, sva);
		if (!pmap_pmd_valid(l2))
			continue;
		l2e = pmap_pmd_entry(l2);
		if (l2e & PTE_HUGE) {
			if (sva + L2_SIZE == va_next && eva >= va_next) {
				if ((l2e & PTE_SW_WIRED) == 0)
					panic("pmap_unwire: l2 %#jx is missing "
					    "PTE_SW_WIRED", (uintmax_t)l2e);
				pmap_clear_bits(l2, PTE_SW_WIRED);
				continue;
			} else {
				if (!pv_lists_locked) {
					pv_lists_locked = true;
					if (!rw_try_rlock(&pvh_global_lock)) {
						PMAP_UNLOCK(pmap);
						rw_rlock(&pvh_global_lock);
						/* Repeat sva. */
						goto retry;
					}
				}
				if (!pmap_demote_l2(pmap, l2, sva))
					panic("pmap_unwire: demotion failed");
			}
		}

		if (va_next > eva)
			va_next = eva;
		for (l3 = pmap_l2_to_l3(l2, sva); sva != va_next; l3++,
		    sva += L3_SIZE) {
			if (!pmap_pte_valid(l3))
				continue;
			l3e = pmap_pte_entry(l3);
			if ((l3e & PTE_SW_WIRED) == 0)
				panic("pmap_unwire: l3 %#jx is missing "
				    "PTE_SW_WIRED", (uintmax_t)l3e);

			/*
			 * PG_W must be cleared atomically.  Although the pmap
			 * lock synchronizes access to PG_W, another processor
			 * could be setting PG_M and/or PG_A concurrently.
			 */
			pmap_clear_bits(l3, PTE_SW_WIRED);
			pmap->pm_stats.wired_count--;
		}
	}
	if (pv_lists_locked)
		rw_runlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
}

/*
 *	Copy the range specified by src_addr/len
 *	from the source map to the range dst_addr/len
 *	in the destination map.
 *
 *	This routine is only advisory and need not do anything.
 */

void
pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, vm_offset_t dst_addr, vm_size_t len,
    vm_offset_t src_addr)
{

}

/*
 *	pmap_zero_page zeros the specified hardware page by mapping
 *	the page into KVM and using bzero to clear its contents.
 */
void
pmap_zero_page(vm_page_t m)
{
	vm_offset_t va = (vm_offset_t)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(m));

	pagezero((void *)va);
}

/*
 *	pmap_zero_page_area zeros the specified hardware page by mapping 
 *	the page into KVM and using bzero to clear its contents.
 *
 *	off and size may not cover an area beyond a single hardware page.
 */
void
pmap_zero_page_area(vm_page_t m, int off, int size)
{
	vm_offset_t va = (vm_offset_t)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(m));

	if (off == 0 && size == PAGE_SIZE)
		pagezero((void *)va);
	else
		bzero((char *)va + off, size);
}

/*
 *	pmap_copy_page copies the specified (machine independent)
 *	page by mapping the page into virtual memory and using
 *	bcopy to copy the page, one machine dependent page at a
 *	time.
 */
void
pmap_copy_page(vm_page_t msrc, vm_page_t mdst)
{
	vm_offset_t src = (vm_offset_t)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(msrc));
	vm_offset_t dst = (vm_offset_t)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(mdst));

	pagecopy((void *)src, (void *)dst);
}

int unmapped_buf_allowed = 1;

void
pmap_copy_pages(vm_page_t ma[], vm_offset_t a_offset, vm_page_t mb[],
    vm_offset_t b_offset, int xfersize)
{
	void *a_cp, *b_cp;
	vm_page_t m_a, m_b;
	vm_paddr_t p_a, p_b;
	vm_offset_t a_pg_offset, b_pg_offset;
	int cnt;

	while (xfersize > 0) {
		a_pg_offset = a_offset & PAGE_MASK;
		m_a = ma[a_offset >> PAGE_SHIFT];
		p_a = m_a->phys_addr;
		b_pg_offset = b_offset & PAGE_MASK;
		m_b = mb[b_offset >> PAGE_SHIFT];
		p_b = m_b->phys_addr;
		cnt = min(xfersize, PAGE_SIZE - a_pg_offset);
		cnt = min(cnt, PAGE_SIZE - b_pg_offset);
		if (__predict_false(!PHYS_IN_DMAP(p_a))) {
			panic("!DMAP a %lx", p_a);
		} else {
			a_cp = (char *)PHYS_TO_DMAP(p_a) + a_pg_offset;
		}
		if (__predict_false(!PHYS_IN_DMAP(p_b))) {
			panic("!DMAP b %lx", p_b);
		} else {
			b_cp = (char *)PHYS_TO_DMAP(p_b) + b_pg_offset;
		}
		bcopy(a_cp, b_cp, cnt);
		a_offset += cnt;
		b_offset += cnt;
		xfersize -= cnt;
	}
}

void *
pmap_quick_enter_page(vm_page_t m)
{

	return (PHYS_TO_DMAP(VM_PAGE_TO_PHYS(m)));
}

void
pmap_quick_remove_page(void *addr)
{
}

/*
 * Returns true if the pmap's pv is one of the first
 * 16 pvs linked to from this page.  This count may
 * be changed upwards or downwards in the future; it
 * is only necessary that true be returned for a small
 * subset of pmaps for proper page aging.
 */
bool
pmap_page_exists_quick(pmap_t pmap, vm_page_t m)
{
	struct md_page *pvh;
	struct rwlock *lock;
	pv_entry_t pv;
	int loops = 0;
	bool rv;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_page_exists_quick: page %p is not managed", m));
	rv = false;
	rw_rlock(&pvh_global_lock);
	lock = VM_PAGE_TO_PV_LIST_LOCK(m);
	rw_rlock(lock);
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_next) {
		if (PV_PMAP(pv) == pmap) {
			rv = true;
			break;
		}
		loops++;
		if (loops >= 16)
			break;
	}
	if (!rv && loops < 16 && (m->flags & PG_FICTITIOUS) == 0) {
		pvh = pa_to_pvh(VM_PAGE_TO_PHYS(m));
		TAILQ_FOREACH(pv, &pvh->pv_list, pv_next) {
			if (PV_PMAP(pv) == pmap) {
				rv = true;
				break;
			}
			loops++;
			if (loops >= 16)
				break;
		}
	}
	rw_runlock(lock);
	rw_runlock(&pvh_global_lock);
	return (rv);
}

/*
 *	pmap_page_wired_mappings:
 *
 *	Return the number of managed mappings to the given physical page
 *	that are wired.
 */
int
pmap_page_wired_mappings(vm_page_t m)
{
	struct md_page *pvh;
	struct rwlock *lock;
	pmap_t pmap;
	pd_entry_t *l2;
	pt_entry_t *l3;
	pv_entry_t pv;
	int count, md_gen, pvh_gen;

	if ((m->oflags & VPO_UNMANAGED) != 0)
		return (0);
	rw_rlock(&pvh_global_lock);
	lock = VM_PAGE_TO_PV_LIST_LOCK(m);
	rw_rlock(lock);
restart:
	count = 0;
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_next) {
		pmap = PV_PMAP(pv);
		if (!PMAP_TRYLOCK(pmap)) {
			md_gen = m->md.pv_gen;
			rw_runlock(lock);
			PMAP_LOCK(pmap);
			rw_rlock(lock);
			if (md_gen != m->md.pv_gen) {
				PMAP_UNLOCK(pmap);
				goto restart;
			}
		}
		l2 = pmap_l2(pmap, pv->pv_va);
		KASSERT(!(pmap_pmd_entry(l2) & PTE_HUGE),
		    ("%s: found a 2mpage in page %p's pv list", __func__, m));

		l3 = pmap_l2_to_l3(l2, pv->pv_va);
		if ((pmap_pte_entry(l3) & PTE_SW_WIRED) != 0)
			count++;
		PMAP_UNLOCK(pmap);
	}
	if ((m->flags & PG_FICTITIOUS) == 0) {
		pvh = pa_to_pvh(VM_PAGE_TO_PHYS(m));
		TAILQ_FOREACH(pv, &pvh->pv_list, pv_next) {
			pmap = PV_PMAP(pv);
			if (!PMAP_TRYLOCK(pmap)) {
				md_gen = m->md.pv_gen;
				pvh_gen = pvh->pv_gen;
				rw_runlock(lock);
				PMAP_LOCK(pmap);
				rw_rlock(lock);
				if (md_gen != m->md.pv_gen ||
				    pvh_gen != pvh->pv_gen) {
					PMAP_UNLOCK(pmap);
					goto restart;
				}
			}
			l2 = pmap_l2(pmap, pv->pv_va);
			if ((pmap_pmd_entry(l2) & PTE_SW_WIRED) != 0)
				count++;
			PMAP_UNLOCK(pmap);
		}
	}
	rw_runlock(lock);
	rw_runlock(&pvh_global_lock);
	return (count);
}

/*
 * Returns true if the given page is mapped individually or as part of
 * a 2mpage.  Otherwise, returns false.
 */
bool
pmap_page_is_mapped(vm_page_t m)
{
	struct rwlock *lock;
	bool rv;

	if ((m->oflags & VPO_UNMANAGED) != 0)
		return (false);
	lock = VM_PAGE_TO_PV_LIST_LOCK(m);
	rw_rlock(lock);
	rv = !TAILQ_EMPTY(&m->md.pv_list) ||
	    ((m->flags & PG_FICTITIOUS) == 0 &&
	    !TAILQ_EMPTY(&pa_to_pvh(VM_PAGE_TO_PHYS(m))->pv_list));
	rw_runlock(lock);
	return (rv);
}

static void
pmap_remove_pages_pv(pmap_t pmap, vm_page_t m, pv_entry_t pv,
    struct spglist *free, bool superpage)
{
	struct md_page *pvh;
	vm_page_t mpte, mt;

	if (superpage) {
		pmap_resident_count_dec(pmap, Ln_ENTRIES);
		pvh = pa_to_pvh(m->phys_addr);
		TAILQ_REMOVE(&pvh->pv_list, pv, pv_next);
		pvh->pv_gen++;
		if (TAILQ_EMPTY(&pvh->pv_list)) {
			for (mt = m; mt < &m[Ln_ENTRIES]; mt++)
				if (TAILQ_EMPTY(&mt->md.pv_list) &&
				    (mt->a.flags & PGA_WRITEABLE) != 0)
					vm_page_aflag_clear(mt, PGA_WRITEABLE);
		}
		mpte = pmap_remove_pt_page(pmap, pv->pv_va);
		if (mpte != NULL) {
			KASSERT(vm_page_any_valid(mpte),
			    ("pmap_remove_pages: pte page not promoted"));
			pmap_resident_count_dec(pmap, 1);
			KASSERT(mpte->ref_count == Ln_ENTRIES,
			    ("pmap_remove_pages: pte page ref count error"));
			/*
			 * Restore the single real wiring (VM_ALLOC_WIRED at
			 * demote time) before dropping it, matching
			 * pmap_remove_l2(): a bare ref_count = 0 would free the
			 * page without vm_wire_sub(1), leaking the global
			 * wired-page counter for every promoted 2MB page-table
			 * page removed via this (process-exit) path.
			 */
			mpte->ref_count = 1;
			vm_page_unwire_noq(mpte);
			pmap_add_delayed_free_list(mpte, free, false);
		}
	} else {
		pmap_resident_count_dec(pmap, 1);
		TAILQ_REMOVE(&m->md.pv_list, pv, pv_next);
		m->md.pv_gen++;
		if (TAILQ_EMPTY(&m->md.pv_list) &&
		    (m->a.flags & PGA_WRITEABLE) != 0) {
			pvh = pa_to_pvh(m->phys_addr);
			if (TAILQ_EMPTY(&pvh->pv_list))
				vm_page_aflag_clear(m, PGA_WRITEABLE);
		}
	}
}

int
pmap_growkernel(vm_offset_t addr)
{
	int rv;

	rv = pmap_growkernel_nopanic(addr);
	if (rv != KERN_SUCCESS && pmap_growkernel_panic)
		panic("pmap_growkernel: no memory to grow kernel");
	return (rv);
}

/*
 * Destroy all managed, non-wired mappings in the given user-space
 * pmap.  This pmap cannot be active on any processor besides the
 * caller.
 *
 * This function cannot be applied to the kernel pmap.  Moreover, it
 * is not intended for general use.  It is only to be used during
 * process termination.  Consequently, it can be implemented in ways
 * that make it faster than pmap_remove().  First, it can more quickly
 * destroy mappings by iterating over the pmap's collection of PV
 * entries, rather than searching the page table.  Second, it doesn't
 * have to test and clear the page table entries atomically, because
 * no processor is currently accessing the user address space.  In
 * particular, a page table entry's dirty bit won't change state once
 * this function starts.
 */
void
pmap_remove_pages(pmap_t pmap)
{
	struct spglist free;
	pd_entry_t ptepde;
	pt_entry_t *pte, tpte;
	vm_page_t m, mt;
	pv_entry_t pv;
	struct pv_chunk *pc, *npc;
	struct rwlock *lock;
	int64_t bit;
	uint64_t inuse, bitmask;
	int allfree, field, freed __pv_stat_used, idx;
	bool superpage;

	lock = NULL;

	SLIST_INIT(&free);
	rw_rlock(&pvh_global_lock);
	PMAP_LOCK(pmap);
	TAILQ_FOREACH_SAFE(pc, &pmap->pm_pvchunk, pc_list, npc) {
		allfree = 1;
		freed = 0;
		for (field = 0; field < _NPCM; field++) {
			inuse = ~pc->pc_map[field] & pc_freemask[field];
			while (inuse != 0) {
				bit = ffsl(inuse) - 1;
				bitmask = 1UL << bit;
				idx = field * 64 + bit;
				pv = &pc->pc_pventry[idx];
				inuse &= ~bitmask;

				pte = pmap_l1(pmap, pv->pv_va);
				KASSERT(pte != NULL,
					("%s: l1 address is NULL", __func__));
				ptepde = pmap_pud_entry(pte);
				pte = pmap_l1_to_l2(pte, pv->pv_va);
				tpte = pmap_pmd_entry(pte);

				KASSERT(pmap_pmd_valid(pte),
				    ("L2 PTE is invalid... bogus PV entry? "
				    "va=%#lx, pte=%#lx", pv->pv_va, tpte));
				if (tpte & PTE_HUGE) {
					superpage = true;
				} else {
					ptepde = tpte;
					pte = pmap_l2_to_l3(pte, pv->pv_va);
					tpte = pmap_pte_entry(pte);
					superpage = false;
				}

				/*
				 * We cannot remove wired pages from a
				 * process' mapping at this time.
				 */
				if (tpte & PTE_SW_WIRED) {
					allfree = 0;
					continue;
				}

				m = PTE_TO_VM_PAGE(tpte);
				KASSERT((m->flags & PG_FICTITIOUS) != 0 ||
				    m < &vm_page_array[vm_page_array_size],
				    ("pmap_remove_pages: bad pte %#jx",
				    (uintmax_t)tpte));

				/*
				 * For a 2MB superpage, pte is the L2
				 * (directory) entry: clear it to the sentinel,
				 * NOT 0.  A directory slot of 0 makes the HW
				 * page walker follow phys 0 and read garbage,
				 * which is the user-space corruption we have
				 * been chasing. (pmap_remove_l2() does the same
				 * with pmap_store(l2, invalid_pmd_table).)  For
				 * a 4KB page, pte is the L3 (leaf) PTE,
				 * correctly cleared to 0.
				 */
				if (superpage)
					pmap_store(pte, invalid_pmd_table);
				else
					pmap_clear(pte);

				/*
				 * Update the vm_page_t clean/reference bits.
				 */
				if ((tpte & (PTE_D | PTE_W)) ==
				    (PTE_D | PTE_W)) {
					if (superpage)
						for (mt = m;
						    mt < &m[Ln_ENTRIES]; mt++)
							vm_page_dirty(mt);
					else
						vm_page_dirty(m);
				}

				CHANGE_PV_LIST_LOCK_TO_VM_PAGE(&lock, m);

				/* Mark free */
				pc->pc_map[field] |= bitmask;

				pmap_remove_pages_pv(pmap, m, pv, &free,
				    superpage);
				pmap_unuse_pt(pmap, pv->pv_va, ptepde, &free);
				freed++;
			}
		}
		PV_STAT(atomic_add_long(&pv_entry_frees, freed));
		PV_STAT(atomic_add_int(&pv_entry_spare, freed));
		PV_STAT(atomic_subtract_long(&pv_entry_count, freed));
		if (allfree) {
			TAILQ_REMOVE(&pmap->pm_pvchunk, pc, pc_list);
			free_pv_chunk(pc);
		}
	}
	if (lock != NULL)
		rw_wunlock(lock);
	pmap_invalidate_all(pmap);
	rw_runlock(&pvh_global_lock);
	PMAP_UNLOCK(pmap);
	vm_page_free_pages_toq(&free, false);
}

static bool
pmap_page_test_mappings(vm_page_t m, bool accessed, bool modified)
{
	struct md_page *pvh;
	struct rwlock *lock;
	pd_entry_t *l2;
	pt_entry_t *l3, mask;
	pv_entry_t pv;
	pmap_t pmap;
	int md_gen, pvh_gen;
	bool rv;

	mask = 0;
	if (modified)
		mask |= PTE_D;

	if (accessed)
		mask |= PTE_A;

	rv = false;
	rw_rlock(&pvh_global_lock);
	lock = VM_PAGE_TO_PV_LIST_LOCK(m);
	rw_rlock(lock);
restart:
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_next) {
		pmap = PV_PMAP(pv);
		if (!PMAP_TRYLOCK(pmap)) {
			md_gen = m->md.pv_gen;
			rw_runlock(lock);
			PMAP_LOCK(pmap);
			rw_rlock(lock);
			if (md_gen != m->md.pv_gen) {
				PMAP_UNLOCK(pmap);
				goto restart;
			}
		}
		l2 = pmap_l2(pmap, pv->pv_va);
		KASSERT(!(pmap_pmd_entry(l2) & PTE_HUGE),
		    ("%s: found a 2mpage in page %p's pv list", __func__, m));
		l3 = pmap_l2_to_l3(l2, pv->pv_va);
		rv = (pmap_pte_entry(l3) & mask) == mask;
		PMAP_UNLOCK(pmap);
		if (rv)
			goto out;
	}
	if ((m->flags & PG_FICTITIOUS) == 0) {
		pvh = pa_to_pvh(VM_PAGE_TO_PHYS(m));
		TAILQ_FOREACH(pv, &pvh->pv_list, pv_next) {
			pmap = PV_PMAP(pv);
			if (!PMAP_TRYLOCK(pmap)) {
				md_gen = m->md.pv_gen;
				pvh_gen = pvh->pv_gen;
				rw_runlock(lock);
				PMAP_LOCK(pmap);
				rw_rlock(lock);
				if (md_gen != m->md.pv_gen ||
				    pvh_gen != pvh->pv_gen) {
					PMAP_UNLOCK(pmap);
					goto restart;
				}
			}
			l2 = pmap_l2(pmap, pv->pv_va);
			rv = (pmap_pmd_entry(l2) & mask) == mask;
			PMAP_UNLOCK(pmap);
			if (rv)
				goto out;
		}
	}
out:
	rw_runlock(lock);
	rw_runlock(&pvh_global_lock);
	return (rv);
}

/*
 *	pmap_is_modified:
 *
 *	Return whether or not the specified physical page was modified
 *	in any physical maps.
 */
bool
pmap_is_modified(vm_page_t m)
{

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_is_modified: page %p is not managed", m));

	/*
	 * If the page is not busied then this check is racy.
	 */
	if (!pmap_page_is_write_mapped(m))
		return (false);
	return (pmap_page_test_mappings(m, false, true));
}

/*
 *	pmap_is_prefaultable:
 *
 *	Return whether or not the specified virtual address is eligible
 *	for prefault.
 */
bool
pmap_is_prefaultable(pmap_t pmap, vm_offset_t addr)
{
	pt_entry_t *l3;
	bool rv;

	/*
	 * Return true if and only if the L3 entry for the specified virtual
	 * address is allocated but invalid.
	 */
	rv = false;
	PMAP_LOCK(pmap);
	l3 = pmap_l3(pmap, addr);
	if (l3 != NULL && !pmap_pte_valid(l3)) {
		rv = true;
	}
	PMAP_UNLOCK(pmap);
	return (rv);
}

/*
 *	pmap_is_referenced:
 *
 *	Return whether or not the specified physical page was referenced
 *	in any physical maps.
 */
bool
pmap_is_referenced(vm_page_t m)
{

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_is_referenced: page %p is not managed", m));
	return (pmap_page_test_mappings(m, true, false));
}

/*
 * Clear the write and modified bits in each of the given page's mappings.
 */
void
pmap_remove_write(vm_page_t m)
{
	struct md_page *pvh;
	struct rwlock *lock;
	pmap_t pmap;
	pd_entry_t *l2;
	pt_entry_t *l3, oldl3, newl3;
	pv_entry_t next_pv, pv;
	vm_offset_t va;
	int md_gen, pvh_gen;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_remove_write: page %p is not managed", m));
	vm_page_assert_busied(m);

	if (!pmap_page_is_write_mapped(m))
		return;
	lock = VM_PAGE_TO_PV_LIST_LOCK(m);
	pvh = (m->flags & PG_FICTITIOUS) != 0 ? &pv_dummy :
	    pa_to_pvh(VM_PAGE_TO_PHYS(m));
	rw_rlock(&pvh_global_lock);
retry_pv_loop:
	rw_wlock(lock);
	TAILQ_FOREACH_SAFE(pv, &pvh->pv_list, pv_next, next_pv) {
		pmap = PV_PMAP(pv);
		if (!PMAP_TRYLOCK(pmap)) {
			pvh_gen = pvh->pv_gen;
			rw_wunlock(lock);
			PMAP_LOCK(pmap);
			rw_wlock(lock);
			if (pvh_gen != pvh->pv_gen) {
				PMAP_UNLOCK(pmap);
				rw_wunlock(lock);
				goto retry_pv_loop;
			}
		}
		va = pv->pv_va;
		l2 = pmap_l2(pmap, va);
		if ((pmap_pmd_entry(l2) & PTE_W) != 0)
			(void)pmap_demote_l2_locked(pmap, l2, va, &lock);
		KASSERT(lock == VM_PAGE_TO_PV_LIST_LOCK(m),
		    ("inconsistent pv lock %p %p for page %p",
		    lock, VM_PAGE_TO_PV_LIST_LOCK(m), m));
		PMAP_UNLOCK(pmap);
	}
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_next) {
		pmap = PV_PMAP(pv);
		if (!PMAP_TRYLOCK(pmap)) {
			pvh_gen = pvh->pv_gen;
			md_gen = m->md.pv_gen;
			rw_wunlock(lock);
			PMAP_LOCK(pmap);
			rw_wlock(lock);
			if (pvh_gen != pvh->pv_gen || md_gen != m->md.pv_gen) {
				PMAP_UNLOCK(pmap);
				rw_wunlock(lock);
				goto retry_pv_loop;
			}
		}
		l2 = pmap_l2(pmap, pv->pv_va);
		KASSERT(!(pmap_pmd_entry(l2) & PTE_HUGE),
		    ("%s: found a 2mpage in page %p's pv list", __func__, m));
		l3 = pmap_l2_to_l3(l2, pv->pv_va);
		oldl3 = pmap_pte_entry(l3);
retry:
		if ((oldl3 & PTE_W) != 0) {
			newl3 = oldl3 & ~(PTE_D | PTE_W);
			if (!pmap_cmpset(l3, &oldl3, newl3))
				goto retry;
			if ((oldl3 & PTE_D) != 0)
				vm_page_dirty(m);
			pmap_invalidate_page(pmap, pv->pv_va);
		}
		PMAP_UNLOCK(pmap);
	}
	rw_wunlock(lock);
	vm_page_aflag_clear(m, PGA_WRITEABLE);
	rw_runlock(&pvh_global_lock);
}

/*
 *	pmap_ts_referenced:
 *
 *	Return a count of reference bits for a page, clearing those bits.
 *	It is not necessary for every reference bit to be cleared, but it
 *	is necessary that 0 only be returned when there are truly no
 *	reference bits set.
 *
 *	As an optimization, update the page's dirty field if a modified bit is
 *	found while counting reference bits.  This opportunistic update can be
 *	performed at low cost and can eliminate the need for some future calls
 *	to pmap_is_modified().  However, since this function stops after
 *	finding PMAP_TS_REFERENCED_MAX reference bits, it may not detect some
 *	dirty pages.  Those dirty pages will only be detected by a future call
 *	to pmap_is_modified().
 */
int
pmap_ts_referenced(vm_page_t m)
{
	struct spglist free;
	struct md_page *pvh;
	struct rwlock *lock;
	pv_entry_t pv, pvf;
	pmap_t pmap;
	pd_entry_t *l2, l2e;
	pt_entry_t *l3, l3e;
	vm_paddr_t pa;
	vm_offset_t va;
	int cleared, md_gen, not_cleared, pvh_gen;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("pmap_ts_referenced: page %p is not managed", m));
	SLIST_INIT(&free);
	cleared = 0;
	pa = VM_PAGE_TO_PHYS(m);
	pvh = (m->flags & PG_FICTITIOUS) != 0 ? &pv_dummy : pa_to_pvh(pa);

	lock = PHYS_TO_PV_LIST_LOCK(pa);
	rw_rlock(&pvh_global_lock);
	rw_wlock(lock);
retry:
	not_cleared = 0;
	if ((pvf = TAILQ_FIRST(&pvh->pv_list)) == NULL)
		goto small_mappings;
	pv = pvf;
	do {
		pmap = PV_PMAP(pv);
		if (!PMAP_TRYLOCK(pmap)) {
			pvh_gen = pvh->pv_gen;
			rw_wunlock(lock);
			PMAP_LOCK(pmap);
			rw_wlock(lock);
			if (pvh_gen != pvh->pv_gen) {
				PMAP_UNLOCK(pmap);
				goto retry;
			}
		}
		va = pv->pv_va;
		l2 = pmap_l2(pmap, va);
		l2e = pmap_pmd_entry(l2);
		if ((l2e & (PTE_W | PTE_D)) == (PTE_W | PTE_D)) {
			/*
			 * Although l2e is mapping a 2MB page, because
			 * this function is called at a 4KB page granularity,
			 * we only update the 4KB page under test.
			 */
			vm_page_dirty(m);
		}

		if ((l2e & PTE_A) != 0) {
			/*
			 * Since this reference bit is shared by 512 4KB
			 * pages, it should not be cleared every time it is
			 * tested.  Apply a simple "hash" function on the
			 * physical page number, the virtual superpage number,
			 * and the pmap address to select one 4KB page out of
			 * the 512 on which testing the reference bit will
			 * result in clearing that reference bit.  This
			 * function is designed to avoid the selection of the
			 * same 4KB page for every 2MB page mapping.
			 *
			 * On demotion, a mapping that hasn't been referenced
			 * is simply destroyed.  To avoid the possibility of a
			 * subsequent page fault on a demoted wired mapping,
			 * always leave its reference bit set.  Moreover,
			 * since the superpage is wired, the current state of
			 * its reference bit won't affect page replacement.
			 */
			if ((((pa >> PAGE_SHIFT) ^ (pv->pv_va >> L2_SHIFT) ^
			    (uintptr_t)pmap) & (Ln_ENTRIES - 1)) == 0 &&
			    (l2e & PTE_SW_WIRED) == 0) {
				pmap_clear_bits(l2, PTE_V);
				pmap_invalidate_range(pmap, va & ~L2_OFFSET,
				    (va & ~L2_OFFSET) + L2_SIZE);
				cleared++;
			} else
				not_cleared++;
		}
		PMAP_UNLOCK(pmap);
		/* Rotate the PV list if it has more than one entry. */
		if (pv != NULL && TAILQ_NEXT(pv, pv_next) != NULL) {
			TAILQ_REMOVE(&pvh->pv_list, pv, pv_next);
			TAILQ_INSERT_TAIL(&pvh->pv_list, pv, pv_next);
			pvh->pv_gen++;
		}
		if (cleared + not_cleared >= PMAP_TS_REFERENCED_MAX)
			goto out;
	} while ((pv = TAILQ_FIRST(&pvh->pv_list)) != pvf);
small_mappings:
	if ((pvf = TAILQ_FIRST(&m->md.pv_list)) == NULL)
		goto out;
	pv = pvf;
	do {
		pmap = PV_PMAP(pv);
		if (!PMAP_TRYLOCK(pmap)) {
			pvh_gen = pvh->pv_gen;
			md_gen = m->md.pv_gen;
			rw_wunlock(lock);
			PMAP_LOCK(pmap);
			rw_wlock(lock);
			if (pvh_gen != pvh->pv_gen || md_gen != m->md.pv_gen) {
				PMAP_UNLOCK(pmap);
				goto retry;
			}
		}
		l2 = pmap_l2(pmap, pv->pv_va);

		KASSERT(!(pmap_pmd_entry(l2) & PTE_HUGE),
		    ("pmap_ts_referenced: found an invalid l2 table"));

		l3 = pmap_l2_to_l3(l2, pv->pv_va);
		l3e = pmap_pte_entry(l3);

		if ((l3e & PTE_D) != 0)
			vm_page_dirty(m);
		if ((l3e & PTE_A) != 0) {
			if ((l3e & PTE_SW_WIRED) == 0) {
				/*
				 * Wired pages cannot be paged out so
				 * doing accessed bit emulation for
				 * them is wasted effort. We do the
				 * hard work for unwired pages only.
				 */
				pmap_clear_bits(l3, PTE_V);
				pmap_invalidate_page(pmap, pv->pv_va);
				cleared++;
			} else
				not_cleared++;
		}
		PMAP_UNLOCK(pmap);
		/* Rotate the PV list if it has more than one entry. */
		if (pv != NULL && TAILQ_NEXT(pv, pv_next) != NULL) {
			TAILQ_REMOVE(&m->md.pv_list, pv, pv_next);
			TAILQ_INSERT_TAIL(&m->md.pv_list, pv, pv_next);
			m->md.pv_gen++;
		}
	} while ((pv = TAILQ_FIRST(&m->md.pv_list)) != pvf && cleared +
	    not_cleared < PMAP_TS_REFERENCED_MAX);
out:
	rw_wunlock(lock);
	rw_runlock(&pvh_global_lock);
	vm_page_free_pages_toq(&free, false);
	return (cleared + not_cleared);
}

/*
 *	Apply the given advice to the specified range of addresses within the
 *	given pmap.  Depending on the advice, clear the referenced and/or
 *	modified flags in each mapping and set the mapped page's dirty field.
 */
void
pmap_advise(pmap_t pmap, vm_offset_t sva, vm_offset_t eva, int advice)
{
}

/*
 *	Clear the modify bits on the specified physical page.
 */
void
pmap_clear_modify(vm_page_t m)
{
	struct md_page *pvh;
	struct rwlock *lock;
	pmap_t pmap;
	pv_entry_t next_pv, pv;
	pd_entry_t *l2, oldl2;
	pt_entry_t *l3;
	vm_offset_t va;
	int md_gen, pvh_gen;

	KASSERT((m->oflags & VPO_UNMANAGED) == 0,
	    ("%s: page %p is not managed", __func__, m));
	vm_page_assert_busied(m);

	if (!pmap_page_is_write_mapped(m))
	        return;

	pvh = (m->flags & PG_FICTITIOUS) != 0 ? &pv_dummy :
	    pa_to_pvh(VM_PAGE_TO_PHYS(m));
	lock = VM_PAGE_TO_PV_LIST_LOCK(m);
	rw_rlock(&pvh_global_lock);
	rw_wlock(lock);
restart:
	TAILQ_FOREACH_SAFE(pv, &pvh->pv_list, pv_next, next_pv) {
		pmap = PV_PMAP(pv);
		if (!PMAP_TRYLOCK(pmap)) {
			pvh_gen = pvh->pv_gen;
			rw_wunlock(lock);
			PMAP_LOCK(pmap);
			rw_wlock(lock);
			if (pvh_gen != pvh->pv_gen) {
				PMAP_UNLOCK(pmap);
				goto restart;
			}
		}
		va = pv->pv_va;
		l2 = pmap_l2(pmap, va);
		oldl2 = pmap_pmd_entry(l2);
		/* If oldl2 has PTE_W set, then it also has PTE_D set. */
		if ((oldl2 & PTE_W) != 0 &&
		    pmap_demote_l2_locked(pmap, l2, va, &lock) &&
		    (oldl2 & PTE_SW_WIRED) == 0) {
			/*
			 * Write protect the mapping to a single page so that
			 * a subsequent write access may repromote.
			 */
			va += VM_PAGE_TO_PHYS(m) - PTE_TO_PHYS(oldl2);
			l3 = pmap_l2_to_l3(l2, va);
			pmap_clear_bits(l3, PTE_D | PTE_W);
			vm_page_dirty(m);
			pmap_invalidate_page(pmap, va);
		}
		PMAP_UNLOCK(pmap);
	}
	TAILQ_FOREACH(pv, &m->md.pv_list, pv_next) {
		pmap = PV_PMAP(pv);
		if (!PMAP_TRYLOCK(pmap)) {
			md_gen = m->md.pv_gen;
			pvh_gen = pvh->pv_gen;
			rw_wunlock(lock);
			PMAP_LOCK(pmap);
			rw_wlock(lock);
			if (pvh_gen != pvh->pv_gen || md_gen != m->md.pv_gen) {
				PMAP_UNLOCK(pmap);
				goto restart;
			}
		}
		l2 = pmap_l2(pmap, pv->pv_va);
		KASSERT(!(pmap_pmd_entry(l2) & PTE_HUGE),
		    ("%s: found a 2mpage in page %p's pv list", __func__, m));
		l3 = pmap_l2_to_l3(l2, pv->pv_va);
		if ((pmap_pte_entry(l3) & (PTE_D | PTE_W)) == (PTE_D | PTE_W)) {
			pmap_clear_bits(l3, PTE_D | PTE_W);
			pmap_invalidate_page(pmap, pv->pv_va);
		}
		PMAP_UNLOCK(pmap);
	}
	rw_wunlock(lock);
	rw_runlock(&pvh_global_lock);
}

void *
pmap_mapbios(vm_paddr_t pa, vm_size_t size)
{
	vm_offset_t off, va;

	/* In the direct-map range: just use the DMAP. */
	if (PHYS_IN_DMAP(pa) && PHYS_IN_DMAP(pa + size - 1))
		return ((void *)PHYS_TO_DMAP(pa));

	/* Out of DMAP (MMIO / device memory): allocate KVA and map. */
	off = pa & PAGE_MASK;
	size = roundup2(off + size, PAGE_SIZE);
	va = (vm_offset_t)kva_alloc(size);
	if (va == 0)
		return (NULL);
	pmap_kenter_device(va, size, pa & ~PAGE_MASK);
	return ((void *)(va + off));
}

void
pmap_unmapbios(void *p, vm_size_t size)
{
	vm_offset_t va, off;

	va = (vm_offset_t)p;
	off = va & PAGE_MASK;
	/* DMAP addresses are permanent (no-op). */
	if (VIRT_IN_DMAP(va))
		return;
	/* KVA mappings: remove and free. */
	size = roundup2(off + size, PAGE_SIZE);
	pmap_qremove((void *)(va - off), atop(size));
	kva_free((void *)(va - off), size);
}

/*
 * Sets the memory attribute for the specified page.
 */
void
pmap_page_set_memattr(vm_page_t m, vm_memattr_t ma)
{
	if (m->md.pv_memattr == ma)
		return;

	m->md.pv_memattr = ma;

	/*
	 * If "m" is a normal page, update its direct mapping.  This update
	 * can be relied upon to perform any cache operations that are
	 * required for data coherence.
	 */
	if ((m->flags & PG_FICTITIOUS) == 0 &&
	    pmap_change_attr((void *)PHYS_TO_DMAP(VM_PAGE_TO_PHYS(m)),
	    PAGE_SIZE, m->md.pv_memattr) != 0)
		panic("memory attribute change on the direct map failed");
}

/*
 * Changes the specified virtual address range's memory type to that given by
 * the parameter "mode".  The specified virtual address range must be
 * completely contained within either the direct map or the kernel map.
 *
 * Returns zero if the change completed successfully, and either EINVAL or
 * ENOMEM if the change failed.  Specifically, EINVAL is returned if some part
 * of the virtual address range was not mapped, and ENOMEM is returned if
 * there was insufficient memory available to complete the change.  In the
 * latter case, the memory type may have been changed on some part of the
 * virtual address range.
 */
int
pmap_change_attr(void *va, vm_size_t size, int mode)
{
	int error;

	PMAP_LOCK(kernel_pmap);
	error = pmap_change_attr_locked(va, size, mode);
	PMAP_UNLOCK(kernel_pmap);
	return (error);
}

/*
 * Translate a VM_MEMATTR_* value into the corresponding PTE MAT field bits.
 * The absence of MAT bits selects strongly-ordered uncached (PTE_SUC).
 */
static inline pt_entry_t
pmap_memattr_bits(vm_memattr_t mode)
{

	switch (mode) {
	case VM_MEMATTR_UNCACHEABLE:
	case VM_MEMATTR_DEVICE:
		return (PTE_SUC);
	case VM_MEMATTR_WRITE_COMBINING:
		return (PTE_WUC);
	default:
		return (PTE_CC);
	}
}

static int
pmap_change_attr_locked(void *addr, vm_size_t size, int mode)
{
	vm_offset_t base, offset, tmpva, va;
	vm_paddr_t phys;
	pd_entry_t *l1, l1e;
	pd_entry_t *l2, l2e;
	pt_entry_t *l3, l3e;
	pt_entry_t bits, mask;
	bool anychanged = false;
	int error = 0;

	PMAP_LOCK_ASSERT(kernel_pmap, MA_OWNED);
	va = (vm_offset_t)addr;
	base = trunc_page(va);
	offset = va & PAGE_MASK;
	size = round_page(offset + size);

	if (!VIRT_IN_DMAP(base) &&
	    !(base >= VM_MIN_KERNEL_ADDRESS && base < VM_MAX_KERNEL_ADDRESS))
		return (EINVAL);

	bits = pmap_memattr_bits(mode);
	mask = PTE_MAT_MASK;

	/* First loop: perform PTE validation and demotions as necessary. */
	for (tmpva = base; tmpva < base + size; ) {
		l1 = pmap_l1(kernel_pmap, tmpva);
		if (l1 == NULL || !pmap_pud_valid(l1))
			return (EINVAL);
		l1e = pmap_pud_entry(l1);
		if ((l1e & PTE_HUGE) != 0) {
			/*
			 * If the existing entry has the correct attributes,
			 * then no need to demote.
			 */
			if ((l1e & mask) == bits) {
				tmpva = (tmpva & ~L1_OFFSET) + L1_SIZE;
				continue;
			}

			/*
			 * If the 1GB page fits in the remaining range, we
			 * don't need to demote.
			 */
			if ((tmpva & L1_OFFSET) == 0 &&
			    tmpva + L1_SIZE <= base + size) {
				tmpva += L1_SIZE;
				continue;
			}

			if (!pmap_demote_l1(kernel_pmap, l1, tmpva))
				return (ENOMEM);
			continue;
		}
		l2 = pmap_l1_to_l2(l1, tmpva);
		if (!pmap_pmd_valid(l2))
			return (EINVAL);
		l2e = pmap_pmd_entry(l2);
		if ((l2e & PTE_HUGE) != 0) {
			/*
			 * If the existing entry has the correct attributes,
			 * then no need to demote.
			 */
			if ((l2e & mask) == bits) {
				tmpva = (tmpva & ~L2_OFFSET) + L2_SIZE;
				continue;
			}

			/*
			 * If the 2MB page fits in the remaining range, we
			 * don't need to demote.
			 */
			if ((tmpva & L2_OFFSET) == 0 &&
			    tmpva + L2_SIZE <= base + size) {
				tmpva += L2_SIZE;
				continue;
			}

			if (!pmap_demote_l2(kernel_pmap, l2, tmpva))
				return (ENOMEM);
			continue;
		}
		l3 = pmap_l2_to_l3(l2, tmpva);
		if (!pmap_pte_valid(l3))
			return (EINVAL);

		tmpva += PAGE_SIZE;
	}

	/* Second loop: perform PTE updates. */
	for (tmpva = base; tmpva < base + size; ) {
		l1 = pmap_l1(kernel_pmap, tmpva);
		l1e = pmap_pud_entry(l1);
		if ((l1e & PTE_HUGE) != 0) {
			/* Unchanged. */
			if ((l1e & mask) == bits) {
				tmpva += L1_SIZE;
				continue;
			}

			pmap_store(l1, (l1e & ~mask) | bits);
			anychanged = true;
			tmpva += L1_SIZE;

			/* Update the corresponding DMAP entry. */
			phys = PTE_TO_PHYS(l1e);
			if (!VIRT_IN_DMAP(tmpva) && PHYS_IN_DMAP(phys)) {
				error = pmap_change_attr_locked(
				    (void *)PHYS_TO_DMAP(phys), L1_SIZE, mode);
				if (error != 0)
					break;
			}
			continue;
		}

		l2 = pmap_l1_to_l2(l1, tmpva);
		l2e = pmap_pmd_entry(l2);
		if ((l2e & PTE_HUGE) != 0) {
			/* Unchanged. */
			if ((l2e & mask) == bits) {
				tmpva += L2_SIZE;
				continue;
			}

			pmap_store(l2, (l2e & ~mask) | bits);
			anychanged = true;
			tmpva += L2_SIZE;

			/* Update the corresponding DMAP entry. */
			phys = PTE_TO_PHYS(l2e);
			if (!VIRT_IN_DMAP(tmpva) && PHYS_IN_DMAP(phys)) {
				error = pmap_change_attr_locked(
				    (void *)PHYS_TO_DMAP(phys), L2_SIZE, mode);
				if (error != 0)
					break;
			}
			continue;
		}

		l3 = pmap_l2_to_l3(l2, tmpva);
		l3e = pmap_load(l3);

		/* Unchanged. */
		if ((l3e & mask) == bits) {
			tmpva += PAGE_SIZE;
			continue;
		}

		pmap_store(l3, (l3e & ~mask) | bits);
		anychanged = true;
		tmpva += PAGE_SIZE;

		/* Update the corresponding DMAP entry. */
		phys = PTE_TO_PHYS(l3e);
		if (!VIRT_IN_DMAP(tmpva) && PHYS_IN_DMAP(phys)) {
			error = pmap_change_attr_locked(
			    (void *)PHYS_TO_DMAP(phys), L3_SIZE, mode);
			if (error != 0)
				break;
		}
	}

	if (anychanged) {
		/*
		 * The second loop may advance "tmpva" past the end of the
		 * range by up to a superpage size when it skips an
		 * already-correct superpage; only invalidate what we changed.
		 */
		pmap_invalidate_range(kernel_pmap, base,
		    MIN(tmpva, base + size));
		/*
		 * No cache maintenance is required for a cached to uncached
		 * transition: LoongArch maintains cache coherency in
		 * hardware.
		 */
	}

	return (error);
}

/*
 * Perform the pmap work for mincore(2).  If the page is not both referenced and
 * modified by this pmap, returns its physical address so that the caller can
 * find other mappings.
 */
int
pmap_mincore(pmap_t pmap, vm_offset_t addr, vm_paddr_t *pap)
{
	pt_entry_t *l2, *l3, tpte;
	vm_paddr_t pa;
	int val;
	bool managed;

	PMAP_LOCK(pmap);
	l2 = pmap_l2(pmap, addr);
	if (l2 != NULL && pmap_pmd_valid(l2)) {
		tpte = pmap_pmd_entry(l2);
		if (tpte & PTE_HUGE) {
			pa = PTE_TO_PHYS(tpte) | (addr & L2_OFFSET);
			val = MINCORE_INCORE | MINCORE_PSIND(1);
		} else {
			l3 = pmap_l2_to_l3(l2, addr);
			if (!pmap_pte_valid(l3)) {
				PMAP_UNLOCK(pmap);
				return (0);
			}
			tpte = pmap_pte_entry(l3);
			pa = PTE_TO_PHYS(tpte) | (addr & L3_OFFSET);
			val = MINCORE_INCORE;
		}

		if ((tpte & PTE_D) != 0)
			val |= MINCORE_MODIFIED | MINCORE_MODIFIED_OTHER;
		if ((tpte & PTE_A) != 0)
			val |= MINCORE_REFERENCED | MINCORE_REFERENCED_OTHER;
		managed = (tpte & PTE_SW_MANAGED) == PTE_SW_MANAGED;
	} else {
		managed = false;
		val = 0;
	}
	if ((val & (MINCORE_MODIFIED_OTHER | MINCORE_REFERENCED_OTHER)) !=
	    (MINCORE_MODIFIED_OTHER | MINCORE_REFERENCED_OTHER) && managed) {
		*pap = pa;
	}
	PMAP_UNLOCK(pmap);
	return (val);
}

void
pmap_activate_sw(struct thread *td)
{
	pmap_t oldpmap, pmap;
	u_int cpuid;
	bool need_tlb_flush = false;

	oldpmap = PCPU_GET(curpmap);
	pmap = vmspace_pmap(td->td_proc->p_vmspace);
	if (pmap == oldpmap) {
		la_watch_load(td);
		return;
	}

	cpuid = PCPU_GET(cpuid);
#ifdef SMP
	CPU_SET_ATOMIC(cpuid, &pmap->pm_active);
#else
	CPU_SET(cpuid, &pmap->pm_active);
#endif
	/*
	 * Invalidate oldpmap's ASID on this CPU BEFORE removing it from
	 * pm_active.  This preserves the invariant that pm_active is exactly
	 * the set of CPUs holding a live ASID for a pmap.  If we cleared
	 * pm_active first, a concurrent remote shooter checking pm_active
	 * would skip this CPU, but stale TLB entries with the old ASID could
	 * still match if the ASID is later recycled (ASID wraparound).
	 *
	 * Zeroing asid[cpuid] also forces a fresh ASID next time oldpmap is
	 * activated here, so the vfork shared-pmap case is still covered.
	 */
	if (oldpmap != kernel_pmap) {
		la_stale_add(pmap_get_asid(oldpmap, cpuid));
		oldpmap->asid[cpuid] = 0;
	}
#ifdef SMP
	CPU_CLR_ATOMIC(cpuid, &oldpmap->pm_active);
#else
	CPU_CLR(cpuid, &oldpmap->pm_active);
#endif

	if (!pmap_asid_valid(pmap, cpuid))
		pmap_new_asid(pmap, cpuid, &need_tlb_flush);

	/*
	 * On an ASID generation wrap, pmap_new_asid() recycled the low ASID
	 * bits, which may match stale TLB entries from the previous generation.
	 * Flush all G=0 entries BEFORE loading the new ASID: flush_tlb_user()
	 * is INVTLB_CURRENT_GFALSE (ASID-agnostic), so it evicts those stale
	 * entries regardless of which ASID is currently loaded, ensuring none
	 * can survive to collide with the freshly-installed ASID.
	 */
	if (need_tlb_flush) {
		flush_tlb_user();
		/*
		 * The wrap evicted every G=0 entry on this CPU, so the
		 * stale-ASID list (old-generation numbers) is obsolete.  Drop
		 * it now, before those numbers are recycled onto new pmaps and
		 * a later drain spuriously flushes a live address space.
		 */
		la_stale_reset();
	}

	/*
	 * Switch the user-space page table base.  PGDL is used for
	 * addresses with VA[63]=0 (user space).  PGDH was set at boot
	 * to the kernel page table and never changes.
	 */
	set_pgd_asid(pmap_get_asid(pmap, cpuid), pmap->pm_pgdl);

	PCPU_SET(curpmap, pmap);

	/*
	 * Program the incoming thread's watchpoints now that the new pmap's
	 * ASID is installed on this CPU.  Runs on every switch (including the
	 * same-pmap thread switch above) so a previous thread's watchpoints
	 * cannot linger.
	 */
	la_watch_load(td);
}

void
pmap_activate(struct thread *td)
{

	critical_enter();
	pmap_activate_sw(td);
	critical_exit();
}

void
pmap_activate_boot(pmap_t pmap)
{
	u_int cpuid;

	cpuid = PCPU_GET(cpuid);
#ifdef SMP
	CPU_SET_ATOMIC(cpuid, &pmap->pm_active);
#else
	CPU_SET(cpuid, &pmap->pm_active);
#endif
	PCPU_SET(curpmap, pmap);
	csr_write64(pmap->pm_pgdl, LOONGARCH_CSR_PGDL);
	csr_write64(pmap->pm_pgdh, LOONGARCH_CSR_PGDH);

	/*
	 * Discard all TLB entries on this CPU.  APs join pm_active after the
	 * BSP has already mapped kernel pages (dpcpu zones, boot stacks); any
	 * TLB entries filled during the AP's early boot (before it could
	 * receive shootdown IPIs) may be stale — even/odd pair entries where
	 * the partner half was valid when the pair was filled but later
	 * invalidated, or entries mapping the wrong PFN.  A full flush forces
	 * the HW walker to re-walk the (now current) page tables on first use.
	 */
	invtlb_all(INVTLB_CURRENT_ALL);
}

void
pmap_active_cpus(pmap_t pmap, cpuset_t *res)
{
	*res = pmap->pm_active;
}

void
pmap_sync_icache(pmap_t pmap, vm_offset_t va, vm_size_t sz)
{

	/*
	 * Flush the local icache to make any recent data stores visible
	 * as instructions.  Remote cpus will flush their own icache on
	 * the next context switch or exception return.  We do not issue
	 * remote IPIs here because tlb_invalidate_all_remote spin-waits
	 * with a global counter that cannot handle concurrent callers,
	 * and the icache flush is not urgent enough to warrant a complex
	 * synchronization protocol.
	 */
	flush_icache();
}

/*
 *	Increase the starting virtual address of the given mapping if a
 *	different alignment might result in more superpage mappings.
 */
void
pmap_align_superpage(vm_object_t object, vm_ooffset_t offset,
    vm_offset_t *addr, vm_size_t size)
{
	vm_offset_t superpage_offset;

	if (size < L2_SIZE)
		return;
	if (object != NULL && (object->flags & OBJ_COLORED) != 0)
		offset += ptoa(object->pg_color);
	superpage_offset = offset & L2_OFFSET;
	if (size - ((L2_SIZE - superpage_offset) & L2_OFFSET) < L2_SIZE ||
	    (*addr & L2_OFFSET) == superpage_offset)
		return;
	if ((*addr & L2_OFFSET) < superpage_offset)
		*addr = (*addr & ~L2_OFFSET) + superpage_offset;
	else
		*addr = ((*addr + L2_OFFSET) & ~L2_OFFSET) + superpage_offset;
}

/**
 * Get the kernel virtual address of a set of physical pages. If there are
 * physical addresses not covered by the DMAP perform a transient mapping
 * that will be removed when calling pmap_unmap_io_transient.
 *
 * \param page        The pages the caller wishes to obtain the virtual
 *                    address on the kernel memory map.
 * \param vaddr       On return contains the kernel virtual memory address
 *                    of the pages passed in the page parameter.
 * \param count       Number of pages passed in.
 * \param can_fault   true if the thread using the mapped pages can take
 *                    page faults, false otherwise.
 *
 * \returns true if the caller must call pmap_unmap_io_transient when
 *          finished or false otherwise.
 *
 */
bool
pmap_map_io_transient(vm_page_t page[], void *vaddr[], int count,
    bool can_fault)
{
	vm_paddr_t paddr;
	vmem_addr_t addr;
	bool needs_mapping;
	int error __diagused, i;

	/*
	 * Allocate any KVA space that we need, this is done in a separate
	 * loop to prevent calling vmem_alloc while pinned.
	 */
	needs_mapping = false;
	for (i = 0; i < count; i++) {
		paddr = VM_PAGE_TO_PHYS(page[i]);
		if (__predict_false(paddr >= DMAP_MAX_PHYSADDR)) {
			error = vmem_alloc(kernel_arena, PAGE_SIZE,
			    M_BESTFIT | M_WAITOK, &addr);
			KASSERT(error == 0, ("vmem_alloc failed: %d", error));
			vaddr[i] = (void*)addr;
			needs_mapping = true;
		} else {
			vaddr[i] = (void*)PHYS_TO_DMAP(paddr);
		}
	}

	/* Exit early if everything is covered by the DMAP */
	if (!needs_mapping)
		return (false);

	if (!can_fault)
		sched_pin();
	for (i = 0; i < count; i++) {
		paddr = VM_PAGE_TO_PHYS(page[i]);
		if (paddr >= DMAP_MAX_PHYSADDR) {
			panic(
			   "pmap_map_io_transient: TODO: Map out of DMAP data");
		}
	}

	return (needs_mapping);
}

void
pmap_unmap_io_transient(vm_page_t page[], void *vaddr[], int count,
    bool can_fault)
{
	vm_paddr_t paddr;
	int i;

	if (!can_fault)
		sched_unpin();
	for (i = 0; i < count; i++) {
		paddr = VM_PAGE_TO_PHYS(page[i]);
		if (paddr >= DMAP_MAX_PHYSADDR) {
			panic("TODO: pmap_unmap_io_transient: Unmap data");
		}
	}
}

bool
pmap_is_valid_memattr(pmap_t pmap __unused, vm_memattr_t mode)
{

	return (mode >= VM_MEMATTR_DEVICE && mode <= VM_MEMATTR_WRITE_BACK);
}

bool
pmap_get_tables(pmap_t pmap, vm_offset_t va, pd_entry_t **l1, pd_entry_t **l2,
    pt_entry_t **l3)
{
	pd_entry_t *l1p, *l2p;

	/* Get l1 directory entry. */
	l1p = pmap_l1(pmap, va);
	*l1 = l1p;

	if (l1p == NULL || !pmap_pud_valid(l1p))
		return (false);

	if (pmap_pud_entry(l1p) & PTE_HUGE) {
		*l2 = NULL;
		*l3 = NULL;
		return (true);
	}

	/* Get l2 directory entry. */
	l2p = pmap_l1_to_l2(l1p, va);
	*l2 = l2p;

	if (l2p == NULL || !pmap_pmd_valid(l2p))
		return (false);

	if (pmap_pmd_entry(l2p) & PTE_HUGE) {
		*l3 = NULL;
		return (true);
	}

	/* Get l3 page table entry. */
	*l3 = pmap_l2_to_l3(l2p, va);

	return (true);
}

/*
 * Track a range of the kernel's virtual address space that is contiguous
 * in various mapping attributes.
 */
struct pmap_kernel_map_range {
	vm_offset_t sva;
	pt_entry_t attrs;
	int l3pages;
	int l2pages;
	int l1pages;
};

static void
sysctl_kmaps_dump(struct sbuf *sb, struct pmap_kernel_map_range *range,
    vm_offset_t eva)
{

	if (eva <= range->sva)
		return;

	sbuf_printf(sb, "0x%016lx-0x%016lx r%c%c%c%c %d %d %d\n",
	    range->sva, eva,
	    (range->attrs & PTE_W) == PTE_W ? 'w' : '-',
	    (range->attrs & PTE_NX) == 0 ? 'x' : '-',
	    (range->attrs & PTE_U) == PTE_U ? 'u' : 's',
	    (range->attrs & PTE_G) == PTE_G ? 'g' : '-',
	    range->l1pages, range->l2pages, range->l3pages);

	/* Reset to sentinel value. */
	range->sva = 0xfffffffffffffffful;
}

/*
 * Determine whether the attributes specified by a page table entry match those
 * being tracked by the current range.
 */
static bool
sysctl_kmaps_match(struct pmap_kernel_map_range *range, pt_entry_t attrs)
{

	return (range->attrs == attrs);
}

static void
sysctl_kmaps_reinit(struct pmap_kernel_map_range *range, vm_offset_t va,
    pt_entry_t attrs)
{

	memset(range, 0, sizeof(*range));
	range->sva = va;
	range->attrs = attrs;
}

/*
 * Given a leaf PTE, derive the mapping's attributes. If they do not match
 * those of the current run, dump the address range and its attributes, and
 * begin a new run.
 */
static void
sysctl_kmaps_check(struct sbuf *sb, struct pmap_kernel_map_range *range,
    vm_offset_t va, pd_entry_t l1e, pd_entry_t l2e, pt_entry_t l3e)
{
	pt_entry_t attrs;

	/* The PTE global bit is inherited by lower levels. */
	attrs = l1e & PTE_G;
	if (l1e & PTE_HUGE)
		attrs |= l1e & (PTE_W | PTE_U);
	else if (l2e != 0)
		attrs |= l2e & PTE_G;
	if (l2e & PTE_HUGE)
		attrs |= l2e & (PTE_W | PTE_U);
	else if (l3e != 0)
		attrs |= l3e & (PTE_W | PTE_U | PTE_G);

	if (range->sva > va || !sysctl_kmaps_match(range, attrs)) {
		sysctl_kmaps_dump(sb, range, va);
		sysctl_kmaps_reinit(range, va, attrs);
	}
}

static int
sysctl_kmaps(SYSCTL_HANDLER_ARGS)
{
	struct pmap_kernel_map_range range;
	struct sbuf sbuf, *sb;
	pd_entry_t *l1, l1e, *l2, l2e;
	pt_entry_t *l3, l3e;
	vm_offset_t sva;
	vm_paddr_t pa;
	int error, i, j, k;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);
	sb = &sbuf;
	sbuf_new_for_sysctl(sb, NULL, PAGE_SIZE, req);

	/* Sentinel value. */
	range.sva = 0xfffffffffffffffful;

	/*
	 * Iterate over the kernel page tables without holding the kernel pmap
	 * lock. Kernel page table pages are never freed, so at worst we will
	 * observe inconsistencies in the output.
	 */
	sva = VM_MIN_KERNEL_ADDRESS;
	for (i = pmap_l1_index(sva); i < Ln_ENTRIES; i++) {
		if (i == pmap_l1_index(DMAP_MIN_ADDRESS))
			sbuf_printf(sb, "\nDirect map:\n");
		else if (i == pmap_l1_index(VM_MIN_KERNEL_ADDRESS))
			sbuf_printf(sb, "\nKernel map:\n");

		l1 = pmap_l1(kernel_pmap, sva);
		if (!pmap_pud_valid(l1)) {
			sysctl_kmaps_dump(sb, &range, sva);
			sva += L1_SIZE;
			continue;
		}
		l1e = pmap_pud_entry(l1);
		if (l1e & PTE_HUGE) {
			sysctl_kmaps_check(sb, &range, sva, l1e, 0, 0);
			range.l1pages++;
			sva += L1_SIZE;
			continue;
		}
		pa = PTE_TO_PHYS(l1e);
		l2 = (pd_entry_t *)PHYS_TO_DMAP(pa);

		for (j = pmap_l2_index(sva); j < Ln_ENTRIES; j++) {
			if (!pmap_pmd_valid(&l2[j])) {
				sysctl_kmaps_dump(sb, &range, sva);
				sva += L2_SIZE;
				continue;
			}
			l2e = pmap_pmd_entry(&l2[j]);
			if (l2e & PTE_HUGE) {
				sysctl_kmaps_check(sb, &range, sva,
				    l1e, l2e, 0);
				range.l2pages++;
				sva += L2_SIZE;
				continue;
			}
			pa = PTE_TO_PHYS(l2e);
			l3 = (pd_entry_t *)PHYS_TO_DMAP(pa);

			for (k = pmap_l3_index(sva); k < Ln_ENTRIES; k++,
			    sva += L3_SIZE) {
				if (!pmap_pte_valid(&l3[k])) {
					sysctl_kmaps_dump(sb, &range, sva);
					continue;
				}
				l3e = pmap_pte_entry(&l3[k]);
				sysctl_kmaps_check(sb, &range, sva,
				    l1e, l2e, l3e);
				range.l3pages++;
			}
		}
	}

	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}
SYSCTL_OID(_vm_pmap, OID_AUTO, kernel_maps,
    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE | CTLFLAG_SKIP,
    NULL, 0, sysctl_kmaps, "A",
    "Dump kernel address layout");
