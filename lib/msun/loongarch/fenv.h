/*-
 * Copyright (c) 2004-2005 David Schultz <das@FreeBSD.ORG>
 * Copyright (c) 2015-2016 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2024 Xiaoqiang Zhao <zhaoxiaoqiang007@gmail.com>
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
 *    documentation and other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef	_FENV_H_
#define	_FENV_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

#ifndef	__fenv_static
#define	__fenv_static	static
#endif

typedef	__uint64_t	fenv_t;
typedef	__uint64_t	femode_t;
typedef	__uint64_t	fexcept_t;

/*
 * LoongArch FCSR0 register layout (32-bit):
 *
 * Bits 0-4:   Exception enable bits (E)
 * Bits 5-7:   Reserved
 * Bits 8-9:   Rounding mode (RM)
 * Bits 10-15: Reserved
 * Bits 16-20: Exception flag bits (F)
 * Bits 21-23: Reserved
 * Bits 24-28: Exception cause bits (C)
 * Bits 29-31: Reserved
 */

/* Exception flags (bits 16-20) */
#define	FE_INVALID	0x100000
#define	FE_DIVBYZERO	0x080000
#define	FE_OVERFLOW	0x040000
#define	FE_UNDERFLOW	0x020000
#define	FE_INEXACT	0x010000
#define	FE_ALL_EXCEPT	(FE_DIVBYZERO | FE_INEXACT | \
			 FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW)

/* Rounding mode (bits 8-9) */
#define	_ROUND_SHIFT	8
#define	FE_TONEAREST	(0x0 << _ROUND_SHIFT)
#define	FE_TOWARDZERO	(0x1 << _ROUND_SHIFT)
#define	FE_UPWARD	(0x2 << _ROUND_SHIFT)
#define	FE_DOWNWARD	(0x3 << _ROUND_SHIFT)
#define	_ROUND_MASK	(FE_TONEAREST | FE_DOWNWARD | \
			 FE_UPWARD | FE_TOWARDZERO)

/* Exception enable bits (bits 0-4) */
#define	_ENABLE_SHIFT	16
#define	_ENABLE_MASK	0x1f

/* Exception cause bits (bits 24-28) */
#define	_CAUSE_SHIFT	8

__BEGIN_DECLS

/* Default floating-point environment */
extern const fenv_t	__fe_dfl_env;
#define	FE_DFL_ENV	(&__fe_dfl_env)

/* Default floating-point control modes */
extern const femode_t	__fe_dfl_mode;
#define	FE_DFL_MODE	(&__fe_dfl_mode)

#ifdef __loongarch_soft_float
#error "LoongArch soft float ABI is not supported"
#endif

#define	__rfs(__fcsr)	__asm __volatile("movfcsr2gr %0, $fcsr0" : "=r"(__fcsr))
#define	__wfs(__fcsr)	__asm __volatile("movgr2fcsr $fcsr0, %0" :: "r"(__fcsr))

int feclearexcept(int);
int fegetexceptflag(fexcept_t *, int);
int fesetexceptflag(const fexcept_t *, int);
int feraiseexcept(int);
int fetestexcept(int);
int fegetround(void);
int fesetround(int);
int fegetmode(femode_t *);
int fesetmode(const femode_t *);
int fegetenv(fenv_t *);
int feholdexcept(fenv_t *);
int fesetenv(const fenv_t *);
int feupdateenv(const fenv_t *);

/*
 * C permits a standard library function to also be exposed as a function-like
 * macro (C23 7.1.4), and msun uses that here to inline the fast path.  C++
 * forbids it: <cfenv> imports these names into namespace std (using
 * ::feclearexcept; etc.), so std::feclearexcept() and friends must denote the
 * actual functions.  Expose the inlining macros to C only; C++ uses the real
 * extern functions (defined in the matching lib/msun/<arch>/fenv.c).
 */
#ifndef __cplusplus
#define	feclearexcept(a)	__feclearexcept_int(a)
#define	fegetexceptflag(e, a)	__fegetexceptflag_int(e, a)
#define	fesetexceptflag(e, a)	__fesetexceptflag_int(e, a)
#define	feraiseexcept(a)	__feraiseexcept_int(a)
#define	fetestexcept(a)		__fetestexcept_int(a)
#define	fegetround()		__fegetround_int()
#define	fesetround(a)		__fesetround_int(a)
#define	fegetmode(m)		__fegetmode_int(m)
#define	fesetmode(m)		__fesetmode_int(m)
#define	fegetenv(e)		__fegetenv_int(e)
#define	feholdexcept(e)		__feholdexcept_int(e)
#define	fesetenv(e)		__fesetenv_int(e)
#define	feupdateenv(e)		__feupdateenv_int(e)
#endif /* !__cplusplus */

__fenv_static inline int
__feclearexcept_int(int __excepts)
{
	fexcept_t __fcsr;

	__excepts &= FE_ALL_EXCEPT;
	__rfs(__fcsr);
	__fcsr &= ~(__excepts | (__excepts << _CAUSE_SHIFT));
	__wfs(__fcsr);

	return (0);
}

