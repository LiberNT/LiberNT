/*
 * PROJECT:   Registry manipulation library
 * LICENSE:   GPL - See COPYING in the top level directory
 * COPYRIGHT: Copyright 2005 Filip Navara <navaraf@reactos.org>
 *            Copyright 2001 - 2005 Eric Kohl
 */

#include "cmlib.h"

/**
 * @name HvpHiveHeaderChecksum
 *
 * Compute checksum of hive header and return it.
 */

ULONG CMAPI
HvpHiveHeaderChecksum(
    PHBASE_BLOCK HiveHeader)
{
    PULONG Buffer = (PULONG)HiveHeader;
    ULONG Sum = 0;
    ULONG i;

    for (i = 0; i < 127; i++)
        Sum ^= Buffer[i];
    if (Sum == (ULONG)-1)
        Sum = (ULONG)-2;
    if (Sum == 0)
        Sum = 1;

    return Sum;
}

#define HV_LOG_HASH_SEED 0x82EF4D887A4E55C5ULL
#define HV_LOG_HASH_ROTL(Value, Count) (((Value) << (Count)) | ((Value) >> (32 - (Count))))

static
VOID
HvpMarvin32Block(
    _Inout_ PULONG Low,
    _Inout_ PULONG High)
{
    *High ^= *Low;
    *Low = HV_LOG_HASH_ROTL(*Low, 20);
    *Low += *High;
    *High = HV_LOG_HASH_ROTL(*High, 9);
    *High ^= *Low;
    *Low = HV_LOG_HASH_ROTL(*Low, 27);
    *Low += *High;
    *High = HV_LOG_HASH_ROTL(*High, 19);
}

ULONGLONG CMAPI
HvpComputeLogHash(
    _In_ PVOID Buffer,
    _In_ ULONG Length)
{
    PUCHAR Data = Buffer;
    ULONG Low = (ULONG)HV_LOG_HASH_SEED;
    ULONG High = (ULONG)(HV_LOG_HASH_SEED >> 32);
    ULONG Final;

    for (; Length >= sizeof(ULONG); Length -= sizeof(ULONG), Data += sizeof(ULONG))
    {
        Low += Data[0] | (Data[1] << 8) | (Data[2] << 16) | ((ULONG)Data[3] << 24);
        HvpMarvin32Block(&Low, &High);
    }

    Final = 0x80;
    while (Length)
        Final = (Final << 8) | Data[--Length];
    Low += Final;
    HvpMarvin32Block(&Low, &High);
    HvpMarvin32Block(&Low, &High);
    return ((ULONGLONG)High << 32) | Low;
}
