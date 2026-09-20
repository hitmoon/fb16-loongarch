/*-
 * Copyright (c) 2015-2017 Ruslan Bukin <br@bsdpad.com>
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

#ifndef _MACHINE_LOONGARCH_H_
#define	_MACHINE_LOONGARCH_H_

#define	INSN_SIZE		4

/* Generic bitmask helper (used by identcpu.c) */
#define	_GENMASK(h, l)		(((~0UL) >> (63 - (h))) & ((~0UL) << (l)))

/* Bit fields for CPUCFG registers */
#define	LOONGARCH_CPUCFG0	0x0

#define	BIT_U(n)		(1U << (n))
#define	BIT_ULL(nr)		(1ULL << (nr))

#define	LOONGARCH_CPUCFG1	0x1
#define	CPUCFG1_ISGR32		BIT_U(0)
#define	CPUCFG1_ISGR64		BIT_U(1)
#define	CPUCFG1_UAL		BIT_U(20)
#define	CPUCFG1_CRC32		BIT_U(25)

#define	LOONGARCH_CPUCFG2	0x2
#define	CPUCFG2_FP		BIT_U(0)
#define	CPUCFG2_LSX		BIT_U(6)
#define	CPUCFG2_LASX		BIT_U(7)
#define	CPUCFG2_COMPLEX		BIT_U(8)
#define	CPUCFG2_CRYPTO		BIT_U(9)
#define	CPUCFG2_LVZP		BIT_U(10)
#define	CPUCFG2_LLFTP		BIT_U(14)
#define	CPUCFG2_X86BT		BIT_U(18)
#define	CPUCFG2_ARMBT		BIT_U(19)
#define	CPUCFG2_MIPSBT		BIT_U(20)
#define	CPUCFG2_LAM		BIT_U(22)
#define	CPUCFG2_PTW		BIT_U(24)

#define	LOONGARCH_CPUCFG4	0x4

#define	LOONGARCH_CPUCFG5	0x5

#define	LOONGARCH_CPUCFG6	0x6
#define	CPUCFG6_PMP		BIT_U(0)

/* CSR */

