/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for the secure memory cache callback APIs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *FN_AddSecureMemoryCacheCallback)(PSECURE_MEMORY_CACHE_CALLBACK);
typedef BOOL (WINAPI *FN_RemoveSecureMemoryCacheCallback)(PSECURE_MEMORY_CACHE_CALLBACK);
typedef NTSTATUS (NTAPI *FN_RtlRegisterSecureMemoryCacheCallback)(PSECURE_MEMORY_CACHE_CALLBACK);
typedef NTSTATUS (NTAPI *FN_RtlDeregisterSecureMemoryCacheCallback)(PSECURE_MEMORY_CACHE_CALLBACK);
typedef BOOLEAN (NTAPI *FN_RtlFlushSecureMemoryCache)(PVOID, SIZE_T);

static FN_AddSecureMemoryCacheCallback pAddSecureMemoryCacheCallback;
static FN_RemoveSecureMemoryCacheCallback pRemoveSecureMemoryCacheCallback;
static FN_RtlRegisterSecureMemoryCacheCallback pRtlRegisterSecureMemoryCacheCallback;
static FN_RtlDeregisterSecureMemoryCacheCallback pRtlDeregisterSecureMemoryCacheCallback;
static FN_RtlFlushSecureMemoryCache pRtlFlushSecureMemoryCache;

static LONG NotSecuredCalls;
static PVOID NotSecuredAddress;
static SIZE_T NotSecuredRange;
static LONG SecuredCalls;
static PVOID SecuredAddress;
static SIZE_T SecuredRange;

static
BOOLEAN
NTAPI
NotSecuredCallback(
    _In_ PVOID Addr,
    _In_ SIZE_T Range)
{
    NotSecuredCalls++;
    NotSecuredAddress = Addr;
    NotSecuredRange = Range;
    return FALSE;
}

static
BOOLEAN
NTAPI
SecuredCallback(
    _In_ PVOID Addr,
    _In_ SIZE_T Range)
{
    SecuredCalls++;
    SecuredAddress = Addr;
    SecuredRange = Range;
    return TRUE;
}

static
BOOL
CallRemove(
    _In_ PSECURE_MEMORY_CACHE_CALLBACK Callback,
    _Out_ PNTSTATUS ExceptionCode)
{
    BOOL Result = FALSE;

    *ExceptionCode = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Result = pRemoveSecureMemoryCacheCallback(Callback);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        *ExceptionCode = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Result;
}

static
VOID
TestKernel32(VOID)
{
    UCHAR Buffer[64];
    NTSTATUS ExceptionCode;
    PVOID Memory;
    BOOL Result;

    SetLastError(0xdeadbeef);
    Result = pAddSecureMemoryCacheCallback(NotSecuredCallback);
    ok_eq_bool(Result, TRUE);
    ok_eq_ulong(GetLastError(), 0xdeadbeef);

    ok_eq_int(pRtlFlushSecureMemoryCache(Buffer, sizeof(Buffer)), FALSE);
    ok_eq_long(NotSecuredCalls, 1);
    ok_eq_pointer(NotSecuredAddress, (PVOID)Buffer);
    ok_eq_size(NotSecuredRange, sizeof(Buffer));

    ok_eq_bool(pAddSecureMemoryCacheCallback(SecuredCallback), TRUE);
    ok_eq_int(pRtlFlushSecureMemoryCache(Buffer + 16, 16), TRUE);
    ok_eq_long(NotSecuredCalls, 2);
    ok_eq_long(SecuredCalls, 1);
    ok_eq_pointer(SecuredAddress, (PVOID)(Buffer + 16));
    ok_eq_size(SecuredRange, 16);

    Memory = VirtualAlloc(NULL, 0x10000, MEM_COMMIT, PAGE_READWRITE);
    ok(Memory != NULL, "VirtualAlloc failed with %lu\n", GetLastError());
    if (Memory != NULL)
        ok_eq_bool(VirtualFree(Memory, 0, MEM_RELEASE), TRUE);
    ok_eq_long(NotSecuredCalls, 2);
    ok_eq_long(SecuredCalls, 1);

    SetLastError(0xdeadbeef);
    Result = CallRemove(SecuredCallback, &ExceptionCode);
    ok_eq_hex(ExceptionCode, STATUS_SUCCESS);
    ok_eq_bool(Result, TRUE);
    ok_eq_ulong(GetLastError(), 0xdeadbeef);

    ok_eq_int(pRtlFlushSecureMemoryCache(Buffer, sizeof(Buffer)), FALSE);
    ok_eq_long(NotSecuredCalls, 3);
    ok_eq_long(SecuredCalls, 1);

    SetLastError(0xdeadbeef);
    Result = CallRemove(SecuredCallback, &ExceptionCode);
    ok_eq_hex(ExceptionCode, STATUS_SUCCESS);
    ok_eq_bool(Result, FALSE);
    ok_eq_ulong(GetLastError(), ERROR_NOT_FOUND);

    Result = CallRemove(NotSecuredCallback, &ExceptionCode);
    ok_eq_hex(ExceptionCode, STATUS_SUCCESS);
    ok_eq_bool(Result, TRUE);

    ok_eq_int(pRtlFlushSecureMemoryCache(Buffer, sizeof(Buffer)), FALSE);
    ok_eq_long(NotSecuredCalls, 3);
    ok_eq_long(SecuredCalls, 1);

    SetLastError(0xdeadbeef);
    Result = pAddSecureMemoryCacheCallback(NULL);
    ok_eq_bool(Result, FALSE);
    ok_eq_ulong(GetLastError(), ERROR_INVALID_PARAMETER);
}

