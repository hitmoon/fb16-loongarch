/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Xiaoqiang Zhao <zhaoxiaoqiang007@gmail.com>
 *
 * Local header for the CPU interrupt controller (cpuic) and
 * intr_machdep shared data.
 */

#ifndef _LOONGARCH_INTC_H_
#define	_LOONGARCH_INTC_H_

#include <sys/intr.h>
#include <machine/intr.h>

struct intc_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
};

/* isrcs moved to cpuic_softc — no global array */

#endif /* !_LOONGARCH_INTC_H_ */
