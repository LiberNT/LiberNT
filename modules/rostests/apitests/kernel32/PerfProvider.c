/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the PERFLIB V2 provider functions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winperf.h>
#include <perflib.h>

typedef ULONG (WINAPI *PPERF_START_PROVIDER)(LPGUID, PERFLIBREQUEST, PHANDLE);
typedef ULONG (WINAPI *PPERF_START_PROVIDER_EX)(LPGUID, PPERF_PROVIDER_CONTEXT, PHANDLE);
typedef ULONG (WINAPI *PPERF_STOP_PROVIDER)(HANDLE);
typedef ULONG (WINAPI *PPERF_SET_COUNTER_SET_INFO)(HANDLE, PPERF_COUNTERSET_INFO, ULONG);
typedef PPERF_COUNTERSET_INSTANCE (WINAPI *PPERF_CREATE_INSTANCE)(HANDLE, LPCGUID, PCWSTR, ULONG);
typedef ULONG (WINAPI *PPERF_DELETE_INSTANCE)(HANDLE, PPERF_COUNTERSET_INSTANCE);
typedef PPERF_COUNTERSET_INSTANCE (WINAPI *PPERF_QUERY_INSTANCE)(HANDLE, LPCGUID, PCWSTR, ULONG);
typedef ULONG (WINAPI *PPERF_SET_COUNTER_REF_VALUE)(HANDLE, PPERF_COUNTERSET_INSTANCE, ULONG, PVOID);
typedef ULONG (WINAPI *PPERF_ULONG_VALUE)(HANDLE, PPERF_COUNTERSET_INSTANCE, ULONG, ULONG);
typedef ULONG (WINAPI *PPERF_ULONGLONG_VALUE)(HANDLE, PPERF_COUNTERSET_INSTANCE, ULONG, ULONGLONG);

typedef struct _PERF_FUNCTIONS
{
    PPERF_START_PROVIDER StartProvider;
    PPERF_START_PROVIDER_EX StartProviderEx;
    PPERF_STOP_PROVIDER StopProvider;
    PPERF_SET_COUNTER_SET_INFO SetCounterSetInfo;
    PPERF_CREATE_INSTANCE CreateInstance;
    PPERF_DELETE_INSTANCE DeleteInstance;
    PPERF_QUERY_INSTANCE QueryInstance;
    PPERF_SET_COUNTER_REF_VALUE SetCounterRefValue;
    PPERF_ULONG_VALUE SetULongCounterValue;
    PPERF_ULONGLONG_VALUE SetULongLongCounterValue;
    PPERF_ULONG_VALUE IncrementULongCounterValue;
    PPERF_ULONGLONG_VALUE IncrementULongLongCounterValue;
    PPERF_ULONG_VALUE DecrementULongCounterValue;
    PPERF_ULONGLONG_VALUE DecrementULongLongCounterValue;
} PERF_FUNCTIONS;

typedef struct _COUNTER_TEMPLATE
{
    PERF_COUNTERSET_INFO CounterSet;
    PERF_COUNTER_INFO Counter[2];
} COUNTER_TEMPLATE;

static GUID ProviderGuid = { 0x3c5f7a31, 0x41b2, 0x4d0e, { 0x9a, 0x61, 0x2b, 0x7e, 0x15, 0x0c, 0x6d, 0x01 } };
static GUID CounterSetGuid = { 0x3c5f7a31, 0x41b2, 0x4d0e, { 0x9a, 0x61, 0x2b, 0x7e, 0x15, 0x0c, 0x6d, 0x02 } };
static GUID CounterSetGuid2 = { 0x3c5f7a31, 0x41b2, 0x4d0e, { 0x9a, 0x61, 0x2b, 0x7e, 0x15, 0x0c, 0x6d, 0x03 } };

static LONG Allocations;
static LONG Frees;
static PVOID Blocks[8];
static SIZE_T BlockSizes[8];
static PVOID AllocatorContext = (PVOID)(ULONG_PTR)0x1234;
static BOOL BadContext;

static ULONG WINAPI
ControlCallback(ULONG RequestCode, PVOID Buffer, ULONG BufferSize)
{
    ok(FALSE, "The control callback was called with request %lu\n", RequestCode);
    return ERROR_SUCCESS;
}

