/*-
 * Copyright (c) 1991 Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and William Jolitz of UUNET Technologies Inc.
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
 */

#ifndef _MACHINE_PMAP_H_
#define	_MACHINE_PMAP_H_

#include <machine/pte.h>

#ifndef LOCORE

#include <sys/queue.h>
#include <sys/_cpuset.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/_pv_entry.h>

#include <vm/_vm_radix.h>
#include <sys/pcpu.h>
#include <machine/cpu.h>
#include <machine/tlb.h>
#include <machine/elf.h>

#ifdef _KERNEL

#define	vtophys(va)	pmap_kextract((vm_offset_t)(va))

#endif

#define	pmap_page_get_memattr(m)	((m)->md.pv_memattr)
#define	pmap_page_is_write_mapped(m)				\
	(((m)->a.flags & PGA_WRITEABLE) != 0)
void pmap_page_set_memattr(vm_page_t m, vm_memattr_t ma);
#define	pmap_map_delete(pmap, sva, eva)				\
	pmap_remove(pmap, sva, eva)

/*
 * Pmap stuff
 */

struct md_page {
	TAILQ_HEAD(,pv_entry)	pv_list;
	int			pv_gen;
	vm_memattr_t		pv_memattr;
};

/*
 * This structure is used to hold a virtual<->physical address
 * association and is used mostly by bootstrap code
 */
struct pv_addr {
	SLIST_ENTRY(pv_addr) pv_list;
	vm_offset_t	pv_va;
	vm_paddr_t	pv_pa;
};

struct pmap {
	struct mtx		pm_mtx;
	struct pmap_statistics	pm_stats;	/* pmap statictics */
	pd_entry_t		*pm_top; /* top-level page table page */
	u_long			pm_pgdl;
			/* page global directory base address lower half*/
	u_long			pm_pgdh;
			/* page global directory base address higher half*/
	cpuset_t		pm_active;	/* active on cpus */
	TAILQ_HEAD(,pv_chunk)	pm_pvchunk;
			/* list of mappings in pmap */
	struct vm_radix		pm_root;
	uint64_t		asid[MAXCPU];		/* current asid */
};

typedef struct pmap *pmap_t;

#ifdef _KERNEL
extern struct pmap	kernel_pmap_store;
#define	kernel_pmap	(&kernel_pmap_store)
#define	pmap_kernel()	kernel_pmap

#define	PMAP_ASSERT_LOCKED(pmap) \
				mtx_assert(&(pmap)->pm_mtx, MA_OWNED)
#define	PMAP_LOCK(pmap)		mtx_lock(&(pmap)->pm_mtx)
#define	PMAP_LOCK_ASSERT(pmap, type) \
				mtx_assert(&(pmap)->pm_mtx, (type))
#define	PMAP_LOCK_DESTROY(pmap)	mtx_destroy(&(pmap)->pm_mtx)
#define	PMAP_LOCK_INIT(pmap)	mtx_init(&(pmap)->pm_mtx, "pmap", \
				    NULL, MTX_DEF | MTX_DUPOK)
#define	PMAP_OWNED(pmap)	mtx_owned(&(pmap)->pm_mtx)
#define	PMAP_MTX(pmap)		(&(pmap)->pm_mtx)
#define	PMAP_TRYLOCK(pmap)	mtx_trylock(&(pmap)->pm_mtx)
#define	PMAP_UNLOCK(pmap)	mtx_unlock(&(pmap)->pm_mtx)

extern vm_offset_t virtual_avail;
extern vm_offset_t virtual_end;

/*
 * Macros to test if a mapping is mappable with an L1 Section mapping
 * or an L2 Large Page mapping.
 */
#define	L1_MAPPABLE_P(va, pa, size)				\
	((((va) | (pa)) & L1_OFFSET) == 0 && (size) >= L1_SIZE)

enum pmap_mode {
	PMAP_MODE_LA48,
};

extern enum pmap_mode pmap_mode;

/* Check if an address resides in a mappable region. */
#define	VIRT_IS_VALID(va)					\
	((va) < VM_MAX_USER_ADDRESS || (va) >= VM_MIN_KERNEL_ADDRESS)

struct thread;

#define	pmap_vm_page_alloc_check(m)

void	pmap_activate_boot(pmap_t);
void	pmap_activate_sw(struct thread *);
void	pmap_bootstrap(vm_offset_t, vm_paddr_t, vm_size_t);
int	pmap_change_attr(void *va, vm_size_t size, int mode);
void	pmap_kenter(vm_offset_t sva, vm_size_t size, vm_paddr_t pa,
	    int mode);
