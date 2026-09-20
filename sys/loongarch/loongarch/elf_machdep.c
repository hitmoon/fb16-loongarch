/*-
 * Copyright 1996-1998 John D. Polstra.
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
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

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/linker.h>
#include <sys/proc.h>
#include <sys/reg.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/imgact_elf.h>
#include <sys/syscall.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_param.h>

#include <machine/elf.h>
#include <machine/frame.h>
#include <machine/loongarchreg.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/reg.h>

u_long elf_hwcap;

static struct sysentvec elf64_freebsd_sysvec = {
	.sv_size	= SYS_MAXSYSCALL,
	.sv_table	= sysent,
	.sv_fixup	= __elfN(freebsd_fixup),
	.sv_sendsig	= sendsig,
	.sv_sigcode	= sigcode,
	.sv_szsigcode	= &szsigcode,
	.sv_name	= "FreeBSD ELF64",
	.sv_coredump	= __elfN(coredump),
	.sv_elf_core_osabi = ELFOSABI_FREEBSD,
	.sv_elf_core_abi_vendor = FREEBSD_ABI_VENDOR,
	.sv_elf_core_prepare_notes = __elfN(prepare_notes),
	.sv_minsigstksz	= MINSIGSTKSZ,
	.sv_minuser	= VM_MIN_ADDRESS,
	.sv_maxuser	= 0,	/* Filled in during boot. */
	.sv_usrstack	= 0,	/* Filled in during boot. */
	.sv_psstrings	= 0,	/* Filled in during boot. */
	.sv_psstringssz	= sizeof(struct ps_strings),
	.sv_stackprot	= VM_PROT_READ | VM_PROT_WRITE,
	.sv_copyout_auxargs = __elfN(freebsd_copyout_auxargs),
	.sv_copyout_strings	= exec_copyout_strings,
	.sv_setregs	= exec_setregs,
	.sv_fixlimit	= NULL,
	.sv_maxssiz	= NULL,
	.sv_flags	= SV_ABI_FREEBSD | SV_LP64 | SV_SHP | SV_TIMEKEEP |
		    SV_ASLR | SV_RNG_SEED_VER,
	.sv_set_syscall_retval = cpu_set_syscall_retval,
	.sv_fetch_syscall_args = cpu_fetch_syscall_args,
	.sv_syscallnames = syscallnames,
	.sv_shared_page_base = 0,	/* Filled in during boot. */
	.sv_shared_page_len = PAGE_SIZE,
	.sv_schedtail	= NULL,
	.sv_thread_detach = NULL,
	.sv_trap	= NULL,
	.sv_hwcap	= &elf_hwcap,
	.sv_onexec_old	= exec_onexec_old,
	.sv_onexit	= exit_onexit,
	.sv_regset_begin = SET_BEGIN(__elfN(regset)),
	.sv_regset_end  = SET_LIMIT(__elfN(regset)),
};
INIT_SYSENTVEC(elf64_sysvec, &elf64_freebsd_sysvec);

static Elf64_Brandinfo freebsd_brand_info = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_LOONGARCH,
	.compat_3_brand	= "FreeBSD",
	.interp_path	= "/libexec/ld-elf.so.1",
	.sysvec		= &elf64_freebsd_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &elf64_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};
SYSINIT(elf64, SI_SUB_EXEC, SI_ORDER_FIRST,
    (sysinit_cfunc_t)elf64_insert_brand_entry, &freebsd_brand_info);

static void
elf64_register_sysvec(void *arg)
{
	struct sysentvec *sv;

	sv = arg;
	sv->sv_maxuser = VM_MAX_USER_ADDRESS;
	sv->sv_usrstack = USRSTACK;
	sv->sv_psstrings = PS_STRINGS;
	sv->sv_shared_page_base = SHAREDPAGE;
}
SYSINIT(elf64_register_sysvec, SI_SUB_VM, SI_ORDER_ANY, elf64_register_sysvec,
    &elf64_freebsd_sysvec);

static bool debug_kld;
SYSCTL_BOOL(_debug, OID_AUTO, kld_reloc, CTLFLAG_RW, &debug_kld, 0,
    "Activate debug prints in elf_reloc_internal()");

void
elf64_dump_thread(struct thread *td, void *dst, size_t *off)
{

}