static PVOID CALLBACK
TestAlloc(SIZE_T Size, PVOID Context)
{
    PVOID Block = HeapAlloc(GetProcessHeap(), 0, Size);

    if (Context != AllocatorContext)
        BadContext = TRUE;
    if (Block && Allocations < (LONG)ARRAYSIZE(Blocks))
    {
        Blocks[Allocations] = Block;
        BlockSizes[Allocations] = Size;
    }
    Allocations++;
    return Block;
}

static VOID CALLBACK
TestFree(PVOID Block, PVOID Context)
{
    if (Context != AllocatorContext)
        BadContext = TRUE;
    Frees++;
    HeapFree(GetProcessHeap(), 0, Block);
}

static BOOL
IsInsideAllocatedBlock(PVOID Pointer, SIZE_T Size)
{
    LONG Index;

    for (Index = 0; Index < Allocations && Index < (LONG)ARRAYSIZE(Blocks); Index++)
    {
        if ((ULONG_PTR)Pointer >= (ULONG_PTR)Blocks[Index] &&
            (ULONG_PTR)Pointer + Size <= (ULONG_PTR)Blocks[Index] + BlockSizes[Index])
            return TRUE;
    }
    return FALSE;
}

static VOID
InitTemplate(COUNTER_TEMPLATE *Template, ULONG Type, ULONGLONG Attrib)
{
    ULONG Index;

    ZeroMemory(Template, sizeof(*Template));
    Template->CounterSet.CounterSetGuid = CounterSetGuid;
    Template->CounterSet.ProviderGuid = ProviderGuid;
    Template->CounterSet.NumCounters = 2;
    Template->CounterSet.InstanceType = PERF_COUNTERSET_MULTI_INSTANCES;
    for (Index = 0; Index < 2; Index++)
    {
        Template->Counter[Index].CounterId = Index + 1;
        Template->Counter[Index].Type = Type;
        Template->Counter[Index].Attrib = Attrib;
        Template->Counter[Index].Size = sizeof(PERF_COUNTER_INFO);
        Template->Counter[Index].DetailLevel = PERF_DETAIL_NOVICE;
        Template->Counter[Index].Offset = 0xdeadbeef;
    }
}

static ULONG
CounterULong(PPERF_COUNTERSET_INSTANCE Instance, ULONG Index)
{
    return *(ULONG *)((BYTE *)(Instance + 1) + Index * sizeof(UINT64));
}

static ULONGLONG
CounterULongLong(PPERF_COUNTERSET_INSTANCE Instance, ULONG Index)
{
    return *(ULONGLONG *)((BYTE *)(Instance + 1) + Index * sizeof(UINT64));
}

