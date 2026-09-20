/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Xiaoqiang Zhao <zhaoxiaoqiang007@gmail.com>
 *
 * LoongArch PCH-PIC interrupt controller driver.
 *
 * The PCH-PIC is a hardware interrupt router in the LS7A chipset that
 * maps up to 64 device interrupt inputs (INTx, LPC, etc.) to EIOINTC
 * vectors.  It does NOT have its own interrupt handler — each input is
 * individually routed to a specific EIOINTC vector via the HTVEC
 * register.  EIOINTC handles all dispatch.
 *
 * QEMU virt configures 32 PCH-PIC inputs starting at EIOINTC vector 0.
 * PCI INTx uses inputs 16-19 (VIRT_DEVICE_IRQS).
 *
 * Registers (MMIO, base typically 0x10000000):
 *   PCH_PIC_MASK     0x20    Mask (1 = disabled)
 *   PCH_PIC_HTMSI_EN  0x40    HT MSI enable
 *   PCH_PIC_EDGE      0x60    Edge/Level (1 = edge)
 *   PCH_PIC_CLR       0x80    Clear edge latch
 *   PCH_PIC_AUTO0     0xc0    Auto-bounce control
 *   PCH_PIC_AUTO1     0xe0
 *   PCH_INT_ROUTE(i)  0x100+i Per-IRQ route byte
 *   PCH_INT_HTVEC(i)  0x200+i Per-IRQ HT vector number
 *   PCH_PIC_POL       0x3e0    Polarity (1 = active low)
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/interrupt.h>
#include <sys/callout.h>
#include <sys/time.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/machdep.h>

#include "opt_acpi.h"
#ifdef DEV_ACPI
#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>
#endif

#include "pic_if.h"
#include "eioic.h"

#define	PCH_PIC_MASK		0x20
#define	PCH_PIC_HTMSI_EN	0x40
#define	PCH_PIC_EDGE		0x60
#define	PCH_PIC_CLR		0x80
#define	PCH_PIC_AUTO0		0xc0
#define	PCH_PIC_AUTO1		0xe0
#define	PCH_INT_ROUTE(i)	(0x100 + (i))
#define	PCH_INT_HTVEC(i)	(0x200 + (i))
#define	PCH_PIC_POL		0x3e0

#define	PIC_COUNT_PER_REG	32
#define	PIC_COUNT		64

/*
 * Spurious-storm throttling.
 *
 * An input whose physical line is stuck asserted (e.g. the LS7A OHCI INTx
 * line, which stays low even at intrstatus==0) re-fires on every post_ithread
 * unmask, producing ~329k interrupts/sec (~10% CPU) with no real work.
 *
 * We detect this by measuring the gap between an unmask (post_ithread) and
 * the next fire (pre_ithread): a gap below PCH_PIC_STORM_THRESH means the line
 * was still asserted when we re-enabled it.  storm_refire counts such gaps;
 * once it reaches PCH_PIC_STORM_LIMIT we stop unmasking in post_ithread and
 * instead re-arm the input from a callout every PCH_PIC_STORM_COOLDOWN,
 * collapsing the rate to ~1/COOLDOWN while still letting real interrupts
 * (e.g. OHCI WDH) through.  A normal gap decrements storm_refire, so a line
 * that stops being stuck resumes immediate unmasking automatically.  Normal
 * inputs (ahci/xhci/MSI) never see small gaps, so they are never throttled.
 */
#define PCH_PIC_STORM_THRESH (SBT_1MS * 2) /* gap < 2ms = fast re-fire */
#define PCH_PIC_STORM_LIMIT 8 /* consecutive fast re-fires to trip */
#define PCH_PIC_STORM_SAT (PCH_PIC_STORM_LIMIT + 4) /* refire counter cap */
#define PCH_PIC_STORM_COOLDOWN SBT_1MS /* re-arm interval while throttled */

#define	FDT_IRQ_TYPE_LEVEL_HIGH	4

struct pch_pic_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq; /* PCH-PIC input index */
	int			storm_refire; /* fast re-fire counter */
	bool			storm_throttled; /* delayed-unmask mode */
	sbintime_t		storm_last_post; /* last post_ithread time */
	struct callout		storm_unmask_co; /* deferred re-arm while
						 * throttled */
};