/*
 * SOP (Stack-Oriented Programming) relocation stack.
 *
 * LoongArch SOP relocations use a stack to compute address expressions.
 * PUSH operations push values onto the stack, arithmetic operations pop
 * operands and push results, and POP operations pop the final value and
 * insert it into the instruction's immediate field.
 *
 * The stack persists across consecutive calls to elf_reloc_internal()
 * because FreeBSD processes relocations one at a time.
 */
#define	RELA_STACK_DEPTH	16

static long rela_stack[RELA_STACK_DEPTH];
static size_t rela_stack_top;
static linker_file_t sop_last_lf;	/* reset the SOP stack at module
					 * boundary */

static int
rela_stack_push(long value)
{

	if (rela_stack_top >= RELA_STACK_DEPTH) {
		printf("%s: SOP stack overflow\n", __func__);
		return (-1);
	}
	rela_stack[rela_stack_top++] = value;
	return (0);
}

static int
rela_stack_pop(long *value)
{

	if (rela_stack_top == 0) {
		printf("%s: SOP stack underflow\n", __func__);
		return (-1);
	}
	*value = rela_stack[--rela_stack_top];
	return (0);
}

/*
 * LoongArch instruction format helpers.
 *
 * reg1i20 (pcalau12i, pcaddu12i, lu12i.w):
 *   bits [31:25] = opcode, [24:5] = si20, [4:0] = rd
 *
 * reg2i12 (addi.w/d, ld.w/d, st.w/d, andi, ori):
 *   bits [31:22] = opcode, [21:10] = si12/ui12, [9:5] = rj, [4:0] = rd
 *
 * reg2i16 (beq, bne, blt, bge):
 *   bits [31:26] = opcode, [25:10] = offs16, [9:5] = rj, [4:0] = rd
 *
 * reg1i21 (beqz, bnez):
 *   bits [31:26] = opcode, [25:10] = offs_l[15:0], [9:5] = rj,
 *   [4:0] = offs_h[20:16]
 *
 * reg0i26 (b, bl):
 *   bits [31:26] = opcode, [25:10] = offs_l[15:0],
 *   [9:0] = offs_h[25:16]
 */

/* Insert 20-bit immediate into reg1i20 format (bits [24:5]) */
static uint32_t
insn_set_si20(uint32_t insn, int32_t imm)
{

	insn &= ~(0xfffffU << 5);
	insn |= ((uint32_t)(imm & 0xfffff) << 5);
	return (insn);
}

/* Insert 12-bit immediate into reg2i12 format (bits [21:10]) */
static uint32_t
insn_set_imm12(uint32_t insn, int32_t imm)
{

	insn &= ~(0xfffU << 10);
	insn |= ((uint32_t)(imm & 0xfff) << 10);
	return (insn);
}

/*
 * Rewrite a reg2i12 instruction into addi.d, preserving rj and rd
 * (bits [9:0]).  addi.d's opcode occupies bits [31:22] (0x02c00000); the
 * immediate field is left for the caller to fill via insn_set_imm12().
 *
 * Kernel modules have no GOT, so a GOT pointer load emitted as
 * "pcalau12i + ld.d" must be collapsed into a direct address computation
 * "pcalau12i + addi.d" -- the ld.d is rewritten to addi.d here.  This is
 * the same transform a relaxing linker applies to a R_LARCH_GOT_PC pair.
 */
static uint32_t
insn_to_addi_d(uint32_t insn)
{

	return ((insn & 0x3ffU) | 0x02c00000U);
}

/* Insert 16-bit immediate into reg2i16 format (bits [25:10]) */
static uint32_t
insn_set_si16(uint32_t insn, int32_t imm)
{

	insn &= ~(0xffffU << 10);
	insn |= ((uint32_t)(imm & 0xffff) << 10);
	return (insn);
}

/*
 * Insert 26-bit offset into reg0i26 format (b, bl).
 * The offset is in bytes; stored in the instruction shifted right by 2.
 */
static uint32_t
insn_set_offs26(uint32_t insn, int64_t offs)
{
	uint32_t imm_l, imm_h;

	imm_l = ((uint32_t)offs >> 2) & 0xffff;
	imm_h = ((uint32_t)(offs >> 2) >> 16) & 0x3ff;
	insn &= ~((0xffffU << 10) | 0x3ffU);
	insn |= (imm_l << 10) | imm_h;
	return (insn);
}

/*
 * Insert 21-bit offset into reg1i21 format (beqz, bnez).
 * The offset is in bytes; stored in the instruction shifted right by 2.
 */