static
VOID
TestNtdll(VOID)
{
    UCHAR Buffer[16];

    ok_eq_hex(pRtlRegisterSecureMemoryCacheCallback(SecuredCallback), STATUS_SUCCESS);
    ok_eq_int(pRtlFlushSecureMemoryCache(Buffer, sizeof(Buffer)), TRUE);
    ok_eq_long(SecuredCalls, 2);
    ok_eq_hex(pRtlDeregisterSecureMemoryCacheCallback(SecuredCallback), STATUS_SUCCESS);
    ok_eq_hex(pRtlDeregisterSecureMemoryCacheCallback(SecuredCallback), STATUS_NOT_FOUND);
    ok_eq_int(pRtlFlushSecureMemoryCache(Buffer, sizeof(Buffer)), FALSE);
    ok_eq_long(SecuredCalls, 2);
}

START_TEST(SecureMemoryCache)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");

    pAddSecureMemoryCacheCallback = (FN_AddSecureMemoryCacheCallback)
        GetProcAddress(Kernel32, "AddSecureMemoryCacheCallback");
    pRemoveSecureMemoryCacheCallback = (FN_RemoveSecureMemoryCacheCallback)
        GetProcAddress(Kernel32, "RemoveSecureMemoryCacheCallback");
    pRtlRegisterSecureMemoryCacheCallback = (FN_RtlRegisterSecureMemoryCacheCallback)
        GetProcAddress(Ntdll, "RtlRegisterSecureMemoryCacheCallback");
    pRtlDeregisterSecureMemoryCacheCallback = (FN_RtlDeregisterSecureMemoryCacheCallback)
        GetProcAddress(Ntdll, "RtlDeregisterSecureMemoryCacheCallback");
    pRtlFlushSecureMemoryCache = (FN_RtlFlushSecureMemoryCache)
        GetProcAddress(Ntdll, "RtlFlushSecureMemoryCache");

    ok(pAddSecureMemoryCacheCallback != NULL, "AddSecureMemoryCacheCallback is missing\n");
    ok(pRemoveSecureMemoryCacheCallback != NULL, "RemoveSecureMemoryCacheCallback is missing\n");
    ok(pRtlRegisterSecureMemoryCacheCallback != NULL, "RtlRegisterSecureMemoryCacheCallback is missing\n");
    ok(pRtlDeregisterSecureMemoryCacheCallback != NULL, "RtlDeregisterSecureMemoryCacheCallback is missing\n");
    ok(pRtlFlushSecureMemoryCache != NULL, "RtlFlushSecureMemoryCache is missing\n");
    if (!pAddSecureMemoryCacheCallback || !pRemoveSecureMemoryCacheCallback ||
        !pRtlRegisterSecureMemoryCacheCallback || !pRtlDeregisterSecureMemoryCacheCallback ||
        !pRtlFlushSecureMemoryCache)
    {
        return;
    }

    TestKernel32();
    TestNtdll();
}