void	pmap_kenter_device(vm_offset_t, vm_size_t, vm_paddr_t);
vm_paddr_t pmap_kextract(vm_offset_t va);
void	pmap_kremove(vm_offset_t);
void	pmap_kremove_device(vm_offset_t, vm_size_t);
void	*pmap_mapdev_attr(vm_paddr_t pa, vm_size_t size, vm_memattr_t ma);
bool	pmap_page_is_mapped(vm_page_t m);
bool	pmap_ps_enabled(pmap_t);

void	*pmap_mapdev(vm_paddr_t, vm_size_t);
void	*pmap_mapbios(vm_paddr_t, vm_size_t);
void	pmap_unmapdev(void *, vm_size_t);
void	pmap_unmapbios(void *, vm_size_t);
void	pmap_preboot_map_attr(vm_paddr_t, vm_offset_t, vm_size_t, vm_prot_t,
	    vm_memattr_t);

bool	pmap_map_io_transient(vm_page_t *, void **, int, bool);
void	pmap_unmap_io_transient(vm_page_t *, void **, int, bool);

bool	pmap_get_tables(pmap_t, vm_offset_t, pd_entry_t **, pd_entry_t **,
    pt_entry_t **);

int	pmap_fault(pmap_t, vm_offset_t, vm_prot_t);
bool	pmap_update_tlb(pmap_t, vm_offset_t);

static inline int
pmap_vmspace_copy(pmap_t dst_pmap __unused, pmap_t src_pmap __unused)
{

	return (0);
}

extern struct cpu_desc cpu_desc[MAXCPU];

static inline void
asid_init(int cpu)
{
	struct pcpu *pc = pcpu_find(cpu);

	pc->pc_asid_mask = cpu_desc[cpu].cpuinfo.asid_mask;
	pc->pc_asid_value = pc->pc_asid_mask + 1;
	/* initial version: 1, asid: 0 */
}

static inline void
pt_walker_init(int cpu)
{
	uint32_t pwcl = PWCL;
	uint32_t pwch = PWCH;

	if (cpu_desc[cpu].cpuinfo.hwcap & HWCAP_LOONGARCH_PTW)
		pwch |= CSR_PWCTL1_PTW;

	csr_write32(pwcl, LOONGARCH_CSR_PWCTL0);
	csr_write32(pwch, LOONGARCH_CSR_PWCTL1);
	csr_write32(curcpu, LOONGARCH_CSR_TMID);
}

static inline uint64_t
asid_version_mask(int cpu)
{
	struct pcpu *pc = pcpu_find(cpu);

	return (~pc->pc_asid_mask);
}

static inline bool
pmap_asid_valid(pmap_t pmap, int cpu)
{
	struct pcpu *pc;
	uint64_t asid;

	pc = pcpu_find(cpu);
	asid = pc->pc_asid_value;

	if ((pmap->asid[cpu] ^ asid) & asid_version_mask(cpu))
		return false;

	return true;
}

static inline void
pmap_new_asid(pmap_t pmap, int cpu, bool *flush)
{
	struct pcpu *pc;
	uint64_t asid;

	pc = pcpu_find(cpu);
	asid = pc->pc_asid_value + 1;

	if ((asid & pc->pc_asid_mask) == 0)
		*flush = true;

	pc->pc_asid_value = pmap->asid[cpu] = asid;
}

static inline uint32_t
pmap_get_asid(pmap_t pmap, int cpu)
{
	struct pcpu *pc = pcpu_find(cpu);
	return (pmap->asid[cpu] & pc->pc_asid_mask);
}

static inline void
pmap_update_asid(pmap_t pmap, int cpu)
{
	struct pcpu *pc = pcpu_find(cpu);
	uint32_t asid = csr_read32(LOONGARCH_CSR_ASID) & pc->pc_asid_mask;

	if (asid == pmap_get_asid(pmap, cpu)) {
		bool need_tlb_flush = false;
		pmap_new_asid(pmap, cpu, &need_tlb_flush);
		csr_write32(pmap_get_asid(pmap, cpu), LOONGARCH_CSR_ASID);
		if (need_tlb_flush)
			flush_tlb_user();
	} else {
		/* mark as invalid */
		pmap->asid[cpu] = 0;
	}
}

#endif	/* _KERNEL */

#endif	/* !LOCORE */

#endif	/* !_MACHINE_PMAP_H_ */
