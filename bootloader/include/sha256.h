/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SHA-256 (FIPS 180-4) for the integrity checks of the boot applications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define BL_SHA256_SIZE  32

typedef struct _BL_SHA256
{
    ULONG State[8];
    ULONGLONG Length;
    UCHAR Block[64];
    ULONG Used;
} BL_SHA256, *PBL_SHA256;

VOID
BlSha256Init(
    _Out_ PBL_SHA256 Context);

VOID
BlSha256Update(
    _Inout_ PBL_SHA256 Context,
    _In_reads_bytes_(Length) const VOID* Data,
    _In_ SIZE_T Length);

VOID
BlSha256Final(
    _Inout_ PBL_SHA256 Context,
    _Out_writes_bytes_(BL_SHA256_SIZE) PUCHAR Digest);