static uint32_t
insn_set_offs21(uint32_t insn, int64_t offs)
{
	uint32_t imm_l, imm_h;

	imm_l = ((uint32_t)offs >> 2) & 0xffff;
	imm_h = ((uint32_t)(offs >> 2) >> 16) & 0x1f;
	insn &= ~((0xffffU << 10) | 0x1fU);
	insn |= (imm_l << 10) | imm_h;
	return (insn);
}

/*
 * Insert 16-bit offset into reg2i16 format (conditional branches) for
 * B16 relocs.  The offset is in bytes; stored shifted right by 2,
 * into bits [25:10].
 */
static uint32_t
insn_set_offs16(uint32_t insn, int64_t offs)
{

	return (insn_set_si16(insn, offs >> 2));
}

struct type2str_ent {
	int type;
	const char *str;
};

static const struct type2str_ent t2s[] = {
	{ R_LARCH_NONE,			"R_LARCH_NONE"			},
	{ R_LARCH_32,			"R_LARCH_32"			},
	{ R_LARCH_64,			"R_LARCH_64"			},
	{ R_LARCH_RELATIVE,		"R_LARCH_RELATIVE"		},
	{ R_LARCH_COPY,			"R_LARCH_COPY"			},
	{ R_LARCH_JUMP_SLOT,		"R_LARCH_JUMP_SLOT"		},
	{ R_LARCH_IRELATIVE,		"R_LARCH_IRELATIVE"		},
	{ R_LARCH_MARK_LA,		"R_LARCH_MARK_LA"		},
	{ R_LARCH_MARK_PCREL,		"R_LARCH_MARK_PCREL"		},
	{ R_LARCH_SOP_PUSH_PCREL,	"R_LARCH_SOP_PUSH_PCREL"	},
	{ R_LARCH_SOP_PUSH_ABSOLUTE,	"R_LARCH_SOP_PUSH_ABSOLUTE"	},
	{ R_LARCH_SOP_PUSH_DUP,	"R_LARCH_SOP_PUSH_DUP"		},
	{ R_LARCH_SOP_PUSH_PLT_PCREL,	"R_LARCH_SOP_PUSH_PLT_PCREL"	},
	{ R_LARCH_SOP_SUB,		"R_LARCH_SOP_SUB"		},
	{ R_LARCH_SOP_SL,		"R_LARCH_SOP_SL"		},
	{ R_LARCH_SOP_SR,		"R_LARCH_SOP_SR"		},
	{ R_LARCH_SOP_ADD,		"R_LARCH_SOP_ADD"		},
	{ R_LARCH_SOP_AND,		"R_LARCH_SOP_AND"		},
	{ R_LARCH_SOP_IF_ELSE,		"R_LARCH_SOP_IF_ELSE"		},
	{ R_LARCH_SOP_POP_32_S_10_5,	"R_LARCH_SOP_POP_32_S_10_5"	},
	{ R_LARCH_SOP_POP_32_U_10_12,	"R_LARCH_SOP_POP_32_U_10_12"	},
	{ R_LARCH_SOP_POP_32_S_10_12,	"R_LARCH_SOP_POP_32_S_10_12"	},
	{ R_LARCH_SOP_POP_32_S_10_16,	"R_LARCH_SOP_POP_32_S_10_16"	},
	{ R_LARCH_SOP_POP_32_S_10_16_S2,"R_LARCH_SOP_POP_32_S_10_16_S2"	},
	{ R_LARCH_SOP_POP_32_S_5_20,	"R_LARCH_SOP_POP_32_S_5_20"	},
	{ R_LARCH_SOP_POP_32_S_0_5_10_16_S2,
		"R_LARCH_SOP_POP_32_S_0_5_10_16_S2" },
	{ R_LARCH_SOP_POP_32_S_0_10_10_16_S2,
		"R_LARCH_SOP_POP_32_S_0_10_10_16_S2" },
	{ R_LARCH_SOP_POP_32_U,	"R_LARCH_SOP_POP_32_U"		},
	{ R_LARCH_ADD32,		"R_LARCH_ADD32"			},
	{ R_LARCH_ADD64,		"R_LARCH_ADD64"			},
	{ R_LARCH_SUB32,		"R_LARCH_SUB32"			},
	{ R_LARCH_SUB64,		"R_LARCH_SUB64"			},
	{ R_LARCH_B26,			"R_LARCH_B26"			},
	{ R_LARCH_PCALA_HI20,		"R_LARCH_PCALA_HI20"		},
	{ R_LARCH_PCALA_LO12,		"R_LARCH_PCALA_LO12"		},
	{ R_LARCH_PCALA64_LO20,	"R_LARCH_PCALA64_LO20"		},
	{ R_LARCH_PCALA64_HI12,	"R_LARCH_PCALA64_HI12"		},
	{ R_LARCH_GOT_PC_HI20,		"R_LARCH_GOT_PC_HI20"		},
	{ R_LARCH_GOT_PC_LO12,		"R_LARCH_GOT_PC_LO12"		},
	{ R_LARCH_64_PCREL,		"R_LARCH_64_PCREL"		},
	{ R_LARCH_PCADD_HI20,		"R_LARCH_PCADD_HI20"		},
	{ R_LARCH_PCADD_LO12,		"R_LARCH_PCADD_LO12"		},
	{ R_LARCH_GOT_PCADD_HI20,	"R_LARCH_GOT_PCADD_HI20"	},
	{ R_LARCH_GOT_PCADD_LO12,	"R_LARCH_GOT_PCADD_LO12"	},
};

