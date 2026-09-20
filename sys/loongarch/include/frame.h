/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
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

#ifndef _MACHINE_FRAME_H_
#define	_MACHINE_FRAME_H_

#ifndef LOCORE

#include <sys/signal.h>
#include <sys/ucontext.h>

/*
 * The GPR union mirrors the named-register layout of struct reg, but
 * struct trapframe is NOT byte-identical to struct reg: struct reg is
 * aligned to the LLVM register-info layout (see <machine/reg.h>).
 */
struct trapframe {
	/* general registers */
	union {
		uint64_t tf_regs[32];
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

#define tf_a u.a
#define tf_t u.t
#define tf_s u.s
#define tf_sp u.sp
#define tf_tp u.tp
#define tf_ra u.ra
#define tf_fp u.fp

	/* Special CSR registers. */
	uint64_t tf_crmd;
	uint64_t tf_prmd;
	uint64_t tf_euen;
	uint64_t tf_misc;
	uint64_t tf_ecfg;
	uint64_t tf_estat;
	uint64_t tf_era;
	uint64_t tf_badvaddr;

	union {
		uint64_t tf_fregs[34];
		struct {
			uint64_t a[8];
			uint64_t t[16];
			uint64_t s[8];
			uint64_t fcsr0;
			uint64_t fcc;
		} uf;
	};
	/*
	 * Vector register storage for LSX (128-bit) / LASX (256-bit).
	 * 32 slots of 32 bytes each; saved only when EUEN has LSXEN/LASXEN
	 * set.  The low 64/128 bits alias the scalar FP / LSX view of the
	 * same physical register.
	 */
	uint64_t tf_vregs[32][4];
	/* Original syscal arg0 */
	uint64_t tf_a0;
};

/*
 * Signal frame. Pushed onto user stack before calling sigcode.
 */
/*
 * Optional SIMD extension appended to struct sigframe (kernel-internal
 * layout; not part of the user signal ABI).  When mcontext flags
 * _MC_FP_SIMD, mc_spare[0] holds the user-space address of this block.
 */
#define	LOONGARCH_SIMD_MAGIC_LSX	0x53580001	/* "SX\1"  */
#define	LOONGARCH_SIMD_MAGIC_LASX	0x41535801	/* "ASX\1" */
struct loongarch_simd_ctx {
	uint32_t	d_magic;		/* LSX or LASX magic */
	uint32_t	d_size;			/* sizeof(this struct) */
	uint32_t	d_pad[2];		/* align payload to 16 bytes */
	uint8_t		d_regs[32][32];	/* 32 vector regs: low 16 bytes
					 * valid for LSX, full 32 for LASX */
};

struct sigframe {
	siginfo_t			sf_si;	/* actual saved siginfo */
	ucontext_t			sf_uc;	/* actual saved ucontext */
	struct loongarch_simd_ctx	sf_simd; /* optional SIMD state */
};

#endif /* !LOCORE */

/* Definitions for syscalls */
#define	NARGREG		8	/* 8 args in regs */

#endif /* !_MACHINE_FRAME_H_ */
