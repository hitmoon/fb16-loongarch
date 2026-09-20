/*-
 * Copyright (c) 2015-2017 Ruslan Bukin <br@bsdpad.com>
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

/*
 * RISC-V Timer
 */

#include "opt_platform.h"

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/vdso.h>
#include <sys/watchdog.h>

#include <machine/cpufunc.h>
#include <machine/intr.h>
#include <machine/md_var.h>

#include <dev/ofw/openfirm.h>

struct loongarch_timer_softc {
	void			*ih;
	uint32_t		clkfreq;
	struct eventtimer	et;
};
static struct loongarch_timer_softc *loongarch_timer_sc = NULL;

static timecounter_get_t loongarch_timer_tc_get_timecount;
static timecounter_fill_vdso_timehands_t loongarch_timer_tc_fill_vdso_timehands;

static struct timecounter loongarch_timer_timecount = {
	.tc_name           = "LoongArch Timecounter",
	.tc_get_timecount  = loongarch_timer_tc_get_timecount,
	.tc_poll_pps       = NULL,
	.tc_counter_mask   = ~0u,
	.tc_frequency      = 0,
	.tc_quality        = 1000,
	.tc_fill_vdso_timehands = loongarch_timer_tc_fill_vdso_timehands,
};

static inline uint64_t
get_timecount(void)
{
	return (rdtime());
}

static u_int
loongarch_timer_tc_get_timecount(struct timecounter *tc __unused)
{
	return (get_timecount());
}

static uint32_t
loongarch_timer_tc_fill_vdso_timehands(struct vdso_timehands *vdso_th,
    struct timecounter *tc)
{
	vdso_th->th_algo = VDSO_TH_ALGO_LOONGARCH_RDTIME;
	bzero(vdso_th->th_res, sizeof(vdso_th->th_res));
	return (1);
}

static int
loongarch_timer_et_start(struct eventtimer *et, sbintime_t first,
    sbintime_t period)
{
	uint64_t counts;
	uint64_t val;

	if (first == 0 && period == 0)
		return (EINVAL);

	if (first != 0) {
		counts = ((uint32_t)et->et_frequency * first) >> 32;
	} else {
		counts = ((uint32_t)et->et_frequency * period) >> 32;
	}

	counts &= CSR_TCFG_VAL;
	val = counts | CSR_TCFG_EN;

	if (period) {
		val |= CSR_TCFG_PERIOD;
	} else {
		val &= ~CSR_TCFG_PERIOD;
	}

	csr_write64(val, LOONGARCH_CSR_TCFG);

	return (0);
}

static int
loongarch_timer_et_stop(struct eventtimer *et)
{
	uint64_t val;

	val = csr_read64(LOONGARCH_CSR_TCFG);
	/* Disable timer */
	val &= ~CSR_TCFG_EN;
	csr_write64(val, LOONGARCH_CSR_TCFG);
	return (0);
}

static int
loongarch_timer_intr(void *arg)
{
	struct loongarch_timer_softc *sc;

	sc = (struct loongarch_timer_softc *)arg;

	/* Clear the timer interrupt. */
	csr_write32(1, LOONGARCH_CSR_TINTCLR);

	if (sc->et.et_active)
		sc->et.et_event_cb(&sc->et, sc->et.et_arg);

	return (FILTER_HANDLED);
}

static int
loongarch_timer_get_timebase(device_t dev, uint32_t *freq)
{
	uint32_t res;
	uint32_t base_freq;
	uint32_t cfm, cfd;

	res = read_cpucfg(LOONGARCH_CPUCFG2);
	if (!(res & CPUCFG2_LLFTP))
		return (ENXIO);
	base_freq = read_cpucfg(LOONGARCH_CPUCFG4);
	res = read_cpucfg(LOONGARCH_CPUCFG5);
	cfm = res & 0xffff;
	cfd = (res >> 16) & 0xffff;

	if (!base_freq || !cfm || !cfd)
		return (ENXIO);

	*freq = (base_freq * cfm / cfd);

	return (0);
}

