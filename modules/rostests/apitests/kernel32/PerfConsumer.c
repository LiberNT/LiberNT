/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the PERFLIB V2 consumer functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winperf.h>
#include <perflib.h>
#include <wmistr.h>

typedef ULONG (WINAPI *PPERF_OPEN_QUERY_HANDLE)(LPCWSTR, PHANDLE);
typedef ULONG (WINAPI *PPERF_CLOSE_QUERY_HANDLE)(HANDLE);
typedef ULONG (WINAPI *PPERF_COUNTERS)(HANDLE, PPERF_COUNTER_IDENTIFIER, DWORD);
typedef ULONG (WINAPI *PPERF_QUERY_COUNTER_INFO)(HANDLE, PPERF_COUNTER_IDENTIFIER, DWORD, LPDWORD);
typedef ULONG (WINAPI *PPERF_QUERY_COUNTER_DATA)(HANDLE, PPERF_DATA_HEADER, DWORD, LPDWORD);
typedef ULONG (WINAPI *PPERF_ENUMERATE_COUNTER_SET)(LPCWSTR, LPGUID, DWORD, LPDWORD);
typedef ULONG (WINAPI *PPERF_ENUMERATE_INSTANCES)(LPCWSTR, LPCGUID, PPERF_INSTANCE_HEADER, DWORD, LPDWORD);
typedef ULONG (WINAPI *PPERF_QUERY_REGISTRATION_INFO)(LPCWSTR, LPCGUID, PerfRegInfoType, DWORD, LPBYTE, DWORD,
                                                      LPDWORD);
typedef ULONG (WINAPI *PPERF_START_PROVIDER_EX)(LPGUID, PPERF_PROVIDER_CONTEXT, PHANDLE);
typedef ULONG (WINAPI *PPERF_STOP_PROVIDER)(HANDLE);
typedef ULONG (WINAPI *PPERF_SET_COUNTER_SET_INFO)(HANDLE, PPERF_COUNTERSET_INFO, ULONG);
typedef PPERF_COUNTERSET_INSTANCE (WINAPI *PPERF_CREATE_INSTANCE)(HANDLE, LPCGUID, PCWSTR, ULONG);
typedef ULONG (WINAPI *PPERF_DELETE_INSTANCE)(HANDLE, PPERF_COUNTERSET_INSTANCE);
typedef ULONG (WINAPI *PPERF_SET_COUNTER_REF_VALUE)(HANDLE, PPERF_COUNTERSET_INSTANCE, ULONG, PVOID);
typedef ULONG (WINAPI *PPERF_ULONG_VALUE)(HANDLE, PPERF_COUNTERSET_INSTANCE, ULONG, ULONG);
typedef ULONG (WINAPI *PPERF_ULONGLONG_VALUE)(HANDLE, PPERF_COUNTERSET_INSTANCE, ULONG, ULONGLONG);

static struct
{
    PPERF_OPEN_QUERY_HANDLE OpenQueryHandle;
    PPERF_CLOSE_QUERY_HANDLE CloseQueryHandle;
    PPERF_COUNTERS AddCounters;
    PPERF_COUNTERS DeleteCounters;
    PPERF_QUERY_COUNTER_INFO QueryCounterInfo;
    PPERF_QUERY_COUNTER_DATA QueryCounterData;
    PPERF_ENUMERATE_COUNTER_SET EnumerateCounterSet;
    PPERF_ENUMERATE_INSTANCES EnumerateCounterSetInstances;
    PPERF_QUERY_REGISTRATION_INFO QueryCounterSetRegistrationInfo;
    PPERF_START_PROVIDER_EX StartProviderEx;
    PPERF_STOP_PROVIDER StopProvider;
    PPERF_SET_COUNTER_SET_INFO SetCounterSetInfo;
    PPERF_CREATE_INSTANCE CreateInstance;
    PPERF_DELETE_INSTANCE DeleteInstance;
    PPERF_SET_COUNTER_REF_VALUE SetCounterRefValue;
    PPERF_ULONG_VALUE SetULongCounterValue;
    PPERF_ULONGLONG_VALUE SetULongLongCounterValue;
} Perf;

typedef struct _COUNTER_TEMPLATE
{
    PERF_COUNTERSET_INFO CounterSet;
    PERF_COUNTER_INFO Counter[3];
} COUNTER_TEMPLATE;

typedef struct _SPEC
{
    PERF_COUNTER_IDENTIFIER Id;
    WCHAR Name[16];
} SPEC;

typedef struct _CHILD_SHARED
{
    LONG Calls[16];
    LONG AddResult;
    LONG Failures;
    PERF_COUNTER_IDENTITY Identity;
    ULONG WnodeSize;
    ULONG BufferSize;
    WCHAR Machine[MAX_COMPUTERNAME_LENGTH + 1];
    WCHAR Name[16];
} CHILD_SHARED;

static GUID LocalProviderGuid = { 0x8d2b6e40, 0x5c1a, 0x4f7e, { 0xa3, 0x09, 0x61, 0x2c, 0x7f, 0x40, 0x11, 0x01 } };
static GUID LocalSetGuid = { 0x8d2b6e40, 0x5c1a, 0x4f7e, { 0xa3, 0x09, 0x61, 0x2c, 0x7f, 0x40, 0x11, 0x02 } };
static GUID LocalSingleGuid = { 0x8d2b6e40, 0x5c1a, 0x4f7e, { 0xa3, 0x09, 0x61, 0x2c, 0x7f, 0x40, 0x11, 0x03 } };
static GUID ChildProviderGuid = { 0x8d2b6e40, 0x5c1a, 0x4f7e, { 0xa3, 0x09, 0x61, 0x2c, 0x7f, 0x40, 0x11, 0x11 } };
static GUID ChildSetGuid = { 0x8d2b6e40, 0x5c1a, 0x4f7e, { 0xa3, 0x09, 0x61, 0x2c, 0x7f, 0x40, 0x11, 0x12 } };
static GUID ChildSingleGuid = { 0x8d2b6e40, 0x5c1a, 0x4f7e, { 0xa3, 0x09, 0x61, 0x2c, 0x7f, 0x40, 0x11, 0x13 } };
static GUID UnknownGuid = { 0x8d2b6e40, 0x5c1a, 0x4f7e, { 0xa3, 0x09, 0x61, 0x2c, 0x7f, 0x40, 0x11, 0xff } };
static GUID StopProviderGuid = { 0x8d2b6e40, 0x5c1a, 0x4f7e, { 0xa3, 0x09, 0x61, 0x2c, 0x7f, 0x40, 0x11, 0x21 } };
static GUID StopSetGuid = { 0x8d2b6e40, 0x5c1a, 0x4f7e, { 0xa3, 0x09, 0x61, 0x2c, 0x7f, 0x40, 0x11, 0x22 } };

static ULONG64 LocalRef1 = 0x3333333333ULL;
static ULONG64 LocalRef2 = 0x44;
static ULONG64 ChildRef1 = 0x300000003ULL;
static ULONG64 ChildRef2 = 0x23;
static CHILD_SHARED *ChildShared;
static WCHAR ComputerName[MAX_COMPUTERNAME_LENGTH + 1];
static HANDLE StopProvider;
static LONG StopCalls;
static ULONG StopResult = 0xdeadbeef;
static LONG StopAllocations;
static LONG StopFrees;

static void
InitTemplate(COUNTER_TEMPLATE *Template, const GUID *Provider, const GUID *CounterSet, ULONG InstanceType,
             ULONG FirstId, ULONG Count)
{
    ULONG i;

    memset(Template, 0, sizeof(*Template));
    Template->CounterSet.CounterSetGuid = *CounterSet;
    Template->CounterSet.ProviderGuid = *Provider;
    Template->CounterSet.NumCounters = Count;
    Template->CounterSet.InstanceType = InstanceType;
    for (i = 0; i < Count; i++)
    {
        Template->Counter[i].CounterId = FirstId + i;
        Template->Counter[i].Type = i ? PERF_COUNTER_LARGE_RAWCOUNT : PERF_COUNTER_RAWCOUNT;
        Template->Counter[i].Attrib = (i == 2) ? PERF_ATTRIB_BY_REFERENCE : 0;
        Template->Counter[i].Size = i ? sizeof(ULONGLONG) : sizeof(ULONG);
        Template->Counter[i].DetailLevel = (i == 1) ? PERF_DETAIL_ADVANCED : PERF_DETAIL_NOVICE;
        Template->Counter[i].Scale = (i == 1) ? -1 : 0;
        Template->Counter[i].Offset = i * sizeof(ULONGLONG);
    }
}

static ULONG
TemplateSize(ULONG Count)
{
    return FIELD_OFFSET(COUNTER_TEMPLATE, Counter[Count]);
}

static void
InitSpec(SPEC *Spec, const GUID *CounterSet, ULONG CounterId, ULONG InstanceId, const WCHAR *Name)
{
    memset(Spec, 0, sizeof(*Spec));
    Spec->Id.CounterSetGuid = *CounterSet;
    Spec->Id.CounterId = CounterId;
    Spec->Id.InstanceId = InstanceId;
    Spec->Id.Size = sizeof(Spec->Id);
    if (Name)
    {
        StringCchCopyW(Spec->Name, ARRAYSIZE(Spec->Name), Name);
        Spec->Id.Size = (sizeof(Spec->Id) + (ULONG)(wcslen(Name) + 1) * sizeof(WCHAR) + 7) & ~7;
    }
}

static ULONG
AddSpec(HANDLE Query, const GUID *CounterSet, ULONG CounterId, ULONG InstanceId, const WCHAR *Name, ULONG *Status)
{
    SPEC Spec;
    ULONG Ret;

    InitSpec(&Spec, CounterSet, CounterId, InstanceId, Name);
    Spec.Id.Status = 0xdeadbeef;
    Ret = Perf.AddCounters(Query, &Spec.Id, Spec.Id.Size);
    *Status = Spec.Id.Status;
    return Ret;
}

static ULONG
DeleteSpec(HANDLE Query, const GUID *CounterSet, ULONG CounterId, ULONG InstanceId, const WCHAR *Name, ULONG *Status)
{
    SPEC Spec;
    ULONG Ret;

    InitSpec(&Spec, CounterSet, CounterId, InstanceId, Name);
    Spec.Id.Status = 0xdeadbeef;
    Ret = Perf.DeleteCounters(Query, &Spec.Id, Spec.Id.Size);
    *Status = Spec.Id.Status;
    return Ret;
}

