/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for RtlCopyMemoryNonTemporal and CopyMemoryNonTemporal
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef VOID (NTAPI *FN_CopyMemoryNonTemporal)(PVOID, const VOID *, SIZE_T);

static
VOID
CheckCopies(
    _In_ FN_CopyMemoryNonTemporal pCopy,
    _In_ PCSTR Name)
{
    static const SIZE_T Lengths[] = { 0, 1, 7, 8, 9, 63, 64, 65, 255, 4096 };
    UCHAR Source[4096 + 64], Destination[4096 + 64];
    SIZE_T Index, Offset, Length;

    for (Offset = 0; Offset < sizeof(Source); Offset++)
        Source[Offset] = (UCHAR)(Offset * 7 + 3);

    for (Index = 0; Index < RTL_NUMBER_OF(Lengths); Index++)
    {
        Length = Lengths[Index];
        RtlFillMemory(Destination, sizeof(Destination), 0xcc);
        pCopy(Destination + 3, Source + 5, Length);
        ok(RtlCompareMemory(Destination + 3, Source + 5, Length) == Length, "%s: %Iu bytes not copied\n", Name, (size_t)Length);
        ok(Destination[0] == 0xcc && Destination[1] == 0xcc && Destination[2] == 0xcc,
           "%s: %Iu bytes wrote before the destination\n", Name, (size_t)Length);
        ok(Destination[3 + Length] == 0xcc, "%s: %Iu bytes wrote past the destination\n", Name, (size_t)Length);
    }
}

START_TEST(RtlCopyMemoryNonTemporal)
{
    FN_CopyMemoryNonTemporal pRtlCopyMemoryNonTemporal, pCopyMemoryNonTemporal;
    HMODULE KernelBase;

    pRtlCopyMemoryNonTemporal = (FN_CopyMemoryNonTemporal)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                                         "RtlCopyMemoryNonTemporal");
    if (sizeof(PVOID) == sizeof(ULONG))
    {
        ok(pRtlCopyMemoryNonTemporal == NULL, "RtlCopyMemoryNonTemporal is exported by 32-bit ntdll\n");
        return;
    }

    if (!pRtlCopyMemoryNonTemporal)
    {
        skip("RtlCopyMemoryNonTemporal is not exported\n");
        return;
    }
    CheckCopies(pRtlCopyMemoryNonTemporal, "RtlCopyMemoryNonTemporal");

    KernelBase = GetModuleHandleW(L"kernelbase.dll");
    pCopyMemoryNonTemporal = KernelBase ? (FN_CopyMemoryNonTemporal)GetProcAddress(KernelBase, "CopyMemoryNonTemporal")
                                        : NULL;
    if (!pCopyMemoryNonTemporal)
    {
        skip("CopyMemoryNonTemporal is not exported by kernelbase\n");
        return;
    }
    CheckCopies(pCopyMemoryNonTemporal, "CopyMemoryNonTemporal");
}