static VOID
TestProviderStart(const PERF_FUNCTIONS *Perf, PCSTR Module)
{
    PERF_PROVIDER_CONTEXT Context;
    HANDLE Provider, Provider2, Duplicate;
    ULONG Ret;

    Provider = (HANDLE)0xdeadbeef;
    Ret = Perf->StartProvider(NULL, ControlCallback, &Provider);
    ok(Ret == ERROR_INVALID_PARAMETER, "%s: PerfStartProvider(NULL) returned %lu\n", Module, Ret);
    ok(Provider == (HANDLE)0xdeadbeef, "%s: the provider handle was written\n", Module);
    Ret = Perf->StartProvider(&ProviderGuid, ControlCallback, NULL);
    ok(Ret == ERROR_INVALID_PARAMETER, "%s: PerfStartProvider(NULL handle) returned %lu\n", Module, Ret);

    Ret = Perf->StartProvider(&ProviderGuid, ControlCallback, &Provider);
    ok(Ret == ERROR_SUCCESS, "%s: PerfStartProvider returned %lu\n", Module, Ret);
    if (Ret != ERROR_SUCCESS)
        return;
    Provider2 = Provider;
    Ret = Perf->StartProvider(&ProviderGuid, ControlCallback, &Provider2);
    ok(Ret == ERROR_SUCCESS, "%s: second PerfStartProvider returned %lu\n", Module, Ret);
    ok(Provider2 != Provider, "%s: the same provider handle was returned twice\n", Module);
    if (Ret == ERROR_SUCCESS)
        ok_long(Perf->StopProvider(Provider2), ERROR_SUCCESS);

    SetLastError(0xdeadbeef);
    ok(!DuplicateHandle(GetCurrentProcess(), Provider, GetCurrentProcess(), &Duplicate, 0, FALSE,
                        DUPLICATE_SAME_ACCESS), "%s: the provider handle is a kernel handle\n", Module);
    ok_long(GetLastError(), ERROR_INVALID_HANDLE);
    ok_long(Perf->StopProvider(Provider), ERROR_SUCCESS);

    ZeroMemory(&Context, sizeof(Context));
    Provider = (HANDLE)0xdeadbeef;
    Ret = Perf->StartProviderEx(&ProviderGuid, &Context, &Provider);
    ok(Ret == ERROR_INVALID_PARAMETER, "%s: PerfStartProviderEx(size 0) returned %lu\n", Module, Ret);
    ok(Provider == (HANDLE)0xdeadbeef, "%s: the provider handle was written\n", Module);
    Context.ContextSize = sizeof(Context) + 1;
    Ret = Perf->StartProviderEx(&ProviderGuid, &Context, &Provider);
    ok(Ret == ERROR_SUCCESS, "%s: PerfStartProviderEx returned %lu\n", Module, Ret);
    if (Ret == ERROR_SUCCESS)
        ok_long(Perf->StopProvider(Provider), ERROR_SUCCESS);

    Provider = (HANDLE)0xdeadbeef;
    Ret = Perf->StartProviderEx(&ProviderGuid, NULL, &Provider);
    ok(Ret == ERROR_SUCCESS, "%s: PerfStartProviderEx(NULL context) returned %lu\n", Module, Ret);
    if (Ret == ERROR_SUCCESS)
        ok_long(Perf->StopProvider(Provider), ERROR_SUCCESS);
    Context.ContextSize = sizeof(Context);
    Ret = Perf->StartProviderEx(NULL, &Context, &Provider);
    ok(Ret == ERROR_INVALID_PARAMETER, "%s: PerfStartProviderEx(NULL guid) returned %lu\n", Module, Ret);
}