static const char *
reloctype_to_str(int type)
{

	for (int i = 0; i < nitems(t2s); ++i)
		if (type == t2s[i].type)
			return (t2s[i].str);
	return ("*unknown*");
}

bool
elf_is_ifunc_reloc(Elf_Size r_info)
{

	return (ELF_R_TYPE(r_info) == R_LARCH_IRELATIVE);
}

/*
 * Compute the HI20 value for pcalau12i.
 *
 * pcalau12i rd, si20 computes: rd = (PC & ~0xfff) + SignExtend(si20 << 12)
 *
 * The +0x800 adjustment accounts for sign extension of the LO12 immediate:
 * if the lower 12 bits of the target are >= 0x800, sign extension of the
 * 12-bit immediate produces a negative value, so the HI20 page must be
 * one higher to compensate.
 */
static int32_t
pcala_hi20_offset(Elf_Addr val, Elf_Addr location)
{
	int32_t off;

	off = (int32_t)(((val + 0x800) & ~0xfffUL) - (location & ~0xfffUL));
	return (off >> 12);
}

/*
 * Compute the HI20 value for pcaddi/pcaddu12i.
 *
 * pcaddi rd, si20 computes: rd = PC + SignExtend(si20 << 12)
 *
 * Unlike pcalau12i, pcaddi does NOT clear the lower 12 bits of PC.
 */
static int32_t
pcadd_hi20_offset(Elf_Addr val, Elf_Addr location)
{
	int32_t off;

	off = (int32_t)((val + 0x800) - location);
	return (off >> 12);
}

/*
 * Validate a PC-relative branch/call offset: it must be 4-byte aligned and
 * fit in 'bits' signed bits (the immediate field width per the psABI).
 * Without this an out-of-range branch is silently truncated and jumps to a
 * wrong address.  Ranges match lld's checkInt()/checkAlignment().
 */
static int
check_pcrel_range(u_long rtype, int64_t val, int bits)
{
	int64_t lim = (int64_t)1 << (bits - 1);

	if ((val & 3) != 0) {
		printf("%s: reloc %lu offset %ld not 4-byte aligned\n",
		    __func__, rtype, (long)val);
		return (-1);
	}
	if (val < -lim || val >= lim) {
		printf("%s: reloc %lu offset %ld out of %d-bit range\n",
		    __func__, rtype, (long)val, bits);
		return (-1);
	}
	return (0);
}

