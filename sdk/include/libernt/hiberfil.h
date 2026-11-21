/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private hibernation file format shared by the kernel and winload
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define LNT_HIBER_SIGNATURE         0x4249484C
#define LNT_HIBER_VERSION           1
#define LNT_HIBER_CHUNK_PAGES       16

#define LNT_HIBER_RECORD_ZERO       0
#define LNT_HIBER_RECORD_RAW        1
#define LNT_HIBER_RECORD_LZ         2
#define LNT_HIBER_RECORD_END        0xFFFF

#define LNT_HIBER_MAX_RUNTIME_RANGES 32

#define LNT_EFI_MEMORY_RUNTIME      0x8000000000000000ULL

typedef struct _LNT_EFI_MEMORY_DESCRIPTOR
{
    ULONG Type;
    ULONG64 PhysicalStart;
    ULONG64 VirtualStart;
    ULONG64 NumberOfPages;
    ULONG64 Attribute;
} LNT_EFI_MEMORY_DESCRIPTOR, *PLNT_EFI_MEMORY_DESCRIPTOR;

#define LNT_HIBER_RANGE_DATA        1
#define LNT_HIBER_RANGE_ZERO        2

typedef struct _LNT_HIBER_RANGE
{
    ULONG64 BasePage;
    ULONG PageCount;
    ULONG Class;
} LNT_HIBER_RANGE, *PLNT_HIBER_RANGE;

typedef struct _LNT_HIBER_RUNTIME_RANGE
{
    ULONG64 PhysicalStart;
    ULONG64 VirtualStart;
    ULONG64 PageCount;
    ULONG64 Attribute;
    ULONG Type;
    ULONG Reserved;
} LNT_HIBER_RUNTIME_RANGE, *PLNT_HIBER_RUNTIME_RANGE;

typedef struct _LNT_HIBER_HEADER
{
    ULONG Signature;
    ULONG Version;
    ULONG HeaderSize;
    ULONG HeaderCrc;
    ULONG64 RangeTableOffset;
    ULONG64 RangeCount;
    ULONG64 RecordOffset;
    ULONG64 RecordLength;
    ULONG64 TargetPages;
    ULONG64 DataPages;
    ULONG64 HighestPage;
    ULONG64 LowStubPhysical;
    ULONG LowStubPages;
    ULONG LowStubProt32Offset;
    ULONG LowStubGdtOffset;
    ULONG LowStubGdtLimit;
    ULONG RuntimeRangeCount;
    ULONG Reserved;
    LNT_HIBER_RUNTIME_RANGE RuntimeRanges[LNT_HIBER_MAX_RUNTIME_RANGES];
} LNT_HIBER_HEADER, *PLNT_HIBER_HEADER;

typedef struct _LNT_HIBER_RECORD
{
    ULONG64 TargetPage;
    ULONG PageCount;
    USHORT Type;
    USHORT Reserved;
    ULONG DataLength;
    ULONG Reserved2;
} LNT_HIBER_RECORD, *PLNT_HIBER_RECORD;

typedef struct _LNT_HIBER_RESUME_TARGET
{
    ULONG64 StubPhysical;
    ULONG StubPages;
    ULONG Prot32Offset;
    ULONG GdtOffset;
    ULONG GdtLimit;
} LNT_HIBER_RESUME_TARGET, *PLNT_HIBER_RESUME_TARGET;

C_ASSERT(sizeof(LNT_HIBER_RECORD) == 24);
C_ASSERT(sizeof(LNT_HIBER_RANGE) == 16);
C_ASSERT(sizeof(LNT_HIBER_HEADER) <= 4096);