struct pch_pic_softc {
	device_t		dev;
	struct resource		*mem_res;
	int			mem_rid;
	u_int			vec_base;	/* EIOINTC vector base */
	u_int			vec_count;
	u_int			gsi_base;	/* ACPI GSI base
						 * (LOONGSON_PCH_IRQ_BASE) */
	bool			is_acpi;	/* ACPI/direct vs FDT
						 * instantiation */
	intptr_t		xref;		/* this PIC's xref (FDT or
						 * ACPI_INTR_XREF) */
	struct mtx		mtx;
	struct pch_pic_irqsrc	isrcs[PIC_COUNT];
};

static device_t pch_pic_dev;	/* single-instance guard */

/*
 * Look up the PCH-PIC's xref (0 until the pch_pic driver has attached).
 * Used by nexus for PCI INTx routing.
 */
intptr_t
pch_pic_get_xref(void)
{
	device_t d;
	struct pch_pic_softc *sc;

	d = devclass_get_device(devclass_find("pch_pic"), 0);
	if (d == NULL)
		return (0);
	sc = device_get_softc(d);
	return (sc->xref);
}

static inline uint32_t
pch_pic_read(struct pch_pic_softc *sc, bus_size_t off)
{

	return (bus_read_4(sc->mem_res, off));
}

static inline void
pch_pic_write(struct pch_pic_softc *sc, bus_size_t off, uint32_t val)
{

	bus_write_4(sc->mem_res, off, val);
}

static void
pch_pic_bit_set(struct pch_pic_softc *sc, int offset, int bit)
{
	uint32_t val;
	bus_size_t reg;

	reg = offset + (bit / PIC_COUNT_PER_REG) * 4;
	val = pch_pic_read(sc, reg);
	val |= (1U << (bit % PIC_COUNT_PER_REG));
	pch_pic_write(sc, reg, val);
}

static void
pch_pic_bit_clr(struct pch_pic_softc *sc, int offset, int bit)
{
	uint32_t val;
	bus_size_t reg;

	reg = offset + (bit / PIC_COUNT_PER_REG) * 4;
	val = pch_pic_read(sc, reg);
	val &= ~(1U << (bit % PIC_COUNT_PER_REG));
	pch_pic_write(sc, reg, val);
}

static void
pch_pic_reset(struct pch_pic_softc *sc)
{
	int i;

	for (i = 0; i < (int)sc->vec_count; i++) {
		/* Write EIOINTC vector number to HTVEC register */
		bus_write_1(sc->mem_res, PCH_INT_HTVEC(i),
		    sc->vec_base + i);
		/* Route to HT0 */
		bus_write_1(sc->mem_res, PCH_INT_ROUTE(i), 1);
	}

	for (i = 0; i < 2; i++) {
		/*
		 * Clear all MASK bits (0 = enabled).  The MASK register
		 * gates interrupt forwarding; since the ISRC returned by
		 * pic_map_intr is owned by EIOINTC, PIC_ENABLE_INTR goes
		 * to EIOINTC and never reaches PCH-PIC to clear MASK.
		 * We unmask everything at init and rely on EIOINTC's
		 * per-vector enable/disable for interrupt control.
		 */
		pch_pic_write(sc, PCH_PIC_MASK + 4 * i, 0);
		pch_pic_write(sc, PCH_PIC_CLR + 4 * i, 0xFFFFFFFF);
		/*
		 * Distribution mode = fixed (7A1000 manual §5.4: AUTO_CTRL
		 * selects fixed/rotating/idle/busy distribution among the two
		 * interrupt outputs -- NOT "auto-bounce"; fixed is
		 * recommended and must not be changed mid-run).  Routing
		 * follows ROUTE_ENTRY.
		 */
		pch_pic_write(sc, PCH_PIC_AUTO0 + 4 * i, 0);
		pch_pic_write(sc, PCH_PIC_AUTO1 + 4 * i, 0);
		/* Enable HTMSI transformer for all */
		pch_pic_write(sc, PCH_PIC_HTMSI_EN + 4 * i, 0xFFFFFFFF);
		/*
		 * PCI legacy INTx is active-low, level-triggered (PCI spec).
		 * Default all inputs to level/active-low here.
		 */
		pch_pic_write(sc, PCH_PIC_EDGE + 4 * i, 0);
		pch_pic_write(sc, PCH_PIC_POL + 4 * i, 0xFFFFFFFF);
	}
}

