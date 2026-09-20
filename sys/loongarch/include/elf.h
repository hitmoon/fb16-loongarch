/*-
 * Copyright (c) 1996-1997 John D. Polstra.
 * All rights reserved.
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

#ifndef	_MACHINE_ELF_H_
#define	_MACHINE_ELF_H_

/*
 * ELF definitions for the LoongArch architecture.
 */

#include <sys/elf32.h>	/* Definitions common to all 32 bit architectures. */
#include <sys/elf64.h>	/* Definitions common to all 64 bit architectures. */

#define	__ELF_WORD_SIZE	64	/* Used by <sys/elf_generic.h> */
#include <sys/elf_generic.h>

/*
 * Auxiliary vector entries for passing information to the interpreter.
 */

typedef struct {	/* Auxiliary vector entry on initial stack */
	int	a_type;			/* Entry type. */
	union {
		int	a_val;		/* Integer value. */
	} a_un;
} Elf32_Auxinfo;

typedef struct {	/* Auxiliary vector entry on initial stack */
	long	a_type;			/* Entry type. */
	union {
		long	a_val;		/* Integer value. */
		void	*a_ptr;		/* Address. */
		void	(*a_fcn)(void);	/* Function pointer (not used). */
	} a_un;
} Elf64_Auxinfo;

__ElfType(Auxinfo);

#define	ELF_ARCH	EM_LOONGARCH

#define	ELF_MACHINE_OK(x)	((x) == (ELF_ARCH))

/* Define "machine" characteristics */
#define	ELF_TARG_CLASS	ELFCLASS64
#define	ELF_TARG_DATA	ELFDATA2LSB
#define	ELF_TARG_MACH	EM_LOONGARCH
#define	ELF_TARG_VER	1

/* TODO: set correct value */
#define	ET_DYN_LOAD_ADDR	0x100000

/* Flags passed in AT_HWCAP */
#define	HWCAP_LOONGARCH_CPUCFG	(1 << 0)
#define	HWCAP_LOONGARCH_LAM	(1 << 1)
#define	HWCAP_LOONGARCH_UAL	(1 << 2)
#define	HWCAP_LOONGARCH_FPU	(1 << 3)
#define	HWCAP_LOONGARCH_LSX	(1 << 4)
#define	HWCAP_LOONGARCH_LASX	(1 << 5)
#define	HWCAP_LOONGARCH_CRC32	(1 << 6)
#define	HWCAP_LOONGARCH_COMPLEX	(1 << 7)
#define	HWCAP_LOONGARCH_CRYPTO	(1 << 8)
#define	HWCAP_LOONGARCH_LVZ	(1 << 9)
#define	HWCAP_LOONGARCH_LBT_X86	(1 << 10)
#define	HWCAP_LOONGARCH_LBT_ARM	(1 << 11)
#define	HWCAP_LOONGARCH_LBT_MIPS	(1 << 12)
#define	HWCAP_LOONGARCH_PTW	(1 << 13)

/* LoongArch note types (core dumps / PT_GETREGSET), per the LoongArch psABI. */
#define	NT_LOONGARCH_LSX	0xa02	/* LoongArch SIMD (LSX, 128-bit)
					 * registers */
#define	NT_LOONGARCH_LASX	0xa03	/* LoongArch Adv. SIMD (LASX, 256-bit)
					 * registers */

/*
 * LoongArch ELF relocation types.  The authoritative set lives in
 * <sys/elf_common.h> (included via <sys/elf64.h>); the subset below is kept
 * for the entries not present there (the PCADD family and R_LARCH_64_PCREL,
 * used by the kernel module loader).  The remainder are redundant with
 * elf_common.h and match it.
 */
#define	R_LARCH_NONE		0
#define	R_LARCH_32		1
#define	R_LARCH_64		2
#define	R_LARCH_RELATIVE	3
#define	R_LARCH_COPY		4
#define	R_LARCH_JUMP_SLOT	5
#define	R_LARCH_IRELATIVE	12
#define	R_LARCH_MARK_LA		20
#define	R_LARCH_MARK_PCREL	21
#define	R_LARCH_SOP_PUSH_PCREL	22
#define	R_LARCH_SOP_PUSH_ABSOLUTE	23
#define	R_LARCH_SOP_PUSH_DUP	24
#define	R_LARCH_SOP_PUSH_PLT_PCREL	29
#define	R_LARCH_SOP_SUB		32
#define	R_LARCH_SOP_SL		33
#define	R_LARCH_SOP_SR		34
#define	R_LARCH_SOP_ADD		35
#define	R_LARCH_SOP_AND		36
#define	R_LARCH_SOP_IF_ELSE	37
#define	R_LARCH_SOP_POP_32_S_10_5	38
#define	R_LARCH_SOP_POP_32_U_10_12	39
#define	R_LARCH_SOP_POP_32_S_10_12	40
#define	R_LARCH_SOP_POP_32_S_10_16	41
#define	R_LARCH_SOP_POP_32_S_10_16_S2	42
#define	R_LARCH_SOP_POP_32_S_5_20	43
#define	R_LARCH_SOP_POP_32_S_0_5_10_16_S2	44
#define	R_LARCH_SOP_POP_32_S_0_10_10_16_S2	45
#define	R_LARCH_SOP_POP_32_U	46
#define	R_LARCH_ADD32		50
#define	R_LARCH_ADD64		51
#define	R_LARCH_SUB32		55
#define	R_LARCH_SUB64		56
#define	R_LARCH_B26		66
#define	R_LARCH_PCALA_HI20	71
#define	R_LARCH_PCALA_LO12	72
#define	R_LARCH_PCALA64_LO20	73
#define	R_LARCH_PCALA64_HI12	74
#define	R_LARCH_GOT_PC_HI20	75
#define	R_LARCH_GOT_PC_LO12	76
#define	R_LARCH_64_PCREL	109
#define	R_LARCH_PCADD_HI20	128
#define	R_LARCH_PCADD_LO12	129
#define	R_LARCH_GOT_PCADD_HI20	130
#define	R_LARCH_GOT_PCADD_LO12	131

#endif /* !_MACHINE_ELF_H_ */