static PERF_DATA_HEADER *
QueryData(HANDLE Query, DWORD *Size)
{
    PERF_DATA_HEADER *Data;
    DWORD Needed = 0xdeadbeef, Actual = 0xdeadbeef;
    ULONG Ret;

    Ret = Perf.QueryCounterData(Query, NULL, 0, &Needed);
    ok_long(Ret, ERROR_NOT_ENOUGH_MEMORY);
    if (Ret != ERROR_NOT_ENOUGH_MEMORY || Needed < sizeof(*Data) || Needed > 0x100000)
        return NULL;
    Data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Needed);
    if (!Data)
        return NULL;
    Ret = Perf.QueryCounterData(Query, Data, Needed, &Actual);
    ok_long(Ret, ERROR_SUCCESS);
    ok_long(Actual, Needed);
    if (Ret != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, Data);
        return NULL;
    }
    ok_long(Data->dwTotalSize, Needed);
    *Size = Actual;
    return Data;
}

static PERF_COUNTER_HEADER *
GetResult(PERF_DATA_HEADER *Data, ULONG Index)
{
    BYTE *Current = (BYTE *)(Data + 1), *End = (BYTE *)Data + Data->dwTotalSize;
    PERF_COUNTER_HEADER *Header;
    ULONG i;

    for (i = 0; ; i++)
    {
        if (Current + sizeof(*Header) > End)
            return NULL;
        Header = (PERF_COUNTER_HEADER *)Current;
        if (Header->dwSize < sizeof(*Header) || Header->dwSize > (ULONG)(End - Current))
            return NULL;
        if (i == Index)
            return Header;
        Current += Header->dwSize;
    }
}

static ULONG64
CounterValue(const PERF_COUNTER_DATA *Data)
{
    if (Data->dwDataSize == sizeof(ULONG64))
        return *(const ULONG64 UNALIGNED *)(Data + 1);
    return *(const ULONG UNALIGNED *)(Data + 1);
}

static const PERF_COUNTER_DATA *
InstanceCounters(const PERF_INSTANCE_HEADER *Instance)
{
    return (const PERF_COUNTER_DATA *)((const BYTE *)Instance + Instance->Size);
}

static const PERF_COUNTER_DATA *
NextCounter(const PERF_COUNTER_DATA *Data)
{
    return (const PERF_COUNTER_DATA *)((const BYTE *)Data + Data->dwSize);
}

static const PERF_INSTANCE_HEADER *
FindInstance(const PERF_MULTI_INSTANCES *Multi, ULONG CountersPerInstance, const WCHAR *Name)
{
    const PERF_INSTANCE_HEADER *Instance = (const PERF_INSTANCE_HEADER *)(Multi + 1);
    const PERF_COUNTER_DATA *Data;
    ULONG i, j;

    for (i = 0; i < Multi->dwInstances; i++)
    {
        if (!wcscmp((const WCHAR *)(Instance + 1), Name))
            return Instance;
        Data = InstanceCounters(Instance);
        for (j = 0; j < CountersPerInstance; j++)
            Data = NextCounter(Data);
        Instance = (const PERF_INSTANCE_HEADER *)Data;
    }
    return NULL;
}

static void
CheckCounterData(const PERF_COUNTER_DATA *Data, ULONG DataSize, ULONG64 Value, const char *What)
{
    ok(Data->dwDataSize == DataSize, "%s: dwDataSize %lu, expected %lu\n", What, Data->dwDataSize, DataSize);
    ok(Data->dwSize == 16, "%s: dwSize %lu, expected 16\n", What, Data->dwSize);
    ok(CounterValue(Data) == Value, "%s: value 0x%I64x, expected 0x%I64x\n", What, CounterValue(Data), Value);
}

static const PERF_MULTI_COUNTERS *
CheckMultiCounters(const PERF_COUNTER_HEADER *Header, ULONG FirstId, ULONG Count, const char *What)
{
    const PERF_MULTI_COUNTERS *Counters = (const PERF_MULTI_COUNTERS *)(Header + 1);
    const ULONG *Ids = (const ULONG *)(Counters + 1);
    ULONG i;

    ok(Counters->dwCounters == Count, "%s: dwCounters %lu, expected %lu\n", What, Counters->dwCounters, Count);
    ok(Counters->dwSize >= sizeof(*Counters) + Count * sizeof(ULONG), "%s: dwSize %lu\n", What, Counters->dwSize);
    trace("%s: PERF_MULTI_COUNTERS dwSize %lu for %lu counters\n", What, Counters->dwSize, Counters->dwCounters);
    for (i = 0; i < Count && i < Counters->dwCounters; i++)
        ok(Ids[i] == FirstId + i, "%s: counter id %lu is %lu\n", What, i, Ids[i]);
    return Counters;
}

static const PERF_MULTI_INSTANCES *
CheckResult(PERF_DATA_HEADER *Data, ULONG Index, PerfCounterDataType Type, ULONG Instances, const char *What)
{
    PERF_COUNTER_HEADER *Header = GetResult(Data, Index);
    const PERF_MULTI_INSTANCES *Multi;
    const BYTE *After;

    ok(Header != NULL, "%s: no result %lu\n", What, Index);
    if (!Header)
        return NULL;
    ok(Header->dwStatus == ERROR_SUCCESS, "%s: dwStatus %lu\n", What, Header->dwStatus);
    ok(Header->dwType == Type, "%s: dwType %d, expected %d\n", What, Header->dwType, Type);
    ok(!(Header->dwSize & 7), "%s: dwSize %lu is not a multiple of 8\n", What, Header->dwSize);
    if (Header->dwType != Type || (Type != PERF_MULTIPLE_INSTANCES && Type != PERF_COUNTERSET))
        return (const PERF_MULTI_INSTANCES *)(Header + 1);
    After = (const BYTE *)(Header + 1);
    if (Type == PERF_COUNTERSET)
        After += ((const PERF_MULTI_COUNTERS *)After)->dwSize;
    Multi = (const PERF_MULTI_INSTANCES *)After;
    ok(Multi->dwInstances == Instances, "%s: dwInstances %lu, expected %lu\n", What, Multi->dwInstances, Instances);
    ok(Multi->dwTotalSize == (ULONG)((const BYTE *)Header + Header->dwSize - After),
       "%s: dwTotalSize %lu\n", What, Multi->dwTotalSize);
    return Multi;
}

static void
CheckError(PERF_DATA_HEADER *Data, ULONG Index, ULONG Status, const char *What)
{
    PERF_COUNTER_HEADER *Header = GetResult(Data, Index);

    ok(Header != NULL, "%s: no result %lu\n", What, Index);
    if (!Header)
        return;
    ok(Header->dwType == PERF_ERROR_RETURN, "%s: dwType %d\n", What, Header->dwType);
    ok(Header->dwStatus == Status, "%s: dwStatus %lu, expected %lu\n", What, Header->dwStatus, Status);
    ok(Header->dwSize == sizeof(*Header), "%s: dwSize %lu\n", What, Header->dwSize);
}

static BOOL
HasCounterSet(const GUID *CounterSet)
{
    DWORD Count = 0, Actual = 0, i;
    BOOL Found = FALSE;
    GUID *Guids;
    ULONG Ret;

    Ret = Perf.EnumerateCounterSet(NULL, NULL, 0, &Count);
    if (Ret == ERROR_SUCCESS)
        return FALSE;
    ok_long(Ret, ERROR_NOT_ENOUGH_MEMORY);
    if (Ret != ERROR_NOT_ENOUGH_MEMORY || !Count || Count > 4096)
        return FALSE;
    Guids = HeapAlloc(GetProcessHeap(), 0, Count * sizeof(GUID));
    if (!Guids)
        return FALSE;
    Ret = Perf.EnumerateCounterSet(NULL, Guids, Count, &Actual);
    ok_long(Ret, ERROR_SUCCESS);
    ok(Actual == Count, "Actual %lu, Count %lu\n", Actual, Count);
    for (i = 0; Ret == ERROR_SUCCESS && i < Actual; i++)
        if (IsEqualGUID(&Guids[i], CounterSet))
            Found = TRUE;
    HeapFree(GetProcessHeap(), 0, Guids);
    return Found;
}

static void
CheckInstanceHeader(const PERF_INSTANCE_HEADER *Header, ULONG Id, const WCHAR *Name, const char *What)
{
    ULONG Size = (sizeof(*Header) + (ULONG)(wcslen(Name) + 1) * sizeof(WCHAR) + 7) & ~7;

    ok(Header->Size == Size, "%s: Size %lu, expected %lu\n", What, Header->Size, Size);
    ok(Header->InstanceId == Id, "%s: InstanceId %lu, expected %lu\n", What, Header->InstanceId, Id);
    ok(!wcscmp((const WCHAR *)(Header + 1), Name), "%s: name %s, expected %s\n", What,
       wine_dbgstr_w((const WCHAR *)(Header + 1)), wine_dbgstr_w(Name));
}

static void
TestCounterSetEnumeration(void)
{
    DWORD Count = 0xdeadbeef;
    GUID Guid;
    ULONG Ret;

    ok_long(Perf.EnumerateCounterSet(NULL, NULL, 0, NULL), ERROR_INVALID_PARAMETER);
    Ret = Perf.EnumerateCounterSet(NULL, NULL, 0, &Count);
    ok_long(Ret, ERROR_NOT_ENOUGH_MEMORY);
    ok(Count >= 2 && Count != 0xdeadbeef, "Count %lu\n", Count);
    Count = 0xdeadbeef;
    ok_long(Perf.EnumerateCounterSet(NULL, &Guid, 1, &Count), ERROR_NOT_ENOUGH_MEMORY);
    ok(Count >= 2 && Count != 0xdeadbeef, "Count %lu\n", Count);
    ok(HasCounterSet(&LocalSetGuid), "The multi-instance counter set is not enumerated\n");
    ok(HasCounterSet(&LocalSingleGuid), "The single-instance counter set is not enumerated\n");
    Count = 0xdeadbeef;
    ok_long(Perf.EnumerateCounterSet(ComputerName, NULL, 0, &Count), ERROR_NOT_ENOUGH_MEMORY);
    ok(Count >= 2 && Count != 0xdeadbeef, "Count %lu\n", Count);
}

