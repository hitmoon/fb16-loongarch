/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * Copyright (c) 2016 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
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

#include "opt_kstack_pages.h"
#include "opt_platform.h"
#include "opt_acpi.h"

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/cpuset.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/kdb.h>
#include <sys/sched.h>
#include <sys/smp.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>

#include <machine/intr.h>
#include <machine/cpufunc.h>
#include <machine/smp.h>
#include <machine/tlb.h>
#include <machine/vmparam.h>

#ifdef FDT
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_cpu.h>
#endif

#define	MP_BOOTSTACK_SIZE	(kstack_pages * PAGE_SIZE)

uint32_t __loongarch_boot_ap[MAXCPU];

static enum {
	CPUS_UNKNOWN,
#ifdef FDT
	CPUS_FDT,
#endif
#ifdef DEV_ACPI
	CPUS_ACPI,
#endif
} cpu_enum_method;

static device_identify_t loongarch64_cpu_identify;
static device_probe_t loongarch64_cpu_probe;
static device_attach_t loongarch64_cpu_attach;

static int ipi_handler(void *);

extern uint32_t boot_cpu;
extern cpuset_t all_cpus;
extern void cpu_exception_handler(void);

#ifdef INVARIANTS
static uint32_t cpu_reg[MAXCPU][2];
#endif
static device_t cpu_list[MAXCPU];

void mpentry(u_long cpuid);
void init_secondary(uint32_t);

static struct mtx ap_boot_mtx;

/* Stacks for AP initialization, discarded once idle threads are started. */
void *bootstack;
static void *bootstacks[MAXCPU];

/* Count of started APs, used to synchronize access to bootstack. */
static volatile int aps_started;

/* Set to 1 once we're ready to let the APs out of the pen. */
static volatile int aps_ready;

/*
 * SMP TLB shootdown state.  Each CPU owns a pool of shootdown descriptors
 * (la_tlb_pool[cpu]) and two linked lists built from them: an SLIST free list
 * (la_tlb_free[cpu]) and a pending list (la_tlb_q[cpu]).  An initiator pops a
 * descriptor, fills it, appends it to its pending list, and IPIs the targets;
 * each target drains every other initiator's pending list.  Because every
 * in-flight shootdown from a CPU has its own descriptor, rapid fire-and-forget
 * shootdowns (IE-off callers such as pmap_qenter/UMA) no longer overwrite one
 * another before the targets process them -- the race that silently dropped
 * invalidations and left stale TLBs on remote CPUs under multi-AP load.
 *
 * Per-descriptor cpusets record which targets must flush (targets) and which
 * already have (acked); a descriptor returns to the free list once every
 * target has acked it.  On pool exhaustion the initiator escalates to a full
 * flush (la_tlb_flushall_gen) rather than appending, so no invalidation lost.
 */
#define LA_TLB_POOL	128	/* shootdown descriptors per CPU */

struct la_tlb_sd {
	pmap_t		pmap;
	uint64_t	addr1;
	uint64_t	addr2;
	cpuset_t	targets;		/* CPUs that must flush this */
	cpuset_t	acked;		/* CPUs that have flushed this */
	volatile u_int	remaining; /* atomic: targets not yet acked */
	uint8_t		op;			/* LA_TLB_OP_* */
	SLIST_ENTRY(la_tlb_sd) free_link;	/* on la_tlb_free[cpu] */
	TAILQ_ENTRY(la_tlb_sd) pend_link;	/* on la_tlb_q[cpu] */
};

SLIST_HEAD(la_tlb_freelist, la_tlb_sd);
TAILQ_HEAD(la_tlb_pendingq, la_tlb_sd);

static struct la_tlb_sd la_tlb_pool[MAXCPU][LA_TLB_POOL];
static struct la_tlb_freelist la_tlb_free[MAXCPU];
static struct la_tlb_pendingq la_tlb_q[MAXCPU];
static struct mtx la_tlb_mtx[MAXCPU];	/* guards free+pending lists */
static uint32_t la_tlb_flushall_gen[MAXCPU]; /* bumped on pool exhaustion */
static uint32_t la_tlb_flushall_done[MAXCPU][MAXCPU]; /* [target][initiator] */
static cpuset_t la_tlb_pending; /* initiators with a non-empty la_tlb_q[] */

/*
 * Per-CPU stale-ASID tracking.  When pmap_activate_sw drops an old pmap's
 * ASID, the old ASID's TLB entries linger.  Record it here; la_tlb_local_flush
 * batch-flushes them (via invtlb op 4) before the target ASID's flush.
 */
