/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Xiaoqiang Zhao <zhaoxiaoqiang007@gmail.com>
 *
 * This code implements the Extended I/O Interrupt Controller (EIOINTC) for
 * LoongArch.  The EIOINTC is the main external interrupt controller, routing
 * up to 256 device interrupt vectors to CPU HWI lines.
 *
 * Register layout (all accessed via IOCSR):
 *   NODEMAP (0x14a0): Node map — inter-node routing
 *   IPMAP   (0x14c0): IP map — which CPU interrupt pin each group routes to
 *   ENABLE  (0x1600): Enable mask per vector (64-bit x 4)
 *   BOUNCE  (0x1680): Bounce / edge-trigger re-assert control
 *   ISR     (0x1800): Interrupt Status Register (64-bit x 4)
 *   ROUTE   (0x1c00): Routing per vector (8-bit per vector, 4 vectors per reg)
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/types.h>
#include <sys/smp.h>
#include <sys/rman.h>
#include <sys/interrupt.h>

#include <machine/intr.h>
#include <machine/cpufunc.h>
#include <machine/loongarchreg.h>
#include <machine/machdep.h>	/* loongarch_efi_acpi_rsdp() */

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "pic_if.h"
#include "eioic.h"

#define	VEC_REG_COUNT		4
#define	VEC_COUNT_PER_REG	64
#define	VEC_COUNT		(VEC_REG_COUNT * VEC_COUNT_PER_REG)

/*
 * Sentinel xref used when eioic is directly instantiated (ACPI, no FDT
 * node).
 */
#define	EIOIC_INTR_XREF		((phandle_t)2)

static device_t eioic_dev;	/* single-instance guard */

struct eioic_softc *
eioic_get_softc(void)
{
	device_t d;

	d = devclass_get_device(devclass_find("eioic"), 0);
	return (d == NULL ? NULL : device_get_softc(d));
}

void
eiointc_enable_extioi(void)
{
	uint64_t misc;

	misc = iocsr_read64(LOONGARCH_IOCSR_MISC_FUNC);
	misc |= IOCSR_MISC_FUNC_EXT_IOI_EN;
	iocsr_write64(misc, LOONGARCH_IOCSR_MISC_FUNC);
}

static void
eioic_init_router(int vec_count)
{
	int i, ngroups;
	uint32_t bit, data;

	ngroups = vec_count / 32;

	/*
	 * The previous code wrote 0 to NODEMAP and ROUTE, which selects
	 * no node / no core -- so every device interrupt was dropped before
	 * reaching a CPU and nothing via the EIOINTC ever fired (timer/IPI
	 * use the cpuic directly).  QEMU happened to ignore the empty ROUTE;
	 * real 3A6000 does not.
	 */

	/*
	 * Node map: route EVERY group to Node-0 (bit 0 in the low half).
	 * A per-group pattern spreads groups across nodes (group i -> node
	 * 2i) for multi-node load balancing; on a single node BSP that drops
	 * every group except 0 -- only vector 22 (group 0) ever fired.
	 * Empirically group 0's value (0x00020001) delivers to the BSP, so
	 * replicate it for all groups.
	 */
	for (i = 0; i < ngroups; i++)
		iocsr_write32(0x00020001,
		    LOONGARCH_IOCSR_EXTIOI_NODEMAP_BASE + i * 4);

	/*
	 * IP map: route every group to the EIOINTC's CPU line.  We chain on
	 * HWI1 (ESTAT bit 3); INT_HWI0 is ESTAT bit 2, so the IP bit is
	 * BIT(3 - 2) = BIT(1) = 0x02, replicated across the 4 byte fields.
	 */
	bit = (1u << 1);
	data = bit | (bit << 8) | (bit << 16) | (bit << 24);
	for (i = 0; i < ngroups / 4; i++)
		iocsr_write32(data,
		    LOONGARCH_IOCSR_EXTIOI_IPMAP_BASE + i * 4);

	/*
	 * Route: deliver every vector to Node-0 Core-0.  The low nibble of
	 * each byte is a core bitmap (BIT(core)), so 0 means no core -- use
	 * BIT(0).  (pic_bind_intr can re-route individual vectors later.)
	 */
	bit = (1u << 0);
	data = bit | (bit << 8) | (bit << 16) | (bit << 24);
	for (i = 0; i < vec_count / 4; i++)
		iocsr_write32(data,
		    LOONGARCH_IOCSR_EXTIOI_ROUTE_BASE + i * 4);

	/*
	 * Enable all vectors.  Leave BOUNCE clear: with BOUNCE armed the
	 * EIOINTC treats an HTMSI source as level (ISR re-sets after clear
	 * while HTMSI held), which storms on any PCH-PIC input held asserted
	 * (e.g. OHCI).  With BOUNCE=0 the ISR is edge (one bit per HTMSI
	 * rising edge), so a held source sets the ISR once.  MSI is
	 * unaffected (each MSI write is a discrete rising edge).
	 */
	for (i = 0; i < ngroups; i++) {
		iocsr_write32(0xffffffff,
		    LOONGARCH_IOCSR_EXTIOI_EN_BASE + i * 4);
		iocsr_write32(0,
		    LOONGARCH_IOCSR_EXTIOI_BOUNCE_BASE + i * 4);
	}
}