static int
pch_pic_probe(device_t dev)
{

	/*
	 * Two paths.  FDT node: used only when ACPI is absent (FDT fallback),
	 * otherwise ofwbus duplicates the nexus (ACPI) instance.  nexus/ACPI
	 * direct child (no OFW node): match by name "pch_pic".
	 */
	if (ofw_bus_get_node(dev) != -1) {
		if (loongarch_efi_acpi_rsdp() != 0)
			return (ENXIO);
		if (!ofw_bus_is_compatible(dev, "loongson,pch-pic-1.0"))
			return (ENXIO);
	} else {
		if (strcmp(device_get_name(dev), "pch_pic") != 0)
			return (ENXIO);
	}

	device_set_desc(dev, "LoongArch PCH PIC");
	return (BUS_PROBE_DEFAULT);
}

static int
pch_pic_attach(device_t dev)
{
	struct pch_pic_softc *sc;
	phandle_t node;
	pcell_t vec_base;
	intptr_t xref;
	int i, error;

	/*
	 * Single instance: the FDT and direct/nexus paths must not both
	 * attach.
	 */
	if (pch_pic_dev != NULL)
		return (ENXIO);
	pch_pic_dev = dev;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->mem_rid = 0;
	mtx_init(&sc->mtx, "pch_pic", NULL, MTX_SPIN);

#ifdef DEV_ACPI
	sc->is_acpi = (loongarch_efi_acpi_rsdp() != 0);
#else
	sc->is_acpi = false;
#endif

	if (sc->is_acpi) {
#ifdef DEV_ACPI
		vm_paddr_t addr;
		u_int size, gsi_base;

		/*
		 * Get the PCH-PIC parameters from the MADT BIO_PIC subtable:
		 * MMIO register base, window size and GSI base.  The root
		 * PCH-PIC starts at EIOINTC vector 0.
		 */
		if (!loongarch_acpi_pch_pic_info(&addr, &size, &gsi_base)) {
			device_printf(dev, "no MADT BIO_PIC entry\n");
			return (ENXIO);
		}
		sc->gsi_base = gsi_base;
		sc->vec_base = 0;
		if (bus_set_resource(dev, SYS_RES_MEMORY, 0, addr, size) != 0) {
			device_printf(dev, "cannot set MMIO resource\n");
			return (ENXIO);
		}
#endif
	} else {
		node = ofw_bus_get_node(dev);

		/* Read pic-base-vec from FDT */
		if (OF_getencprop(node, "loongson,pic-base-vec", &vec_base,
		    sizeof(vec_base)) < 0) {
			device_printf(dev, "missing loongson,pic-base-vec\n");
			return (ENXIO);
		}
		sc->vec_base = (u_int)vec_base;
		sc->gsi_base = 0;
	}

	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->mem_rid, RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "cannot allocate memory resource\n");
		return (ENXIO);
	}

	/* Determine number of interrupt inputs from hardware version */
	sc->vec_count = ((pch_pic_read(sc, 4) >> 16) & 0xff) + 1;
	if (sc->vec_count == 0 || sc->vec_count > PIC_COUNT)
		sc->vec_count = PIC_COUNT;

	/* Initialize routing and mask all interrupts */
	pch_pic_reset(sc);

	/*
	 * Register our own ISRCs (one per input).  pic_map_intr returns these
	 * to INTRNG and associates each with its EIOINTC vector (eioic child
	 * pointer), so eioic_intr dispatches the pch_pic ISRC -- and thus our
	 * pre/post_ithread actually run to ack/re-arm (CLR) per interrupt.
	 */
	for (i = 0; i < sc->vec_count; i++) {
		sc->isrcs[i].irq = i;
		sc->isrcs[i].storm_refire = 0;
		sc->isrcs[i].storm_throttled = false;
		sc->isrcs[i].storm_last_post = 0;
		callout_init(&sc->isrcs[i].storm_unmask_co, 1);
		error = intr_isrc_register(&sc->isrcs[i].isrc, dev, 0,
		    "%s,%u", device_get_nameunit(dev), i);
		if (error != 0) {
			device_printf(dev, "cannot register isrc %d\n", i);
			goto fail;
		}
	}

	/*
	 * Register as a PIC.  On ACPI, own the GSI space via ACPI_INTR_XREF;
	 * on FDT, use the device-tree xref.
	 */
	xref = sc->is_acpi ? ACPI_INTR_XREF :
	    OF_xref_from_node(ofw_bus_get_node(dev));
	sc->xref = xref;
	if (intr_pic_register(dev, xref) == NULL) {
		device_printf(dev, "cannot register PIC\n");
		error = ENXIO;
		goto fail;
	}

	device_printf(dev, "vec_base=%u vec_count=%u gsi_base=%u%s\n",
	    sc->vec_base, sc->vec_count, sc->gsi_base,
	    sc->is_acpi ? " (ACPI)" : "");

	return (0);