#define LA_STALE_MAX	128
struct la_stale {
	uint32_t	asids[LA_STALE_MAX];
	int		count;
	bool		overflow;
};
static struct la_stale la_stale[MAXCPU];

void
la_stale_add(uint32_t asid)
{
	int cpuid;

	if (asid == 0)
		return;
	cpuid = PCPU_GET(cpuid);
	if (la_stale[cpuid].count < LA_STALE_MAX)
		la_stale[cpuid].asids[la_stale[cpuid].count++] = asid;
	else
		la_stale[cpuid].overflow = true;
}

/* Temporary variables for init_secondary()  */
void *dpcpu[MAXCPU - 1];

static device_method_t loongarch64_cpu_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	loongarch64_cpu_identify),
	DEVMETHOD(device_probe,		loongarch64_cpu_probe),
	DEVMETHOD(device_attach,	loongarch64_cpu_attach),

	DEVMETHOD_END
};

static driver_t loongarch64_cpu_driver = {
	"loongarch64_cpu",
	loongarch64_cpu_methods,
	0
};

DRIVER_MODULE(loongarch64_cpu, cpu, loongarch64_cpu_driver, 0, 0);

static void
loongarch64_cpu_identify(driver_t *driver, device_t parent)
{

	if (device_find_child(parent, "loongarch64_cpu", -1) != NULL)
		return;
	if (BUS_ADD_CHILD(parent, 0, "loongarch64_cpu", -1) == NULL)
		device_printf(parent, "add child failed\n");
}

static int
loongarch64_cpu_probe(device_t dev)
{
	u_int cpuid;

	cpuid = device_get_unit(dev);
	if (cpuid >= MAXCPU || cpuid > mp_maxid)
		return (EINVAL);

	device_quiet(dev);
	return (0);
}

static int
loongarch64_cpu_attach(device_t dev)
{
	const uint32_t *reg;
	size_t reg_size;
	u_int cpuid;
	int i;

	cpuid = device_get_unit(dev);

	if (cpuid >= MAXCPU || cpuid > mp_maxid)
		return (EINVAL);
	KASSERT(cpu_list[cpuid] == NULL, ("Already have cpu %u", cpuid));

	reg = cpu_get_cpuid(dev, &reg_size);
	if (reg == NULL)
		return (EINVAL);

	if (bootverbose) {
		device_printf(dev, "register <");
		for (i = 0; i < reg_size; i++)
			printf("%s%x", (i == 0) ? "" : " ", reg[i]);
		printf(">\n");
	}

	/* Set the device to start it later */
	cpu_list[cpuid] = dev;

	return (0);
}

static void
release_aps(void *dummy __unused)
{
	int i, tcpu, tdesc;

	if (mp_ncpus == 1)
		return;

	/* Initialize the per-CPU TLB shootdown pools before APs come online. */
	for (tcpu = 0; tcpu < MAXCPU; tcpu++) {
		SLIST_INIT(&la_tlb_free[tcpu]);
		TAILQ_INIT(&la_tlb_q[tcpu]);
		for (tdesc = 0; tdesc < LA_TLB_POOL; tdesc++)
			SLIST_INSERT_HEAD(&la_tlb_free[tcpu],
			    &la_tlb_pool[tcpu][tdesc], free_link);
		mtx_init(&la_tlb_mtx[tcpu], "la tlb shootdown",
		    NULL, MTX_SPIN);
	}

	/* Setup the IPI handler */
	cpuic_setup_ipi(ipi_handler);

	/* Enable all IPI vectors in the IOCSR IPI controller. */
	iocsr_write32(0xffffffff, LOONGARCH_IOCSR_IPI_EN);

	atomic_store_rel_int(&aps_ready, 1);

	/*
	 * APs busy-wait on aps_ready in init_secondary(), so no IPI is needed
	 * to release them.  (They can't take an IPI yet anyway: mpentry leaves
	 * CRMD.IE=0 and their IPI isn't armed until later in init_secondary.)
	 */
	printf("Release APs\n");

	for (i = 0; i < 2000; i++) {
		if (atomic_load_acq_int(&smp_started))
			return;
		DELAY(1000);
	}

	printf("APs not started\n");
}
SYSINIT(start_aps, SI_SUB_SMP, SI_ORDER_FIRST, release_aps, NULL);