#define	__csrrd(reg) \
({ u_long val;        \
  __asm __volatile("csrrd %0, " #reg : "=r" (val)); \
  val;                \
})

#define	__csrwr(val, reg) \
({                    \
	uint64_t __temp = val; \
  __asm __volatile( \
  "csrwr %0, " #reg : "+r" (__temp) \
	:: "memory" \
	); \
})

#define	csr_read32(reg)		__csrrd(reg)
#define	csr_read64(reg)		__csrrd(reg)
#define	csr_write32(val, reg)	__csrwr(val, reg)
#define	csr_write64(val, reg)	__csrwr(val, reg)

/* IOCSR */

#define	__iocsrrd_w(reg) \
({ uint32_t val;        \
  __asm __volatile(			\
      "iocsrrd.w %0, %1" \
     : "=r"(val) \
     : "r"(reg)); \
  val; \
})

#define	__iocsrrd_d(reg) \
({ uint64_t val;        \
  __asm __volatile(			\
      "iocsrrd.d %0, %1" \
     : "=r"(val)	\
     : "r" (reg)); \
  val; \
})

#define	__iocsrwr_w(val, reg) \
({                  \
  __asm __volatile(	\
      "iocsrwr.w %0, %1\n" \
     :: "r"(val), "r"(reg)); \
})

#define	__iocsrwr_d(val, reg) \
({                  \
  __asm __volatile(	\
      "iocsrwr.d %0, %1\n" \
     :: "r" (val), "r"(reg)); \
})

#define	iocsr_read32(reg)	__iocsrrd_w(reg)
#define	iocsr_read64(reg)	__iocsrrd_d(reg)
#define	iocsr_write32(val, reg)	__iocsrwr_w(val, reg)
#define	iocsr_write64(val, reg)	__iocsrwr_d(val, reg)


/* CSR register number */

#define	_ULCAST_(x)		(x##UL)

/* Basic CSR registers */
#define	LOONGARCH_CSR_CRMD	0x0	/* Current mode info */
#define	CSR_CRMD_WE_SHIFT	9
#define	CSR_CRMD_WE		(_ULCAST_(0x1) << CSR_CRMD_WE_SHIFT)
#define	CSR_CRMD_IE_SHIFT	2
#define	CSR_CRMD_IE		(_ULCAST_(0x1) << CSR_CRMD_IE_SHIFT)
#define	CSR_CRMD_PLV_SHIFT	0

#define	PLV_USER		3
#define	PLV_MASK		0x3

#define	LOONGARCH_CSR_PRMD	0x1	/* Prev-exception mode info */
#define	CSR_PRMD_PWE_SHIFT	3
#define	CSR_PRMD_PWE		(_ULCAST_(0x1) << CSR_PRMD_PWE_SHIFT)
#define	CSR_PRMD_PIE_SHIFT	2
#define	CSR_PRMD_PIE		(_ULCAST_(0x1) << CSR_PRMD_PIE_SHIFT)

#define	LOONGARCH_CSR_EUEN	0x2	/* Extended unit enable */
#define	CSR_EUEN_LASXEN_SHIFT	2
#define	CSR_EUEN_LASXEN		(_ULCAST_(0x1) << CSR_EUEN_LASXEN_SHIFT)
#define	CSR_EUEN_LSXEN_SHIFT	1
#define	CSR_EUEN_LSXEN		(_ULCAST_(0x1) << CSR_EUEN_LSXEN_SHIFT)
#define	CSR_EUEN_FPEN_SHIFT	0
#define	CSR_EUEN_FPEN		(_ULCAST_(0x1) << CSR_EUEN_FPEN_SHIFT)

#define	LOONGARCH_CSR_MISC	0x3	/* Misc config */

#define	LOONGARCH_CSR_ECFG	0x4	/* Exception config */
#define	CSR_ECFG_IM_SHIFT	0
#define	CSR_ECFG_IM		(_ULCAST_(0x3fff) << CSR_ECFG_IM_SHIFT)

#define	LOONGARCH_CSR_ESTAT	0x5	/* Exception status */
#define	CSR_ESTAT_EXC_SHIFT	16
#define	CSR_ESTAT_EXC		(_ULCAST_(0x3f) << CSR_ESTAT_EXC_SHIFT)
#define	CSR_ESTAT_IS_SHIFT	0
#define	CSR_ESTAT_IS		(_ULCAST_(0x3fff) << CSR_ESTAT_IS_SHIFT)

#define	LOONGARCH_CSR_ERA	0x6	/* ERA */

#define	LOONGARCH_CSR_BADV	0x7	/* Bad virtual address */

#define	LOONGARCH_CSR_EENTRY	0xc	/* Exception entry */

/* TLB related CSR registers */
#define	LOONGARCH_CSR_TLBIDX	0x10	/* TLB Index, EHINV, PageSize, NP */
#define	CSR_TLBIDX_EHINV_SHIFT	31
#define	CSR_TLBIDX_EHINV	(_ULCAST_(1) << CSR_TLBIDX_EHINV_SHIFT)
#define	CSR_TLBIDX_PS_SHIFT	24
#define	CSR_TLBIDX_PS		(_ULCAST_(0x3f) << CSR_TLBIDX_PS_SHIFT)

#define	LOONGARCH_CSR_TLBEHI	0x11	/* TLB EntryHi */

#define	LOONGARCH_CSR_TLBELO0	0x12	/* TLB EntryLo0 */

#define	LOONGARCH_CSR_TLBELO1	0x13	/* TLB EntryLo1 */

#define	LOONGARCH_CSR_ASID	0x18	/* ASID */
#define	CSR_ASID_BIT_SHIFT	16	/* ASIDBits */
#define	CSR_ASID_BIT		(_ULCAST_(0xff) << CSR_ASID_BIT_SHIFT)
#define	CSR_ASID_ASID_SHIFT	0
#define	CSR_ASID_ASID		(_ULCAST_(0x3ff) << CSR_ASID_ASID_SHIFT)

#define	LOONGARCH_CSR_PGDL	0x19	/* Page table base address when
					 * VA[VALEN-1] = 0 */

#define	LOONGARCH_CSR_PGDH	0x1a	/* Page table base address when
					 * VA[VALEN-1] = 1 */

#define	LOONGARCH_CSR_PGD	0x1b	/* Page table base */

#define	LOONGARCH_CSR_PWCTL0	0x1c	/* PWCtl0 */

#define	LOONGARCH_CSR_PWCTL1	0x1d	/* PWCtl1 */
#define	CSR_PWCTL1_PTW_SHIFT	24
#define	CSR_PWCTL1_PTW		(_ULCAST_(0x1) << CSR_PWCTL1_PTW_SHIFT)

#define	LOONGARCH_CSR_STLBPGSIZE 0x1e

/* Config CSR registers */
#define	LOONGARCH_CSR_CPUID	0x20	/* CPU core id */

#define	LOONGARCH_CSR_PRCFG1	0x21	/* Config1 */
#define	CSR_CONF1_KSNUM		_ULCAST_(0xf)

#define	LOONGARCH_CSR_PRCFG3	0x23	/* Config3 */
#define	CSR_CONF3_STLBIDX_SHIFT	20
#define	CSR_CONF3_STLBIDX	(_ULCAST_(0x3f) << CSR_CONF3_STLBIDX_SHIFT)
#define	CSR_CONF3_STLBWAYS_SHIFT 12
#define	CSR_CONF3_STLBWAYS	(_ULCAST_(0xff) << CSR_CONF3_STLBWAYS_SHIFT)
#define	CSR_CONF3_MTLBSIZE_SHIFT 4
#define	CSR_CONF3_MTLBSIZE	(_ULCAST_(0xff) << CSR_CONF3_MTLBSIZE_SHIFT)
#define	CSR_CONF3_TLBTYPE_SHIFT	0
#define	CSR_CONF3_TLBTYPE	(_ULCAST_(0xf) << CSR_CONF3_TLBTYPE_SHIFT)

/* KSave registers */
#define	LOONGARCH_CSR_KS0	0x30
#define	LOONGARCH_CSR_KS1	0x31
#define	LOONGARCH_CSR_KS2	0x32
#define	LOONGARCH_CSR_KS3	0x33
#define	LOONGARCH_CSR_KS4	0x34

/* Exception allocated KS0, KS1, KS2, and KS3 statically */
#define	EXCEPTION_KS0		LOONGARCH_CSR_KS0
#define	EXCEPTION_KS1		LOONGARCH_CSR_KS1
#define	EXCEPTION_KS2		LOONGARCH_CSR_KS2
#define	EXCEPTION_KS3		LOONGARCH_CSR_KS3
#define	EXC_KSAVE_MASK		(1 << 0 | 1 << 1 | 1 << 2 | 1 << 3)

/* Percpu-data base allocated KS4 statically */
#define	PERCPU_BASE_KS		LOONGARCH_CSR_KS4
#define	PERCPU_KSAVE_MASK	(1 << 4)

/* Timer registers */
#define	LOONGARCH_CSR_TMID	0x40	/* Timer ID */

#define	LOONGARCH_CSR_TCFG	0x41	/* Timer config */
#define	CSR_TCFG_VAL_SHIFT	2
#define	CSR_TCFG_VAL		(_ULCAST_(0x3fffffffffff) << CSR_TCFG_VAL_SHIFT)
#define	CSR_TCFG_PERIOD_SHIFT	1
#define	CSR_TCFG_PERIOD		(_ULCAST_(0x1) << CSR_TCFG_PERIOD_SHIFT)
#define	CSR_TCFG_EN		(_ULCAST_(0x1))

#define	LOONGARCH_CSR_TINTCLR	0x44	/* Timer interrupt clear */

/* TLB Refill registers */
#define	LOONGARCH_CSR_TLBRENTRY	0x88	/* TLB refill exception entry */
#define	LOONGARCH_CSR_TLBRSAVE	0x8b	/* KSave for TLB refill exception */

/* Direct Map windows registers */
#define	LOONGARCH_CSR_DMWIN0	0x180	/* 64 direct map win0: MEM & IF */

/* Direct Map window 0/1 */
#define	DMW_PABITS		48

#define	CSR_DMW0_PLV0		(1 << 0)
#define	CSR_DMW0_VSEG		(_ULCAST_(0x8000))
#define	CSR_DMW0_BASE		(CSR_DMW0_VSEG << DMW_PABITS)
#define	CSR_DMW0_INIT		(CSR_DMW0_BASE | CSR_DMW0_PLV0)

/* Debug registers */
#define	LOONGARCH_CSR_MWPC	0x300	/* data breakpoint config */
#define	LOONGARCH_CSR_MWPS	0x301	/* data breakpoint status */

#define	LOONGARCH_CSR_DB0ADDR	0x310	/* data breakpoint 0 address */
#define	LOONGARCH_CSR_DB0MASK	0x311	/* data breakpoint 0 mask */
#define	LOONGARCH_CSR_DB0CTRL	0x312	/* data breakpoint 0 control */
#define	LOONGARCH_CSR_DB0ASID	0x313	/* data breakpoint 0 asid */

#define	LOONGARCH_CSR_DB1ADDR	0x318	/* data breakpoint 1 address */
#define	LOONGARCH_CSR_DB1MASK	0x319	/* data breakpoint 1 mask */
#define	LOONGARCH_CSR_DB1CTRL	0x31a	/* data breakpoint 1 control */
#define	LOONGARCH_CSR_DB1ASID	0x31b	/* data breakpoint 1 asid */

#define	LOONGARCH_CSR_DB2ADDR	0x320	/* data breakpoint 2 address */
#define	LOONGARCH_CSR_DB2MASK	0x321	/* data breakpoint 2 mask */
#define	LOONGARCH_CSR_DB2CTRL	0x322	/* data breakpoint 2 control */
#define	LOONGARCH_CSR_DB2ASID	0x323	/* data breakpoint 2 asid */

#define	LOONGARCH_CSR_DB3ADDR	0x328	/* data breakpoint 3 address */
#define	LOONGARCH_CSR_DB3MASK	0x329	/* data breakpoint 3 mask */
#define	LOONGARCH_CSR_DB3CTRL	0x32a	/* data breakpoint 3 control */
#define	LOONGARCH_CSR_DB3ASID	0x32b	/* data breakpoint 3 asid */

#define	LOONGARCH_CSR_DB4ADDR	0x330	/* data breakpoint 4 address */
#define	LOONGARCH_CSR_DB4MASK	0x331	/* data breakpoint 4 maks */
#define	LOONGARCH_CSR_DB4CTRL	0x332	/* data breakpoint 4 control */
#define	LOONGARCH_CSR_DB4ASID	0x333	/* data breakpoint 4 asid */

#define	LOONGARCH_CSR_DB5ADDR	0x338	/* data breakpoint 5 address */
#define	LOONGARCH_CSR_DB5MASK	0x339	/* data breakpoint 5 mask */
#define	LOONGARCH_CSR_DB5CTRL	0x33a	/* data breakpoint 5 control */
#define	LOONGARCH_CSR_DB5ASID	0x33b	/* data breakpoint 5 asid */

#define	LOONGARCH_CSR_DB6ADDR	0x340	/* data breakpoint 6 address */
#define	LOONGARCH_CSR_DB6MASK	0x341	/* data breakpoint 6 mask */
#define	LOONGARCH_CSR_DB6CTRL	0x342	/* data breakpoint 6 control */
#define	LOONGARCH_CSR_DB6ASID	0x343	/* data breakpoint 6 asid */

#define	LOONGARCH_CSR_DB7ADDR	0x348	/* data breakpoint 7 address */
#define	LOONGARCH_CSR_DB7MASK	0x349	/* data breakpoint 7 mask */
#define	LOONGARCH_CSR_DB7CTRL	0x34a	/* data breakpoint 7 control */
#define	LOONGARCH_CSR_DB7ASID	0x34b	/* data breakpoint 7 asid */

/*
 * Data watchpoint control (DBnCTRL) bit layout.  A slot is armed by setting a
 * PLV-enable bit (selects which privilege levels trigger); cleared by writing
 * DBnCTRL = 0.  Size 0=8B,1=4B,2=2B,3=1B.
 */
#define	CSR_DBC_LOADEN		(1ul << 8)	/* trigger on loads */
#define	CSR_DBC_STOREEN		(1ul << 9)	/* trigger on stores */
#define	CSR_DBC_SIZE_SHIFT	10
#define	CSR_DBC_SIZE_MASK	(0x3ul << CSR_DBC_SIZE_SHIFT)
#define	CSR_DBC_LEN_8		(0x0ul << CSR_DBC_SIZE_SHIFT)
#define	CSR_DBC_LEN_4		(0x1ul << CSR_DBC_SIZE_SHIFT)
#define	CSR_DBC_LEN_2		(0x2ul << CSR_DBC_SIZE_SHIFT)
#define	CSR_DBC_LEN_1		(0x3ul << CSR_DBC_SIZE_SHIFT)
#define	CSR_DBC_PLV0		(1ul << 1)
#define	CSR_DBC_PLV3		(1ul << 4)
/* MWPC low bits = number of data watchpoint slots. */
#define	CSR_MWPC_SLOTS		0x3f

#define	LOONGARCH_CSR_FWPC	0x380	/* instruction breakpoint config */
#define	LOONGARCH_CSR_FWPS	0x381	/* instruction breakpoint status */

#define	LOONGARCH_CSR_IB0ADDR	0x390	/* inst breakpoint 0 address */
#define	LOONGARCH_CSR_IB0MASK	0x391	/* inst breakpoint 0 mask */
#define	LOONGARCH_CSR_IB0CTRL	0x392	/* inst breakpoint 0 control */
#define	LOONGARCH_CSR_IB0ASID	0x393	/* inst breakpoint 0 asid */

#define	LOONGARCH_CSR_IB1ADDR	0x398	/* inst breakpoint 1 address */
#define	LOONGARCH_CSR_IB1MASK	0x399	/* inst breakpoint 1 mask */
#define	LOONGARCH_CSR_IB1CTRL	0x39a	/* inst breakpoint 1 control */
#define	LOONGARCH_CSR_IB1ASID	0x39b	/* inst breakpoint 1 asid */

#define	LOONGARCH_CSR_IB2ADDR	0x3a0	/* inst breakpoint 2 address */
#define	LOONGARCH_CSR_IB2MASK	0x3a1	/* inst breakpoint 2 mask */
#define	LOONGARCH_CSR_IB2CTRL	0x3a2	/* inst breakpoint 2 control */
#define	LOONGARCH_CSR_IB2ASID	0x3a3	/* inst breakpoint 2 asid */

#define	LOONGARCH_CSR_IB3ADDR	0x3a8	/* inst breakpoint 3 address */
#define	LOONGARCH_CSR_IB3MASK	0x3a9	/* breakpoint 3 mask */
#define	LOONGARCH_CSR_IB3CTRL	0x3aa	/* inst breakpoint 3 control */
#define	LOONGARCH_CSR_IB3ASID	0x3ab	/* inst breakpoint 3 asid */

#define	LOONGARCH_CSR_IB4ADDR	0x3b0	/* inst breakpoint 4 address */
#define	LOONGARCH_CSR_IB4MASK	0x3b1	/* inst breakpoint 4 mask */
#define	LOONGARCH_CSR_IB4CTRL	0x3b2	/* inst breakpoint 4 control */
#define	LOONGARCH_CSR_IB4ASID	0x3b3	/* inst breakpoint 4 asid */

#define	LOONGARCH_CSR_IB5ADDR	0x3b8	/* inst breakpoint 5 address */
#define	LOONGARCH_CSR_IB5MASK	0x3b9	/* inst breakpoint 5 mask */
#define	LOONGARCH_CSR_IB5CTRL	0x3ba	/* inst breakpoint 5 control */
#define	LOONGARCH_CSR_IB5ASID	0x3bb	/* inst breakpoint 5 asid */

#define	LOONGARCH_CSR_IB6ADDR	0x3c0	/* inst breakpoint 6 address */
#define	LOONGARCH_CSR_IB6MASK	0x3c1	/* inst breakpoint 6 mask */
#define	LOONGARCH_CSR_IB6CTRL	0x3c2	/* inst breakpoint 6 control */
#define	LOONGARCH_CSR_IB6ASID	0x3c3	/* inst breakpoint 6 asid */

#define	LOONGARCH_CSR_IB7ADDR	0x3c8	/* inst breakpoint 7 address */
#define	LOONGARCH_CSR_IB7MASK	0x3c9	/* inst breakpoint 7 mask */
#define	LOONGARCH_CSR_IB7CTRL	0x3ca	/* inst breakpoint 7 control */
#define	LOONGARCH_CSR_IB7ASID	0x3cb	/* inst breakpoint 7 asid */

#define	CSR_FWPC_SKIP_SHIFT	16
#define	CSR_FWPC_SKIP		(_ULCAST_(1) << CSR_FWPC_SKIP_SHIFT)

/*
 * CSR_ECFG IM
 */
#define	ECFGF(hwirq)		(_ULCAST_(1) << hwirq)

#define	LOONGARCH_IOCSR_FEATURES 0x8
#define	IOCSRF_EXTIOI		BIT_ULL(3)
#define	IOCSRF_CSRIPI		BIT_ULL(4)
#define	IOCSRF_FREQSCALE	BIT_ULL(6)
#define	IOCSRF_EIODECODE	BIT_ULL(9)
#define	IOCSRF_FLATMODE		BIT_ULL(10)
#define	IOCSRF_VM		BIT_ULL(11)

#define	LOONGARCH_IOCSR_MISC_FUNC 0x420
#define	IOCSR_MISC_FUNC_EXT_IOI_EN BIT_ULL(48)

/* PerCore CSR, only accessible by local cores */
#define	LOONGARCH_IOCSR_IPI_STATUS 0x1000
#define	LOONGARCH_IOCSR_IPI_EN	0x1004
#define	LOONGARCH_IOCSR_IPI_CLEAR 0x100c

#define	LOONGARCH_IOCSR_IPI_SEND 0x1040
#define	IOCSR_IPI_SEND_CPU_SHIFT 16
#define	IOCSR_IPI_SEND_BLOCKING	BIT_U(31)

#define	LOONGARCH_IOCSR_MBUF_SEND 0x1048
#define	IOCSR_MBUF_SEND_BLOCKING BIT_ULL(31)
#define	IOCSR_MBUF_SEND_BOX_SHIFT 2
#define	IOCSR_MBUF_SEND_BOX_LO(box) (box << 1)
#define	IOCSR_MBUF_SEND_BOX_HI(box) ((box << 1) + 1)
#define	IOCSR_MBUF_SEND_CPU_SHIFT 16
#define	IOCSR_MBUF_SEND_BUF_SHIFT 32
#define	IOCSR_MBUF_SEND_H32_MASK 0xFFFFFFFF00000000ULL

/* Register offset and bit definition for CSR access */

#define	LOONGARCH_IOCSR_EXTIOI_NODEMAP_BASE 0x14a0
#define	LOONGARCH_IOCSR_EXTIOI_IPMAP_BASE 0x14c0
#define	LOONGARCH_IOCSR_EXTIOI_EN_BASE 0x1600
#define	LOONGARCH_IOCSR_EXTIOI_BOUNCE_BASE 0x1680
#define	LOONGARCH_IOCSR_EXTIOI_ISR_BASE 0x1800
#define	LOONGARCH_IOCSR_EXTIOI_ROUTE_BASE 0x1c00

#define	read_csr_prcfg1()	csr_read64(LOONGARCH_CSR_PRCFG1)
#define	read_csr_prcfg3()	csr_read64(LOONGARCH_CSR_PRCFG3)


/* ExStatus.ExcCode */
#define	EXCCODE_RSV		0	/* Reserved */
#define	EXCCODE_TLBL		1	/* TLB miss on a load */
#define	EXCCODE_TLBS		2	/* TLB miss on a store */
#define	EXCCODE_TLBI		3	/* TLB miss on a ifetch */
#define	EXCCODE_TLBM		4	/* TLB modified fault */
#define	EXCCODE_TLBNR		5	/* TLB Read-Inhibit exception */
#define	EXCCODE_TLBNX		6	/* TLB Execution-Inhibit exception */
#define	EXCCODE_TLBPE		7	/* TLB Privilege Error */
#define	EXCCODE_ADE		8	/* Address Error */
#define	EXCCODE_ALE		9	/* Unalign Access */
#define	EXCCODE_BCE		10	/* Bounds Check Error */
#define	EXCCODE_SYS		11	/* System call */
#define	EXCCODE_BP		12	/* Breakpoint */
#define	EXCCODE_INE		13	/* Inst. Not Exist */
#define	EXCCODE_IPE		14	/* Inst. Privileged Error */
#define	EXCCODE_FPDIS		15	/* FPU Disabled */
#define	EXCCODE_LSXDIS		16	/* LSX Disabled */
#define	EXCCODE_LASXDIS		17	/* LASX Disabled */
#define	EXCCODE_FPE		18	/* Floating Point Exception */
#define	EXCCODE_WATCH		19	/* WatchPoint Exception */
#define	EXCCODE_BTDIS		20	/* Binary Trans. Disabled */

/*
 * X the exception cause indicator
 * E the exception enable
 * S the sticky/flag bit
 */
#define	FPU_CSR_ALL_X		0x1f000000
#define	FPU_CSR_INV_X		0x10000000
#define	FPU_CSR_DIV_X		0x08000000
#define	FPU_CSR_OVF_X		0x04000000
#define	FPU_CSR_UDF_X		0x02000000
#define	FPU_CSR_INE_X		0x01000000

#define	FPU_CSR_ALL_E		0x0000001f

#define	GRLEN			64
#define	GRLEN_BYTES		(GRLEN / 8)

#endif /* !_MACHINE_LOONGARCH_H_ */
