/*-
 * Copyright (c) 2016 Ruslan Bukin <br@bsdpad.com>
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

#ifndef	_MACHINE_SMP_H_
#define	_MACHINE_SMP_H_

#include <machine/pcb.h>

#define	IPI_AST		(1 << 0)
#define	IPI_PREEMPT	(1 << 1)
#define	IPI_RENDEZVOUS	(1 << 2)
#define	IPI_STOP	(1 << 3)
#define	IPI_STOP_HARD	(1 << 4)
#define	IPI_HARDCLOCK	(1 << 5)
#define	IPI_TLB		(1 << 6)

#define	INTR_IPI_COUNT	1

void ipi_all_but_self(u_int ipi);
void ipi_cpu(int cpu, u_int ipi);
void ipi_selected(cpuset_t cpus, u_int ipi);
uint32_t ipi_read_clear(int cpu);
void csr_mail_send(uint64_t data, int cpu, int mailbox);

/* SMP TLB shootdown op codes (stored in a shootdown descriptor's op field). */
#define	LA_TLB_OP_PAGE		0
#define	LA_TLB_OP_RANGE		1
#define	LA_TLB_OP_ALL		2

struct pmap;

/* Synchronous TLB shootdown via IPIs with a 2-D scoreboard. */
void smp_targeted_tlb_shootdown(struct pmap *pmap, vm_offset_t addr1,
    vm_offset_t addr2, uint8_t op);

/* Record a stale ASID whose TLB entries should be batch-flushed. */
void la_stale_add(uint32_t asid);
void la_stale_reset(void);

#endif /* !_MACHINE_SMP_H_ */
