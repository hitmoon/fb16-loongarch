/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Xiaoqiang Zhao <zhaoxiaoqiang007@gmail.com>
 *
 * This code implements the CPU interrupt controller (CPUINTC) for LoongArch,
 * serving as the root PIC in the INTRNG interrupt framework.
 *
 * The CPUINTC manages 14 internal interrupt lines via CSR registers:
 *   - CSR_ECFG (0x4): Exception Config — 14-bit interrupt mask
 *   - CSR_ESTAT (0x5): Exception Status — 14-bit pending status
 *
 * Hardware interrupt vector layout:
 *   0: SWI0, 1: SWI1, 2-9: HWI0-7, 10: PCOV, 11: Timer, 12: IPI, 13: NMI
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/cpuset.h>
#include <sys/interrupt.h>
#include <sys/smp.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/frame.h>
#include <machine/intr.h>
#include <machine/loongarchreg.h>
#include <machine/machdep.h>	/* loongarch_efi_acpi_rsdp() */

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "pic_if.h"
#include "intc.h"

struct cpuic_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
};

struct cpuic_softc {
	device_t		dev;
	struct cpuic_irqsrc	isrcs[INTC_NIRQS];
};

static struct cpuic_softc *cpuic_sc;	/* for the CPU-local intr helpers */

/*
 * Sentinel xref used when the cpuic is directly instantiated (ACPI, no FDT
 * node) -- it is the root PIC and is reached via cpuic_setup_cpu_intr().
 */
#define	CPUIC_INTR_XREF		((phandle_t)1)

static int	cpuic_intr(void *arg);

static int
cpuic_probe(device_t dev)
{
	/*
	 * Two attach paths, kept strictly separate by whether the device has
	 * an OFW node:
	 *
	 *  - FDT path (ofwbus child, node != -1): match ONLY by the
	 *    "loongson,cpu-interrupt-controller" compatible.  Do NOT fall back
	 *    to the device name here -- ofwbus may hand several FDT children
	 *    the same name, and matching by name would make cpuic claim every
	 *    one of them (the cpuic1 spam / "board broken" regression).
	 *
	 *  - nexus/ACPI path (direct child, node == -1): match by name
	 *    "cpuic", the name nexus_add_child() gives it.
	 */

	if (ofw_bus_get_node(dev) != -1) {
		/*
		 * ACPI is the preferred path: ignore the FDT node when ACPI is
		 * present, otherwise the ofwbus enumeration duplicates the
		 * nexus (ACPI) instance (cpuic0 from nexus vs cpuic1 from
		 * ofwbus).  The FDT node is only used when ACPI is absent
		 * (pure-FDT systems).
		 */
		if (loongarch_efi_acpi_rsdp() != 0)
			return (ENXIO);
		if (!ofw_bus_is_compatible(dev,
		    "loongson,cpu-interrupt-controller"))
			return (ENXIO);
	} else {
		if (strcmp(device_get_name(dev), "cpuic") != 0)
			return (ENXIO);
	}

	device_set_desc(dev, "LoongArch CPU Interrupt Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
cpuic_attach(device_t dev)
{
	struct cpuic_irqsrc *isrcs;
	struct cpuic_softc *sc;
	struct intr_pic *pic;
	const char *name;
	phandle_t node, xref;
	u_int flags;
	uint32_t ecfg;
	int i, error;

	/*
	 * Single instance: the FDT and direct/nexus paths must not both
	 * attach.
	 */
	if (cpuic_sc != NULL)
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* Mask all interrupts — clear stale firmware IM bits in ECFG. */
	ecfg = csr_read32(LOONGARCH_CSR_ECFG);
	ecfg &= ~CSR_ECFG_IM;
	csr_write32(ecfg, LOONGARCH_CSR_ECFG);

	name = device_get_nameunit(dev);
	/*
	 * FDT devices carry an OFW node (use its xref); a directly-instantiated
	 * nexus child has none, so use a fixed sentinel xref.  Devices that
	 * chain to the cpuic (timer, eioic) reach it via
	 * cpuic_setup_cpu_intr(), not via the OFW
	 * interrupt-parent/xref lookup, so the sentinel is fine.
	 */
	node = ofw_bus_get_node(dev);
	xref = (node > 0) ? OF_xref_from_node(node) : CPUIC_INTR_XREF;

	isrcs = sc->isrcs;
	for (i = 0; i < INTC_NIRQS; i++) {
		isrcs[i].irq = i;
		flags = 0;
		if (i == IRQ_IPI)
			flags |= INTR_ISRCF_IPI;
		/*
		 * Only per-CPU interrupts (timer, PMC, IPI) are PPIs.
		 * HWI0-7 are shared — e.g., HWI0 carries all EIOINTC
		 * device interrupts and must NOT be dispatched on
		 * multiple CPUs simultaneously.
		 */
		if (i == IRQ_TI || i == IRQ_PCOV)
			flags |= INTR_ISRCF_PPI;
		error = intr_isrc_register(&isrcs[i].isrc, sc->dev, flags,
		    "%s,%u", name, i);
		if (error != 0) {
			device_printf(dev, "Can't register interrupt %d\n", i);
			return (error);
		}
	}

	cpuic_sc = sc;

	pic = intr_pic_register(sc->dev, xref);
	if (pic == NULL)
		return (ENXIO);

	return (intr_pic_claim_root(sc->dev, xref, cpuic_intr, sc,
	    INTR_ROOT_IRQ));
}

static void
cpuic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq;
	uint32_t val;

	irq = ((struct cpuic_irqsrc *)isrc)->irq;
	if (irq >= INTC_NIRQS)
		panic("%s: Unsupported IRQ %u", __func__, irq);

	val = csr_read32(LOONGARCH_CSR_ECFG);
	val &= ~ECFGF(irq);
	csr_write32(val, LOONGARCH_CSR_ECFG);
}

static void
cpuic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	u_int irq;
	uint32_t val;

	irq = ((struct cpuic_irqsrc *)isrc)->irq;
	if (irq >= INTC_NIRQS)
		panic("%s: Unsupported IRQ %u", __func__, irq);

	val = csr_read32(LOONGARCH_CSR_ECFG);
	val |= ECFGF(irq);
	csr_write32(val, LOONGARCH_CSR_ECFG);
}

