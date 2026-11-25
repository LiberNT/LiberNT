/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     win32u exports the WDDM services under the NtDxgk names
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"

typedef NTSTATUS (APIENTRY *PFN_ENUMADAPTERS3)(D3DKMT_ENUMADAPTERS3 *);
typedef NTSTATUS (APIENTRY *PFN_CLOSEADAPTER)(D3DKMT_CLOSEADAPTER *);

static const PCSTR Win32uDxgkNames[] =
{
    "NtDxgkEnumAdapters3",
    "NtDxgkGetNativeFenceLogDetail",
    "NtDxgkIsFeatureEnabled",
    "NtDxgkRegisterVailProcess",
    "NtDxgkSubmitPresentBltToHwQueue",
    "NtDxgkSubmitPresentToHwQueue",
};

static const PCSTR Win32uNoGdiNames[] =
{
    "NtGdiDdDDIEnumAdapters3",
    "NtGdiDdDDIGetNativeFenceLogDetail",
    "NtGdiDdDDIIsFeatureEnabled",
    "NtGdiDdDDIRegisterVailProcess",
    "NtGdiDdDDISubmitPresentBltToHwQueue",
    "NtGdiDdDDISubmitPresentToHwQueue",
};

START_TEST(win32unames)
{
    PFN_ENUMADAPTERS3 pEnumAdapters3;
    PFN_CLOSEADAPTER pCloseAdapter;
    D3DKMT_ADAPTERINFO Adapters[16];
    D3DKMT_ENUMADAPTERS3 Enum;
    D3DKMT_CLOSEADAPTER Close;
    HMODULE Win32u;
    NTSTATUS Status;
    ULONG Index;

    LoadLibraryA("gdi32.dll");
    Win32u = LoadLibraryA("win32u.dll");
    ok(Win32u != NULL, "win32u.dll did not load (%lu)\n", GetLastError());
    if (Win32u == NULL)
        return;

    for (Index = 0; Index < ARRAYSIZE(Win32uDxgkNames); ++Index)
        ok(GetProcAddress(Win32u, Win32uDxgkNames[Index]) != NULL, "win32u does not export %s\n", Win32uDxgkNames[Index]);
    for (Index = 0; Index < ARRAYSIZE(Win32uNoGdiNames); ++Index)
        ok(GetProcAddress(Win32u, Win32uNoGdiNames[Index]) == NULL, "win32u exports %s\n", Win32uNoGdiNames[Index]);

    pEnumAdapters3 = (PFN_ENUMADAPTERS3)GetProcAddress(Win32u, "NtDxgkEnumAdapters3");
    pCloseAdapter = (PFN_CLOSEADAPTER)GetProcAddress(Win32u, "NtGdiDdDDICloseAdapter");
    if (pEnumAdapters3 == NULL || pCloseAdapter == NULL)
        return;

    ZeroMemory(&Enum, sizeof(Enum));
    ZeroMemory(Adapters, sizeof(Adapters));
    Enum.Filter.IncludeDisplayOnly = 1;
    Enum.NumAdapters = ARRAYSIZE(Adapters);
    Enum.pAdapters = Adapters;
    Status = pEnumAdapters3(&Enum);
    ok(Status == STATUS_SUCCESS, "NtDxgkEnumAdapters3 returned 0x%08lx\n", Status);
    if (Status != STATUS_SUCCESS)
        return;
    ok(Enum.NumAdapters >= 1 && Enum.NumAdapters <= ARRAYSIZE(Adapters), "NtDxgkEnumAdapters3 returned %lu adapters\n",
       Enum.NumAdapters);
    for (Index = 0; Index < Enum.NumAdapters && Index < ARRAYSIZE(Adapters); ++Index)
    {
        ok(Adapters[Index].hAdapter != 0, "adapter %lu has no handle\n", Index);
        Close.hAdapter = Adapters[Index].hAdapter;
        ok(pCloseAdapter(&Close) == STATUS_SUCCESS, "closing adapter %lu failed\n", Index);
    }

    ZeroMemory(&Enum, sizeof(Enum));
    Enum.Filter.Reserved = 1;
    Enum.NumAdapters = ARRAYSIZE(Adapters);
    Enum.pAdapters = Adapters;
    ok(pEnumAdapters3(&Enum) == STATUS_INVALID_PARAMETER, "a reserved filter bit was accepted\n");
}