fail:
	bus_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
	return (error);
}

/* Map an interrupt (FDT or ACPI) to the EIOINTC ISRC for its vector. */
static int
pch_pic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	struct pch_pic_softc *sc;
	struct eioic_softc *eioic;
	u_int irq, eioic_vec;

	sc = device_get_softc(dev);

	switch (data->type) {
#ifdef DEV_ACPI
	case INTR_MAP_DATA_ACPI: {
		struct intr_map_data_acpi *daa;

		daa = (struct intr_map_data_acpi *)data;
		/* GSI space: gsi_base .. gsi_base + vec_count - 1 */
		if (daa->irq < sc->gsi_base ||
		    daa->irq >= sc->gsi_base + sc->vec_count)
			return (EINVAL);
		irq = daa->irq - sc->gsi_base;
		break;
	}
#endif
	case INTR_MAP_DATA_FDT: {
		struct intr_map_data_fdt *daf;

		daf = (struct intr_map_data_fdt *)data;
		if (daf->ncells != 2)
			return (EINVAL);
		irq = daf->cells[0];
		if (irq >= sc->vec_count)
			return (EINVAL);
		break;
	}
	default:
		return (ENOTSUP);
	}

	eioic_vec = sc->vec_base + irq;

	/*
	 * Return our own ISRC for this input, and associate it with the
	 * EIOINTC vector so eioic_intr dispatches us (the child) instead of
	 * the EIOINTC's own ISRC.  This makes our pre/post_ithread run.
	 */
	eioic = eioic_get_softc();
	if (eioic == NULL)
		return (ENXIO);
	if (eioic_vec >= eioic->vec_count)
		return (EINVAL);
	eioic->isrcs[eioic_vec].child = &sc->isrcs[irq].isrc;

	*isrcp = &sc->isrcs[irq].isrc;
	return (0);
}

static void
pch_pic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct pch_pic_softc *sc = device_get_softc(dev);
	struct pch_pic_irqsrc *pisrc = (struct pch_pic_irqsrc *)isrc;
	u_int irq = pisrc->irq;

	if (irq >= sc->vec_count)
		return;

	mtx_lock_spin(&sc->mtx);
	/* Ack/re-arm + unmask so the source can fire. */
	pch_pic_bit_set(sc, PCH_PIC_CLR, irq);
	pch_pic_bit_clr(sc, PCH_PIC_MASK, irq);
	mtx_unlock_spin(&sc->mtx);
}

static void
pch_pic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct pch_pic_softc *sc = device_get_softc(dev);
	struct pch_pic_irqsrc *pisrc = (struct pch_pic_irqsrc *)isrc;
	u_int irq = pisrc->irq;

	if (irq >= sc->vec_count)
		return;

	mtx_lock_spin(&sc->mtx);
	pch_pic_bit_set(sc, PCH_PIC_MASK, irq);
	mtx_unlock_spin(&sc->mtx);
}

static int
pch_pic_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct pch_pic_softc *sc = device_get_softc(dev);
	struct pch_pic_irqsrc *pisrc = (struct pch_pic_irqsrc *)isrc;
	bool edge = false, pol_low = false;
	u_int irq;

	if (data == NULL)
		return (0);

	irq = pisrc->irq;
	if (irq >= sc->vec_count)
		return (EINVAL);

	switch (data->type) {
#ifdef DEV_ACPI
	case INTR_MAP_DATA_ACPI:
		/*
		 * LS7A PCH-PIC INTx: level-triggered, active-low.
		 * Per-source only -- the global reset leaves unused inputs
		 * at the default level/high so floating lines don't fire.
		 */
		edge = true;
		pol_low = true;
		break;
#endif
	case INTR_MAP_DATA_FDT: {
		struct intr_map_data_fdt *daf;

		daf = (struct intr_map_data_fdt *)data;
		if (daf->ncells < 2)
			return (0);
		switch (daf->cells[1] & 0xf) {
		case 1:	/* EDGE_RISING */
			edge = true; pol_low = false; break;
		case 2:	/* EDGE_FALLING */
			edge = true; pol_low = true; break;
		case 4:	/* LEVEL_HIGH */
			edge = false; pol_low = false; break;
		case 8:	/* LEVEL_LOW */
			edge = false; pol_low = true; break;
		default:
			return (0);
		}
		break;
	}
	default:
		return (0);
	}

	mtx_lock_spin(&sc->mtx);

	/* Configure edge/level and polarity for this input. */
	if (edge)
		pch_pic_bit_set(sc, PCH_PIC_EDGE, irq);
	else
		pch_pic_bit_clr(sc, PCH_PIC_EDGE, irq);
	if (pol_low)
		pch_pic_bit_set(sc, PCH_PIC_POL, irq);
	else
		pch_pic_bit_clr(sc, PCH_PIC_POL, irq);

	mtx_unlock_spin(&sc->mtx);
	return (0);
}