static void
TestRegistrationInfo(void)
{
    BYTE Buffer[sizeof(PERF_COUNTERSET_REG_INFO) + 4 * sizeof(PERF_COUNTER_REG_INFO)];
    PERF_COUNTERSET_REG_INFO *SetInfo = (PERF_COUNTERSET_REG_INFO *)Buffer;
    PERF_COUNTER_REG_INFO *Counter = (PERF_COUNTER_REG_INFO *)(SetInfo + 1);
    DWORD Actual, Expected = sizeof(*SetInfo) + 3 * sizeof(*Counter);
    GUID Guid;
    ULONG Ret, i;

    Actual = 0xdeadbeef;
    ok_long(Perf.QueryCounterSetRegistrationInfo(NULL, &LocalSetGuid, 0, 0, Buffer, sizeof(Buffer), &Actual),
            ERROR_INVALID_PARAMETER);
    ok_long(Perf.QueryCounterSetRegistrationInfo(NULL, &LocalSetGuid, PERF_REG_COUNTER_ENGLISH_NAMES + 1, 0,
                                                 Buffer, sizeof(Buffer), &Actual), ERROR_INVALID_PARAMETER);
    ok_long(Perf.QueryCounterSetRegistrationInfo(NULL, &UnknownGuid, PERF_REG_COUNTERSET_STRUCT, 0,
                                                 Buffer, sizeof(Buffer), &Actual), ERROR_WMI_GUID_NOT_FOUND);
    ok_long(Perf.QueryCounterSetRegistrationInfo(NULL, NULL, PERF_REG_COUNTERSET_STRUCT, 0,
                                                 Buffer, sizeof(Buffer), &Actual), ERROR_INVALID_PARAMETER);
    ok_long(Perf.QueryCounterSetRegistrationInfo(NULL, &LocalSetGuid, PERF_REG_COUNTERSET_STRUCT, 0,
                                                 Buffer, sizeof(Buffer), NULL), ERROR_INVALID_PARAMETER);

    Actual = 0xdeadbeef;
    ok_long(Perf.QueryCounterSetRegistrationInfo(NULL, &LocalSetGuid, PERF_REG_COUNTERSET_STRUCT, 0, NULL, 0, &Actual),
            ERROR_NOT_ENOUGH_MEMORY);
    ok_long(Actual, Expected);
    Actual = 0xdeadbeef;
    ok_long(Perf.QueryCounterSetRegistrationInfo(NULL, &LocalSetGuid, PERF_REG_COUNTERSET_STRUCT, 0,
                                                 Buffer, Expected - 1, &Actual), ERROR_NOT_ENOUGH_MEMORY);
    ok_long(Actual, Expected);
    memset(Buffer, 0xcc, sizeof(Buffer));
    Actual = 0xdeadbeef;
    Ret = Perf.QueryCounterSetRegistrationInfo(NULL, &LocalSetGuid, PERF_REG_COUNTERSET_STRUCT, 0,
                                               Buffer, sizeof(Buffer), &Actual);
    ok_long(Ret, ERROR_SUCCESS);
    ok_long(Actual, Expected);
    if (Ret == ERROR_SUCCESS)
    {
        ok(IsEqualGUID(&SetInfo->CounterSetGuid, &LocalSetGuid), "Wrong counter set GUID\n");
        ok_long(SetInfo->CounterSetType, 0);
        ok_long(SetInfo->NumCounters, 3);
        ok_long(SetInfo->InstanceType, PERF_COUNTERSET_MULTI_INSTANCES);
        trace("Counter set DetailLevel %lu\n", SetInfo->DetailLevel);
        for (i = 0; i < 3; i++)
        {
            ok(Counter[i].CounterId == 1 + i, "Counter %lu: id %lu\n", i, Counter[i].CounterId);
            ok(Counter[i].Type == (i ? PERF_COUNTER_LARGE_RAWCOUNT : PERF_COUNTER_RAWCOUNT),
               "Counter %lu: type 0x%lx\n", i, Counter[i].Type);
            ok(Counter[i].Attrib == ((i == 2) ? PERF_ATTRIB_BY_REFERENCE : 0), "Counter %lu: attrib 0x%I64x\n", i,
               Counter[i].Attrib);
            ok(Counter[i].DetailLevel == ((i == 1) ? PERF_DETAIL_ADVANCED : PERF_DETAIL_NOVICE),
               "Counter %lu: detail level %lu\n", i, Counter[i].DetailLevel);
            ok(Counter[i].DefaultScale == ((i == 1) ? -1 : 0), "Counter %lu: scale %ld\n", i, Counter[i].DefaultScale);
            ok(Counter[i].BaseCounterId == 0xffffffff, "Counter %lu: base 0x%lx\n", i, Counter[i].BaseCounterId);
            ok(Counter[i].PerfTimeId == 0xffffffff, "Counter %lu: time 0x%lx\n", i, Counter[i].PerfTimeId);
            ok(Counter[i].PerfFreqId == 0xffffffff, "Counter %lu: freq 0x%lx\n", i, Counter[i].PerfFreqId);
            ok(Counter[i].MultiId == 0xffffffff, "Counter %lu: multi 0x%lx\n", i, Counter[i].MultiId);
            ok(Counter[i].AggregateFunc == PERF_AGGREGATE_UNDEFINED, "Counter %lu: aggregate %lu\n", i,
               Counter[i].AggregateFunc);
        }
    }

    Actual = 0xdeadbeef;
    Ret = Perf.QueryCounterSetRegistrationInfo(NULL, &LocalSingleGuid, PERF_REG_COUNTERSET_STRUCT, 0,
                                               Buffer, sizeof(Buffer), &Actual);
    ok_long(Ret, ERROR_SUCCESS);
    ok_long(Actual, sizeof(*SetInfo) + sizeof(*Counter));
    if (Ret == ERROR_SUCCESS)
    {
        ok_long(SetInfo->NumCounters, 1);
        ok_long(SetInfo->InstanceType, PERF_COUNTERSET_SINGLE_INSTANCE);
        ok_long(Counter[0].CounterId, 5);
    }

    Actual = 0xdeadbeef;
    ok_long(Perf.QueryCounterSetRegistrationInfo(NULL, &LocalSetGuid, PERF_REG_COUNTER_STRUCT, 2, NULL, 0, &Actual),
            ERROR_NOT_ENOUGH_MEMORY);
    ok_long(Actual, sizeof(*Counter));
    memset(Buffer, 0xcc, sizeof(Buffer));
    Ret = Perf.QueryCounterSetRegistrationInfo(NULL, &LocalSetGuid, PERF_REG_COUNTER_STRUCT, 2,
                                               Buffer, sizeof(Buffer), &Actual);
    ok_long(Ret, ERROR_SUCCESS);
    ok_long(Actual, sizeof(*Counter));
    if (Ret == ERROR_SUCCESS)
    {
        Counter = (PERF_COUNTER_REG_INFO *)Buffer;
        ok_long(Counter->CounterId, 2);
        ok_long(Counter->Type, PERF_COUNTER_LARGE_RAWCOUNT);
        ok_long(Counter->DetailLevel, PERF_DETAIL_ADVANCED);
        ok_long(Counter->DefaultScale, -1);
        ok_long(Counter->BaseCounterId, 0xffffffff);
    }
    ok_long(Perf.QueryCounterSetRegistrationInfo(NULL, &LocalSetGuid, PERF_REG_COUNTER_STRUCT, 99,
                                                 Buffer, sizeof(Buffer), &Actual), ERROR_WMI_ITEMID_NOT_FOUND);

    Actual = 0xdeadbeef;
    ok_long(Perf.QueryCounterSetRegistrationInfo(NULL, &LocalSetGuid, PERF_REG_PROVIDER_GUID, 0,
                                                 (BYTE *)&Guid, sizeof(ULONG), &Actual), ERROR_NOT_ENOUGH_MEMORY);
    ok_long(Actual, sizeof(GUID));
    memset(&Guid, 0, sizeof(Guid));
    ok_long(Perf.QueryCounterSetRegistrationInfo(NULL, &LocalSetGuid, PERF_REG_PROVIDER_GUID, 0,
                                                 (BYTE *)&Guid, sizeof(Guid), &Actual), ERROR_SUCCESS);
    ok_long(Actual, sizeof(GUID));
    ok(IsEqualGUID(&Guid, &LocalProviderGuid), "Wrong provider GUID\n");

    Ret = Perf.QueryCounterSetRegistrationInfo(NULL, &LocalSetGuid, PERF_REG_COUNTERSET_NAME_STRING, 0,
                                               Buffer, sizeof(Buffer), &Actual);
    trace("PERF_REG_COUNTERSET_NAME_STRING without a manifest: %lu\n", Ret);
}

static void
TestInstanceEnumeration(void)
{
    BYTE Buffer[64];
    PERF_INSTANCE_HEADER *Header = (PERF_INSTANCE_HEADER *)Buffer;
    DWORD Actual;
    ULONG Ret;

    ok_long(Perf.EnumerateCounterSetInstances(NULL, NULL, NULL, 0, &Actual), ERROR_INVALID_PARAMETER);
    ok_long(Perf.EnumerateCounterSetInstances(NULL, &LocalSetGuid, NULL, 0, NULL), ERROR_INVALID_PARAMETER);
    ok_long(Perf.EnumerateCounterSetInstances(NULL, &UnknownGuid, NULL, 0, &Actual), ERROR_WMI_GUID_NOT_FOUND);

    Actual = 0xdeadbeef;
    ok_long(Perf.EnumerateCounterSetInstances(NULL, &LocalSetGuid, NULL, 0, &Actual), ERROR_NOT_ENOUGH_MEMORY);
    ok_long(Actual, 48);
    Actual = 0xdeadbeef;
    ok_long(Perf.EnumerateCounterSetInstances(NULL, &LocalSetGuid, Header, 47, &Actual), ERROR_NOT_ENOUGH_MEMORY);
    ok_long(Actual, 48);
    memset(Buffer, 0xcc, sizeof(Buffer));
    Actual = 0xdeadbeef;
    Ret = Perf.EnumerateCounterSetInstances(NULL, &LocalSetGuid, Header, sizeof(Buffer), &Actual);
    ok_long(Ret, ERROR_SUCCESS);
    ok_long(Actual, 48);
    if (Ret == ERROR_SUCCESS)
    {
        CheckInstanceHeader(Header, 7, L"First", "First instance");
        CheckInstanceHeader((PERF_INSTANCE_HEADER *)(Buffer + 24), 9, L"Second", "Second instance");
    }

    Actual = 0xdeadbeef;
    Ret = Perf.EnumerateCounterSetInstances(ComputerName, &LocalSingleGuid, Header, sizeof(Buffer), &Actual);
    ok_long(Ret, ERROR_SUCCESS);
    ok_long(Actual, 24);
    if (Ret == ERROR_SUCCESS)
        CheckInstanceHeader(Header, 0, L"Only", "Single instance");
}