void
init_secondary(uint32_t cpuid)
{
	struct pcpu *pcpup;

	if (cpuid < boot_cpu)
		cpuid += mp_maxid + 1;
	cpuid -= boot_cpu;

	/* Setup the pcpu pointer */
	pcpup = &__pcpu[cpuid];
	__asm __volatile("move $r21, %0" :: "r"(pcpup));

	csr_write64((uint64_t)pcpup, PERCPU_BASE_KS);

	/* cpu identify */
	identify_cpu(cpuid);
	/* Configure per-CPU CSRs: ASID and page table walker. */
	asid_init(cpuid);
	pt_walker_init(cpuid);

	/* eentry */
	csr_write64((uint64_t)cpu_exception_handler, LOONGARCH_CSR_EENTRY);

	/* No printf can be called before while loop exit
		pc_curthread is NULL !!!
	 */

	/* Signal the BSP and spin until it has released all APs.  Busy-wait */
	atomic_add_int(&aps_started, 1);
	while (!atomic_load_int(&aps_ready))
		cpu_spinwait();

	/* Clear the IPI. */
	ipi_read_clear(cpuid);

	/* Initialize curthread */
	KASSERT(PCPU_GET(idlethread) != NULL, ("no idle thread"));
	pcpup->pc_curthread = pcpup->pc_idlethread;
	schedinit_ap();

	/*
	 * Enable IOCSR IPI delivery; ECFG unmasking of the IRQ_IPI line is
	 * done by cpuic_init_secondary() below (it enables any IRQ with a
	 * registered handler, and cpuic_setup_ipi registered the filter).
	 */
	iocsr_write32(0xffffffff, LOONGARCH_IOCSR_IPI_EN);

	/* Initialize per-CPU EIOINTC routing */
	eiointc_init_cpu();

	/*
	 * Initialize per-CPU interrupt state for the root PIC.
	 * This re-enables any interrupts that were set up on the BSP.
	 */
	intr_pic_init_secondary();

#ifndef EARLY_AP_STARTUP
	/* Start per-CPU event timers. */
	cpu_initclocks_ap();
#endif

	/* Activate this cpu in the kernel pmap. */
	CPU_SET_ATOMIC(cpuid, &kernel_pmap->pm_active);

	/* Activate process 0's pmap. */
	pmap_activate_boot(vmspace_pmap(proc0.p_vmspace));

	mtx_lock_spin(&ap_boot_mtx);

	atomic_add_rel_32(&smp_cpus, 1);

	if (smp_cpus == mp_ncpus) {
		/* enable IPI's, tlb shootdown, freezes etc */
		atomic_store_rel_int(&smp_started, 1);
	}

	mtx_unlock_spin(&ap_boot_mtx);

	if (bootverbose)
		printf("Secondary CPU %u fully online\n", cpuid);

	sched_ap_entry();

	panic("scheduler returned us to init_secondary");
	/* NOTREACHED */
}

static void
smp_after_idle_runnable(void *arg __unused)
{
	int cpu;

	if (mp_ncpus == 1)
		return;

	KASSERT(smp_started != 0, ("%s: SMP not started yet", __func__));

	/*
	 * Wait for all APs to handle an interrupt.  After that, we know that
	 * the APs have entered the scheduler at least once, so the boot stacks
	 * are safe to free.
	 */
	smp_rendezvous(smp_no_rendezvous_barrier, NULL,
	    smp_no_rendezvous_barrier, NULL);

	for (cpu = 1; cpu <= mp_maxid; cpu++) {
		if (bootstacks[cpu] != NULL)
			kmem_free(bootstacks[cpu], MP_BOOTSTACK_SIZE);
	}
}
SYSINIT(smp_after_idle_runnable, SI_SUB_SMP, SI_ORDER_ANY,
    smp_after_idle_runnable, NULL);

void
la_stale_reset(void)
{
	int cpuid = PCPU_GET(cpuid);

	la_stale[cpuid].count = 0;
	la_stale[cpuid].overflow = false;
}

/*
 * Perform the local TLB invalidation described by (pmap, op, addr1, addr2).
 */
