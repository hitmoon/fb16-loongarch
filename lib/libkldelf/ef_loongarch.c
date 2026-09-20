/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Xiaoqiang Zhao <zhaoxiaoqiang007@gmail.com>
 *
 */

#include <sys/endian.h>

#include <err.h>
#include <errno.h>
#include <gelf.h>

#include "kldelf.h"

/*
 * Apply relocations to the values obtained from the file. `relbase' is the
 * target relocation address of the section, and `dataoff/len' is the region
 * that is to be relocated, and has been copied to *dest
 */
static int
ef_loongarch_reloc(struct elf_file *ef, const void *reldata, Elf_Type reltype,
    GElf_Addr relbase, GElf_Addr dataoff, size_t len, void *dest)
{
	char *where;
	GElf_Addr addr, addend;
	GElf_Size rtype, symidx;
	const GElf_Rela *rela;

	switch (reltype) {
	case ELF_T_RELA:
		rela = (const GElf_Rela *)reldata;
		where = (char *)dest + (relbase + rela->r_offset - dataoff);
		addend = rela->r_addend;
		rtype = GELF_R_TYPE(rela->r_info);
		symidx = GELF_R_SYM(rela->r_info);
		break;
	default:
		return (EINVAL);
	}

	if (where < (char *)dest || where >= (char *)dest + len)
		return (0);

	switch (rtype) {
	case R_LARCH_64:	/* S + A */
		addr = EF_SYMADDR(ef, symidx) + addend;
		le64enc(where, addr);
		break;
	case R_LARCH_RELATIVE:	/* B + A */
		addr = relbase + addend;
		le64enc(where, addr);
		break;
	default:
		warnx("unhandled relocation type %d", (int)rtype);
	}
	return (0);
}

ELF_RELOC(ELFCLASS64, ELFDATA2LSB, EM_LOONGARCH, ef_loongarch_reloc);