static int
cpuic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct intr_map_data_fdt *daf;
	struct cpuic_softc *sc;

	sc = device_get_softc(dev);

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	daf = (struct intr_map_data_fdt *)data;
	if (daf->ncells != 1 || daf->cells[0] >= INTC_NIRQS)
		return (EINVAL);

	*isrcp = &sc->isrcs[daf->cells[0]].isrc;

	return (0);
}

static int
cpuic_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{

	if (isrc->isrc_flags & INTR_ISRCF_PPI)
		CPU_SET(PCPU_GET(cpuid), &isrc->isrc_cpu);

	return (0);
}

static void
cpuic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{

	/* HWI lines are never masked during dispatch; nothing to do. */
}

static void
cpuic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	cpuic_disable_intr(dev, isrc);
}

static void
cpuic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	cpuic_enable_intr(dev, isrc);
}

#ifdef SMP
static void
cpuic_init_secondary(device_t dev, uint32_t rootnum)
{
	struct cpuic_softc *sc;
	struct intr_irqsrc *isrc;
	u_int cpu, irq;

	sc = device_get_softc(dev);
	cpu = PCPU_GET(cpuid);

	/*
	 * Unmask interrupts that have active handlers on this CPU.
	 * Only enable IRQs with registered handlers to avoid
	 * interrupt storms from unhandled sources (e.g. HWI0).
	 */
	for (irq = 0; irq < INTC_NIRQS; irq++) {
		isrc = &sc->isrcs[irq].isrc;
		if (isrc->isrc_handlers > 0 &&
		    intr_isrc_init_on_cpu(isrc, cpu))
			cpuic_enable_intr(dev, isrc);
	}
}
#endif

static int
cpuic_intr(void *arg)
{
	struct trapframe *frame;
	struct cpuic_softc *sc;
	uint32_t estatus, is;
	int bit;
	struct cpuic_irqsrc *src;

	sc = arg;
	frame = curthread->td_intr_frame;

	estatus = csr_read32(LOONGARCH_CSR_ESTAT);
	is = estatus & CSR_ESTAT_IS;

	while ((bit = ffs(is)) != 0) {
		bit--;
		is &= ~(1U << bit);

		if (bit >= INTC_NIRQS)
			continue;

		src = &sc->isrcs[bit];
		if (intr_isrc_dispatch(&src->isrc, frame) != 0) {
			cpuic_disable_intr(sc->dev, &src->isrc);
			device_printf(sc->dev, "Stray irq %u disabled\n", bit);
		}
	}

	return (FILTER_HANDLED);
}