static void
la_tlb_local_flush(pmap_t pmap, uint8_t op, uint64_t addr1, uint64_t addr2)
{
	uint32_t asid;
	uint64_t va;

	if (pmap == kernel_pmap) {
		asid = csr_read32(LOONGARCH_CSR_ASID) & CSR_ASID_ASID;
		switch (op) {
		case LA_TLB_OP_PAGE:
			invtlb(INVTLB_ADDR_GTRUE_OR_ASID, asid,
			    addr1 & ~((PAGE_SIZE << 1) - 1));
			break;
		case LA_TLB_OP_RANGE:
			for (va = addr1 & ~((PAGE_SIZE << 1) - 1);
			    va < addr2; va += (PAGE_SIZE << 1))
				invtlb(INVTLB_ADDR_GTRUE_OR_ASID, asid, va);
			break;
		default:
			invtlb_all(INVTLB_CURRENT_ALL, 0, 0);
			/*
			 * CURRENT_ALL flushed every entry on this CPU,
			 * including all stale user-ASID entries.  Reset
			 * the stale tracker so the next user-pmap flush
			 * doesn't repeat a full G=0 invalidation for
			 * entries that no longer exist.
			 */
			la_stale_reset();
			break;
		}
	} else {
		int cpuid = PCPU_GET(cpuid);
		int i;

		critical_enter();

		/*
		 * Batch-flush stale ASIDs from prior context switches.
		 * If the stale array overflowed (many switches without
		 * an invalidation), do a full G=0 flush to catch all
		 * stale ASIDs including the dropped ones -- the target
		 * ASID's entries are also flushed, so skip it.
		 */
		if (la_stale[cpuid].overflow) {
			invtlb_all(INVTLB_CURRENT_GFALSE, 0, 0);
		} else {
			for (i = 0; i < la_stale[cpuid].count; i++)
				invtlb(INVTLB_GFALSE_AND_ASID,
				    la_stale[cpuid].asids[i], 0);
			/* Flush the target pmap's ASID entries. */
			asid = pmap_get_asid(pmap, cpuid);
			if (asid != 0)
				invtlb(INVTLB_GFALSE_AND_ASID, asid, 0);
			else
				invtlb_all(INVTLB_CURRENT_GFALSE, 0, 0);
		}
		la_stale[cpuid].count = 0;
		la_stale[cpuid].overflow = false;

		critical_exit();
	}
}

/*
 * Synchronous TLB shootdown via per-CPU lists of descriptors.
 *
 * The initiator pops a descriptor from its per-CPU pool, fills it, appends it
 * to its pending list, and IPIs the targets; the handler drains every other
 * initiator's pending list.  Because every in-flight shootdown from a CPU owns
 * its own descriptor, fire-and-forget shootdowns from the same CPU (IE-off
 * callers such as pmap_qenter/UMA) no longer overwrite an earlier one before
 * the targets have flushed it -- the race that silently dropped invalidations
 * and left stale TLBs on remote CPUs under multi-AP load.
 *
 * A descriptor returns to the pool once every target has acked it
 * (sd->remaining == 0); the synchronous (IE-on) path spin-waits on that.  An
 * IE-off caller skips the wait (fire-and-forget) -- its descriptor stays on the
 * list until the targets drain it.  If the pool is exhausted the initiator
 * escalates to a full flush (la_tlb_flushall_gen), so no invalidation is lost.
 */