static VOID
TestReferenceCounters(const PERF_FUNCTIONS *Perf, PCSTR Module)
{
    PPERF_COUNTERSET_INSTANCE Instance, Other, Found;
    COUNTER_TEMPLATE Template;
    UINT64 Counter1, Counter2;
    HANDLE Provider;
    ULONG Ret, Size;

    if (Perf->StartProvider(&ProviderGuid, ControlCallback, &Provider) != ERROR_SUCCESS)
    {
        ok(FALSE, "%s: PerfStartProvider failed\n", Module);
        return;
    }

    InitTemplate(&Template, PERF_COUNTER_COUNTER, PERF_ATTRIB_BY_REFERENCE);
    Template.CounterSet.NumCounters = 0;
    Ret = Perf->SetCounterSetInfo(Provider, &Template.CounterSet, sizeof(Template.CounterSet));
    ok(Ret == ERROR_INVALID_PARAMETER, "%s: PerfSetCounterSetInfo(0 counters) returned %lu\n", Module, Ret);
    Template.CounterSet.NumCounters = 2;
    Ret = Perf->SetCounterSetInfo(Provider, &Template.CounterSet, sizeof(Template) - 1);
    ok(Ret == ERROR_INVALID_PARAMETER, "%s: PerfSetCounterSetInfo(short template) returned %lu\n", Module, Ret);
    Ret = Perf->SetCounterSetInfo(Provider, &Template.CounterSet, sizeof(Template));
    ok(Ret == ERROR_SUCCESS, "%s: PerfSetCounterSetInfo returned %lu\n", Module, Ret);
    Template.CounterSet.CounterSetGuid = CounterSetGuid2;
    Ret = Perf->SetCounterSetInfo(Provider, &Template.CounterSet, sizeof(Template));
    ok(Ret == ERROR_SUCCESS, "%s: PerfSetCounterSetInfo(second set) returned %lu\n", Module, Ret);
    Ret = Perf->SetCounterSetInfo(Provider, &Template.CounterSet, sizeof(Template));
    ok(Ret == ERROR_ALREADY_EXISTS, "%s: PerfSetCounterSetInfo(again) returned %lu\n", Module, Ret);

    SetLastError(0xdeadbeef);
    ok(!Perf->CreateInstance(Provider, NULL, L"1", 1), "%s: instance created without a counter set\n", Module);
    ok_long(GetLastError(), ERROR_INVALID_PARAMETER);
    SetLastError(0xdeadbeef);
    ok(!Perf->CreateInstance(Provider, &ProviderGuid, L"1", 1), "%s: instance of an unknown set\n", Module);
    ok_long(GetLastError(), ERROR_NOT_FOUND);
    SetLastError(0xdeadbeef);
    ok(!Perf->CreateInstance(Provider, &CounterSetGuid, NULL, 1), "%s: instance created without a name\n", Module);
    ok_long(GetLastError(), ERROR_INVALID_PARAMETER);

    SetLastError(0xdeadbeef);
    Instance = Perf->CreateInstance(Provider, &CounterSetGuid, L"11", 1);
    ok(Instance != NULL, "%s: PerfCreateInstance failed with %lu\n", Module, GetLastError());
    if (!Instance)
    {
        Perf->StopProvider(Provider);
        return;
    }
    ok_long(GetLastError(), 0xdeadbeef);
    ok_long(Instance->InstanceId, 1);
    ok_long(Instance->InstanceNameSize, 6);
    ok(IsEqualGUID(&Instance->CounterSetGuid, &CounterSetGuid), "%s: wrong counter set GUID\n", Module);
    ok_long(Instance->InstanceNameOffset, sizeof(*Instance) + 2 * sizeof(UINT64));
    ok(!wcscmp((WCHAR *)((BYTE *)Instance + Instance->InstanceNameOffset), L"11"), "%s: wrong instance name\n",
       Module);
    Size = (sizeof(*Instance) + 2 * sizeof(UINT64) + Instance->InstanceNameSize + 7) & ~7;
    ok_long(Instance->dwSize, Size);

    ok_long(Perf->SetCounterRefValue(Provider, Instance, 1, &Counter1), ERROR_SUCCESS);
    ok_long(Perf->SetCounterRefValue(Provider, Instance, 2, &Counter2), ERROR_SUCCESS);
    ok_long(Perf->SetCounterRefValue(Provider, Instance, 0, &Counter2), ERROR_NOT_FOUND);
    ok(*(void **)(Instance + 1) == &Counter1, "%s: wrong first reference\n", Module);
    ok(*(void **)((BYTE *)(Instance + 1) + sizeof(UINT64)) == &Counter2, "%s: wrong second reference\n", Module);

    ok_long(Perf->SetULongCounterValue(Provider, Instance, 1, 666), ERROR_INVALID_PARAMETER);
    ok_long(Perf->SetULongLongCounterValue(Provider, Instance, 2, 666), ERROR_INVALID_PARAMETER);
    ok_long(Perf->IncrementULongCounterValue(Provider, Instance, 1, 1), ERROR_INVALID_PARAMETER);
    ok_long(Perf->DecrementULongLongCounterValue(Provider, Instance, 2, 1), ERROR_INVALID_PARAMETER);
    ok_long(Perf->SetULongCounterValue(Provider, Instance, 0, 42), ERROR_NOT_FOUND);
    ok_long(Perf->IncrementULongCounterValue(Provider, Instance, 0, 42), ERROR_NOT_FOUND);

    Found = Perf->QueryInstance(Provider, &CounterSetGuid, L"11", 1);
    ok(Found == Instance, "%s: PerfQueryInstance returned %p, expected %p\n", Module, Found, Instance);
    SetLastError(0xdeadbeef);
    Found = Perf->QueryInstance(Provider, &CounterSetGuid, L"11", 2);
    ok(!Found, "%s: PerfQueryInstance found an instance with another identifier\n", Module);
    trace("%s: PerfQueryInstance(missing id) error %lu\n", Module, GetLastError());
    SetLastError(0xdeadbeef);
    Found = Perf->QueryInstance(Provider, &CounterSetGuid, L"12", 1);
    ok(!Found, "%s: PerfQueryInstance found an instance with another name\n", Module);
    SetLastError(0xdeadbeef);
    Found = Perf->QueryInstance(Provider, &CounterSetGuid2, L"11", 1);
    ok(!Found, "%s: PerfQueryInstance found an instance in another counter set\n", Module);

    Other = Perf->CreateInstance(Provider, &CounterSetGuid2, L"11", 1);
    ok(Other != NULL && Other != Instance, "%s: instance of the second set failed with %lu\n", Module,
       GetLastError());
    Found = Perf->QueryInstance(Provider, &CounterSetGuid2, L"11", 1);
    ok(Found == Other, "%s: PerfQueryInstance returned %p, expected %p\n", Module, Found, Other);
    if (Other)
        ok_long(Perf->DeleteInstance(Provider, Other), ERROR_SUCCESS);
    SetLastError(0xdeadbeef);
    ok(!Perf->QueryInstance(Provider, &CounterSetGuid2, L"11", 1), "%s: deleted instance still found\n", Module);

    ok_long(Perf->DeleteInstance(Provider, Instance), ERROR_SUCCESS);
    ok_long(Perf->StopProvider(Provider), ERROR_SUCCESS);
}

