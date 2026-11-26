/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a disabled extended feature is not looked up in the context
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef struct _TEST_CONTEXT_CHUNK
{
    LONG Offset;
    ULONG Length;
} TEST_CONTEXT_CHUNK;

typedef struct _TEST_CONTEXT_EX
{
    TEST_CONTEXT_CHUNK All;
    TEST_CONTEXT_CHUNK Legacy;
    TEST_CONTEXT_CHUNK XState;
} TEST_CONTEXT_EX;

typedef PVOID (NTAPI *FN_RtlLocateExtendedFeature)(PVOID, ULONG, PULONG);
typedef ULONG64 (NTAPI *FN_RtlGetEnabledExtendedFeatures)(ULONG64);

START_TEST(RtlLocateExtendedFeature)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    FN_RtlLocateExtendedFeature pRtlLocateExtendedFeature;
    FN_RtlGetEnabledExtendedFeatures pRtlGetEnabledExtendedFeatures;
    PUCHAR Buffer;
    PCONTEXT Context;
    TEST_CONTEXT_EX *ContextEx;
    ULONG64 Enabled;
    ULONG Feature, Tested = 0;
    DWORD OldProtect;

    pRtlLocateExtendedFeature = (FN_RtlLocateExtendedFeature)GetProcAddress(Ntdll, "RtlLocateExtendedFeature");
    pRtlGetEnabledExtendedFeatures = (FN_RtlGetEnabledExtendedFeatures)GetProcAddress(Ntdll, "RtlGetEnabledExtendedFeatures");
    if (!pRtlLocateExtendedFeature || !pRtlGetEnabledExtendedFeatures)
    {
        skip("RtlLocateExtendedFeature or RtlGetEnabledExtendedFeatures is missing\n");
        return;
    }
    Enabled = pRtlGetEnabledExtendedFeatures(~(ULONG64)0);

    Buffer = VirtualAlloc(NULL, 2 * PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ok(Buffer != NULL, "VirtualAlloc failed: %lu\n", GetLastError());
    if (!Buffer)
        return;
    ok(VirtualProtect(Buffer + PAGE_SIZE, PAGE_SIZE, PAGE_NOACCESS, &OldProtect), "VirtualProtect failed: %lu\n", GetLastError());

    /* The extended area points into the inaccessible page, so any read of it faults */
    Context = (PCONTEXT)Buffer;
    ContextEx = (TEST_CONTEXT_EX *)(Context + 1);
    Context->ContextFlags = CONTEXT_ALL;
#ifdef CONTEXT_XSTATE
    Context->ContextFlags |= CONTEXT_XSTATE;
#endif
    ContextEx->All.Offset = -(LONG)sizeof(CONTEXT);
    ContextEx->All.Length = 2 * PAGE_SIZE;
    ContextEx->Legacy.Offset = -(LONG)sizeof(CONTEXT);
    ContextEx->Legacy.Length = sizeof(CONTEXT);
    ContextEx->XState.Offset = (LONG)(Buffer + PAGE_SIZE - (PUCHAR)ContextEx);
    ContextEx->XState.Length = PAGE_SIZE;

    for (Feature = 2; Feature < 64; Feature++)
    {
        ULONG Length = 0xdeadbeef;
        PVOID Result = (PVOID)1;
        NTSTATUS Status = STATUS_SUCCESS;

        if (Enabled & ((ULONG64)1 << Feature))
            continue;
        Tested++;
        _SEH2_TRY
        {
            Result = pRtlLocateExtendedFeature(ContextEx, Feature, &Length);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        ok(Status == STATUS_SUCCESS, "feature %lu: the disabled feature read the context: %lx\n", Feature, Status);
        ok(Result == NULL, "feature %lu: %p for a disabled feature\n", Feature, Result);
        ok(Length == 0xdeadbeef, "feature %lu: length set to %lu for a disabled feature\n", Feature, Length);

#ifdef CONTEXT_XSTATE
        Result = (PVOID)1;
        Length = 0xdeadbeef;
        Status = STATUS_SUCCESS;
        _SEH2_TRY
        {
            Result = LocateXStateFeature(Context, Feature, &Length);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        ok(Status == STATUS_SUCCESS, "feature %lu: LocateXStateFeature read the context: %lx\n", Feature, Status);
        ok(Result == NULL, "feature %lu: LocateXStateFeature returned %p\n", Feature, Result);
        ok(Length == 0xdeadbeef, "feature %lu: LocateXStateFeature set length %lu\n", Feature, Length);
#endif
    }
    ok(Tested != 0, "every extended feature is enabled\n");

    VirtualFree(Buffer, 0, MEM_RELEASE);
}
