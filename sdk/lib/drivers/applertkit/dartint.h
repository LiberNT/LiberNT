/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DART register layout shared by the stream and table helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <reactos/drivers/appledart.h>

#define DART_PARAMS1                0x00
#define DART_PARAMS2                0x04
#define DART_PARAMS2_BYPASS         0x1
#define DART_T8110_PARAMS3          0x08
#define DART_T8110_PARAMS4          0x0c
#define DART_T8110_TLB_CMD          0x80
#define DART_T8110_TLB_CMD_BUSY     0x80000000
#define DART_T8110_TLB_CMD_FLUSH    0x00000100
#define DART_T8110_PROTECT          0x200
#define DART_T8110_PROTECT_TCR      0x1
#define DART_T8110_ENABLE_STREAMS   0xc00
#define DART_T8110_TCR              0x1000
#define DART_T8110_TCR_TRANSLATE    0x1
#define DART_T8110_TCR_BYPASS       0x6
#define DART_T8110_TCR_FOUR_LEVEL   0x8
#define DART_T8110_TTBR             0x1400
#define DART_T8110_TTBR_VALID       0x1

#define DART_PAGE_SHIFT             14
#define DART_PAGE_SIZE              (1ULL << DART_PAGE_SHIFT)
#define DART_LEVEL_BITS             11
#define DART_TABLE_ENTRIES          (1UL << DART_LEVEL_BITS)
#define DART_PTE_VALID              0x1ULL
#define DART_PTE_SUBPAGE_END        (0xfffULL << 40)
#define DART_PTE_ADDRESS_MASK       0x0000003ffffffc00ULL
#define DART_TAG                    'tDpA'
#define DART_LOW_WINDOW_BASE        0x01000000ULL
#define DART_LOW_WINDOW_END         0xc0000000ULL

static __inline
ULONG
AppleDartRead(
    _In_ PUCHAR Base,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Base + Offset));
}

static __inline
VOID
AppleDartWrite(
    _In_ PUCHAR Base,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Base + Offset), Value);
}

VOID
AppleDartFlush(
    _In_ PCFDT_DRIVER_IO Io,
    _In_ PUCHAR Base,
    _In_ ULONG Sid,
    _In_z_ PCSTR Owner);

NTSTATUS
AppleDartIdentityStream(
    _In_ PUCHAR Base,
    _In_ ULONG Sid,
    _In_ ULONG Params3,
    _In_z_ PCSTR Owner);