static void
pch_pic_deferred_unmask(void *arg)
{
	struct pch_pic_irqsrc *pisrc = arg;
	struct pch_pic_softc *sc = device_get_softc(pch_pic_dev);

	mtx_lock_spin(&sc->mtx);
	pch_pic_bit_clr(sc, PCH_PIC_MASK, pisrc->irq);
	mtx_unlock_spin(&sc->mtx);
}

static void
pch_pic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct pch_pic_softc *sc = device_get_softc(dev);
	struct pch_pic_irqsrc *pisrc = (struct pch_pic_irqsrc *)isrc;
	sbintime_t now;

	now = getsbinuptime();
	mtx_lock_spin(&sc->mtx);
	if (now - pisrc->storm_last_post < PCH_PIC_STORM_THRESH) {
		/* Re-fired within THRESH of the last unmask: the line is still
		 * asserted.  Charge it toward throttling. */
		if (pisrc->storm_refire < PCH_PIC_STORM_SAT)
			pisrc->storm_refire++;
	} else if (pisrc->storm_refire > 0) {
		/* Normal gap: a previously-stuck line may be recovering. */
		pisrc->storm_refire--;
	}
	pisrc->storm_throttled = (pisrc->storm_refire >= PCH_PIC_STORM_LIMIT);
	/* Ack/re-arm (CLR) while masked. */
	pch_pic_bit_set(sc, PCH_PIC_MASK, pisrc->irq);
	pch_pic_bit_set(sc, PCH_PIC_CLR, pisrc->irq);
	mtx_unlock_spin(&sc->mtx);
}

static void
pch_pic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct pch_pic_softc *sc = device_get_softc(dev);
	struct pch_pic_irqsrc *pisrc = (struct pch_pic_irqsrc *)isrc;

	mtx_lock_spin(&sc->mtx);
	pisrc->storm_last_post = getsbinuptime();
	if (pisrc->storm_throttled) {
		/* Leave masked; re-arm from a callout to collapse the
		 * unmask->refire storm while still letting real interrupts
		 * (e.g. OHCI WDH) through every PCH_PIC_STORM_COOLDOWN. */
		callout_reset_sbt(&pisrc->storm_unmask_co,
		    PCH_PIC_STORM_COOLDOWN, 0, pch_pic_deferred_unmask, pisrc,
		    C_DIRECT_EXEC);
	} else {
		/* Unmask immediately: normal input. */
		pch_pic_bit_clr(sc, PCH_PIC_MASK, pisrc->irq);
	}
	mtx_unlock_spin(&sc->mtx);
}

static void
pch_pic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{

	pch_pic_post_ithread(dev, isrc);
}

static device_method_t pch_pic_methods[] = {
	DEVMETHOD(device_probe,		pch_pic_probe),
	DEVMETHOD(device_attach,	pch_pic_attach),

	DEVMETHOD(pic_disable_intr,	pch_pic_disable_intr),
	DEVMETHOD(pic_enable_intr,	pch_pic_enable_intr),
	DEVMETHOD(pic_map_intr,		pch_pic_map_intr),
	DEVMETHOD(pic_setup_intr,	pch_pic_setup_intr),
	DEVMETHOD(pic_pre_ithread,	pch_pic_pre_ithread),
	DEVMETHOD(pic_post_ithread,	pch_pic_post_ithread),
	DEVMETHOD(pic_post_filter,	pch_pic_post_filter),

	DEVMETHOD_END
};

static driver_t pch_pic_driver = {
	"pch_pic",
	pch_pic_methods,
	sizeof(struct pch_pic_softc),
};

EARLY_DRIVER_MODULE(pch_pic, simplebus, pch_pic_driver, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
/* Also register on nexus for direct instantiation on ACPI systems. */
EARLY_DRIVER_MODULE(pch_pic, nexus, pch_pic_driver, 0, 0,
    BUS_PASS_INTERRUPT + BUS_PASS_ORDER_MIDDLE);