static void
TestAddValidation(HANDLE Query)
{
    BYTE Buffer[sizeof(SPEC) * 2];
    SPEC *Spec = (SPEC *)Buffer;
    DWORD Actual;
    ULONG Status;

    InitSpec(Spec, &LocalSetGuid, PERF_WILDCARD_COUNTER, PERF_WILDCARD_COUNTER, L"*");
    ok_long(Perf.AddCounters(Query, NULL, sizeof(*Spec)), ERROR_INVALID_PARAMETER);
    ok_long(Perf.AddCounters(Query, &Spec->Id, 8), ERROR_INVALID_PARAMETER);
    Spec->Id.Size = 0;
    ok_long(Perf.AddCounters(Query, &Spec->Id, sizeof(Spec->Id)), ERROR_INVALID_PARAMETER);
    Spec->Id.Size = sizeof(Spec->Id) - 8;
    ok_long(Perf.AddCounters(Query, &Spec->Id, sizeof(Spec->Id)), ERROR_INVALID_PARAMETER);
    Spec->Id.Size = sizeof(*Spec);
    ok_long(Perf.AddCounters(Query, &Spec->Id, sizeof(Spec->Id)), ERROR_INVALID_PARAMETER);

    ok_long(AddSpec(Query, &UnknownGuid, PERF_WILDCARD_COUNTER, PERF_WILDCARD_COUNTER, L"*", &Status), ERROR_SUCCESS);
    ok_long(Status, ERROR_WMI_GUID_NOT_FOUND);
    ok_long(AddSpec(Query, &LocalSetGuid, 42, PERF_WILDCARD_COUNTER, L"*", &Status), ERROR_SUCCESS);
    ok_long(Status, ERROR_WMI_ITEMID_NOT_FOUND);
    ok_long(AddSpec(Query, &LocalSetGuid, 1, PERF_WILDCARD_COUNTER, NULL, &Status), ERROR_SUCCESS);
    ok_long(Status, ERROR_INVALID_PARAMETER);
    ok_long(AddSpec(Query, &LocalSetGuid, 1, PERF_WILDCARD_COUNTER, L"", &Status), ERROR_SUCCESS);
    ok_long(Status, ERROR_INVALID_PARAMETER);
    ok_long(AddSpec(Query, &LocalSingleGuid, 5, PERF_WILDCARD_COUNTER, L"Only", &Status), ERROR_SUCCESS);
    ok_long(Status, ERROR_INVALID_PARAMETER);
    ok_long(AddSpec(Query, &LocalSetGuid, 1, PERF_WILDCARD_COUNTER, L"Nope", &Status), ERROR_SUCCESS);
    ok_long(Status, ERROR_PATH_NOT_FOUND);

    InitSpec(Spec, &LocalSetGuid, 1, PERF_WILDCARD_COUNTER, NULL);
    Spec->Id.Size = sizeof(Spec->Id) + 8;
    memcpy(Spec->Name, L"Firs", 4 * sizeof(WCHAR));
    Spec->Id.Status = 0xdeadbeef;
    ok_long(Perf.AddCounters(Query, &Spec->Id, Spec->Id.Size), ERROR_SUCCESS);
    ok_long(Spec->Id.Status, ERROR_INVALID_PARAMETER);

    Actual = 0xdeadbeef;
    ok_long(Perf.QueryCounterInfo(Query, NULL, 0, &Actual), ERROR_SUCCESS);
    ok_long(Actual, 0);
}

static void
CheckIdentifier(const PERF_COUNTER_IDENTIFIER *Id, const GUID *CounterSet, ULONG CounterId, ULONG InstanceId,
                const WCHAR *Name, ULONG Index)
{
    ULONG Size = Name ? (sizeof(*Id) + (ULONG)(wcslen(Name) + 1) * sizeof(WCHAR) + 7) & ~7 : sizeof(*Id);

    ok(IsEqualGUID(&Id->CounterSetGuid, CounterSet), "Identifier %lu: wrong GUID\n", Index);
    ok(Id->Status == ERROR_SUCCESS, "Identifier %lu: Status %lu\n", Index, Id->Status);
    ok(Id->Size == Size, "Identifier %lu: Size %lu, expected %lu\n", Index, Id->Size, Size);
    ok(Id->CounterId == CounterId, "Identifier %lu: CounterId %lu\n", Index, Id->CounterId);
    ok(Id->InstanceId == InstanceId, "Identifier %lu: InstanceId %lu\n", Index, Id->InstanceId);
    ok(Id->Index == Index, "Identifier %lu: Index %lu\n", Index, Id->Index);
    if (Name)
        ok(!wcscmp((const WCHAR *)(Id + 1), Name), "Identifier %lu: name %s\n", Index,
           wine_dbgstr_w((const WCHAR *)(Id + 1)));
}