void
smp_targeted_tlb_shootdown(pmap_t pmap, vm_offset_t addr1, vm_offset_t addr2,
    uint8_t op)
{
	struct la_tlb_sd *sd, *old;
	cpuset_t mask;
	u_int count;
	uint32_t fagen;
	int cpu, self;

	if (kdb_active || KERNEL_PANICKED() || !smp_started) {
		la_tlb_local_flush(pmap, op, addr1, addr2);
		return;
	}

	mask = pmap->pm_active;
	self = PCPU_GET(cpuid);
	CPU_CLR(self, &mask);
	if (CPU_EMPTY(&mask)) {
		la_tlb_local_flush(pmap, op, addr1, addr2);
		return;
	}
	count = CPU_COUNT(&mask);

	/*
	 * Critical section + per-CPU spinlock across the list manipulation,
	 * IPI, and (optional) wait.  IE stays on so incoming shootdown IPIs are
	 * still serviced; la_invlop_handler skips this CPU's own list, so it
	 * never blocks on the lock we hold here (no self-deadlock).
	 */
	critical_enter();
	mtx_lock_spin(&la_tlb_mtx[self]);

	/* Reclaim fully-acked descriptors (any position) back to the
	 * free list. */
	TAILQ_FOREACH_SAFE(sd, &la_tlb_q[self], pend_link, old) {
		if (atomic_load_int(&sd->remaining) == 0) {
			TAILQ_REMOVE(&la_tlb_q[self], sd, pend_link);
			SLIST_INSERT_HEAD(&la_tlb_free[self], sd, free_link);
		}
	}
	if (TAILQ_EMPTY(&la_tlb_q[self]))
		CPU_CLR_ATOMIC(self, &la_tlb_pending);

	sd = SLIST_FIRST(&la_tlb_free[self]);
	if (sd != NULL) {
		SLIST_REMOVE_HEAD(&la_tlb_free[self], free_link);
		sd->pmap = pmap;
		sd->addr1 = addr1;
		sd->addr2 = addr2;
		sd->op = op;
		sd->targets = mask;
		CPU_ZERO(&sd->acked);
		atomic_store_int(&sd->remaining, count);
		TAILQ_INSERT_TAIL(&la_tlb_q[self], sd, pend_link);
		CPU_SET_ATOMIC(self, &la_tlb_pending);
		mtx_unlock_spin(&la_tlb_mtx[self]);

		ipi_selected(mask, IPI_TLB);
		la_tlb_local_flush(pmap, op, addr1, addr2);

		if ((csr_read32(LOONGARCH_CSR_CRMD) & CSR_CRMD_IE) != 0) {
			TLB_DBG("TLB shootdown: cpu%d sync wait mask=%#x "
			    "op=%d va=%#lx\n",
			    self, *(uint32_t *)&mask, op, (u_long)addr1);
			while (atomic_load_int(&sd->remaining) != 0)
				cpu_spinwait();
		} else {
			/*
			 * Interrupts disabled: we cannot spin-wait for the
			 * targets to drain this descriptor.  It stays on the
			 * pending list, still referencing sd->pmap, until
			 * la_invlop_handler drains it (and calls
			 * pmap_get_asid(sd->pmap)).  Only kernel_pmap
			 * (immortal) is safe to reference that way -- a user
			 * pmap could be freed by pmap_release before the
			 * targets drain, a use-after-free.  Every current
			 * IE-off caller targets kernel_pmap; keep it that way.
			 */
			KASSERT(pmap == kernel_pmap,
			    ("IE-off TLB shootdown posted for non-kernel "
			    "pmap %p",
			    pmap));
		}
	} else {
		/*
		 * Pool exhausted (>= LA_TLB_POOL in-flight from this CPU):
		 * escalate to a full flush so no invalidation is lost; the
		 * targets pick this up via la_tlb_flushall_gen.
		 */
		atomic_add_int(&la_tlb_flushall_gen[self], 1);
		fagen = atomic_load_int(&la_tlb_flushall_gen[self]);
		CPU_SET_ATOMIC(self, &la_tlb_pending);
		mtx_unlock_spin(&la_tlb_mtx[self]);

		ipi_selected(mask, IPI_TLB);
		la_tlb_local_flush(pmap, LA_TLB_OP_ALL, 0, 0);

		if ((csr_read32(LOONGARCH_CSR_CRMD) & CSR_CRMD_IE) != 0) {
			CPU_FOREACH(cpu) {
				if (!CPU_ISSET(cpu, &mask))
					continue;
				while (atomic_load_int(
				    &la_tlb_flushall_done[cpu][self]) < fagen)
					cpu_spinwait();
			}
		}
	}
	critical_exit();
}

/*
 * IPI_TLB handler: drain every other initiator's pending list, flushing each
 * descriptor this CPU hasn't acked yet, and service any pending full-flush
 * fallback.  This CPU's own list (i == self) is skipped -- the initiator
 * flushes itself locally, and taking our own lock here would self-deadlock if
 * we interrupted the initiator while it holds it.  The flush happens before
 * the ack so the initiator's wait (on sd->remaining) only completes once this
 * CPU's stale entry is actually gone.
 */
static void
la_invlop_handler(void)
{
	struct la_tlb_sd *sd;
	cpuset_t pend;
	uint32_t fagen;
	int self, i;

	self = PCPU_GET(cpuid);
	/*
	 * Scan only initiators that currently have descriptors queued, instead
	 * of every CPU.  An initiator sets its bit under la_tlb_mtx[] before
	 * sending the IPI, and the IPI doorbell fence publishes that store
	 * before this handler runs, so a just-posted descriptor is never
	 * missed.  A stale bit (the initiator drained its queue after this
	 * snapshot) is harmless: we lock, scan an empty queue, and exit.
	 */
	pend = la_tlb_pending;
	CPU_CLR(self, &pend);
	CPU_FOREACH(i) {
		if (!CPU_ISSET(i, &pend))
			continue;
		mtx_lock_spin(&la_tlb_mtx[i]);
		TAILQ_FOREACH(sd, &la_tlb_q[i], pend_link) {
			if (CPU_ISSET(self, &sd->targets) &&
			    !CPU_ISSET(self, &sd->acked)) {
				la_tlb_local_flush(sd->pmap, sd->op,
				    sd->addr1, sd->addr2);
				CPU_SET(self, &sd->acked);
				atomic_subtract_int(&sd->remaining, 1);
			}
		}
		fagen = atomic_load_int(&la_tlb_flushall_gen[i]);
		if (fagen > atomic_load_int(&la_tlb_flushall_done[self][i])) {
			la_tlb_local_flush(kernel_pmap, LA_TLB_OP_ALL, 0, 0);
			atomic_store_int(&la_tlb_flushall_done[self][i], fagen);
		}
		mtx_unlock_spin(&la_tlb_mtx[i]);
	}
}