void
eiointc_enable_vector(device_t dev, u_int vector)
{
	struct eioic_softc *sc = device_get_softc(dev);
	uint32_t reg, bit, val;

	if (vector >= sc->vec_count)
		return;

	mtx_lock_spin(&sc->mtx);
	/*
	 * Set the ENABLE bit only.  BOUNCE is left untouched: it is
	 * initialized once in eioic_init_router() and should not be
	 * toggled.  Clearing BOUNCE on disable would discard edge-
	 * triggered interrupts that arrived while the vector was
	 * masked; setting it on enable would force edge-triggered
	 * behavior on level-triggered sources.
	 */
	reg = LOONGARCH_IOCSR_EXTIOI_EN_BASE + (vector / 32) * 4;
	bit = vector % 32;
	val = iocsr_read32(reg);
	val |= (1U << bit);
	iocsr_write32(val, reg);
	mtx_unlock_spin(&sc->mtx);
}

void
eiointc_disable_vector(device_t dev, u_int vector)
{
	struct eioic_softc *sc = device_get_softc(dev);
	uint32_t reg, bit, val;

	if (vector >= sc->vec_count)
		return;

	mtx_lock_spin(&sc->mtx);
	/* Clear only ENABLE; BOUNCE is left untouched. */
	reg = LOONGARCH_IOCSR_EXTIOI_EN_BASE + (vector / 32) * 4;
	bit = vector % 32;
	val = iocsr_read32(reg);
	val &= ~(1U << bit);
	iocsr_write32(val, reg);
	mtx_unlock_spin(&sc->mtx);
}

static int
eioic_intr(void *arg)
{
	struct eioic_softc *sc = arg;
	struct trapframe *tf;
	int i, bit;
	uint64_t isr;

	tf = curthread->td_intr_frame;

	for (i = 0; i < VEC_REG_COUNT; i++) {
		isr = iocsr_read64(
		    LOONGARCH_IOCSR_EXTIOI_ISR_BASE + (i << 3));
		if (!isr)
			continue;

		/*
		 * Clear each pending bit individually after dispatch.
		 * Clearing the whole group at once would lose vectors
		 * that arrive between the clear and the end of the
		 * while loop.
		 */
		while (isr) {
			bit = ffsll(isr) - 1;
			isr &= ~(1UL << bit);

			int irq = bit + VEC_COUNT_PER_REG * i;
			if (irq >= EIOINTC_MAX_IRQS)
				continue;

			/* Clear this vector's ISR bit in hardware. */
			iocsr_write64(1ULL << bit,
			    LOONGARCH_IOCSR_EXTIOI_ISR_BASE + (i << 3));

			/*
			 * Do NOT mask the EIOINTC vector here.  The ISR clear
			 * above consumes the edge for MSI sources (nvme,
			 * XHCI, re0); a new MSI sets a fresh ISR bit on the
			 * next pass.  Level-triggered INTx (OHCI/EHCI via
			 * pch_pic) is masked/re-armed at the PCH-PIC child
			 * (pre/post_ithread CLR), not here, so the EIOINTC
			 * vector stays enabled.
			 */
			{
				struct intr_irqsrc *isrc = sc->isrcs[irq].child;

				if (isrc == NULL)
					isrc = &sc->isrcs[irq].isrc;
				/*
				 * Suppress the "Stray vector" printf: with
				 * all vectors enabled at boot, unclaimed
				 * PCH-PIC inputs (no driver) assert during
				 * probe and would flood the console.
				 * intr_isrc_dispatch still counts each
				 * stray internally (vmstat -i).
				 */
				(void)intr_isrc_dispatch(isrc, tf);
			}
		}
	}

	return (FILTER_HANDLED);
}