static void
TestLocalQuery(HANDLE Provider, PERF_COUNTERSET_INSTANCE *First, PERF_COUNTERSET_INSTANCE *Second)
{
    BYTE Buffer[sizeof(SPEC) * 5];
    BYTE Info[512];
    PERF_COUNTER_IDENTIFIER *Id;
    const PERF_MULTI_INSTANCES *Multi;
    const PERF_INSTANCE_HEADER *Instance;
    const PERF_COUNTER_DATA *Data;
    const PERF_COUNTER_HEADER *Header;
    LARGE_INTEGER Frequency, Before, After;
    FILETIME Time1, Time2, SystemFileTime;
    PERF_DATA_HEADER *Result, Small;
    HANDLE Query, Query2;
    DWORD Size, Actual, Offset, Expected;
    ULONG Ret, Status, i;

    ok_long(Perf.OpenQueryHandle(NULL, NULL), ERROR_INVALID_PARAMETER);
    Query = (HANDLE)0xdeadbeef;
    ok_long(Perf.OpenQueryHandle(NULL, &Query), ERROR_SUCCESS);
    ok(Query != NULL && Query != (HANDLE)0xdeadbeef, "Query %p\n", Query);
    Query2 = NULL;
    ok_long(Perf.OpenQueryHandle(ComputerName, &Query2), ERROR_SUCCESS);
    ok(Query2 != NULL && Query2 != Query, "Query2 %p\n", Query2);
    ok_long(Perf.CloseQueryHandle(Query2), ERROR_SUCCESS);

    Actual = 0xdeadbeef;
    ok_long(Perf.QueryCounterData(Query, NULL, 0, NULL), ERROR_INVALID_PARAMETER);
    ok_long(Perf.QueryCounterData(Query, NULL, 0, &Actual), ERROR_NOT_ENOUGH_MEMORY);
    ok_long(Actual, sizeof(PERF_DATA_HEADER));
    ok_long(Perf.QueryCounterData(Query, &Small, sizeof(Small), NULL), ERROR_INVALID_PARAMETER);
    ok_long(Perf.QueryCounterInfo(Query, NULL, 0, NULL), ERROR_INVALID_PARAMETER);

    TestAddValidation(Query);

    memset(Buffer, 0, sizeof(Buffer));
    Offset = 0;
    InitSpec((SPEC *)(Buffer + Offset), &LocalSetGuid, 1, PERF_WILDCARD_COUNTER, L"*");
    Offset += ((SPEC *)(Buffer + Offset))->Id.Size;
    InitSpec((SPEC *)(Buffer + Offset), &LocalSetGuid, PERF_WILDCARD_COUNTER, PERF_WILDCARD_COUNTER, L"Sec*");
    Offset += ((SPEC *)(Buffer + Offset))->Id.Size;
    InitSpec((SPEC *)(Buffer + Offset), &LocalSingleGuid, 5, PERF_WILDCARD_COUNTER, NULL);
    Offset += ((SPEC *)(Buffer + Offset))->Id.Size;
    InitSpec((SPEC *)(Buffer + Offset), &LocalSetGuid, PERF_WILDCARD_COUNTER, 7, L"*");
    Offset += ((SPEC *)(Buffer + Offset))->Id.Size;
    InitSpec((SPEC *)(Buffer + Offset), &LocalSingleGuid, PERF_WILDCARD_COUNTER, PERF_WILDCARD_COUNTER, L"");
    Offset += ((SPEC *)(Buffer + Offset))->Id.Size;
    ok_long(Perf.AddCounters(Query, (PERF_COUNTER_IDENTIFIER *)Buffer, Offset), ERROR_SUCCESS);
    for (i = 0, Size = 0; Size < Offset; i++)
    {
        Id = (PERF_COUNTER_IDENTIFIER *)(Buffer + Size);
        ok(Id->Status == ERROR_SUCCESS, "Identifier %lu: Status %lu\n", i, Id->Status);
        Size += Id->Size;
    }
    ok_long(AddSpec(Query, &LocalSetGuid, 1, PERF_WILDCARD_COUNTER, L"*", &Status), ERROR_SUCCESS);
    ok_long(Status, ERROR_ALREADY_EXISTS);

    Expected = 48 + 56 + 40 + 48 + 40;
    Actual = 0xdeadbeef;
    ok_long(Perf.QueryCounterInfo(Query, NULL, 0, &Actual), ERROR_NOT_ENOUGH_MEMORY);
    ok_long(Actual, Expected);
    Actual = 0xdeadbeef;
    ok_long(Perf.QueryCounterInfo(Query, (PERF_COUNTER_IDENTIFIER *)Info, Expected - 1, &Actual),
            ERROR_NOT_ENOUGH_MEMORY);
    ok_long(Actual, Expected);
    memset(Info, 0xcc, sizeof(Info));
    Ret = Perf.QueryCounterInfo(Query, (PERF_COUNTER_IDENTIFIER *)Info, sizeof(Info), &Actual);
    ok_long(Ret, ERROR_SUCCESS);
    ok_long(Actual, Expected);
    if (Ret == ERROR_SUCCESS)
    {
        Id = (PERF_COUNTER_IDENTIFIER *)Info;
        CheckIdentifier(Id, &LocalSetGuid, 1, PERF_WILDCARD_COUNTER, L"*", 0);
        Id = (PERF_COUNTER_IDENTIFIER *)((BYTE *)Id + Id->Size);
        CheckIdentifier(Id, &LocalSetGuid, PERF_WILDCARD_COUNTER, PERF_WILDCARD_COUNTER, L"Sec*", 1);
        Id = (PERF_COUNTER_IDENTIFIER *)((BYTE *)Id + Id->Size);
        CheckIdentifier(Id, &LocalSingleGuid, 5, PERF_WILDCARD_COUNTER, NULL, 2);
        Id = (PERF_COUNTER_IDENTIFIER *)((BYTE *)Id + Id->Size);
        CheckIdentifier(Id, &LocalSetGuid, PERF_WILDCARD_COUNTER, 7, L"*", 3);
        Id = (PERF_COUNTER_IDENTIFIER *)((BYTE *)Id + Id->Size);
        CheckIdentifier(Id, &LocalSingleGuid, PERF_WILDCARD_COUNTER, PERF_WILDCARD_COUNTER, NULL, 4);
    }

    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Before);
    GetSystemTimeAsFileTime(&Time1);
    Result = QueryData(Query, &Size);
    QueryPerformanceCounter(&After);
    GetSystemTimeAsFileTime(&Time2);
    if (Result)
    {
        ok_long(Result->dwNumCounters, 5);
        ok(Result->PerfFreq == Frequency.QuadPart, "PerfFreq %I64d\n", Result->PerfFreq);
        ok(Result->PerfTimeStamp >= Before.QuadPart && Result->PerfTimeStamp <= After.QuadPart,
           "PerfTimeStamp %I64d not in [%I64d, %I64d]\n", Result->PerfTimeStamp, Before.QuadPart, After.QuadPart);
        ok(Result->PerfTime100NSec >= ((LONGLONG)Time1.dwHighDateTime << 32 | Time1.dwLowDateTime)
           && Result->PerfTime100NSec <= ((LONGLONG)Time2.dwHighDateTime << 32 | Time2.dwLowDateTime),
           "PerfTime100NSec %I64d\n", Result->PerfTime100NSec);
        ok(SystemTimeToFileTime(&Result->SystemTime, &SystemFileTime), "Invalid SystemTime\n");

        Multi = CheckResult(Result, 0, PERF_MULTIPLE_INSTANCES, 2, "Counter 1 of every instance");
        if (Multi && Multi->dwInstances == 2)
        {
            Instance = FindInstance(Multi, 1, L"First");
            ok(Instance != NULL, "First is missing\n");
            if (Instance)
            {
                CheckInstanceHeader(Instance, 7, L"First", "Counter 1 of First");
                CheckCounterData(InstanceCounters(Instance), 4, 0x11, "Counter 1 of First");
            }
            Instance = FindInstance(Multi, 1, L"Second");
            ok(Instance != NULL, "Second is missing\n");
            if (Instance)
                CheckCounterData(InstanceCounters(Instance), 4, 0x21, "Counter 1 of Second");
        }

        Header = GetResult(Result, 1);
        Multi = CheckResult(Result, 1, PERF_COUNTERSET, 1, "Every counter of Sec*");
        if (Header && Multi && Multi->dwInstances == 1)
        {
            CheckMultiCounters(Header, 1, 3, "Every counter of Sec*");
            Instance = (const PERF_INSTANCE_HEADER *)(Multi + 1);
            CheckInstanceHeader(Instance, 9, L"Second", "Every counter of Sec*");
            Data = InstanceCounters(Instance);
            CheckCounterData(Data, 4, 0x21, "Counter 1 of Second");
            Data = NextCounter(Data);
            CheckCounterData(Data, 8, 0x22, "Counter 2 of Second");
            Data = NextCounter(Data);
            CheckCounterData(Data, 8, 0x44, "Counter 3 of Second");
        }

        Header = GetResult(Result, 2);
        CheckResult(Result, 2, PERF_SINGLE_COUNTER, 0, "Counter 5 of the single instance");
        if (Header && Header->dwType == PERF_SINGLE_COUNTER)
        {
            ok_long(Header->dwSize, sizeof(*Header) + 16);
            CheckCounterData((const PERF_COUNTER_DATA *)(Header + 1), 4, 0x55, "Counter 5");
        }

        Header = GetResult(Result, 3);
        Multi = CheckResult(Result, 3, PERF_COUNTERSET, 1, "Every counter of instance 7");
        if (Header && Multi && Multi->dwInstances == 1)
        {
            CheckMultiCounters(Header, 1, 3, "Every counter of instance 7");
            Instance = (const PERF_INSTANCE_HEADER *)(Multi + 1);
            CheckInstanceHeader(Instance, 7, L"First", "Every counter of instance 7");
            Data = InstanceCounters(Instance);
            CheckCounterData(Data, 4, 0x11, "Counter 1 of First");
            Data = NextCounter(Data);
            CheckCounterData(Data, 8, 0x1000000022ULL, "Counter 2 of First");
            Data = NextCounter(Data);
            CheckCounterData(Data, 8, 0x3333333333ULL, "Counter 3 of First");
        }

        Header = GetResult(Result, 4);
        CheckResult(Result, 4, PERF_MULTIPLE_COUNTERS, 0, "Every counter of the single instance");
        if (Header && Header->dwType == PERF_MULTIPLE_COUNTERS)
        {
            const PERF_MULTI_COUNTERS *Counters = CheckMultiCounters(Header, 5, 1, "Every counter of the single instance");

            CheckCounterData((const PERF_COUNTER_DATA *)((const BYTE *)Counters + Counters->dwSize), 4, 0x55,
                             "Counter 5 in the counter list");
        }

        Actual = 0xdeadbeef;
        ok_long(Perf.QueryCounterData(Query, Result, Size - 1, &Actual), ERROR_NOT_ENOUGH_MEMORY);
        ok_long(Actual, Size);
        HeapFree(GetProcessHeap(), 0, Result);
    }

    ok_long(Perf.SetULongCounterValue(Provider, First, 1, 0x12), ERROR_SUCCESS);
    LocalRef1 = 0x3333333334ULL;
    ok_long(Perf.SetCounterRefValue(Provider, Second, 3, NULL), ERROR_SUCCESS);
    Result = QueryData(Query, &Size);
    if (Result)
    {
        Multi = CheckResult(Result, 0, PERF_MULTIPLE_INSTANCES, 2, "Updated counter 1");
        if (Multi && Multi->dwInstances == 2)
        {
            Instance = FindInstance(Multi, 1, L"First");
            if (Instance)
                CheckCounterData(InstanceCounters(Instance), 4, 0x12, "Updated counter 1 of First");
        }
        CheckError(Result, 1, ERROR_NO_DATA, "Null reference of Second");
        Multi = CheckResult(Result, 3, PERF_COUNTERSET, 1, "Updated reference of First");
        if (Multi && Multi->dwInstances == 1)
        {
            Data = InstanceCounters((const PERF_INSTANCE_HEADER *)(Multi + 1));
            Data = NextCounter(NextCounter(Data));
            CheckCounterData(Data, 8, 0x3333333334ULL, "Updated counter 3 of First");
        }
        HeapFree(GetProcessHeap(), 0, Result);
    }
    ok_long(Perf.SetCounterRefValue(Provider, Second, 3, &LocalRef2), ERROR_SUCCESS);

    ok_long(DeleteSpec(Query, &LocalSingleGuid, 5, PERF_WILDCARD_COUNTER, NULL, &Status), ERROR_SUCCESS);
    ok_long(Status, ERROR_SUCCESS);
    ok_long(DeleteSpec(Query, &LocalSingleGuid, 5, PERF_WILDCARD_COUNTER, NULL, &Status), ERROR_SUCCESS);
    ok_long(Status, ERROR_INVALID_PARAMETER);
    ok_long(DeleteSpec(Query, &LocalSetGuid, 1, PERF_WILDCARD_COUNTER, L"Nope", &Status), ERROR_SUCCESS);
    ok_long(Status, ERROR_INVALID_PARAMETER);
    Ret = Perf.QueryCounterInfo(Query, (PERF_COUNTER_IDENTIFIER *)Info, sizeof(Info), &Actual);
    ok_long(Ret, ERROR_SUCCESS);
    ok_long(Actual, 48 + 56 + 48 + 40);
    if (Ret == ERROR_SUCCESS)
    {
        Id = (PERF_COUNTER_IDENTIFIER *)Info;
        Id = (PERF_COUNTER_IDENTIFIER *)((BYTE *)Id + Id->Size);
        Id = (PERF_COUNTER_IDENTIFIER *)((BYTE *)Id + Id->Size);
        CheckIdentifier(Id, &LocalSetGuid, PERF_WILDCARD_COUNTER, 7, L"*", 2);
    }

    ok_long(Perf.DeleteInstance(Provider, Second), ERROR_SUCCESS);
    Result = QueryData(Query, &Size);
    if (Result)
    {
        ok_long(Result->dwNumCounters, 4);
        Multi = CheckResult(Result, 0, PERF_MULTIPLE_INSTANCES, 1, "Counter 1 after deleting Second");
        if (Multi && Multi->dwInstances == 1)
            CheckInstanceHeader((const PERF_INSTANCE_HEADER *)(Multi + 1), 7, L"First", "Remaining instance");
        CheckResult(Result, 1, PERF_COUNTERSET, 0, "Sec* after deleting Second");
        HeapFree(GetProcessHeap(), 0, Result);
    }
    Actual = 0xdeadbeef;
    ok_long(Perf.EnumerateCounterSetInstances(NULL, &LocalSetGuid, NULL, 0, &Actual), ERROR_NOT_ENOUGH_MEMORY);
    ok_long(Actual, 24);

    ok_long(Perf.StopProvider(Provider), ERROR_SUCCESS);
    Result = QueryData(Query, &Size);
    if (Result)
    {
        for (i = 0; i < 4; i++)
            CheckError(Result, i, ERROR_WMI_GUID_NOT_FOUND, "After PerfStopProvider");
        HeapFree(GetProcessHeap(), 0, Result);
    }
    ok(!HasCounterSet(&LocalSetGuid), "The counter set is still enumerated after PerfStopProvider\n");
    ok_long(Perf.EnumerateCounterSetInstances(NULL, &LocalSetGuid, NULL, 0, &Actual), ERROR_WMI_GUID_NOT_FOUND);
    ok_long(Perf.QueryCounterSetRegistrationInfo(NULL, &LocalSetGuid, PERF_REG_COUNTERSET_STRUCT, 0, NULL, 0, &Actual),
            ERROR_WMI_GUID_NOT_FOUND);

    ok_long(Perf.CloseQueryHandle(Query), ERROR_SUCCESS);
    Ret = Perf.CloseQueryHandle(Query);
    ok(Ret != ERROR_SUCCESS, "Closing a closed query handle returned %lu\n", Ret);
    trace("Closing a closed query handle: %lu\n", Ret);
}