#ifdef SMP
/*
 * Register the IPI receive filter on the CPU-local IRQ_IPI line and unmask it
 * in ECFG.  The filter itself (which processes the per-CPU pending IPI action
 * bitmap) lives in mp_machdep.c; this attaches it to the cpuic IRQ_IPI ISRC,
 * replacing the old loongarch_setup_ipihandler() helper in intr_machdep.c.
 */
void
cpuic_setup_ipi(driver_filter_t filt)
{
	struct intr_irqsrc *isrc;

	if (cpuic_sc == NULL)
		panic("%s: cpuic not initialized", __func__);

	isrc = &cpuic_sc->isrcs[IRQ_IPI].isrc;
	if (isrc->isrc_event == NULL) {
		if (intr_event_create(&isrc->isrc_event, isrc, 0, IRQ_IPI,
		    NULL, NULL, NULL, NULL, "ipi") != 0)
			panic("%s: intr_event_create failed", __func__);
		cpuic_enable_intr(cpuic_sc->dev, isrc);
	}
	if (intr_event_add_handler(isrc->isrc_event, "ipi", filt, NULL, NULL,
	    intr_priority(INTR_TYPE_MISC), INTR_TYPE_MISC, NULL) != 0)
		panic("%s: intr_event_add_handler failed", __func__);
}
#endif	/* SMP */

/*
 * CPU-local interrupt setup for devices without an FDT interrupt node
 * (e.g. the CPU timer).  Registers a handler directly on the cpuic ISRC
 * for the given ESTAT line and unmasks it in ECFG.
 */
int
cpuic_setup_cpu_intr(const char *name, driver_filter_t *filt,
    void (*handler)(void *), void *arg, u_int irq, int flags,
    void **cookiep)
{
	struct intr_irqsrc *isrc;
	int error;

	if (cpuic_sc == NULL || irq >= INTC_NIRQS)
		panic("%s: cpuic not initialized or bad irq %u", __func__, irq);

	isrc = &cpuic_sc->isrcs[irq].isrc;
	if (isrc->isrc_event == NULL) {
		error = intr_event_create(&isrc->isrc_event, isrc, 0, irq,
		    NULL, NULL, NULL, NULL, "int%u", irq);
		if (error != 0)
			return (error);
		cpuic_enable_intr(cpuic_sc->dev, isrc);
	}

	error = intr_event_add_handler(isrc->isrc_event, name, filt, handler,
	    arg, intr_priority(flags), flags, cookiep);
	return (error);
}

static device_method_t cpuic_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		cpuic_probe),
	DEVMETHOD(device_attach,	cpuic_attach),

	/* Interrupt controller interface */
	DEVMETHOD(pic_disable_intr,	cpuic_disable_intr),
	DEVMETHOD(pic_enable_intr,	cpuic_enable_intr),
	DEVMETHOD(pic_map_intr,		cpuic_map_intr),
	DEVMETHOD(pic_setup_intr,	cpuic_setup_intr),
	DEVMETHOD(pic_post_filter,	cpuic_post_filter),
	DEVMETHOD(pic_pre_ithread,	cpuic_pre_ithread),
	DEVMETHOD(pic_post_ithread,	cpuic_post_ithread),
#ifdef SMP
	DEVMETHOD(pic_init_secondary,	cpuic_init_secondary),
#endif

	DEVMETHOD_END
};

DEFINE_CLASS_0(cpuic, cpuic_driver, cpuic_methods,
    sizeof(struct cpuic_softc));
EARLY_DRIVER_MODULE(cpuic, ofwbus, cpuic_driver, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_FIRST);
/*
 * Also register on nexus so the cpuic can be directly instantiated on ACPI
 * systems (no FDT/ofwbus node).  The ACPI guard in nexus_attach() ensures no
 * nexus "cpuic" child exists on FDT systems, so QEMU's ofwbus path is used.
 */
EARLY_DRIVER_MODULE(cpuic, nexus, cpuic_driver, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_FIRST);
