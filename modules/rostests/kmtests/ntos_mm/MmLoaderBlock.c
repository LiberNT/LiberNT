/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The loader parameter block is discarded once the system has started
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

START_TEST(MmLoaderBlock)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"KeLoaderBlock");
    BOOTDISK_INFORMATION_EX InformationEx;
    BOOTDISK_INFORMATION Information;
    PVOID *LoaderBlock;
    NTSTATUS Status;

    LoaderBlock = MmGetSystemRoutineAddress(&Name);
    if (!skip(LoaderBlock != NULL, "KeLoaderBlock is not exported\n"))
        ok(*LoaderBlock == NULL, "KeLoaderBlock is %p after boot\n", *LoaderBlock);

    RtlZeroMemory(&Information, sizeof(Information));
    Status = IoGetBootDiskInformation(&Information, sizeof(Information));
    ok_eq_hex(Status, STATUS_TOO_LATE);

    RtlZeroMemory(&InformationEx, sizeof(InformationEx));
    Status = IoGetBootDiskInformation((PBOOTDISK_INFORMATION)&InformationEx, sizeof(InformationEx));
    ok_eq_hex(Status, STATUS_TOO_LATE);
}