static void
TestLocalProvider(void)
{
    PERF_COUNTERSET_INSTANCE *First, *Second, *Single;
    COUNTER_TEMPLATE Template;
    DWORD Actual;
    HANDLE Provider;
    ULONG Ret;

    Ret = Perf.StartProviderEx(&LocalProviderGuid, NULL, &Provider);
    ok_long(Ret, ERROR_SUCCESS);
    if (Ret != ERROR_SUCCESS)
        return;
    InitTemplate(&Template, &LocalProviderGuid, &LocalSetGuid, PERF_COUNTERSET_MULTI_INSTANCES, 1, 3);
    ok_long(Perf.SetCounterSetInfo(Provider, &Template.CounterSet, TemplateSize(3)), ERROR_SUCCESS);
    InitTemplate(&Template, &LocalProviderGuid, &LocalSingleGuid, PERF_COUNTERSET_SINGLE_INSTANCE, 5, 1);
    ok_long(Perf.SetCounterSetInfo(Provider, &Template.CounterSet, TemplateSize(1)), ERROR_SUCCESS);

    TestCounterSetEnumeration();
    TestRegistrationInfo();

    Actual = 0xdeadbeef;
    ok_long(Perf.EnumerateCounterSetInstances(NULL, &LocalSetGuid, NULL, 0, &Actual), ERROR_WMI_INSTANCE_NOT_FOUND);

    First = Perf.CreateInstance(Provider, &LocalSetGuid, L"First", 7);
    Second = Perf.CreateInstance(Provider, &LocalSetGuid, L"Second", 9);
    Single = Perf.CreateInstance(Provider, &LocalSingleGuid, L"Only", 0);
    ok(First && Second && Single, "PerfCreateInstance failed: %p %p %p\n", First, Second, Single);
    if (!First || !Second || !Single)
    {
        Perf.StopProvider(Provider);
        return;
    }
    ok_long(Perf.SetULongCounterValue(Provider, First, 1, 0x11), ERROR_SUCCESS);
    ok_long(Perf.SetULongLongCounterValue(Provider, First, 2, 0x1000000022ULL), ERROR_SUCCESS);
    ok_long(Perf.SetCounterRefValue(Provider, First, 3, &LocalRef1), ERROR_SUCCESS);
    ok_long(Perf.SetULongCounterValue(Provider, Second, 1, 0x21), ERROR_SUCCESS);
    ok_long(Perf.SetULongLongCounterValue(Provider, Second, 2, 0x22), ERROR_SUCCESS);
    ok_long(Perf.SetCounterRefValue(Provider, Second, 3, &LocalRef2), ERROR_SUCCESS);
    ok_long(Perf.SetULongCounterValue(Provider, Single, 5, 0x55), ERROR_SUCCESS);

    TestInstanceEnumeration();
    TestLocalQuery(Provider, First, Second);
}

static ULONG WINAPI
StopCallback(ULONG RequestCode, PVOID Buffer, ULONG BufferSize)
{
    if (RequestCode == PERF_ENUM_INSTANCES && InterlockedIncrement(&StopCalls) == 1)
        StopResult = Perf.StopProvider(StopProvider);
    return ERROR_SUCCESS;
}

static PVOID CALLBACK
StopAlloc(SIZE_T Size, PVOID Context)
{
    InterlockedIncrement(&StopAllocations);
    return HeapAlloc(GetProcessHeap(), 0, Size);
}

static void CALLBACK
StopFree(PVOID Block, PVOID Context)
{
    InterlockedIncrement(&StopFrees);
    HeapFree(GetProcessHeap(), 0, Block);
}

static void
TestStopInCallback(void)
{
    PERF_PROVIDER_CONTEXT Context;
    COUNTER_TEMPLATE Template;
    DWORD Actual = 0xdeadbeef, Start, Elapsed;
    ULONG Ret;

    memset(&Context, 0, sizeof(Context));
    Context.ContextSize = sizeof(Context);
    Context.ControlCallback = StopCallback;
    Context.MemAllocRoutine = StopAlloc;
    Context.MemFreeRoutine = StopFree;
    Ret = Perf.StartProviderEx(&StopProviderGuid, &Context, &StopProvider);
    ok_long(Ret, ERROR_SUCCESS);
    if (Ret != ERROR_SUCCESS)
        return;
    InitTemplate(&Template, &StopProviderGuid, &StopSetGuid, PERF_COUNTERSET_MULTI_INSTANCES, 1, 1);
    ok_long(Perf.SetCounterSetInfo(StopProvider, &Template.CounterSet, TemplateSize(1)), ERROR_SUCCESS);
    ok(Perf.CreateInstance(StopProvider, &StopSetGuid, L"Stopped", 1) != NULL, "PerfCreateInstance failed\n");
    ok(HasCounterSet(&StopSetGuid), "The counter set is not enumerated\n");

    Start = GetTickCount();
    Ret = Perf.EnumerateCounterSetInstances(NULL, &StopSetGuid, NULL, 0, &Actual);
    Elapsed = GetTickCount() - Start;
    ok_long(Ret, ERROR_WMI_GUID_NOT_FOUND);
    ok_long(StopCalls, 1);
    ok_long(StopResult, ERROR_SUCCESS);
    ok(Elapsed < 1000, "The request that stopped the provider took %lu ms\n", Elapsed);
    ok_long(StopAllocations, 1);
    ok_long(StopFrees, 1);
    ok(!HasCounterSet(&StopSetGuid), "The counter set is still enumerated after PerfStopProvider in its callback\n");
    ok_long(Perf.EnumerateCounterSetInstances(NULL, &StopSetGuid, NULL, 0, &Actual), ERROR_WMI_GUID_NOT_FOUND);
    ok_long(StopCalls, 1);
}

static ULONG WINAPI
ChildCallback(ULONG RequestCode, PVOID Buffer, ULONG BufferSize)
{
    const WNODE_HEADER *Wnode = Buffer;
    const PERF_COUNTER_IDENTITY *Identity;
    const BYTE *Payload;

    if (RequestCode < ARRAYSIZE(ChildShared->Calls))
        InterlockedIncrement(&ChildShared->Calls[RequestCode]);
    if (!Buffer || BufferSize < sizeof(*Wnode))
        return ERROR_SUCCESS;
    ChildShared->WnodeSize = Wnode->BufferSize;
    ChildShared->BufferSize = BufferSize;
    Payload = (const BYTE *)Buffer + sizeof(*Wnode);
    if (RequestCode == PERF_ADD_COUNTER || RequestCode == PERF_REMOVE_COUNTER)
    {
        if (BufferSize < sizeof(*Wnode) + sizeof(*Identity))
            return ERROR_SUCCESS;
        Identity = (const PERF_COUNTER_IDENTITY *)Payload;
        ChildShared->Identity = *Identity;
        ChildShared->Machine[0] = 0;
        ChildShared->Name[0] = 0;
        if (Identity->MachineOffset && Identity->MachineOffset < BufferSize - sizeof(*Wnode))
            StringCchCopyW(ChildShared->Machine, ARRAYSIZE(ChildShared->Machine),
                           (const WCHAR *)(Payload + Identity->MachineOffset));
        if (Identity->NameOffset && Identity->NameOffset < BufferSize - sizeof(*Wnode))
            StringCchCopyW(ChildShared->Name, ARRAYSIZE(ChildShared->Name),
                           (const WCHAR *)(Payload + Identity->NameOffset));
        return (RequestCode == PERF_ADD_COUNTER) ? (ULONG)ChildShared->AddResult : ERROR_SUCCESS;
    }
    StringCchCopyW(ChildShared->Machine, ARRAYSIZE(ChildShared->Machine), (const WCHAR *)Payload);
    return ERROR_SUCCESS;
}

static void
ChildCheck(BOOL Condition)
{
    if (!Condition)
        InterlockedIncrement(&ChildShared->Failures);
}