static VOID
TestValueCounters(const PERF_FUNCTIONS *Perf, PCSTR Module)
{
    PPERF_COUNTERSET_INSTANCE Instance;
    COUNTER_TEMPLATE Template;
    UINT64 Counter = 0;
    HANDLE Provider;

    if (Perf->StartProvider(&ProviderGuid, ControlCallback, &Provider) != ERROR_SUCCESS)
    {
        ok(FALSE, "%s: PerfStartProvider failed\n", Module);
        return;
    }
    InitTemplate(&Template, PERF_COUNTER_COUNTER, 0);
    ok_long(Perf->SetCounterSetInfo(Provider, &Template.CounterSet, sizeof(Template)), ERROR_SUCCESS);
    Instance = Perf->CreateInstance(Provider, &CounterSetGuid, L"11", 1);
    ok(Instance != NULL, "%s: PerfCreateInstance failed with %lu\n", Module, GetLastError());
    if (Instance)
    {
        ok_long(Perf->SetULongCounterValue(Provider, Instance, 1, 666), ERROR_SUCCESS);
        ok_long(Perf->SetULongCounterValue(Provider, Instance, 2, 900000), ERROR_SUCCESS);
        ok_long(Perf->SetULongCounterValue(Provider, Instance, 0, 42), ERROR_NOT_FOUND);
        ok_long(CounterULong(Instance, 0), 666);
        ok_long(CounterULong(Instance, 1), 900000);

        ok_long(Perf->IncrementULongCounterValue(Provider, Instance, 1, 5), ERROR_SUCCESS);
        ok_long(CounterULong(Instance, 0), 671);
        ok_long(Perf->DecrementULongCounterValue(Provider, Instance, 2, 100), ERROR_SUCCESS);
        ok_long(CounterULong(Instance, 1), 899900);
        ok_long(Perf->SetULongCounterValue(Provider, Instance, 1, 0xffffffff), ERROR_SUCCESS);
        ok_long(Perf->IncrementULongCounterValue(Provider, Instance, 1, 2), ERROR_SUCCESS);
        ok_long(CounterULong(Instance, 0), 1);
        ok_long(Perf->DecrementULongCounterValue(Provider, Instance, 1, 3), ERROR_SUCCESS);
        ok_long(CounterULong(Instance, 0), 0xfffffffe);

        ok_long(Perf->SetULongLongCounterValue(Provider, Instance, 1, 1), ERROR_INVALID_PARAMETER);
        ok_long(Perf->IncrementULongLongCounterValue(Provider, Instance, 1, 1), ERROR_INVALID_PARAMETER);
        ok_long(Perf->DecrementULongLongCounterValue(Provider, Instance, 2, 1), ERROR_INVALID_PARAMETER);
        ok_long(Perf->SetCounterRefValue(Provider, Instance, 1, &Counter), ERROR_INVALID_PARAMETER);
        ok_long(Perf->DeleteInstance(Provider, Instance), ERROR_SUCCESS);
    }
    ok_long(Perf->StopProvider(Provider), ERROR_SUCCESS);

    if (Perf->StartProvider(&ProviderGuid, ControlCallback, &Provider) != ERROR_SUCCESS)
    {
        ok(FALSE, "%s: PerfStartProvider failed\n", Module);
        return;
    }
    InitTemplate(&Template, (PERF_COUNTER_COUNTER & ~PERF_SIZE_DWORD) | PERF_SIZE_LARGE, 0);
    ok_long(Perf->SetCounterSetInfo(Provider, &Template.CounterSet, sizeof(Template)), ERROR_SUCCESS);
    Instance = Perf->CreateInstance(Provider, &CounterSetGuid, L"11", 1);
    ok(Instance != NULL, "%s: PerfCreateInstance failed with %lu\n", Module, GetLastError());
    if (Instance)
    {
        ok_long(Perf->SetULongLongCounterValue(Provider, Instance, 1, 900000), ERROR_SUCCESS);
        ok_long(Perf->SetULongLongCounterValue(Provider, Instance, 2, 666), ERROR_SUCCESS);
        ok_long(Perf->SetULongLongCounterValue(Provider, Instance, 0, 42), ERROR_NOT_FOUND);
        ok(CounterULongLong(Instance, 0) == 900000, "%s: wrong first value\n", Module);
        ok(CounterULongLong(Instance, 1) == 666, "%s: wrong second value\n", Module);

        ok_long(Perf->IncrementULongLongCounterValue(Provider, Instance, 1, 0x100000000ULL), ERROR_SUCCESS);
        ok(CounterULongLong(Instance, 0) == 0x1000dbba0ULL, "%s: wrong incremented value\n", Module);
        ok_long(Perf->DecrementULongLongCounterValue(Provider, Instance, 2, 667), ERROR_SUCCESS);
        ok(CounterULongLong(Instance, 1) == ~0ULL, "%s: wrong decremented value\n", Module);
        ok_long(Perf->IncrementULongLongCounterValue(Provider, Instance, 2, 2), ERROR_SUCCESS);
        ok(CounterULongLong(Instance, 1) == 1, "%s: wrong wrapped value\n", Module);

        ok_long(Perf->SetULongCounterValue(Provider, Instance, 1, 1), ERROR_INVALID_PARAMETER);
        ok_long(Perf->IncrementULongCounterValue(Provider, Instance, 1, 1), ERROR_INVALID_PARAMETER);
        ok_long(Perf->DecrementULongCounterValue(Provider, Instance, 2, 1), ERROR_INVALID_PARAMETER);
        ok_long(Perf->IncrementULongLongCounterValue(Provider, Instance, 0, 1), ERROR_NOT_FOUND);
        ok_long(Perf->DeleteInstance(Provider, Instance), ERROR_SUCCESS);
    }
    ok_long(Perf->StopProvider(Provider), ERROR_SUCCESS);
}