static int
elf_reloc_internal(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, int local, elf_lookup_fn lookup)
{
	Elf_Size rtype, symidx;
	const Elf_Rela *rela;
	Elf_Addr val, addr;
	Elf64_Addr *where;
	Elf_Addr addend;
	uint32_t before32;
	uint64_t before64;
	uint32_t *insn32p;
	int error;

	/*
	 * The SOP relocation stack is global and must persist across the
	 * per-reloc calls that make up one SOP patch sequence, but a sequence
	 * that aborts mid-way would leak stale entries into the next module's
	 * SOP processing.  Reset the stack when relocation of a different
	 * module begins.  (SOP relocs are legacy psABI v1; modern v2 toolchains
	 * don't emit them, so this is defensive.)
	 */
	if (lf != sop_last_lf) {
		rela_stack_top = 0;
		sop_last_lf = lf;
	}

	switch (type) {
	case ELF_RELOC_RELA:
		rela = (const Elf_Rela *)data;
		where = (Elf_Addr *)(relocbase + rela->r_offset);
		insn32p = (uint32_t *)where;
		addend = rela->r_addend;
		rtype = ELF_R_TYPE(rela->r_info);
		symidx = ELF_R_SYM(rela->r_info);
		break;
	default:
		printf("%s:%d unknown reloc type %d\n",
		    __FUNCTION__, __LINE__, type);
		return (-1);
	}

	switch (rtype) {

	/* ---- Basic data relocations ---- */

	case R_LARCH_NONE:
	case R_LARCH_MARK_LA:
	case R_LARCH_MARK_PCREL:
		/*
		 * Hint relocations paired with a primary relocation (e.g.
		 * R_LARCH_PCALA_*).  The kernel module linker does not perform
		 * linker relaxation, so treat them as no-ops; the paired
		 * relocation is handled on its own entry.
		 */
	case R_LARCH_RELAX:
	case R_LARCH_ALIGN:
		break;

	case R_LARCH_32:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		before32 = *(uint32_t *)where;
		*(uint32_t *)where = (uint32_t)(addr + addend);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *(uint32_t *)where);
		break;

	case R_LARCH_32_PCREL:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		before32 = *(uint32_t *)where;
		*(uint32_t *)where = (uint32_t)(addr + addend -
		    (Elf_Addr)where);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *(uint32_t *)where);
		break;

	case R_LARCH_64:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		before64 = *where;
		*where = addr + addend;
		if (debug_kld)
			printf("%p %c %-24s %016lx -> %016lx\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before64, *where);
		break;

	case R_LARCH_RELATIVE:
		before64 = *where;
		*where = elf_relocaddr(lf, relocbase + addend);
		if (debug_kld)
			printf("%p %c %-24s %016lx -> %016lx\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before64, *where);
		break;

	case R_LARCH_JUMP_SLOT:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		before64 = *where;
		*where = addr;
		if (debug_kld)
			printf("%p %c %-24s %016lx -> %016lx\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before64, *where);
		break;

	case R_LARCH_IRELATIVE:
		addr = relocbase + addend;
		before64 = *where;
		*where = ((Elf_Addr (*)(void))addr)();
		if (debug_kld)
			printf("%p %c %-24s %016lx -> %016lx\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before64, *where);
		break;

	/* ---- SOP stack operations ---- */

	case R_LARCH_SOP_PUSH_PCREL:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend - (Elf_Addr)where;
		return (rela_stack_push(val));

	case R_LARCH_SOP_PUSH_ABSOLUTE:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		return (rela_stack_push(addr + addend));

	case R_LARCH_SOP_PUSH_DUP: {
		long opr;

		error = rela_stack_pop(&opr);
		if (error != 0)
			return (-1);
		error = rela_stack_push(opr);
		if (error != 0)
			return (-1);
		return (rela_stack_push(opr));
	}

	case R_LARCH_SOP_PUSH_PLT_PCREL:
		/* No PLT in kernel modules; resolve directly. */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend - (Elf_Addr)where;
		return (rela_stack_push(val));

	/* SOP arithmetic */
	case R_LARCH_SOP_SUB:
	case R_LARCH_SOP_SL:
	case R_LARCH_SOP_SR:
	case R_LARCH_SOP_ADD:
	case R_LARCH_SOP_AND:
	case R_LARCH_SOP_IF_ELSE: {
		long opr1, opr2, opr3, result;

		if (rtype == R_LARCH_SOP_IF_ELSE) {
			error = rela_stack_pop(&opr3);
			if (error != 0)
				return (-1);
		}
		error = rela_stack_pop(&opr2);
		if (error != 0)
			return (-1);
		error = rela_stack_pop(&opr1);
		if (error != 0)
			return (-1);

		switch (rtype) {
		case R_LARCH_SOP_AND: result = opr1 & opr2; break;
		case R_LARCH_SOP_ADD: result = opr1 + opr2; break;
		case R_LARCH_SOP_SUB: result = opr1 - opr2; break;
		case R_LARCH_SOP_SL:  result = opr1 << opr2; break;
		case R_LARCH_SOP_SR:  result = opr1 >> opr2; break;
		case R_LARCH_SOP_IF_ELSE: result = opr1 ? opr2 : opr3; break;
		default:
			return (-1);
		}
		return (rela_stack_push(result));
	}

	/* SOP pop into instruction immediate fields */
	case R_LARCH_SOP_POP_32_U_10_12:
	case R_LARCH_SOP_POP_32_S_10_12: {
		long opr;

		error = rela_stack_pop(&opr);
		if (error != 0)
			return (-1);
		before32 = *insn32p;
		*insn32p = insn_set_imm12(*insn32p, opr);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;
	}

	case R_LARCH_SOP_POP_32_S_10_16: {
		long opr;

		error = rela_stack_pop(&opr);
		if (error != 0)
			return (-1);
		before32 = *insn32p;
		*insn32p = insn_set_si16(*insn32p, opr);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;
	}

	case R_LARCH_SOP_POP_32_S_10_16_S2: {
		long opr;

		error = rela_stack_pop(&opr);
		if (error != 0)
			return (-1);
		before32 = *insn32p;
		*insn32p = insn_set_si16(*insn32p, opr >> 2);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;
	}

	case R_LARCH_SOP_POP_32_S_5_20: {
		long opr;

		error = rela_stack_pop(&opr);
		if (error != 0)
			return (-1);
		before32 = *insn32p;
		*insn32p = insn_set_si20(*insn32p, opr);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;
	}

	case R_LARCH_SOP_POP_32_S_0_5_10_16_S2: {
		long opr;

		error = rela_stack_pop(&opr);
		if (error != 0)
			return (-1);
		before32 = *insn32p;
		*insn32p = insn_set_offs21(*insn32p, opr);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;
	}

	case R_LARCH_SOP_POP_32_S_0_10_10_16_S2: {
		long opr;

		error = rela_stack_pop(&opr);
		if (error != 0)
			return (-1);
		before32 = *insn32p;
		*insn32p = insn_set_offs26(*insn32p, opr);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;
	}

	case R_LARCH_SOP_POP_32_U: {
		long opr;

		error = rela_stack_pop(&opr);
		if (error != 0)
			return (-1);
		before32 = *insn32p;
		*insn32p = (uint32_t)opr;
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;
	}

	/* ---- Add/Sub data relocations ---- */

	case R_LARCH_ADD32:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		*(int32_t *)where += addr + addend;
		break;

	case R_LARCH_ADD64:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		*(int64_t *)where += addr + addend;
		break;

	case R_LARCH_SUB32:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		*(int32_t *)where -= addr + addend;
		break;

	case R_LARCH_SUB64:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		*(int64_t *)where -= addr + addend;
		break;

	/* ---- Branch relocations ---- */

	case R_LARCH_B26:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend - (Elf_Addr)where;
		if (check_pcrel_range(R_LARCH_B26, (int64_t)val, 28) != 0)
			return (-1);
		before32 = *insn32p;
		*insn32p = insn_set_offs26(*insn32p, val);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;

	case R_LARCH_B16:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend - (Elf_Addr)where;
		if (check_pcrel_range(R_LARCH_B16, (int64_t)val, 18) != 0)
			return (-1);
		before32 = *insn32p;
		*insn32p = insn_set_offs16(*insn32p, val);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;

	case R_LARCH_B21:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend - (Elf_Addr)where;
		if (check_pcrel_range(R_LARCH_B21, (int64_t)val, 23) != 0)
			return (-1);
		before32 = *insn32p;
		*insn32p = insn_set_offs21(*insn32p, val);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;

	case R_LARCH_CALL36: {
		/*
		 * Adjacent pcaddu18i + jirl pair patched by one relocation.
		 * hi20 goes into pcaddu18i (si20 at [24:5]); lo16 (the low 18
		 * bits with the lowest 2 bits zero, so >> 2) goes into jirl
		 * (si16 at [25:10]).  The +0x20000 adjustment accounts for
		 * sign-extension of the jirl immediate.
		 */
		uint32_t hi20, lo16;

		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend - (Elf_Addr)where;
		/* CALL36 reaches +-128 GiB (38-bit signed on val + 0x20000). */
		if ((val & 3) != 0 ||
		    (int64_t)(val + (1LL << 17)) < -(1LL << 37) ||
		    (int64_t)(val + (1LL << 17)) >= (1LL << 37)) {
			printf("%s: R_LARCH_CALL36 offset %ld out of range\n",
			    __func__, (long)(int64_t)val);
			return (-1);
		}
		hi20 = (uint32_t)((val + (1LL << 17)) >> 18) & 0xfffff;
		lo16 = (uint32_t)(val >> 2) & 0xffff;
		before32 = *insn32p;
		*insn32p = insn_set_si20(*insn32p, hi20);
		insn32p[1] = insn_set_si16(insn32p[1], lo16);
		if (debug_kld)
			printf("%p %c %-24s %08x %08x -> %08x %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, insn32p[1], *insn32p, insn32p[1]);
		break;
	}

	/* ---- PC-relative addressing (pcalau12i + addi.d) ---- */

	case R_LARCH_PCALA_HI20:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend;
		before32 = *insn32p;
		*insn32p = insn_set_si20(*insn32p, pcala_hi20_offset(val,
		    (Elf_Addr)where));
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;

	case R_LARCH_PCALA_LO12:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend;
		before32 = *insn32p;
		*insn32p = insn_set_imm12(*insn32p, val);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;

	case R_LARCH_PCALA64_LO20:
		/*
		 * lu32i.d sets bits [51:32] of the absolute address.  The
		 * paired pcalau12i+addi.d produce the low 32 bits and lu52i.d
		 * the top 12, so this and HI12 carry (S+A) directly (matching
		 * lld's extractBits(val, 51, 32) / (val, 63, 52)).  The prior
		 * code derived a page-delta remainder that collapsed to 0/-1.
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend;
		before32 = *insn32p;
		*insn32p = insn_set_si20(*insn32p, (val >> 32) & 0xfffff);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;

	case R_LARCH_PCALA64_HI12:
		/* lu52i.d sets bits [63:52] of the absolute address. */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend;
		before32 = *insn32p;
		*insn32p = insn_set_imm12(*insn32p, (val >> 52) & 0xfff);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;

	/* ---- GOT PC-relative (no GOT in kernel, resolve directly) ---- */

	case R_LARCH_GOT_PC_HI20:
		/* Same as PCALA_HI20: resolve symbol address directly */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend;
		before32 = *insn32p;
		*insn32p = insn_set_si20(*insn32p, pcala_hi20_offset(val,
		    (Elf_Addr)where));
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;

	case R_LARCH_GOT_PC_LO12:
		/*
		 * Resolve a GOT indirection directly.  The compiler emitted
		 * "pcalau12i + ld.d" to load &sym from a GOT slot; the paired
		 * HI20 already aims pcalau12i at sym, and with no GOT the
		 * pointer load must become an address computation (ld.d ->
		 * addi.d) so the pair yields &sym rather than loading sym's
		 * value.  Without this rewrite the next dereference would
		 * follow the loaded value as an address.
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend;
		before32 = *insn32p;
		*insn32p = insn_set_imm12(insn_to_addi_d(*insn32p), val);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;

	/* ---- PCADD relocations (pcaddi/pcaddu12i) ---- */

	case R_LARCH_PCADD_HI20:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend;
		before32 = *insn32p;
		*insn32p = insn_set_si20(*insn32p, pcadd_hi20_offset(val,
		    (Elf_Addr)where));
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;

	case R_LARCH_PCADD_LO12:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend;
		before32 = *insn32p;
		*insn32p = insn_set_imm12(*insn32p, val);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;

	case R_LARCH_GOT_PCADD_HI20:
		/* No GOT in kernel; resolve directly as PCADD_HI20 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend;
		before32 = *insn32p;
		*insn32p = insn_set_si20(*insn32p, pcadd_hi20_offset(val,
		    (Elf_Addr)where));
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;

	case R_LARCH_GOT_PCADD_LO12:
		/*
		 * Same no-GOT relaxation as R_LARCH_GOT_PC_LO12: the
		 * "pcaddu12i + ld.d" pair loaded &sym from a GOT slot, so the
		 * pointer load must become an address computation (ld.d ->
		 * addi.d) for the pair to yield &sym directly.  The paired
		 * PCADD-style HI20 already aims pcaddu12i at sym.
		 */
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		val = addr + addend;
		before32 = *insn32p;
		*insn32p = insn_set_imm12(insn_to_addi_d(*insn32p), val);
		if (debug_kld)
			printf("%p %c %-24s %08x -> %08x\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before32, *insn32p);
		break;

	/* ---- PC-relative data ---- */

	case R_LARCH_64_PCREL:
		error = lookup(lf, symidx, 1, &addr);
		if (error != 0)
			return (-1);
		before64 = *where;
		*where = addr + addend - (Elf_Addr)where;
		if (debug_kld)
			printf("%p %c %-24s %016lx -> %016lx\n", where,
			    (local ? 'l' : 'g'), reloctype_to_str(rtype),
			    before64, *where);
		break;

	default:
		printf("kldload: unexpected relocation type %ld, "
		    "symbol index %ld\n", rtype, symidx);
		return (-1);
	}

	return (0);
}

int
elf_reloc(linker_file_t lf, Elf_Addr relocbase, const void *data, int type,
    elf_lookup_fn lookup)
{

	return (elf_reloc_internal(lf, relocbase, data, type, 0, lookup));
}

int
elf_reloc_local(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, elf_lookup_fn lookup)
{

	return (elf_reloc_internal(lf, relocbase, data, type, 1, lookup));
}

int
elf_cpu_load_file(linker_file_t lf __unused)
{

	return (0);
}

int
elf_cpu_unload_file(linker_file_t lf __unused)
{

	return (0);
}

int
elf_cpu_parse_dynamic(caddr_t loadbase __unused, Elf_Dyn *dynamic __unused)
{

	return (0);
}

/*
 * LSX/LASX register sets: expose vector state additively via
 * PT_GETREGSET/PT_SETREGSET (note numbers NT_LOONGARCH_LSX/LASX) and
 * core-dump notes.  Legacy PT_GETFPREGS / struct fpreg are unchanged.
 * Vector state lives in the trapframe's tf_vregs (32-byte slots); LSX uses
 * the low 16 bytes of each slot, LASX the full 32.
 */
static bool
loongarch_get_lsxregset(struct regset *rs, struct thread *td, void *buf,
    size_t *sizep)
{
	struct trapframe *tf;
	struct lsxreg *lr;
	int i;

	*sizep = sizeof(struct lsxreg);
	if (buf == NULL)
		return ((td->td_pcb->pcb_fpflags & PCB_FP_LSX_STARTED) != 0);
	if ((td->td_pcb->pcb_fpflags & PCB_FP_LSX_STARTED) != 0) {
		tf = td->td_frame;
		lr = buf;
		for (i = 0; i < 32; i++)
			memcpy(&lr->xr_regs[i * 2], &tf->tf_vregs[i][0], 16);
	} else
		memset(buf, 0, sizeof(struct lsxreg));
	return (true);
}

static bool
loongarch_set_lsxregset(struct regset *rs, struct thread *td, void *buf,
    size_t size)
{
	struct trapframe *tf = td->td_frame;
	struct lsxreg *lr = buf;
	int i;

	for (i = 0; i < 32; i++)
		memcpy(&tf->tf_vregs[i][0], &lr->xr_regs[i * 2], 16);
	td->td_pcb->pcb_fpflags |= PCB_FP_STARTED | PCB_FP_LSX_STARTED;
	tf->tf_euen |= CSR_EUEN_FPEN | CSR_EUEN_LSXEN;
	return (true);
}

static struct regset loongarch_regset_lsx = {
	.note = NT_LOONGARCH_LSX,
	.size = sizeof(struct lsxreg),
	.get = loongarch_get_lsxregset,
	.set = loongarch_set_lsxregset,
};
ELF_REGSET(loongarch_regset_lsx);

static bool
loongarch_get_lasxregset(struct regset *rs, struct thread *td, void *buf,
    size_t *sizep)
{
	struct trapframe *tf;
	struct lasxreg *lr;
	int i;

	*sizep = sizeof(struct lasxreg);
	if (buf == NULL)
		return ((td->td_pcb->pcb_fpflags & PCB_FP_LASX_STARTED) != 0);
	if ((td->td_pcb->pcb_fpflags & PCB_FP_LASX_STARTED) != 0) {
		tf = td->td_frame;
		lr = buf;
		for (i = 0; i < 32; i++)
			memcpy(&lr->xr_regs[i * 4], &tf->tf_vregs[i][0], 32);
	} else
		memset(buf, 0, sizeof(struct lasxreg));
	return (true);
}

static bool
loongarch_set_lasxregset(struct regset *rs, struct thread *td, void *buf,
    size_t size)
{
	struct trapframe *tf = td->td_frame;
	struct lasxreg *lr = buf;
	int i;

	for (i = 0; i < 32; i++)
		memcpy(&tf->tf_vregs[i][0], &lr->xr_regs[i * 4], 32);
	td->td_pcb->pcb_fpflags |= PCB_FP_STARTED | PCB_FP_LSX_STARTED |
	    PCB_FP_LASX_STARTED;
	tf->tf_euen |= CSR_EUEN_FPEN | CSR_EUEN_LSXEN | CSR_EUEN_LASXEN;
	return (true);
}

static struct regset loongarch_regset_lasx = {
	.note = NT_LOONGARCH_LASX,
	.size = sizeof(struct lasxreg),
	.get = loongarch_get_lasxregset,
	.set = loongarch_set_lasxregset,
};
ELF_REGSET(loongarch_regset_lasx);