static void
RunProvider(DWORD ParentId)
{
    PERF_COUNTERSET_INSTANCE *Alpha, *Beta, *Gamma, *Single;
    PERF_PROVIDER_CONTEXT Context;
    COUNTER_TEMPLATE Template;
    HANDLE Mapping, Go, Ready, Provider;
    WCHAR Name[64];

    StringCchPrintfW(Name, ARRAYSIZE(Name), L"PerfConsumerShared_%lu", ParentId);
    Mapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE, Name);
    if (!Mapping)
        return;
    ChildShared = MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, sizeof(*ChildShared));
    StringCchPrintfW(Name, ARRAYSIZE(Name), L"PerfConsumerGo_%lu", ParentId);
    Go = OpenEventW(SYNCHRONIZE, FALSE, Name);
    StringCchPrintfW(Name, ARRAYSIZE(Name), L"PerfConsumerReady_%lu", ParentId);
    Ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, Name);
    if (!ChildShared || !Go || !Ready)
        return;

    memset(&Context, 0, sizeof(Context));
    Context.ContextSize = sizeof(Context);
    Context.ControlCallback = ChildCallback;
    ChildCheck(Perf.StartProviderEx(&ChildProviderGuid, &Context, &Provider) == ERROR_SUCCESS);
    InitTemplate(&Template, &ChildProviderGuid, &ChildSetGuid, PERF_COUNTERSET_MULTI_INSTANCES, 1, 3);
    ChildCheck(Perf.SetCounterSetInfo(Provider, &Template.CounterSet, TemplateSize(3)) == ERROR_SUCCESS);
    InitTemplate(&Template, &ChildProviderGuid, &ChildSingleGuid, PERF_COUNTERSET_SINGLE_INSTANCE, 10, 1);
    ChildCheck(Perf.SetCounterSetInfo(Provider, &Template.CounterSet, TemplateSize(1)) == ERROR_SUCCESS);
    Alpha = Perf.CreateInstance(Provider, &ChildSetGuid, L"Alpha", 1);
    Beta = Perf.CreateInstance(Provider, &ChildSetGuid, L"Beta", 2);
    Single = Perf.CreateInstance(Provider, &ChildSingleGuid, L"Only", 0);
    ChildCheck(Alpha && Beta && Single);
    if (Alpha && Beta && Single)
    {
        ChildCheck(Perf.SetULongCounterValue(Provider, Alpha, 1, 11) == ERROR_SUCCESS);
        ChildCheck(Perf.SetULongLongCounterValue(Provider, Alpha, 2, 0x100000002ULL) == ERROR_SUCCESS);
        ChildCheck(Perf.SetCounterRefValue(Provider, Alpha, 3, &ChildRef1) == ERROR_SUCCESS);
        ChildCheck(Perf.SetULongCounterValue(Provider, Beta, 1, 21) == ERROR_SUCCESS);
        ChildCheck(Perf.SetULongLongCounterValue(Provider, Beta, 2, 22) == ERROR_SUCCESS);
        ChildCheck(Perf.SetCounterRefValue(Provider, Beta, 3, &ChildRef2) == ERROR_SUCCESS);
        ChildCheck(Perf.SetULongCounterValue(Provider, Single, 10, 77) == ERROR_SUCCESS);
    }
    SetEvent(Ready);
    if (WaitForSingleObject(Go, 10000) != WAIT_OBJECT_0)
        return;

    if (Alpha && Beta)
    {
        ChildCheck(Perf.SetULongCounterValue(Provider, Alpha, 1, 12) == ERROR_SUCCESS);
        ChildRef1 = 0x300000004ULL;
        ChildCheck(Perf.DeleteInstance(Provider, Beta) == ERROR_SUCCESS);
    }
    Gamma = Perf.CreateInstance(Provider, &ChildSetGuid, L"Gamma", 3);
    ChildCheck(Gamma != NULL);
    if (Gamma)
    {
        ChildCheck(Perf.SetULongCounterValue(Provider, Gamma, 1, 31) == ERROR_SUCCESS);
        ChildCheck(Perf.SetULongLongCounterValue(Provider, Gamma, 2, 32) == ERROR_SUCCESS);
    }
    SetEvent(Ready);
    if (WaitForSingleObject(Go, 10000) != WAIT_OBJECT_0)
        return;

    ChildCheck(Perf.StopProvider(Provider) == ERROR_SUCCESS);
    SetEvent(Ready);
    WaitForSingleObject(Go, 10000);
}

static void
CheckIdentity(const GUID *CounterSet, ULONG CounterId, ULONG InstanceId, const WCHAR *Name)
{
    ULONG Machine = (ULONG)(wcslen(ComputerName) + 1) * sizeof(WCHAR);
    ULONG NameSize = Name ? (ULONG)(wcslen(Name) + 1) * sizeof(WCHAR) : 0;
    const PERF_COUNTER_IDENTITY *Identity = &ChildShared->Identity;

    ok(IsEqualGUID(&Identity->CounterSetGuid, CounterSet), "Identity: wrong counter set GUID\n");
    ok_long(Identity->CounterId, CounterId);
    ok_long(Identity->InstanceId, InstanceId);
    ok_long(Identity->BufferSize, sizeof(*Identity) + Machine + NameSize);
    ok_long(Identity->MachineOffset, sizeof(*Identity));
    ok(!wcscmp(ChildShared->Machine, ComputerName), "Identity machine %s, expected %s\n",
       wine_dbgstr_w(ChildShared->Machine), wine_dbgstr_w(ComputerName));
    if (Name)
    {
        ok_long(Identity->NameOffset, sizeof(*Identity) + Machine);
        ok(!wcscmp(ChildShared->Name, Name), "Identity name %s, expected %s\n", wine_dbgstr_w(ChildShared->Name),
           wine_dbgstr_w(Name));
    }
    else
    {
        ok_long(Identity->NameOffset, 0);
    }
    ok_long(ChildShared->WnodeSize, sizeof(WNODE_HEADER) + sizeof(*Identity) + Machine + NameSize);
    ok_long(ChildShared->BufferSize, ChildShared->WnodeSize);
}

static void
TestChildProvider(void)
{
    WCHAR CommandLine[MAX_PATH * 2], Module[MAX_PATH], Name[64];
    const PERF_MULTI_INSTANCES *Multi;
    const PERF_INSTANCE_HEADER *Instance;
    const PERF_COUNTER_DATA *Data;
    PERF_DATA_HEADER *Result;
    PROCESS_INFORMATION Process;
    STARTUPINFOW Startup = { sizeof(Startup) };
    HANDLE Mapping, Go, Ready, Query = NULL;
    BYTE Buffer[96];
    GUID Guid;
    DWORD Actual, Size, ExitCode;
    ULONG Ret, Status;

    StringCchPrintfW(Name, ARRAYSIZE(Name), L"PerfConsumerShared_%lu", GetCurrentProcessId());
    Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*ChildShared), Name);
    ok(Mapping != NULL, "CreateFileMappingW failed with %lu\n", GetLastError());
    if (!Mapping)
        return;
    ChildShared = MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, sizeof(*ChildShared));
    StringCchPrintfW(Name, ARRAYSIZE(Name), L"PerfConsumerGo_%lu", GetCurrentProcessId());
    Go = CreateEventW(NULL, FALSE, FALSE, Name);
    StringCchPrintfW(Name, ARRAYSIZE(Name), L"PerfConsumerReady_%lu", GetCurrentProcessId());
    Ready = CreateEventW(NULL, FALSE, FALSE, Name);
    ok(ChildShared && Go && Ready, "Shared objects missing\n");
    if (!ChildShared || !Go || !Ready)
        return;

    GetModuleFileNameW(NULL, Module, ARRAYSIZE(Module));
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" PerfConsumer provider %lu", Module,
                     GetCurrentProcessId());
    if (!CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
    {
        ok(FALSE, "CreateProcessW failed with %lu\n", GetLastError());
        return;
    }
    if (WaitForSingleObject(Ready, 5000) != WAIT_OBJECT_0)
    {
        ok(FALSE, "The provider process did not start\n");
        TerminateProcess(Process.hProcess, 1);
        CloseHandle(Process.hProcess);
        CloseHandle(Process.hThread);
        return;
    }

    ok(HasCounterSet(&ChildSetGuid), "The counter set of the other process is not enumerated\n");
    ok(HasCounterSet(&ChildSingleGuid), "The single counter set of the other process is not enumerated\n");
    memset(&Guid, 0, sizeof(Guid));
    ok_long(Perf.QueryCounterSetRegistrationInfo(NULL, &ChildSetGuid, PERF_REG_PROVIDER_GUID, 0,
                                                 (BYTE *)&Guid, sizeof(Guid), &Actual), ERROR_SUCCESS);
    ok(IsEqualGUID(&Guid, &ChildProviderGuid), "Wrong provider GUID\n");

    Actual = 0xdeadbeef;
    Ret = Perf.EnumerateCounterSetInstances(NULL, &ChildSetGuid, (PERF_INSTANCE_HEADER *)Buffer, sizeof(Buffer),
                                            &Actual);
    ok_long(Ret, ERROR_SUCCESS);
    ok_long(Actual, 48);
    if (Ret == ERROR_SUCCESS)
    {
        CheckInstanceHeader((PERF_INSTANCE_HEADER *)Buffer, 1, L"Alpha", "Alpha");
        CheckInstanceHeader((PERF_INSTANCE_HEADER *)(Buffer + 24), 2, L"Beta", "Beta");
    }
    ok_long(ChildShared->Calls[PERF_ENUM_INSTANCES], 1);
    ok(!wcscmp(ChildShared->Machine, ComputerName), "Enumeration machine %s\n", wine_dbgstr_w(ChildShared->Machine));
    ok_long(ChildShared->WnodeSize, sizeof(WNODE_HEADER) + (wcslen(ComputerName) + 1) * sizeof(WCHAR));

    ok_long(Perf.OpenQueryHandle(NULL, &Query), ERROR_SUCCESS);
    ok_long(AddSpec(Query, &ChildSetGuid, PERF_WILDCARD_COUNTER, PERF_WILDCARD_COUNTER, L"*", &Status),
            ERROR_SUCCESS);
    ok_long(Status, ERROR_SUCCESS);
    ok_long(ChildShared->Calls[PERF_ADD_COUNTER], 1);
    CheckIdentity(&ChildSetGuid, PERF_WILDCARD_COUNTER, PERF_WILDCARD_COUNTER, L"*");
    ok_long(AddSpec(Query, &ChildSingleGuid, 10, PERF_WILDCARD_COUNTER, NULL, &Status), ERROR_SUCCESS);
    ok_long(Status, ERROR_SUCCESS);
    ok_long(ChildShared->Calls[PERF_ADD_COUNTER], 2);
    CheckIdentity(&ChildSingleGuid, 10, PERF_WILDCARD_COUNTER, NULL);
    ok_long(AddSpec(Query, &ChildSetGuid, 1, PERF_WILDCARD_COUNTER, L"*", &Status), ERROR_SUCCESS);
    ok_long(Status, ERROR_SUCCESS);
    ok_long(AddSpec(Query, &ChildSetGuid, 3, 1, L"Alpha", &Status), ERROR_SUCCESS);
    ok_long(Status, ERROR_SUCCESS);
    ok_long(ChildShared->Calls[PERF_ADD_COUNTER], 4);
    CheckIdentity(&ChildSetGuid, 3, 1, L"Alpha");
    ChildShared->AddResult = ERROR_ACCESS_DENIED;
    ok_long(AddSpec(Query, &ChildSetGuid, 2, 2, L"Beta", &Status), ERROR_SUCCESS);
    ok_long(Status, ERROR_ACCESS_DENIED);
    ChildShared->AddResult = ERROR_SUCCESS;
    ok_long(ChildShared->Calls[PERF_ADD_COUNTER], 5);
    ok_long(Perf.QueryCounterInfo(Query, NULL, 0, &Actual), ERROR_NOT_ENOUGH_MEMORY);
    ok_long(Actual, 48 + 40 + 48 + 56);

    Result = QueryData(Query, &Size);
    ok_long(ChildShared->Calls[PERF_COLLECT_START], 2);
    ok_long(ChildShared->Calls[PERF_COLLECT_END], 2);
    if (Result)
    {
        ok_long(Result->dwNumCounters, 4);
        Multi = CheckResult(Result, 0, PERF_COUNTERSET, 2, "Every counter of the other process");
        if (Multi && Multi->dwInstances == 2)
        {
            Instance = FindInstance(Multi, 3, L"Alpha");
            ok(Instance != NULL, "Alpha is missing\n");
            if (Instance)
            {
                CheckInstanceHeader(Instance, 1, L"Alpha", "Alpha");
                Data = InstanceCounters(Instance);
                CheckCounterData(Data, 4, 11, "Counter 1 of Alpha");
                Data = NextCounter(Data);
                CheckCounterData(Data, 8, 0x100000002ULL, "Counter 2 of Alpha");
                Data = NextCounter(Data);
                CheckCounterData(Data, 8, 0x300000003ULL, "Counter 3 of Alpha");
            }
            Instance = FindInstance(Multi, 3, L"Beta");
            ok(Instance != NULL, "Beta is missing\n");
            if (Instance)
            {
                Data = InstanceCounters(Instance);
                CheckCounterData(Data, 4, 21, "Counter 1 of Beta");
                Data = NextCounter(Data);
                CheckCounterData(Data, 8, 22, "Counter 2 of Beta");
                Data = NextCounter(Data);
                CheckCounterData(Data, 8, 0x23, "Counter 3 of Beta");
            }
        }
        CheckResult(Result, 1, PERF_SINGLE_COUNTER, 0, "Single counter of the other process");
        if (GetResult(Result, 1) && GetResult(Result, 1)->dwType == PERF_SINGLE_COUNTER)
            CheckCounterData((const PERF_COUNTER_DATA *)(GetResult(Result, 1) + 1), 4, 77, "Counter 10");
        HeapFree(GetProcessHeap(), 0, Result);
    }

    SetEvent(Go);
    ok(WaitForSingleObject(Ready, 5000) == WAIT_OBJECT_0, "The provider process did not update its instances\n");
    Result = QueryData(Query, &Size);
    ok_long(ChildShared->Calls[PERF_COLLECT_START], 4);
    if (Result)
    {
        CheckError(Result, 0, ERROR_NO_DATA, "Gamma has a null reference");
        CheckResult(Result, 1, PERF_SINGLE_COUNTER, 0, "Single counter after the update");
        Multi = CheckResult(Result, 2, PERF_MULTIPLE_INSTANCES, 2, "Counter 1 after the update");
        if (Multi && Multi->dwInstances == 2)
        {
            Instance = FindInstance(Multi, 1, L"Alpha");
            ok(Instance != NULL, "Alpha is missing\n");
            if (Instance)
                CheckCounterData(InstanceCounters(Instance), 4, 12, "Updated counter 1 of Alpha");
            Instance = FindInstance(Multi, 1, L"Gamma");
            ok(Instance != NULL, "Gamma is missing\n");
            if (Instance)
            {
                CheckInstanceHeader(Instance, 3, L"Gamma", "Gamma");
                CheckCounterData(InstanceCounters(Instance), 4, 31, "Counter 1 of Gamma");
            }
            ok(FindInstance(Multi, 1, L"Beta") == NULL, "Beta is still reported after PerfDeleteInstance\n");
        }
        Multi = CheckResult(Result, 3, PERF_MULTIPLE_INSTANCES, 1, "Counter 3 of Alpha");
        if (Multi && Multi->dwInstances == 1)
            CheckCounterData(InstanceCounters((const PERF_INSTANCE_HEADER *)(Multi + 1)), 8, 0x300000004ULL,
                             "Updated reference of Alpha");
        HeapFree(GetProcessHeap(), 0, Result);
    }
    Actual = 0xdeadbeef;
    Ret = Perf.EnumerateCounterSetInstances(NULL, &ChildSetGuid, (PERF_INSTANCE_HEADER *)Buffer, sizeof(Buffer),
                                            &Actual);
    ok_long(Ret, ERROR_SUCCESS);
    ok_long(Actual, 48);
    if (Ret == ERROR_SUCCESS)
    {
        CheckInstanceHeader((PERF_INSTANCE_HEADER *)Buffer, 1, L"Alpha", "Alpha after the update");
        CheckInstanceHeader((PERF_INSTANCE_HEADER *)(Buffer + 24), 3, L"Gamma", "Gamma after the update");
    }
    ok_long(DeleteSpec(Query, &ChildSetGuid, 1, PERF_WILDCARD_COUNTER, L"*", &Status), ERROR_SUCCESS);
    ok_long(Status, ERROR_SUCCESS);
    ok_long(ChildShared->Calls[PERF_REMOVE_COUNTER], 1);
    CheckIdentity(&ChildSetGuid, 1, PERF_WILDCARD_COUNTER, L"*");

    SetEvent(Go);
    ok(WaitForSingleObject(Ready, 5000) == WAIT_OBJECT_0, "The provider process did not stop its provider\n");
    ok(!HasCounterSet(&ChildSetGuid), "The counter set is still enumerated after PerfStopProvider\n");
    ok_long(Perf.EnumerateCounterSetInstances(NULL, &ChildSetGuid, NULL, 0, &Actual), ERROR_WMI_GUID_NOT_FOUND);
    Result = QueryData(Query, &Size);
    if (Result)
    {
        ok_long(Result->dwNumCounters, 3);
        CheckError(Result, 0, ERROR_WMI_GUID_NOT_FOUND, "Every counter after PerfStopProvider");
        CheckError(Result, 1, ERROR_WMI_GUID_NOT_FOUND, "Single counter after PerfStopProvider");
        CheckError(Result, 2, ERROR_WMI_GUID_NOT_FOUND, "Counter 3 after PerfStopProvider");
        HeapFree(GetProcessHeap(), 0, Result);
    }
    ok_long(Perf.CloseQueryHandle(Query), ERROR_SUCCESS);

    SetEvent(Go);
    ok(WaitForSingleObject(Process.hProcess, 5000) == WAIT_OBJECT_0, "The provider process did not exit\n");
    ok(GetExitCodeProcess(Process.hProcess, &ExitCode) && ExitCode == 0, "Exit code %lu\n", ExitCode);
    ok_long(ChildShared->Failures, 0);
    CloseHandle(Process.hProcess);
    CloseHandle(Process.hThread);
    UnmapViewOfFile(ChildShared);
    CloseHandle(Mapping);
    CloseHandle(Go);
    CloseHandle(Ready);
}