static int
ipi_handler(void *arg)
{
	u_int ipi_bitmap;
	u_int cpu, ipi;
	int bit;

	cpu = PCPU_GET(cpuid);

	for (;;) {
		mb();

		ipi_bitmap = atomic_readandclear_int(PCPU_PTR(pending_ipis));

		if (ipi_bitmap == 0) {
			/*
			 * Clear the hardware IPI status.  Without this
			 * the interrupt line stays asserted and the CPU
			 * would re-enter the handler immediately on ertn.
			 */
			ipi_read_clear(cpu);
			/*
			 * Re-check after clearing: an IPI may have arrived
			 * between the readandclear and the ipi_read_clear.
			 * Its hardware status was just cleared so the
			 * interrupt will not re-fire — we must process it
			 * here to avoid losing it.
			 */
			if (atomic_load_int(PCPU_PTR(pending_ipis)) == 0)
				return (FILTER_HANDLED);
			continue;
		}

		while ((bit = ffs(ipi_bitmap))) {
			bit = (bit - 1);
			ipi = (1 << bit);
			ipi_bitmap &= ~ipi;

			mb();

			switch (ipi) {
			case IPI_AST:
				CTR0(KTR_SMP, "IPI_AST");
				break;
			case IPI_PREEMPT:
				CTR1(KTR_SMP, "%s: IPI_PREEMPT", __func__);
				sched_preempt(curthread);
				break;
			case IPI_RENDEZVOUS:
				CTR0(KTR_SMP, "IPI_RENDEZVOUS");
				smp_rendezvous_action();
				break;
			case IPI_STOP:
			case IPI_STOP_HARD:
				CTR0(KTR_SMP, (ipi == IPI_STOP) ? "IPI_STOP" :
				    "IPI_STOP_HARD");
				savectx(&stoppcbs[cpu]);

				/* Indicate we are stopped */
				CPU_SET_ATOMIC(cpu, &stopped_cpus);

				/* Wait for restart */
				while (!CPU_ISSET(cpu, &started_cpus))
					cpu_spinwait();

				CPU_CLR_ATOMIC(cpu, &started_cpus);
				CPU_CLR_ATOMIC(cpu, &stopped_cpus);
				CTR0(KTR_SMP, "IPI_STOP (restart)");

				/*
				 * The kernel debugger might have set a
				 * breakpoint, so flush the instruction cache.
				 */
				flush_icache();
				break;
			case IPI_HARDCLOCK:
				CTR1(KTR_SMP, "%s: IPI_HARDCLOCK", __func__);
				hardclockintr();
				break;
			case IPI_TLB:
				CTR1(KTR_SMP, "%s: IPI_TLB", __func__);
				la_invlop_handler();
				break;
			default:
				panic("Unknown IPI %#0x on cpu %d",
				    ipi, curcpu);
			}
		}

		/*
		 * Clear hardware IPI status.  An IPI that arrived
		 * during processing set pc_pending_ipis but its
		 * hardware status is cleared here.  Re-check to avoid
		 * losing it.
		 */
		ipi_read_clear(cpu);

		if (atomic_load_int(PCPU_PTR(pending_ipis)) == 0)
			return (FILTER_HANDLED);
	}
}

struct cpu_group *
cpu_topo(void)
{

	return (smp_topo_none());
}

/* Determine if we running MP machine */
int
cpu_mp_probe(void)
{

	return (mp_ncpus > 1);
}

