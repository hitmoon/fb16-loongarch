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

#ifndef _MACHINE_MACHDEP_H_
#define	_MACHINE_MACHDEP_H_

struct loongarch_bootparams {
	vm_offset_t	kern_pgd;	/* Kernel L1 base */
	vm_offset_t	kern_phys;	/* Kernel base (physical) addr */
	vm_offset_t	kern_stack;
	vm_offset_t	dtbp_virt;	/* Device tree blob virtual addr */
	vm_offset_t	dtbp_phys; /* Device tree blob physical addr */
	vm_offset_t	modulep;	/* loader(8) metadata */
};

void initloongarch(struct loongarch_bootparams *);
void ged_init(void);

/* Early serial output via the DMW0 UART window; safe before cninit() and
 * across pmap_bootstrap (independent of the page-table devmap). */
void	loongarch_early_puts(const char *);
void	loongarch_early_put_hex(uint64_t);
vm_paddr_t loongarch_efi_acpi_rsdp(void); /* ACPI RSDP from EFI config table */

/* LoongArch ACPI MADT (APIC) walkers -- defined when DEV_ACPI. */
int	loongarch_acpi_madt_cpu_count(void);
void	loongarch_acpi_madt_foreach_cpu(bool (*)(u_int));

/*
 * Look up the first MADT BIO_PIC (PCH-PIC) subtable and return its
 * parameters: the MMIO register base (Address), window size (Size) and
 * the GSI base for this instance.  Returns true on success.
 */
bool	loongarch_acpi_pch_pic_info(vm_paddr_t *addr, u_int *size,
	    u_int *gsi_base);

/*
 * Look up the first MADT MSI_PIC (PCH-MSI) subtable: the doorbell MMIO
 * address and the EIOINTC vector range [start, start+count) used for MSI.
 * Returns true on success.
 */
bool	loongarch_acpi_pch_msi_info(vm_paddr_t *doorbell, u_int *start,
	    u_int *count);

#endif /* _MACHINE_MACHDEP_H_ */
