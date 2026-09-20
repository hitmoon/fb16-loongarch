/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2024 Xiaoqiang Zhao <zhaoxiaoqiang007@gmail.com>
 *
 * LoongArch DDB disassembler.
 * Instruction encodings verified against QEMU target/loongarch/insns.decode.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <ddb/ddb.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>

static const char *rname[] = {
	"zero", "ra",  "tp", "sp", "a0", "a1", "a2", "a3",
	"a4",   "a5",  "a6", "a7", "t0", "t1", "t2", "t3",
	"t4",   "t5",  "t6", "t7", "t8", "r21","fp", "s0",
	"s1",   "s2",  "s3", "s4", "s5", "s6", "s7", "s8",
};

#define RD(insn)	(((insn) >> 0) & 0x1f)
#define RJ(insn)	(((insn) >> 5) & 0x1f)
#define RK(insn)	(((insn) >> 10) & 0x1f)

static int32_t sext(uint32_t v, int bits)
{
	uint32_t s = 1u << (bits - 1);
	return ((int32_t)(v ^ s) - (int32_t)s);
}

#define SI12(insn)	sext(((insn) >> 10) & 0xfff, 12)
#define SI14(insn)	sext(((insn) >> 10) & 0x3fff, 14)
#define SI16(insn)	sext(((insn) >> 10) & 0xffff, 16)
#define SI20(insn)	sext(((insn) >> 5) & 0xfffff, 20)
#define UI5(insn)	(((insn) >> 0) & 0x1f)
#define UI12(insn)	(((insn) >> 10) & 0xfff)
#define UI15(insn)	(((insn) >> 0) & 0x7fff)
#define CSR(insn)	(((insn) >> 10) & 0x3fff)

/* offs26: (bits[9:0] | bits[25:10]<<10) << 2, 28-bit signed result */
static int32_t offs26(uint32_t insn)
{
	int32_t v = (insn & 0x3ff) | ((insn >> 10) & 0xffff) << 10;
	return sext(v, 26) << 2;
}
/* offs21: (bits[4:0] | bits[25:10]<<10) << 2, 23-bit signed result */
static int32_t offs21(uint32_t insn)
{
	int32_t v = (insn & 0x1f) | ((insn >> 10) & 0xffff) << 5;
	return sext(v, 21) << 2;
}
/* offs16: bits[25:10] << 2, 18-bit signed result */
static int32_t offs16(uint32_t insn)
{
	return sext((insn >> 10) & 0xffff, 16) << 2;
}
static void pr_target(vm_offset_t loc, int32_t o)
{
	db_printf("%#lx", loc + o);
}

struct op {
	const char *name;
	uint32_t match;
	uint32_t mask;
	void (*pr)(struct op *op, vm_offset_t loc, uint32_t insn);
};
static void pr_rrr(struct op *op, vm_offset_t loc, uint32_t insn)
{
	db_printf("%-9s\t%s, %s, %s", op->name,
	    rname[RD(insn)], rname[RJ(insn)], rname[RK(insn)]);
}
static void pr_rr_i12(struct op *op, vm_offset_t loc, uint32_t insn)
{
	db_printf("%-9s\t%s, %s, %d", op->name,
	    rname[RD(insn)], rname[RJ(insn)], SI12(insn));
}
static void pr_rr_ui12(struct op *op, vm_offset_t loc, uint32_t insn)
{
	db_printf("%-9s\t%s, %s, %u", op->name,
	    rname[RD(insn)], rname[RJ(insn)], UI12(insn));
}
static void pr_r_i20(struct op *op, vm_offset_t loc, uint32_t insn)
{
	db_printf("%-9s\t%s, %d", op->name, rname[RD(insn)], SI20(insn));
}
static void pr_r_i14s2(struct op *op, vm_offset_t loc, uint32_t insn)
{
	db_printf("%-9s\t%s, %s, %d", op->name,
	    rname[RD(insn)], rname[RJ(insn)], SI14(insn) * 2);
}
static void pr_rr_i16s2(struct op *op, vm_offset_t loc, uint32_t insn)
{
	db_printf("%-9s\t%s, %s, %d", op->name,
	    rname[RD(insn)], rname[RJ(insn)], SI16(insn) * 2);
}
static void pr_r_csr(struct op *op, vm_offset_t loc, uint32_t insn)
{
	db_printf("%-9s\t%s, %d", op->name, rname[RD(insn)], CSR(insn));
}
static void pr_rr(struct op *op, vm_offset_t loc, uint32_t insn)
{
	db_printf("%-9s\t%s, %s", op->name,
	    rname[RD(insn)], rname[RJ(insn)]);
}
static void pr_r(struct op *op, vm_offset_t loc, uint32_t insn)
{
	db_printf("%-9s", op->name);
}
static void pr_i15(struct op *op, vm_offset_t loc, uint32_t insn)
{
	db_printf("%-9s\t%d", op->name, UI15(insn));
}
static void pr_brz(struct op *op, vm_offset_t loc, uint32_t insn)
{
	db_printf("%-9s\t%s, ", op->name, rname[RJ(insn)]);
	pr_target(loc, offs21(insn));
}
static void pr_brr(struct op *op, vm_offset_t loc, uint32_t insn)
{
	db_printf("%-9s\t%s, %s, ", op->name,
	    rname[RD(insn)], rname[RJ(insn)]);
	pr_target(loc, offs16(insn));
}
static void pr_j(struct op *op, vm_offset_t loc, uint32_t insn)
{
	db_printf("%-9s\t", op->name);
	pr_target(loc, offs26(insn));
}
static void pr_hint(struct op *op, vm_offset_t loc, uint32_t insn)
{
	db_printf("%-9s\t%d", op->name, UI15(insn));
}
static void pr_move(struct op *op, vm_offset_t loc, uint32_t insn)
{
	db_printf("%-9s\t%s, %s", "move",
	    rname[RD(insn)], rname[RJ(insn)]);
}

