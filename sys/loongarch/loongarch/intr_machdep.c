/*-
 * Copyright (c) 2015-2017 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This file contains LoongArch IPI/mailbox hardware access primitives
 * and legacy helpers needed during the transition to the full INTRNG
 * PIC framework.  The interrupt controllers live in cpuic.c (root)
 * and eioic.c (external).
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/cpuset.h>
#include <sys/interrupt.h>
#include <sys/smp.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/frame.h>
#include <machine/intr.h>

#include "intc.h"

#ifdef SMP
#include <machine/smp.h>

void
csr_mail_send(uint64_t data, int cpu, int mailbox)
{
	uint64_t val;

	val = IOCSR_MBUF_SEND_BLOCKING;
	val |= (IOCSR_MBUF_SEND_BOX_HI(mailbox)
	    << IOCSR_MBUF_SEND_BOX_SHIFT);
	val |= (cpu << IOCSR_MBUF_SEND_CPU_SHIFT);
	val |= (data & IOCSR_MBUF_SEND_H32_MASK);
	iocsr_write64(val, LOONGARCH_IOCSR_MBUF_SEND);

	val = IOCSR_MBUF_SEND_BLOCKING;
	val |= (IOCSR_MBUF_SEND_BOX_LO(mailbox)
	    << IOCSR_MBUF_SEND_BOX_SHIFT);
	val |= (cpu << IOCSR_MBUF_SEND_CPU_SHIFT);
	val |= (data << IOCSR_MBUF_SEND_BUF_SHIFT);
	iocsr_write64(val, LOONGARCH_IOCSR_MBUF_SEND);
}

uint32_t
ipi_read_clear(int cpu)
{
	uint32_t action;

	action = iocsr_read32(LOONGARCH_IOCSR_IPI_STATUS);
	iocsr_write32(action, LOONGARCH_IOCSR_IPI_CLEAR);
	wbflush();
	return (action);
}

void
ipi_write_action(int cpu, uint32_t action)
{
	unsigned int irq;

	while ((irq = ffs(action))) {
		uint32_t val = IOCSR_IPI_SEND_BLOCKING;
		val |= (irq - 1);
		val |= (cpu << IOCSR_IPI_SEND_CPU_SHIFT);
		iocsr_write32(val, LOONGARCH_IOCSR_IPI_SEND);
		action &= ~(1U << (irq - 1));
	}
}

static void
ipi_send(struct pcpu *pc, int ipi)
{
	CTR3(KTR_SMP, "%s: cpu: %d, ipi: %x", __func__, pc->pc_cpuid, ipi);
	atomic_set_32(&pc->pc_pending_ipis, ipi);
	/*
	 * Ensure the pending_ipis store is globally visible before the
	 * IOCSR doorbell write.  Without a fence, the target CPU may
	 * take the IPI but see a zero bitmap and return without processing.
	 */
	fence();
	ipi_write_action(pc->pc_cpuid, (uint32_t)ipi);
	CTR1(KTR_SMP, "%s: sent", __func__);
}

void
ipi_all_but_self(u_int ipi)
{
	cpuset_t other_cpus;

	other_cpus = all_cpus;
	CPU_CLR(PCPU_GET(cpuid), &other_cpus);
	CTR2(KTR_SMP, "%s: ipi: %x", __func__, ipi);
	ipi_selected(other_cpus, ipi);
}

void
ipi_cpu(int cpu, u_int ipi)
{
	ipi_send(cpuid_to_pcpu[cpu], ipi);
}

void
ipi_selected(cpuset_t cpus, u_int ipi)
{
	struct pcpu *pc;

	CTR1(KTR_SMP, "ipi_selected: ipi: %x", ipi);
	STAILQ_FOREACH(pc, &cpuhead, pc_allcpu) {
		if (CPU_ISSET(pc->pc_cpuid, &cpus)) {
			CTR3(KTR_SMP, "%s: pc: %p, ipi: %x\n",
			    __func__, pc, ipi);
			atomic_set_32(&pc->pc_pending_ipis, ipi);
			ipi_send(pc, (uint32_t)ipi);
		}
	}
}

#endif /* SMP */
