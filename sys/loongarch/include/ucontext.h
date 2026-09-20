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

#ifndef _MACHINE_UCONTEXT_H_
#define	_MACHINE_UCONTEXT_H_

struct gpregs {
	union {
		__register_t	gp_regs[32];
		struct {
			__register_t r0;
			__register_t ra;
			__register_t tp;
			__register_t sp;
			__register_t gp_a[8];
			__register_t gp_t[9];
			__register_t r21;
			__register_t fp;
			__register_t gp_s[9];
		};
	};

	__register_t	gp_crmd;
	__register_t	gp_prmd;
	__register_t	gp_euen;
	__register_t	gp_misc;
	__register_t	gp_ecfg;
	__register_t	gp_estat;
	__register_t	gp_era;
	__register_t	gp_badvaddr;
	__register_t	gp_orig_a0;
};

struct fpregs {
	__uint64_t	fp_regs[32];
	__uint64_t	fp_fcsr;
	__uint64_t	fcc;
	int		fp_flags;
	int		pad;
};

struct __mcontext {
	struct gpregs	mc_gpregs;
	struct fpregs	mc_fpregs;
	int		mc_flags;
#define	_MC_FP_VALID 0x1 /* Set when mc_fpregs has valid data */
#define	_MC_FP_SIMD 0x2 /* SIMD block present; mc_spare[0] -> user ptr */
	int		mc_pad;
	__uint64_t	mc_spare[8];	/* Space for expansion */
};

typedef struct __mcontext mcontext_t;

#endif	/* !_MACHINE_UCONTEXT_H_ */
