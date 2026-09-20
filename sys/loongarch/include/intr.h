/*-
 * Copyright (c) 2015-2016 Ruslan Bukin <br@bsdpad.com>
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

#ifndef	_MACHINE_INTR_MACHDEP_H_
#define	_MACHINE_INTR_MACHDEP_H_

#define	MAX_IO_PICS	2
#define	NR_IRQS		(64 + (256 * MAX_IO_PICS))

#define	NIRQ		NR_IRQS
#define	INTR_ROOT_COUNT	1
#define	INTR_ROOT_IRQ	0

/* Interrupt numbers */
#define	INT_SWI0	0	/* Software Interrupts */
#define	INT_SWI1	1
#define	INT_HWI0	2	/* Hardware Interrupts */
#define	INT_HWI1	3
#define	INT_HWI2	4
#define	INT_HWI3	5
#define	INT_HWI4	6
#define	INT_HWI5	7
#define	INT_HWI6	8
#define	INT_HWI7	9
#define	INT_PCOV	10	/* Performance Counter Overflow */
#define	INT_TI		11	/* Timer */
#define	INT_IPI		12
#define	INT_NMI		13
#define	INT_AVEC	14

#ifndef LOCORE
#ifdef INTRNG
#include <sys/intr.h>
#endif

struct trapframe;

/* EIOINTC PIC driver interface */
void	eiointc_enable_vector(device_t dev, u_int vector);
void	eiointc_disable_vector(device_t dev, u_int vector);
void	eiointc_init_cpu(void);

/* PCH-PIC FDT xref, for PCI INTx routing in nexus. */
intptr_t	pch_pic_get_xref(void);

/* CPU-local interrupt setup via the cpuic driver (timer, IPI). */
int	cpuic_setup_cpu_intr(const char *, driver_filter_t *,
	    void (*)(void *), void *, u_int, int, void **);
void	cpuic_setup_ipi(driver_filter_t *);

/* ACPI MSI cross-reference for the generic PCI host bridge (placeholder
 * until the PCH MSI controller is properly wired via MADT MSI_PIC). */
#define	ACPI_MSI_XREF	2

/*
 * ACPI cross-reference for the interrupt controller that owns the GSI
 * (global system interrupt) space.  On ACPI the PCH-PIC maps device GSIs
 * (LOONGSON_PCH_IRQ_BASE == 64 and up) to EIOINTC vectors, so it owns this
 * xref.  nexus_acpi_map_intr resolves every device interrupt through it.
 * xref 1 is cpuic, 2 is eioic; this must not collide with those.
 */
#define	ACPI_INTR_XREF	3

/* IPI/mailbox hardware helpers */
void	csr_mail_send(uint64_t data, int cpu, int mailbox);
void	eiointc_enable_extioi(void);
uint32_t ipi_read_clear(int cpu);
void	ipi_write_action(int cpu, uint32_t action);
void	ipi_all_but_self(u_int ipi);
void	ipi_cpu(int cpu, u_int ipi);
void	ipi_selected(cpuset_t cpus, u_int ipi);

#define	CORES_PER_EIO_NODE	4

#define	LOONGSON_CPU_UART0_VEC	10	/* CPU UART0 */
#define	LOONGSON_CPU_THSENS_VEC	14	/* CPU Thsens */
#define	LOONGSON_CPU_HT0_VEC 16 /* CPU HT0 irq vector base number */
#define	LOONGSON_CPU_HT1_VEC 24 /* CPU HT1 irq vector base number */

/* IRQ number definitions */
#define	LOONGSON_LPC_IRQ_BASE	0
#define	LOONGSON_LPC_LAST_IRQ	(LOONGSON_LPC_IRQ_BASE + 15)

#define	LOONGSON_CPU_IRQ_BASE	16
#define	LOONGSON_CPU_LAST_IRQ	(LOONGSON_CPU_IRQ_BASE + 14)

#define	LOONGSON_PCH_IRQ_BASE	64
#define	LOONGSON_PCH_ACPI_IRQ	(LOONGSON_PCH_IRQ_BASE + 47)
#define	LOONGSON_PCH_LAST_IRQ	(LOONGSON_PCH_IRQ_BASE + 64 - 1)

#define	LOONGSON_MSI_IRQ_BASE	(LOONGSON_PCH_IRQ_BASE + 64)
#define	LOONGSON_MSI_LAST_IRQ	(LOONGSON_PCH_IRQ_BASE + 256 - 1)

#define	GSI_MIN_LPC_IRQ		LOONGSON_LPC_IRQ_BASE
#define	GSI_MAX_LPC_IRQ		(LOONGSON_LPC_IRQ_BASE + 16 - 1)
#define	GSI_MIN_CPU_IRQ		LOONGSON_CPU_IRQ_BASE
#define	GSI_MAX_CPU_IRQ		(LOONGSON_CPU_IRQ_BASE + 48 - 1)
#define	GSI_MIN_PCH_IRQ		LOONGSON_PCH_IRQ_BASE
#define	GSI_MAX_PCH_IRQ		(LOONGSON_PCH_IRQ_BASE + 256 - 1)

enum {
	IRQ_SWI0 = INT_SWI0,
	IRQ_SWI1 = INT_SWI1,
	IRQ_HWI0 = INT_HWI0,
	IRQ_HWI1 = INT_HWI1,
	IRQ_HWI2 = INT_HWI2,
	IRQ_HWI3 = INT_HWI3,
	IRQ_HWI4 = INT_HWI4,
	IRQ_HWI5 = INT_HWI5,
	IRQ_HWI6 = INT_HWI6,
	IRQ_HWI7 = INT_HWI7,
	IRQ_PCOV = INT_PCOV,	/* Performance counter overflow */
	IRQ_TI = INT_TI,	/* Timer */
	IRQ_IPI = INT_IPI,
	IRQ_NMI = INT_NMI,
	IRQ_AVEC = INT_AVEC,
	INTC_NIRQS
};
#endif

#endif /* !_MACHINE_INTR_MACHDEP_H_ */