__fenv_static inline int
__fegetexceptflag_int(fexcept_t *__flagp, int __excepts)
{
	fexcept_t __fcsr;

	__rfs(__fcsr);
	*__flagp = __fcsr & __excepts;

	return (0);
}

__fenv_static inline int
__fesetexceptflag_int(const fexcept_t *__flagp, int __excepts)
{
	fexcept_t __fcsr;

	__excepts &= FE_ALL_EXCEPT;
	__rfs(__fcsr);
	__fcsr &= ~__excepts;
	__fcsr |= *__flagp & __excepts;
	__wfs(__fcsr);

	return (0);
}

__fenv_static inline int
__feraiseexcept_int(int __excepts)
{
	fexcept_t __fcsr;

	__excepts &= FE_ALL_EXCEPT;
	__rfs(__fcsr);
	__fcsr |= __excepts | (__excepts << _CAUSE_SHIFT);
	__wfs(__fcsr);
	/*
	 * Execute a trivial FP instruction so the hardware evaluates Cause vs
	 * Enable and traps if any enabled Cause bit is set.  movgr2fcsr alone
	 * does not trigger FPE — only FP instructions do.  0.0+0.0=0.0 is
	 * exact and sets no new Cause bits, so it won't spuriously trap
	 * for INEXACT.
	 */
	__asm __volatile("fadd.d $f0,$f0,$f0" : : : "$f0");

	return (0);
}

__fenv_static inline int
__fetestexcept_int(int __excepts)
{
	fexcept_t __fcsr;

	__rfs(__fcsr);

	return (__fcsr & __excepts & FE_ALL_EXCEPT);
}

__fenv_static inline int
__fegetround_int(void)
{
	fexcept_t __fcsr;

	__rfs(__fcsr);

	return (__fcsr & _ROUND_MASK);
}

__fenv_static inline int
__fesetround_int(int __round)
{
	fexcept_t __fcsr;

	if (__round & ~_ROUND_MASK)
		return (-1);

	__rfs(__fcsr);
	__fcsr &= ~_ROUND_MASK;
	__fcsr |= __round;
	__wfs(__fcsr);

	return (0);
}

__fenv_static inline int
__fegetmode_int(femode_t *__modep)
{
	fexcept_t __fcsr;

	__rfs(__fcsr);
	*__modep = __fcsr & ~(FE_ALL_EXCEPT |
	    (FE_ALL_EXCEPT << _CAUSE_SHIFT));

	return (0);
}

__fenv_static inline int
__fesetmode_int(const femode_t *__modep)
{
	fexcept_t __fcsr;

	__rfs(__fcsr);
	__fcsr &= FE_ALL_EXCEPT | (FE_ALL_EXCEPT << _CAUSE_SHIFT);
	__fcsr |= *__modep;
	__wfs(__fcsr);

	return (0);
}

__fenv_static inline int
__fegetenv_int(fenv_t *__envp)
{

	__rfs(*__envp);

	return (0);
}

__fenv_static inline int
__feholdexcept_int(fenv_t *__envp)
{
	fexcept_t __fcsr;

	__rfs(__fcsr);
	*__envp = __fcsr;
	__fcsr &= ~(_ENABLE_MASK | FE_ALL_EXCEPT |
	    (FE_ALL_EXCEPT << _CAUSE_SHIFT));
	__wfs(__fcsr);

	return (0);
}

__fenv_static inline int
__fesetenv_int(const fenv_t *__envp)
{

	__wfs(*__envp);

	return (0);
}

__fenv_static inline int
__feupdateenv_int(const fenv_t *__envp)
{
	fexcept_t __fcsr;

	__rfs(__fcsr);
	__wfs(*__envp);
	feraiseexcept(__fcsr & FE_ALL_EXCEPT);

	return (0);
}

#if __BSD_VISIBLE

int feenableexcept(int);
int fedisableexcept(int);

#ifndef __cplusplus	/* see the note above; C++ uses the real functions */
#define	feenableexcept(a)	__feenableexcept_int(a)
#define	fedisableexcept(a)	__fedisableexcept_int(a)
#endif

__fenv_static inline int
__feenableexcept_int(int __mask)
{
	fexcept_t __fcsr, __old;

	__mask &= FE_ALL_EXCEPT;
	__rfs(__fcsr);
	__old = __fcsr & _ENABLE_MASK;
	__fcsr |= __mask >> _ENABLE_SHIFT;
	__wfs(__fcsr);

	return (__old << _ENABLE_SHIFT);
}

__fenv_static inline int
__fedisableexcept_int(int __mask)
{
	fexcept_t __fcsr, __old;

	__mask &= FE_ALL_EXCEPT;
	__rfs(__fcsr);
	__old = __fcsr & _ENABLE_MASK;
	__fcsr &= ~(__mask >> _ENABLE_SHIFT);
	__wfs(__fcsr);

	return (__old << _ENABLE_SHIFT);
}

/* We currently provide no external definition of fegetexcept(). */
static inline int
fegetexcept(void)
{
	fexcept_t __fcsr;

	__rfs(__fcsr);

	return ((__fcsr & _ENABLE_MASK) << _ENABLE_SHIFT);
}

#endif /* __BSD_VISIBLE */

__END_DECLS

#endif	/* !_FENV_H_ */