#ifdef FDT
static bool
cpu_init_fdt(u_int id, phandle_t node, u_int addr_size, pcell_t *reg)
{
	struct pcpu *pcpup;
	u_int cpuid;
	uint64_t reg_data;
	int naps;
	uint64_t addr;

	KASSERT(id < MAXCPU, ("Too many CPUs"));

	KASSERT(addr_size == 1 || addr_size == 2, ("Invalid register size"));
#ifdef INVARIANTS
	cpu_reg[id][0] = reg[0];
	if (addr_size == 2)
		cpu_reg[id][1] = reg[1];
#endif

	reg_data = reg[0];
	if (addr_size == 2) {
		reg_data <<= 32;
		reg_data |= reg[1];
	}

	cpuid = (u_int)reg_data;

	KASSERT(cpuid < MAXCPU, ("Too many cpus."));

	/* We are already running on this cpu */
	if (cpuid == boot_cpu)
		return (true);

	/*
	 * Rotate the CPU IDs to put the boot CPU as CPU 0.
	 * We keep the other CPUs ordered.
	 */
	if (cpuid < boot_cpu)
		cpuid += mp_maxid + 1;
	cpuid -= boot_cpu;

	/* Check if we are able to start this cpu */
	if (cpuid > mp_maxid)
		return (false);

	pcpup = &__pcpu[cpuid];
	pcpu_init(pcpup, cpuid, sizeof(struct pcpu));

	dpcpu[cpuid - 1] = kmem_malloc(DPCPU_SIZE, M_WAITOK | M_ZERO);
	dpcpu_init(dpcpu[cpuid - 1], cpuid);

	bootstacks[cpuid] = kmem_malloc(MP_BOOTSTACK_SIZE, M_WAITOK | M_ZERO);

	naps = atomic_load_int(&aps_started);
	bootstack = (char *)bootstacks[cpuid] + MP_BOOTSTACK_SIZE;

	if (bootverbose)
		printf("Starting CPU %u\n", cpuid);

	addr = pmap_extract(pmap_kernel(), (vm_offset_t)mpentry);
	printf("secondary cpu entry: %p\n", (void*)addr);
	/*
	 * Address the AP by its hardware id (the raw FDT reg value in
	 * reg_data), not the renumbered 0-based cpuid: csr_mail_send targets a
	 * physical CPU, and the AP side (locore.S mpentry) indexes
	 * __loongarch_boot_ap by CSR_CPUID == reg_data.  ipi_cpu() takes the
	 * 0-based cpuid (it indexes cpuid_to_pcpu[]), so it keeps cpuid.
	 */
	csr_mail_send(addr, (u_int)reg_data, 0);
	ipi_cpu(cpuid, 2);

	atomic_store_32(&__loongarch_boot_ap[(u_int)reg_data], 1);

	/* Wait for the AP to switch to its boot stack. */
	while (atomic_load_int(&aps_started) < naps + 1)
		cpu_spinwait();

	CPU_SET(cpuid, &all_cpus);

	return (true);
}
#endif

#ifdef DEV_ACPI
/*
 * ACPI twin of cpu_init_fdt: start one AP identified by its hardware core id
 * (the MADT CORE_PIC CoreId, == CSR_CPUID).  The AP is woken the same way as
 * on FDT (csr_mail_send of the mpentry address + IPI); only the enumeration
 * source differs.
 */
static bool
cpu_init_acpi(u_int coreid)
{
	struct pcpu *pcpup;
	u_int cpuid;
	int naps;
	uint64_t addr;

	if (coreid == boot_cpu)
		return (true);

	cpuid = coreid;
	if (cpuid < boot_cpu)
		cpuid += mp_maxid + 1;
	cpuid -= boot_cpu;

	if (cpuid > mp_maxid)
		return (false);

	pcpup = &__pcpu[cpuid];
	pcpu_init(pcpup, cpuid, sizeof(struct pcpu));

	dpcpu[cpuid - 1] = kmem_malloc(DPCPU_SIZE, M_WAITOK | M_ZERO);
	dpcpu_init(dpcpu[cpuid - 1], cpuid);

	bootstacks[cpuid] = kmem_malloc(MP_BOOTSTACK_SIZE, M_WAITOK | M_ZERO);

	naps = atomic_load_int(&aps_started);
	bootstack = (char *)bootstacks[cpuid] + MP_BOOTSTACK_SIZE;

	if (bootverbose)
		printf("Starting CPU %u\n", cpuid);

	addr = pmap_extract(pmap_kernel(), (vm_offset_t)mpentry);
	csr_mail_send(0, coreid, 0);
	csr_mail_send(addr, coreid, 0);
	ipi_write_action(coreid, 1);

	atomic_store_32(&__loongarch_boot_ap[coreid], 1);

	/* Wait for the AP to switch to its boot stack. */
	while (atomic_load_int(&aps_started) < naps + 1)
		cpu_spinwait();

	/*
	 * Mark the AP present: CPU_ABSENT() tests all_cpus, and without this
	 * cpu_initclocks_ap()->hardclock_sync() panics "Absent CPU N" on every
	 * AP (concurrent panics garble the console).  Matches cpu_init_fdt().
	 */
	CPU_SET(cpuid, &all_cpus);

	return (true);
}
#endif