static VOID
TestAllocator(const PERF_FUNCTIONS *Perf, PCSTR Module)
{
    PERF_PROVIDER_CONTEXT Context;
    PPERF_COUNTERSET_INSTANCE Instance;
    COUNTER_TEMPLATE Template;
    LONG AllocationsBefore, Outstanding;
    HANDLE Provider;
    ULONG Ret;

    Allocations = Frees = 0;
    BadContext = FALSE;
    ZeroMemory(&Context, sizeof(Context));
    Context.ContextSize = sizeof(Context);
    Context.ControlCallback = ControlCallback;
    Context.MemAllocRoutine = TestAlloc;
    Context.MemFreeRoutine = TestFree;
    Context.pMemContext = AllocatorContext;
    Ret = Perf->StartProviderEx(&ProviderGuid, &Context, &Provider);
    ok(Ret == ERROR_SUCCESS, "%s: PerfStartProviderEx returned %lu\n", Module, Ret);
    if (Ret != ERROR_SUCCESS)
        return;
    InitTemplate(&Template, PERF_COUNTER_COUNTER, 0);
    ok_long(Perf->SetCounterSetInfo(Provider, &Template.CounterSet, sizeof(Template)), ERROR_SUCCESS);

    AllocationsBefore = Allocations;
    Outstanding = Allocations - Frees;
    Instance = Perf->CreateInstance(Provider, &CounterSetGuid, L"custom", 7);
    ok(Instance != NULL, "%s: PerfCreateInstance failed with %lu\n", Module, GetLastError());
    ok(Allocations > AllocationsBefore, "%s: the allocation routine was not used\n", Module);
    if (Instance)
    {
        ok(IsInsideAllocatedBlock(Instance, Instance->dwSize), "%s: the instance is outside the allocated blocks\n",
           Module);
        ok_long(Perf->SetULongCounterValue(Provider, Instance, 2, 77), ERROR_SUCCESS);
        ok_long(CounterULong(Instance, 1), 77);
        ok_long(Perf->DeleteInstance(Provider, Instance), ERROR_SUCCESS);
        ok_long(Allocations - Frees, Outstanding);
    }
    ok(!BadContext, "%s: the memory routines got another context\n", Module);

    Instance = Perf->CreateInstance(Provider, &CounterSetGuid, L"left", 8);
    ok(Instance != NULL, "%s: PerfCreateInstance failed with %lu\n", Module, GetLastError());
    ok_long(Perf->StopProvider(Provider), ERROR_SUCCESS);
    trace("%s: %ld allocations, %ld frees after PerfStopProvider\n", Module, Allocations, Frees);
}

