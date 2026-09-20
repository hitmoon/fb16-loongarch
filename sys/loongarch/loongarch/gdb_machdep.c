/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2021 Mitchell Horne <mhorne@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
 * LoongArch GDB register numbering follows the architectural order r0..r31,
 * which is how struct trapframe (tf_regs) and struct pcb (pcb_regs) lay the
 * general-purpose registers out, followed by the PC (tf_era).  GDB_NREGS is
 * therefore 33: 32 GPRs + PC.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/signal.h>

#include <machine/frame.h>
#include <machine/gdb_machdep.h>
#include <machine/pcb.h>
#include <machine/loongarchreg.h>

#include <gdb/gdb.h>

void *
gdb_cpu_getreg(int regnum, size_t *regsz)
{
	*regsz = gdb_cpu_regsz(regnum);

	if (kdb_thread == curthread) {
		if (regnum >= 0 && regnum < GDB_REG_PC)
			return (&kdb_frame->tf_regs[regnum]);
		if (regnum == GDB_REG_PC)
			return (&kdb_frame->tf_era);
		return (NULL);
	}

	/*
	 * For other threads only the callee-saved registers are valid in the
	 * saved pcb (see savectx): ra, tp, sp, fp, and s0-s8.
	 */
	switch (regnum) {
	case GDB_REG_RA:
	case GDB_REG_TP:
	case GDB_REG_SP:
	case GDB_REG_FP:
		return (&kdb_thrctx->pcb_regs[regnum]);
	case GDB_REG_PC:		/* Best effort: the resume PC is ra. */
		return (&kdb_thrctx->pcb_regs[GDB_REG_RA]);
	default:
		if (regnum >= GDB_REG_S0 && regnum < GDB_REG_PC)
			return (&kdb_thrctx->pcb_regs[regnum]);
		return (NULL);
	}
}

void
gdb_cpu_setreg(int regnum, void *val)
{
	register_t regval = *(register_t *)val;

	if (kdb_thread == curthread) {
		if (regnum >= 0 && regnum < GDB_REG_PC)
			kdb_frame->tf_regs[regnum] = regval;
		else if (regnum == GDB_REG_PC)
			kdb_frame->tf_era = regval;
	}

	/* Keep the saved pcb context in sync for the callee-saved registers. */
	switch (regnum) {
	case GDB_REG_RA:
	case GDB_REG_TP:
	case GDB_REG_SP:
	case GDB_REG_FP:
		kdb_thrctx->pcb_regs[regnum] = regval;
		break;
	case GDB_REG_PC:
		kdb_thrctx->pcb_regs[GDB_REG_RA] = regval;
		break;
	default:
		if (regnum >= GDB_REG_S0 && regnum < GDB_REG_PC)
			kdb_thrctx->pcb_regs[regnum] = regval;
		break;
	}
}

int
gdb_cpu_signal(int type, int code)
{

	if (type == EXCCODE_BP)
		return (SIGTRAP);

	return (SIGEMT);
}