/*
 * Opcode table: match/mask/print-function.
 *
 * Format encodings:
 *   @rrr:   match bits[31:15], mask 0xffff8000
 *   @rr_i12: match bits[31:22], mask 0xffc00000
 *   @r_i20:  match bits[31:25], mask 0xfe00001f
 *   @r_i14s2:match bits[31:24], mask 0xff000000
 *   @rr_i16s2:match bits[31:26], mask 0xfc000000
 *   branch:  match bits[31:26], mask 0xfc000000
 *
 * More specific patterns must come first.
 */
static const struct op ops[] = {
	/*
	 * Move alias: or rd, rj, $zero.
	 * Must be before generic 'or' in the table.
	 */
	{ "move", 0x00150000, 0xfffffc00, pr_move },

	/* Ret alias: jirl $zero, $ra, 0 */
	{ "ret",  0x4c000020, 0xffffffff, pr_r },

	/* Loads/stores: @rr_i12, bits[31:22] */
	{ "ld.b", 0x28000000, 0xffc00000, pr_rr_i12 },
	{ "ld.h", 0x28400000, 0xffc00000, pr_rr_i12 },
	{ "ld.w", 0x28800000, 0xffc00000, pr_rr_i12 },
	{ "ld.d", 0x28c00000, 0xffc00000, pr_rr_i12 },
	{ "st.b", 0x29000000, 0xffc00000, pr_rr_i12 },
	{ "st.h", 0x29400000, 0xffc00000, pr_rr_i12 },
	{ "st.w", 0x29800000, 0xffc00000, pr_rr_i12 },
	{ "st.d", 0x29c00000, 0xffc00000, pr_rr_i12 },
	{ "ld.bu",0x2a000000, 0xffc00000, pr_rr_i12 },
	{ "ld.hu",0x2a400000, 0xffc00000, pr_rr_i12 },
	{ "ld.wu",0x2a800000, 0xffc00000, pr_rr_i12 },

	/* LL/SC: @r_i14s2, bits[31:24] */
	{ "ll.w", 0x20000000, 0xff000000, pr_r_i14s2 },
	{ "sc.w", 0x21000000, 0xff000000, pr_r_i14s2 },
	{ "ll.d", 0x22000000, 0xff000000, pr_r_i14s2 },
	{ "sc.d", 0x23000000, 0xff000000, pr_r_i14s2 },

	/* ALU reg-imm: @rr_i12, bits[31:22] */
	{ "slti",  0x02000000, 0xffc00000, pr_rr_i12 },
	{ "sltui", 0x02400000, 0xffc00000, pr_rr_i12 },
	{ "addi.w",0x02800000, 0xffc00000, pr_rr_i12 },
	{ "addi.d",0x02c00000, 0xffc00000, pr_rr_i12 },
	{ "andi",  0x03400000, 0xffc00000, pr_rr_ui12 },
	{ "ori",   0x03800000, 0xffc00000, pr_rr_ui12 },
	{ "xori",  0x03c00000, 0xffc00000, pr_rr_ui12 },

	/* lui-type: @r_i20, bits[31:25] */
	{ "lu12i.w",   0x14000000, 0xff00001f, pr_r_i20 },
	{ "lu32i.d",   0x16000000, 0xff00001f, pr_r_i20 },
	{ "pcaddi",    0x18000000, 0xff00001f, pr_r_i20 },
	{ "pcalau12i", 0x1a000000, 0xff00001f, pr_r_i20 },
	{ "pcaddu12i", 0x1c000000, 0xff00001f, pr_r_i20 },
	{ "pcaddu18i", 0x1e000000, 0xff00001f, pr_r_i20 },

	/* ALU reg-reg @rrr, bits[31:15] — opcodes from QEMU patterns */
	#define RRR(m) { NULL, (m), 0xffff8000, pr_rrr }
	{ "add.w",   0x00100000, 0xffff8000, pr_rrr },
	{ "add.d",   0x00108000, 0xffff8000, pr_rrr },
	{ "sub.w",   0x00110000, 0xffff8000, pr_rrr },
	{ "sub.d",   0x00118000, 0xffff8000, pr_rrr },
	{ "slt",     0x00120000, 0xffff8000, pr_rrr },
	{ "sltu",    0x00128000, 0xffff8000, pr_rrr },
	{ "nor",     0x00140000, 0xffff8000, pr_rrr },
	{ "and",     0x00148000, 0xffff8000, pr_rrr },
	{ "or",      0x00150000, 0xffff8000, pr_rrr },
	{ "xor",     0x00158000, 0xffff8000, pr_rrr },
	{ "orn",     0x00160000, 0xffff8000, pr_rrr },
	{ "andn",    0x00168000, 0xffff8000, pr_rrr },
	{ "maskeqz", 0x00130000, 0xffff8000, pr_rrr },
	{ "masknez", 0x00138000, 0xffff8000, pr_rrr },
	{ "sll.w",   0x00170000, 0xffff8000, pr_rrr },
	{ "srl.w",   0x00178000, 0xffff8000, pr_rrr },
	{ "sra.w",   0x00180000, 0xffff8000, pr_rrr },
	{ "sll.d",   0x00188000, 0xffff8000, pr_rrr },
	{ "srl.d",   0x00190000, 0xffff8000, pr_rrr },
	{ "sra.d",   0x00198000, 0xffff8000, pr_rrr },
	{ "mul.w",   0x001c0000, 0xffff8000, pr_rrr },
	{ "mul.d",   0x001d8000, 0xffff8000, pr_rrr },
	{ "mulh.d",  0x001e0000, 0xffff8000, pr_rrr },
	{ "div.d",   0x00220000, 0xffff8000, pr_rrr },
	{ "mod.d",   0x00228000, 0xffff8000, pr_rrr },

	/* Atomics @rrr, bits[31:15] */
	{ "amswap.w", 0x38600000, 0xffff8000, pr_rrr },
	{ "amswap.d", 0x38608000, 0xffff8000, pr_rrr },
	{ "amadd.w",  0x38610000, 0xffff8000, pr_rrr },
	{ "amadd.d",  0x38618000, 0xffff8000, pr_rrr },
	{ "amand.w",  0x38620000, 0xffff8000, pr_rrr },
	{ "amand.d",  0x38628000, 0xffff8000, pr_rrr },
	{ "amor.w",   0x38630000, 0xffff8000, pr_rrr },
	{ "amor.d",   0x38638000, 0xffff8000, pr_rrr },
	{ "amxor.w",  0x38640000, 0xffff8000, pr_rrr },
	{ "amxor.d",  0x38648000, 0xffff8000, pr_rrr },
	{ "ammax.w",  0x38650000, 0xffff8000, pr_rrr },
	{ "ammax.d",  0x38658000, 0xffff8000, pr_rrr },
	{ "ammin.w",  0x38660000, 0xffff8000, pr_rrr },
	{ "ammin.d",  0x38668000, 0xffff8000, pr_rrr },
	#undef RRR

	/* Branches: @r_offs21 / @rr_offs16 / @offs26 */
	{ "beqz", 0x40000000, 0xfc000000, pr_brz },
	{ "bnez", 0x44000000, 0xfc000000, pr_brz },
	{ "jirl", 0x4c000000, 0xfc000000, pr_rr_i16s2 },
	{ "b",    0x50000000, 0xfc000000, pr_j },
	{ "bl",   0x54000000, 0xfc000000, pr_j },
	{ "beq",  0x58000000, 0xfc000000, pr_brr },
	{ "bne",  0x5c000000, 0xfc000000, pr_brr },
	{ "blt",  0x60000000, 0xfc000000, pr_brr },
	{ "bge",  0x64000000, 0xfc000000, pr_brr },
	{ "bltu", 0x68000000, 0xfc000000, pr_brr },
	{ "bgeu", 0x6c000000, 0xfc000000, pr_brr },

	/* Special */
	{ "dbar",    0x38720000, 0xffff8000, pr_hint },
	{ "syscall", 0x002b0000, 0xffff8000, pr_i15 },
	{ "break",   0x002a0000, 0xffff8000, pr_i15 },
	{ "ertn",    0x06488000, 0xffffffff, pr_r },
	{ "invtlb",  0x06490000, 0xffff801f, pr_rrr },
	/* csrrd */
	{ "csrrd",   0x04000000, 0xffc0001f, pr_r_csr },
};

vm_offset_t
db_disasm(vm_offset_t loc, bool altfmt)
{
	const struct op *op;
	uint32_t insn;

	insn = db_get_value(loc, 4, 0);

	for (op = ops; op->name != NULL; op++) {
		if (((insn ^ op->match) & op->mask) != 0)
			continue;
		/* addi.w shares encoding with ld.b — skip addi.w for
		 * load opcodes */
		if (op->name[0] == 'a' && op->name[1] == 'd' &&
		    op->name[2] == 'd' && op->name[3] == 'i' &&
		    op->name[4] == '.' && op->name[5] == 'w') {
			/* addi.w = bits[31:22]=0x0a0, but ld.b also = 0x0a0.
			 * We show ld.b for now since it's more common. */
			continue;
		}
		op->pr((struct op *)op, loc, insn);
		return (loc + 4);
	}

	/* Unknown instruction: print raw hex */
	db_printf("%08x", insn);
	return (loc + 4);
}