static int
loongarch_timer_probe(device_t dev)
{

	device_set_desc(dev, "LoongArch Timer");

	return (BUS_PROBE_DEFAULT);
}

static int
loongarch_timer_attach(device_t dev)
{
	struct loongarch_timer_softc *sc;
	int error;

	sc = device_get_softc(dev);
	if (loongarch_timer_sc != NULL)
		return (ENXIO);

	if (device_get_unit(dev) != 0)
		return (ENXIO);

	if (loongarch_timer_get_timebase(dev, &sc->clkfreq) != 0) {
		device_printf(dev, "No clock frequency specified\n");
		return (ENXIO);
	}

	loongarch_timer_sc = sc;

	/*
	 * Register the timer interrupt handler on the cpuic IRQ_TI ISRC via
	 * the cpuic driver's CPU-local helper.  bus_setup_intr can't be used
	 * here because the timer has no FDT interrupt node.
	 */
	error = cpuic_setup_cpu_intr(device_get_nameunit(dev),
	    loongarch_timer_intr, NULL, sc, IRQ_TI, INTR_TYPE_CLK,
	    &sc->ih);
	if (error) {
		device_printf(dev, "Unable to alloc int resource.\n");
		return (ENXIO);
	}

	loongarch_timer_timecount.tc_frequency = sc->clkfreq;
	loongarch_timer_timecount.tc_priv = sc;
	tc_init(&loongarch_timer_timecount);

	sc->et.et_name = "LoongArch Eventtimer";
	sc->et.et_flags = ET_FLAGS_ONESHOT | ET_FLAGS_PERIODIC |
	    ET_FLAGS_PERCPU;
	sc->et.et_quality = 1000;

	sc->et.et_frequency = sc->clkfreq;
	sc->et.et_min_period = (0x00000002LLU << 32) / sc->et.et_frequency;
	sc->et.et_max_period = (0xfffffffeLLU << 32) / sc->et.et_frequency;
	sc->et.et_start = loongarch_timer_et_start;
	sc->et.et_stop = loongarch_timer_et_stop;
	sc->et.et_priv = sc;
	et_register(&sc->et);

	set_cputicker(get_timecount, sc->clkfreq, false);

	return (0);
}

static device_method_t loongarch_timer_methods[] = {
	DEVMETHOD(device_probe,		loongarch_timer_probe),
	DEVMETHOD(device_attach,	loongarch_timer_attach),
	{ 0, 0 }
};

static driver_t loongarch_timer_driver = {
	"timer",
	loongarch_timer_methods,
	sizeof(struct loongarch_timer_softc),
};

EARLY_DRIVER_MODULE(timer, nexus, loongarch_timer_driver, 0, 0,
    BUS_PASS_TIMER + BUS_PASS_ORDER_MIDDLE);

void
DELAY(int usec)
{
	int64_t counts, counts_per_usec;
	uint64_t first, last;

	/*
	 * Check the timers are setup, if not just
	 * use a for loop for the meantime
	 */
	if (loongarch_timer_sc == NULL) {
		for (; usec > 0; usec--)
			for (counts = 200; counts > 0; counts--)
				/*
				 * Prevent the compiler from optimizing
				 * out the loop
				 */
				cpufunc_nullop();
		return;
	}
	TSENTER();

	/* Get the number of times to count */
	counts_per_usec = ((loongarch_timer_timecount.tc_frequency /
	    1000000) + 1);

	/*
	 * Clamp the timeout at a maximum value (about 32 seconds with
	 * a 66MHz clock). *Nobody* should be delay()ing for anywhere
	 * near that length of time and if they are, they should be hung
	 * out to dry.
	 */
	if (usec >= (0x80000000U / counts_per_usec))
		counts = (0x80000000U / counts_per_usec) - 1;
	else
		counts = usec * counts_per_usec;

	first = get_timecount();

	while (counts > 0) {
		last = get_timecount();
		counts -= (int64_t)(last - first);
		first = last;
	}
	TSEXIT();
}
