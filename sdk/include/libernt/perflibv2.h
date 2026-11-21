/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared memory published by PERFLIB V2 providers (kernelbase) and read by consumers (advapi32)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _PERFLIBV2_H_
#define _PERFLIBV2_H_

#pragma once

#define PERFLIBV2_SIGNATURE         0x32564650
#define PERFLIBV2_CHANNEL_SIGNATURE 0x43564650
#define PERFLIBV2_VERSION           1

#define PERFLIBV2_DATA_PREFIX       L"PerflibV2Data_"
#define PERFLIBV2_DATA_FORMAT       L"PerflibV2Data_%lu_%lu_%lu"
#define PERFLIBV2_CHANNEL_FORMAT    L"PerflibV2Channel_%lu_%lu"
#define PERFLIBV2_REQUEST_FORMAT    L"PerflibV2Request_%lu_%lu"
#define PERFLIBV2_DONE_FORMAT       L"PerflibV2Done_%lu_%lu"
#define PERFLIBV2_LOCK_FORMAT       L"PerflibV2Lock_%lu_%lu"

#define PERFLIBV2_FLAG_STOPPED      0x00000001
#define PERFLIBV2_FLAG_CALLBACK     0x00000002

#define PERFLIBV2_REQUEST_SIZE      4096
#define PERFLIBV2_CALLBACK_TIMEOUT  1000

typedef struct _PERFLIBV2_INSTANCE_ENTRY
{
    ULONG64 Address;
    ULONG Size;
    ULONG CounterSet;
} PERFLIBV2_INSTANCE_ENTRY, *PPERFLIBV2_INSTANCE_ENTRY;

typedef struct _PERFLIBV2_SHARED
{
    ULONG Signature;
    ULONG Version;
    ULONG SectionSize;
    ULONG ProcessId;
    ULONG ProviderId;
    ULONG Generation;
    ULONG Successor;
    ULONG Flags;
    ULONG64 ProcessCreateTime;
    GUID ProviderGuid;
    LONG Sequence;
    ULONG CounterSetCount;
    ULONG CounterSetOffset;
    ULONG CounterSetBytes;
    ULONG CounterSetCapacity;
    ULONG InstanceCount;
    ULONG InstanceOffset;
    ULONG InstanceCapacity;
} PERFLIBV2_SHARED, *PPERFLIBV2_SHARED;

typedef struct _PERFLIBV2_CHANNEL
{
    ULONG Signature;
    ULONG Version;
    LONG RequestSequence;
    ULONG RequestCode;
    ULONG RequestSize;
    LONG ResponseSequence;
    ULONG ResponseStatus;
    ULONG Reserved;
    BYTE Request[PERFLIBV2_REQUEST_SIZE];
} PERFLIBV2_CHANNEL, *PPERFLIBV2_CHANNEL;

#endif /* _PERFLIBV2_H_ */
