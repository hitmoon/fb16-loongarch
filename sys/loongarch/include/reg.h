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

#ifndef	_MACHINE_REG_H_
#define	_MACHINE_REG_H_

#include <sys/_types.h>

struct reg {
	union {
		__uint64_t	regs[32];	/* general purpose registers */
		struct {
			__uint64_t	r0;
			__uint64_t	ra;
			__uint64_t	tp;
			__uint64_t	sp;
			__uint64_t	a[8];
			__uint64_t	t[9];
			__uint64_t	r21;
			__uint64_t	fp;
			__uint64_t	s[9];
		};
	};

	/*
	 * Layout matches the LLVM/LLDB LoongArch register-info GPR set
	 * (RegisterInfoPOSIX_loongarch64::GPR): 32 GPRs, orig_a0, csr_era (pc),
	 * csr_badv, then a 10-slot tail.  The first six tail slots expose the
	 * privileged CSRs by name for debuggers; the remainder is reserved, so
	 * struct reg is byte-for-byte compatible with PT_GETREGS / NT_PRSTATUS
	 * consumers (lldb, gdb) without per-field translation.
	 */
	__uint64_t	orig_a0;	/* 256: original syscall arg0 */
	__uint64_t	csr_era;	/* 264: PC (exception return addr) */
	__uint64_t	csr_badv;	/* 272: bad virtual address */
	__uint64_t	crmd;		/* 280 */
	__uint64_t	prmd;		/* 288 */
	__uint64_t	euen;		/* 296 */
	__uint64_t	misc;		/* 304 */
	__uint64_t	ecfg;		/* 312 */
	__uint64_t	estat;		/* 320 */
	__uint64_t	_reserved[4];	/* 328: pad to LLVM reserved[10] */
};

struct fpreg {
	/* Matches the LLVM/LLDB FPR set: fpr[32], fcc (8 bits), fcsr. */
	__uint64_t	fpr[32];	/* floating point registers */
	__uint64_t	fcc;		/* 8 FP condition codes */
	__uint32_t	fcsr;		/* FP control/status register */
};

/*
 * LSX (128-bit) and LASX (256-bit) SIMD register sets, exposed via
 * PT_GETREGSET / PT_SETREGSET and core-dump notes (NT_LOONGARCH_LSX /
 * NT_LOONGARCH_LASX).  Legacy PT_GETFPREGS / struct fpreg are unchanged.
 */
struct lsxreg {
	__uint64_t	xr_regs[32 * 2];	/* 32 x 128-bit LSX registers */
};
struct lasxreg {
	__uint64_t	xr_regs[32 * 4];	/* 32 x 256-bit LASX
						 * registers */
};

/*
 * Hardware debug registers (ptrace PT_GETDBREGS / PT_SETDBREGS).
 *
 * Exposes the LoongArch data-watchpoint slots (DB0..DB7).  db_ctrl holds the
 * DBnCTRL bit layout (CSR_DBC_*): a non-zero value arms the slot, 0 leaves it
 * unused.  The ASID is managed internally from the target pmap and is not part
 * of the ABI.
 */
#define	LA_WATCH_SLOTS	8

struct dbreg {
	__uint8_t	db_revision;		/* layout revision */
	__uint8_t	db_nslots;		/* usable watchpoint slots */
	__uint8_t	db_pad[6];
	struct {
		__uint64_t	db_addr;	/* watched VA */
		__uint32_t	db_ctrl;	/* DBnCTRL bits (0 = unused) */
		__uint32_t	db_pad;
	} db_slots[LA_WATCH_SLOTS];
};

#endif /* !_MACHINE_REG_H_ */
