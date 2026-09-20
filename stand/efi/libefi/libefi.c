/*-
 * Copyright (c) 2000 Doug Rabson
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

#include <efi.h>
#include <eficonsctl.h>
#include <efilib.h>
#include <stand.h>
#include <Guid/FdtHob.h>
#include <Guid/HobList.h>
#include <Pi/PiBootMode.h>
#include <Pi/PiHob.h>

EFI_HANDLE		IH;
EFI_SYSTEM_TABLE	*ST;
EFI_BOOT_SERVICES	*BS;
EFI_RUNTIME_SERVICES	*RS;

/*
 * Some firmware (e.g. the EDK2 ArmVirt and LoongArch platforms) does not
 * publish the device tree as a FDT_TABLE_GUID configuration table.  Instead
 * a copy of the FDT is reachable through a FDT_HOB_GUID HOB on the HOB list,
 * which the firmware publishes as a configuration table of its own.  Walk
 * the HOB list and return the FDT address if it is found there.
 */
static void *
efi_get_fdt_hob(EFI_HOB_GENERIC_HEADER *hob)
{
	EFI_HOB_GUID_TYPE *guidh;
	EFI_GUID fdt = FDT_HOB_GUID;
	UINT64 fdtbase;

	while (hob->HobType != EFI_HOB_TYPE_END_OF_HOB_LIST) {
		if (hob->HobType == EFI_HOB_TYPE_GUID_EXTENSION) {
			guidh = (EFI_HOB_GUID_TYPE *)hob;
			if (!memcmp(&guidh->Name, &fdt, sizeof(EFI_GUID))) {
				memcpy(&fdtbase, guidh + 1, sizeof(fdtbase));
				return ((void *)(uintptr_t)fdtbase);
			}
		}
		hob = (EFI_HOB_GENERIC_HEADER *)((char *)hob + hob->HobLength);
	}

	return (NULL);
}

void *
efi_get_table(EFI_GUID *tbl)
{
	EFI_HOB_GENERIC_HEADER *hoblist;
	EFI_GUID *id;
	EFI_GUID hoblist_guid = HOB_LIST_GUID;
	int i;

	hoblist = NULL;
	for (i = 0; i < ST->NumberOfTableEntries; i++) {
		id = &ST->ConfigurationTable[i].VendorGuid;

		if (!memcmp(id, tbl, sizeof(EFI_GUID)))
			return (ST->ConfigurationTable[i].VendorTable);

		if (!memcmp(id, &hoblist_guid, sizeof(EFI_GUID)))
			hoblist = ST->ConfigurationTable[i].VendorTable;
	}

	/*
	 * No configuration table matched; if the firmware published a HOB
	 * list, fall back to it (e.g. for the FDT on LoongArch).
	 */
	if (hoblist != NULL) {
		void *fdt = efi_get_fdt_hob(hoblist);

		if (fdt != NULL)
			return (fdt);
	}
	return (NULL);
}

EFI_STATUS
OpenProtocolByHandle(EFI_HANDLE handle, EFI_GUID *protocol, void **interface)
{
	return (BS->OpenProtocol(handle, protocol, interface, IH, NULL,
	    EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL));
}
