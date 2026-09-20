/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Xiaoqiang Zhao <zhaoxiaoqiang007@gmail.com>
 *
 * Shared definitions for the EIOINTC driver, used by child PICs
 * (PCH-PIC, PCH-MSI) that need to reference EIOINTC ISRCs directly.
 */

#ifndef	_LOONGARCH_EIOIC_H_
#define	_LOONGARCH_EIOIC_H_

#define	EIOINTC_MAX_IRQS	256

struct eioic_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			vec;
	struct intr_irqsrc	*child; /* pch_pic ISRC for INTx vectors;
					 * NULL for MSI */
};

struct eioic_softc {
	device_t		dev;
	struct resource		*irq_res;
	void			*intrhand;
	struct eioic_irqsrc	isrcs[EIOINTC_MAX_IRQS];
	u_int			vec_count;
	struct mtx		mtx;
};

/*
 * Look up the EIOINTC softc (NULL until the eioic driver has attached).
 * Used by child PICs (PCH-PIC) that reference EIOINTC ISRCs, and by AP init.
 */
struct eioic_softc *eioic_get_softc(void);

#endif /* !_LOONGARCH_EIOIC_H_ */