static int
eioic_probe(device_t dev)
{

	/*
	 * Two paths.  FDT (ofwbus) node: used only when ACPI is absent (FDT
	 * fallback) -- otherwise ofwbus duplicates the nexus (ACPI) instance.
	 * nexus/ACPI direct child (no OFW node): match by name "eioic".
	 */
	if (ofw_bus_get_node(dev) != -1) {
		if (loongarch_efi_acpi_rsdp() != 0)
			return (ENXIO);
		if (!ofw_bus_is_compatible(dev, "loongson,ls2k2000-eiointc") &&
		    !ofw_bus_is_compatible(dev, "loongson,eiointc"))
			return (ENXIO);
	} else {
		if (strcmp(device_get_name(dev), "eioic") != 0)
			return (ENXIO);
	}

	device_set_desc(dev, "LoongArch Extended I/O Interrupt Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
eioic_attach(device_t dev)
{
	struct eioic_softc *sc;
	struct eioic_irqsrc *isrcs;
	const char *name;
	phandle_t node, xref;
	int irq, error;

	/*
	 * Single instance: the FDT and direct/nexus paths must not both
	 * attach.
	 */
	if (eioic_dev != NULL)
		return (ENXIO);
	eioic_dev = dev;

	sc = device_get_softc(dev);
	sc->dev = dev;
	mtx_init(&sc->mtx, "eiointc", NULL, MTX_SPIN);

	sc->vec_count = VEC_COUNT;

	eiointc_enable_extioi();
	eioic_init_router(sc->vec_count);

	isrcs = sc->isrcs;
	name = device_get_nameunit(dev);
	for (irq = 0; irq < EIOINTC_MAX_IRQS; irq++) {
		isrcs[irq].vec = irq;
		error = intr_isrc_register(&isrcs[irq].isrc, dev, 0,
		    "%s,%u", name, irq);
		if (error != 0) {
			device_printf(dev, "Cannot register isrc %d\n", irq);
			goto cleanup_isrcs;
		}
	}

	/* FDT devices carry an OFW node (use its xref); a directly-instantiated
	 * nexus child has none, so use a fixed sentinel xref. */
	node = ofw_bus_get_node(dev);
	xref = (node > 0) ? OF_xref_from_node(node) : EIOIC_INTR_XREF;
	if (intr_pic_register(dev, xref) == NULL) {
		device_printf(dev, "Cannot register PIC\n");
		error = ENXIO;
		goto cleanup_isrcs;
	}

	/*
	 * Register our dispatch filter on the cpuic HWI1 ISRC (the EIOINTC is
	 * chained to ESTAT bit 3 via the IPMAP).  We register through the
	 * cpuic driver helper because the FDT-to-INTRNG-IRQ resource path
	 * through nexus is not wired for ofwbus/simplebus children, so
	 * bus_setup_intr cannot resolve the HWI1 line to a cpuic ISRC yet.
	 */
	error = cpuic_setup_cpu_intr(device_get_nameunit(dev),
	    eioic_intr, NULL, sc, IRQ_HWI1, INTR_TYPE_MISC | INTR_MPSAFE,
	    &sc->intrhand);
	if (error) {
		device_printf(dev, "could not setup irq handler: %d\n",
		    error);
		goto cleanup_isrcs;
	}

	OF_device_register_xref(xref, dev);

	return (0);

cleanup_isrcs:
	for (irq = 0; irq < EIOINTC_MAX_IRQS; irq++)
		intr_isrc_deregister(&sc->isrcs[irq].isrc);
	return (error);
}

static void
eioic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{

	eiointc_disable_vector(dev,
	    ((struct eioic_irqsrc *)isrc)->vec);
}

static void
eioic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{

	eiointc_enable_vector(dev,
	    ((struct eioic_irqsrc *)isrc)->vec);
}

static int
eioic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct intr_map_data_fdt *daf;
	struct eioic_softc *sc;
	u_int vec;

	sc = device_get_softc(dev);

	if (data->type != INTR_MAP_DATA_FDT)
		return (ENOTSUP);

	daf = (struct intr_map_data_fdt *)data;
	if (daf->ncells != 1)
		return (EINVAL);

	vec = daf->cells[0];
	if (vec >= sc->vec_count)
		return (EINVAL);

	*isrcp = &sc->isrcs[vec].isrc;
	return (0);
}

static int
eioic_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{

	return (0);
}

static void
eioic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	/*
	 * No mask.  The ISR clear in eioic_intr() consumes the edge; new MSIs
	 * during ithread processing set a fresh ISR bit and are delivered on
	 * the next HWI1 pass.
	 */
}

static void
eioic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	eioic_enable_intr(dev, isrc);
}

static void
eioic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{

	eioic_enable_intr(dev, isrc);
}

void
eiointc_init_cpu(void)
{
	/*
	 * APs don't touch the EIOINTC.  EXTIOI was enabled + the router
	 * programmed once in eioic_attach() (the BSP, before device probe).
	 * Re-enabling EXTIOI on an AP resets the global ROUTE to 0 (dropping
	 * all device interrupts).  eiointc_enable() + eiointc_router_init()
	 * are called only on the first CPU of each node; other CPUs just
	 * enable the ECFG pin (done by cpuic_init_secondary()).
	 */
}

static device_method_t eioic_methods[] = {
	DEVMETHOD(device_probe,		eioic_probe),
	DEVMETHOD(device_attach,	eioic_attach),

	/* PIC interface */
	DEVMETHOD(pic_disable_intr,	eioic_disable_intr),
	DEVMETHOD(pic_enable_intr,	eioic_enable_intr),
	DEVMETHOD(pic_map_intr,		eioic_map_intr),
	DEVMETHOD(pic_setup_intr,	eioic_setup_intr),
	DEVMETHOD(pic_pre_ithread,	eioic_pre_ithread),
	DEVMETHOD(pic_post_ithread,	eioic_post_ithread),
	DEVMETHOD(pic_post_filter,	eioic_post_filter),

	DEVMETHOD_END
};

static driver_t	eioic_driver = {
	"eioic",
	eioic_methods,
	sizeof(struct eioic_softc),
};

EARLY_DRIVER_MODULE(eioic, simplebus, eioic_driver, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
/* Also register on nexus for direct instantiation on ACPI systems. */
EARLY_DRIVER_MODULE(eioic, nexus, eioic_driver, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