/* Initialize and fire up non-boot processors */
void
cpu_mp_start(void)
{
	mtx_init(&ap_boot_mtx, "ap boot", NULL, MTX_SPIN);

	CPU_SET(boot_cpu, &all_cpus);

#ifdef DEV_ACPI
	/*
	 * On ACPI/3A6000: enable all IOCSR IPI vectors and the extended I/O
	 * interrupt controller before waking the APs.  Firmware on QEMU leaves
	 * these enabled (and its FDT eioic driver attaches), but on 3A6000 they
	 * are masked and the eioic driver never attaches, so do it here.
	 */
	iocsr_write32(0xffffffff, LOONGARCH_IOCSR_IPI_EN);
	/*
	 * EXTIOI was already enabled + the EIOINTC router programmed once
	 * in eioic_attach() (BUS_PASS_INTERRUPT, before device probe).
	 * Do NOT re-enable here: eiointc_enable_extioi() resets the global
	 * ROUTE to 0, creating a window where all device interrupts are
	 * dropped.  eiointc_enable() is called only on the first CPU of
	 * each node.
	 */
#endif

	switch(cpu_enum_method) {
#ifdef FDT
	case CPUS_FDT:
		ofw_cpu_early_foreach(cpu_init_fdt, true);
		break;
#endif
#ifdef DEV_ACPI
	case CPUS_ACPI:
		loongarch_acpi_madt_foreach_cpu(cpu_init_acpi);
		break;
#endif
	case CPUS_UNKNOWN:
		break;
	}
}

/* Introduce rest of cores to the world */
void
cpu_mp_announce(void)
{
	u_int cpu;

	CPU_FOREACH(cpu) {
		/* Already announced. */
		if (cpu == 0)
			continue;

		printcpuinfo(cpu);
	}
}

void
cpu_mp_setmaxid(void)
{
	int cores;
	int maxcpus = MAXCPU;

	/*
	 * Honor the kern.maxcpus loader tunable to limit the number of CPUs
	 * brought online (for SMP debugging).  Without this, all cores found in
	 * the MADT/FDT are started regardless.
	 */
	TUNABLE_INT_FETCH("kern.maxcpus", &maxcpus);
	if (maxcpus < 1)
		maxcpus = 1;

#ifdef DEV_ACPI
	if (loongarch_efi_acpi_rsdp() != 0) {
		cores = loongarch_acpi_madt_cpu_count();
		if (cores > 0) {
			cores = MIN(cores, MAXCPU);
			cores = MIN(cores, maxcpus);
			if (bootverbose)
				printf("Found %d CPUs in the MADT\n", cores);
			mp_ncpus = cores;
			mp_maxid = cores - 1;
			cpu_enum_method = CPUS_ACPI;
		} else {
			if (bootverbose)
				printf("No CPUs in MADT, limiting to 1 core\n");
			mp_ncpus = 1;
			mp_maxid = 0;
		}
	} else
#endif
#ifdef FDT
	{
		cores = ofw_cpu_early_foreach(NULL, true);
		if (cores > 0) {
			cores = MIN(cores, MAXCPU);
			cores = MIN(cores, maxcpus);
			if (bootverbose)
				printf("Found %d CPUs in the device "
				    "tree\n", cores);
			mp_ncpus = cores;
			mp_maxid = cores - 1;
			cpu_enum_method = CPUS_FDT;
		} else {
			if (bootverbose)
				printf("No CPU data, limiting to 1 core\n");
			mp_ncpus = 1;
			mp_maxid = 0;
		}
	}
#else
	{
		if (bootverbose)
			printf("No CPU data, limiting to 1 core\n");
		mp_ncpus = 1;
		mp_maxid = 0;
	}
#endif

	if (TUNABLE_INT_FETCH("hw.ncpu", &cores)) {
		if (cores > 0 && cores < mp_ncpus) {
			mp_ncpus = cores;
			mp_maxid = cores - 1;
		}
	}
}
