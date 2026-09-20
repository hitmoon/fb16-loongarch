/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2024 Xiaoqiang Zhao <zhaoxiaoqiang007@gmail.com>
 */

#ifndef	_MACHINE_DB_MACHDEP_H_
#define	_MACHINE_DB_MACHDEP_H_

#include <machine/loongarchreg.h>
#include <machine/frame.h>
#include <machine/trap.h>

#define	T_BREAKPOINT	(EXCCODE_BP)
#define	T_WATCHPOINT	(EXCCODE_WATCH)

#define	HAS_HW_BREAKPOINT
#define	NHBREAKPOINTS	4

typedef vm_offset_t	db_addr_t;
typedef long		db_expr_t;

#define	PC_REGS()	((db_addr_t)kdb_frame->tf_era)

/*
 * LoongArch 'break 0' instruction: 0x002a0000
 * Format: 0000 0000 0010 1010 0000 0000 0000 0000
 *   bits[31:26] = 0b000000
 *   bits[25:21] = 0b00000
 *   bits[20:10] = 0b0101010000
 *   bits[9:5]   = 0b00000
 *   bits[4:0]   = break code (0 for `break 0`)
 */
#define	BKPT_INST	(0x002a0000)
#define	BKPT_SIZE	(INSN_SIZE)		/* Always 4 bytes */
#define	BKPT_SET(inst)	(BKPT_INST)

/*
 * LoongArch has no compressed instructions, skip is always +4.
 */
#define	BKPT_SKIP do {						\
	kdb_frame->tf_era += BKPT_SIZE;				\
} while (0)

#define	db_clear_single_step	kdb_cpu_clear_singlestep
#define	db_set_single_step	kdb_cpu_set_singlestep

#define	IS_BREAKPOINT_TRAP(type, code)	(type == T_BREAKPOINT)
#define	IS_WATCHPOINT_TRAP(type, code)	(type == T_WATCHPOINT)

/*
 * LoongArch instruction recognition for DDB backtrace.
 *
 * ertn (return from exception): 0x06488000
 *   bits[31:26] = 0b000110 (privileged)
 */
#define	inst_trap_return(ins)	((ins) == 0x06488000) /* ertn */

/*
 * ret = jirl $zero, $ra, 0: 0x4c000020
 *   bits[31:26] = 0b010011 (jirl), rd=$zero, rj=$ra, offset=0
 */
#define	inst_return(ins)	((ins) == 0x4c000020)	/* ret */

/*
 * bl  (branch and link):   bits[31:26] = 0b010101
 * jirl (jump and link reg): bits[31:26] = 0b010011
 */
#define	inst_call(ins)		((((ins) >> 26) == 0x15) ||\
				 (((ins) >> 26) == 0x13))	/* bl, jirl */

#define	inst_load(ins) ({					\
	uint32_t tmp_instr = db_get_value(PC_REGS(),		\
	    sizeof(uint32_t), FALSE);				\
	is_load_instr(tmp_instr);				\
})

#define	inst_store(ins) ({					\
	uint32_t tmp_instr = db_get_value(PC_REGS(),		\
	    sizeof(uint32_t), FALSE);				\
	is_store_instr(tmp_instr);				\
})

/*
 * LoongArch load instructions: bits[31:24] == 0x28 (opcode 0b0010_1000xx)
 * Covers LD.B, LD.BU, LD.H, LD.HU, LD.W, LD.WU, LD.D, LDX.{B,BU,H,HU,W,WU,D}
 */
#define	is_load_instr(ins)	(((ins) >> 24) == 0x28)

/*
 * LoongArch store instructions: bits[31:24] == 0x29 (opcode 0b0010_1001xx)
 * Covers ST.B, ST.H, ST.W, ST.D, STX.{B,H,W,D}
 */
#define	is_store_instr(ins)	(((ins) >> 24) == 0x29)

#define	next_instr_address(pc, bd)	((bd) ? (pc) : ((pc) + 4))

#define	DB_ELFSIZE		64

#endif /* !_MACHINE_DB_MACHDEP_H_ */