static BOOL
LoadFunctions(HMODULE Module, PERF_FUNCTIONS *Perf)
{
    Perf->StartProvider = (PPERF_START_PROVIDER)GetProcAddress(Module, "PerfStartProvider");
    Perf->StartProviderEx = (PPERF_START_PROVIDER_EX)GetProcAddress(Module, "PerfStartProviderEx");
    Perf->StopProvider = (PPERF_STOP_PROVIDER)GetProcAddress(Module, "PerfStopProvider");
    Perf->SetCounterSetInfo = (PPERF_SET_COUNTER_SET_INFO)GetProcAddress(Module, "PerfSetCounterSetInfo");
    Perf->CreateInstance = (PPERF_CREATE_INSTANCE)GetProcAddress(Module, "PerfCreateInstance");
    Perf->DeleteInstance = (PPERF_DELETE_INSTANCE)GetProcAddress(Module, "PerfDeleteInstance");
    Perf->QueryInstance = (PPERF_QUERY_INSTANCE)GetProcAddress(Module, "PerfQueryInstance");
    Perf->SetCounterRefValue = (PPERF_SET_COUNTER_REF_VALUE)GetProcAddress(Module, "PerfSetCounterRefValue");
    Perf->SetULongCounterValue = (PPERF_ULONG_VALUE)GetProcAddress(Module, "PerfSetULongCounterValue");
    Perf->SetULongLongCounterValue = (PPERF_ULONGLONG_VALUE)GetProcAddress(Module, "PerfSetULongLongCounterValue");
    Perf->IncrementULongCounterValue = (PPERF_ULONG_VALUE)GetProcAddress(Module, "PerfIncrementULongCounterValue");
    Perf->IncrementULongLongCounterValue =
        (PPERF_ULONGLONG_VALUE)GetProcAddress(Module, "PerfIncrementULongLongCounterValue");
    Perf->DecrementULongCounterValue = (PPERF_ULONG_VALUE)GetProcAddress(Module, "PerfDecrementULongCounterValue");
    Perf->DecrementULongLongCounterValue =
        (PPERF_ULONGLONG_VALUE)GetProcAddress(Module, "PerfDecrementULongLongCounterValue");
    return Perf->StartProvider && Perf->StartProviderEx && Perf->StopProvider && Perf->SetCounterSetInfo &&
           Perf->CreateInstance && Perf->DeleteInstance && Perf->QueryInstance && Perf->SetCounterRefValue &&
           Perf->SetULongCounterValue && Perf->SetULongLongCounterValue && Perf->IncrementULongCounterValue &&
           Perf->IncrementULongLongCounterValue && Perf->DecrementULongCounterValue &&
           Perf->DecrementULongLongCounterValue;
}

static VOID
TestModule(PCWSTR Name, PCSTR Module)
{
    PERF_FUNCTIONS Perf;
    HMODULE Library = LoadLibraryW(Name);

    if (!Library)
    {
        skip("%s is not available\n", Module);
        return;
    }
    if (LoadFunctions(Library, &Perf))
    {
        TestProviderStart(&Perf, Module);
        TestReferenceCounters(&Perf, Module);
        TestValueCounters(&Perf, Module);
        TestAllocator(&Perf, Module);
    }
    else
    {
        ok(FALSE, "%s does not export every PERFLIB provider function\n", Module);
    }
    FreeLibrary(Library);
}

START_TEST(PerfProvider)
{
    TestModule(L"kernelbase.dll", "kernelbase");
    TestModule(L"advapi32.dll", "advapi32");
}