static BOOL
LoadFunctions(void)
{
    HMODULE Advapi32 = LoadLibraryW(L"advapi32.dll");

    if (!Advapi32)
        return FALSE;
    Perf.OpenQueryHandle = (PPERF_OPEN_QUERY_HANDLE)GetProcAddress(Advapi32, "PerfOpenQueryHandle");
    Perf.CloseQueryHandle = (PPERF_CLOSE_QUERY_HANDLE)GetProcAddress(Advapi32, "PerfCloseQueryHandle");
    Perf.AddCounters = (PPERF_COUNTERS)GetProcAddress(Advapi32, "PerfAddCounters");
    Perf.DeleteCounters = (PPERF_COUNTERS)GetProcAddress(Advapi32, "PerfDeleteCounters");
    Perf.QueryCounterInfo = (PPERF_QUERY_COUNTER_INFO)GetProcAddress(Advapi32, "PerfQueryCounterInfo");
    Perf.QueryCounterData = (PPERF_QUERY_COUNTER_DATA)GetProcAddress(Advapi32, "PerfQueryCounterData");
    Perf.EnumerateCounterSet = (PPERF_ENUMERATE_COUNTER_SET)GetProcAddress(Advapi32, "PerfEnumerateCounterSet");
    Perf.EnumerateCounterSetInstances =
        (PPERF_ENUMERATE_INSTANCES)GetProcAddress(Advapi32, "PerfEnumerateCounterSetInstances");
    Perf.QueryCounterSetRegistrationInfo =
        (PPERF_QUERY_REGISTRATION_INFO)GetProcAddress(Advapi32, "PerfQueryCounterSetRegistrationInfo");
    Perf.StartProviderEx = (PPERF_START_PROVIDER_EX)GetProcAddress(Advapi32, "PerfStartProviderEx");
    Perf.StopProvider = (PPERF_STOP_PROVIDER)GetProcAddress(Advapi32, "PerfStopProvider");
    Perf.SetCounterSetInfo = (PPERF_SET_COUNTER_SET_INFO)GetProcAddress(Advapi32, "PerfSetCounterSetInfo");
    Perf.CreateInstance = (PPERF_CREATE_INSTANCE)GetProcAddress(Advapi32, "PerfCreateInstance");
    Perf.DeleteInstance = (PPERF_DELETE_INSTANCE)GetProcAddress(Advapi32, "PerfDeleteInstance");
    Perf.SetCounterRefValue = (PPERF_SET_COUNTER_REF_VALUE)GetProcAddress(Advapi32, "PerfSetCounterRefValue");
    Perf.SetULongCounterValue = (PPERF_ULONG_VALUE)GetProcAddress(Advapi32, "PerfSetULongCounterValue");
    Perf.SetULongLongCounterValue = (PPERF_ULONGLONG_VALUE)GetProcAddress(Advapi32, "PerfSetULongLongCounterValue");

    return Perf.OpenQueryHandle && Perf.CloseQueryHandle && Perf.AddCounters && Perf.DeleteCounters &&
           Perf.QueryCounterInfo && Perf.QueryCounterData && Perf.EnumerateCounterSet &&
           Perf.EnumerateCounterSetInstances && Perf.QueryCounterSetRegistrationInfo && Perf.StartProviderEx &&
           Perf.StopProvider && Perf.SetCounterSetInfo && Perf.CreateInstance && Perf.DeleteInstance &&
           Perf.SetCounterRefValue && Perf.SetULongCounterValue && Perf.SetULongLongCounterValue;
}

START_TEST(PerfConsumer)
{
    DWORD Size = ARRAYSIZE(ComputerName);
    char **Arguments;

    if (winetest_get_mainargs(&Arguments) >= 4 && !strcmp(Arguments[2], "provider"))
    {
        if (LoadFunctions())
            RunProvider(strtoul(Arguments[3], NULL, 10));
        return;
    }

    if (!GetComputerNameW(ComputerName, &Size))
        ComputerName[0] = 0;
    if (!LoadFunctions())
    {
        skip("The PERFLIB V2 consumer functions are not exported by advapi32\n");
        return;
    }
    TestLocalProvider();
    TestStopInCallback();
    TestChildProvider();
}
