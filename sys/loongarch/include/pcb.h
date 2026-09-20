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

#ifndef	_MACHINE_PCB_H_
#define	_MACHINE_PCB_H_

#ifndef LOCORE

struct trapframe;

struct pcb {
	union {
		uint64_t	pcb_regs[32];
		struct {
			uint64_t r0;
			uint64_t ra;
			uint64_t tp;
			uint64_t sp;
			uint64_t a[8];
			uint64_t t[9];
			uint64_t r21;
			uint64_t fp;
			uint64_t s[9];
		} u;
	};
#define pcb_a u.a
#define pcb_t u.t
#define pcb_s u.s
#define pcb_ra u.ra
#define pcb_sp u.sp
	union {
		uint64_t pcb_fregs[34];
		struct {
			uint64_t a[8];
			uint64_t t[16];
			uint64_t s[8];
			uint64_t pcb_fcsr0;
		} uf;
	};
	uint64_t	pcb_a0;
	uint64_t pcb_fpflags;
#define	PCB_FP_STARTED	0x1
#define	PCB_FP_LSX_STARTED	0x2
#define	PCB_FP_LASX_STARTED	0x4
#define	PCB_FP_USERMASK	0x1
	vm_offset_t	pcb_onfault;	/* Copyinout fault handler */
};

#ifdef _KERNEL
void	makectx(struct trapframe *tf, struct pcb *pcb);
int	savectx(struct pcb *pcb) __returns_twice;
#endif

#endif /* !LOCORE */

#endif /* !_MACHINE_PCB_H_ */
