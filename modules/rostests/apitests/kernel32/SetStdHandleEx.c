/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test SetStdHandleEx
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PFN_SET_STD_HANDLE_EX)(DWORD, HANDLE, PHANDLE);

static
VOID
TestSlot(
    _In_ PFN_SET_STD_HANDLE_EX pSetStdHandleEx,
    _In_ DWORD StdHandle)
{
    HANDLE Original;
    HANDLE Previous;
    HANDLE Current;
    HANDLE Restored;
    HANDLE Marker = (HANDLE)(ULONG_PTR)0x1234;
    BOOL Result;
    BOOL Restore;

    Original = GetStdHandle(StdHandle);
    Previous = (HANDLE)(ULONG_PTR)0xdeadbeef;
    Result = pSetStdHandleEx(StdHandle, Marker, &Previous);
    Current = GetStdHandle(StdHandle);
    Restore = pSetStdHandleEx(StdHandle, Original, NULL);
    Restored = GetStdHandle(StdHandle);

    ok(Result, "SetStdHandleEx(%ld) failed with %lu\n", (LONG)StdHandle, GetLastError());
    ok(Previous == Original, "SetStdHandleEx(%ld) returned previous %p, expected %p\n",
       (LONG)StdHandle, Previous, Original);
    ok(Current == Marker, "GetStdHandle(%ld) returned %p, expected %p\n", (LONG)StdHandle, Current, Marker);
    ok(Restore, "SetStdHandleEx(%ld) with no previous value failed with %lu\n", (LONG)StdHandle, GetLastError());
    ok(Restored == Original, "GetStdHandle(%ld) returned %p after restore, expected %p\n",
       (LONG)StdHandle, Restored, Original);
}

START_TEST(SetStdHandleEx)
{
    PFN_SET_STD_HANDLE_EX pSetStdHandleEx;
    PFN_SET_STD_HANDLE_EX pKernelBaseSetStdHandleEx;
    HANDLE Previous;
    HMODULE KernelBase;
    BOOL Result;

    pSetStdHandleEx = (PFN_SET_STD_HANDLE_EX)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetStdHandleEx");
    ok(pSetStdHandleEx != NULL, "kernel32 does not export SetStdHandleEx\n");
    KernelBase = GetModuleHandleW(L"kernelbase.dll");
    pKernelBaseSetStdHandleEx = KernelBase ? (PFN_SET_STD_HANDLE_EX)GetProcAddress(KernelBase, "SetStdHandleEx") : NULL;
    ok(pKernelBaseSetStdHandleEx != NULL, "kernelbase does not export SetStdHandleEx\n");
    if (!pSetStdHandleEx)
        return;

    TestSlot(pSetStdHandleEx, STD_INPUT_HANDLE);
    TestSlot(pSetStdHandleEx, STD_OUTPUT_HANDLE);
    TestSlot(pSetStdHandleEx, STD_ERROR_HANDLE);
    if (pKernelBaseSetStdHandleEx)
        TestSlot(pKernelBaseSetStdHandleEx, STD_ERROR_HANDLE);

    Previous = (HANDLE)(ULONG_PTR)0x5678;
    SetLastError(0xdeadbeef);
    Result = pSetStdHandleEx(0, (HANDLE)(ULONG_PTR)0x1234, &Previous);
    ok(!Result, "SetStdHandleEx(0) succeeded\n");
    ok(GetLastError() == ERROR_INVALID_HANDLE, "SetStdHandleEx(0) failed with %lu\n", GetLastError());
    ok(Previous == (HANDLE)(ULONG_PTR)0x5678, "SetStdHandleEx(0) wrote previous value %p\n", Previous);

    SetLastError(0xdeadbeef);
    Result = pSetStdHandleEx((DWORD)-13, (HANDLE)(ULONG_PTR)0x1234, NULL);
    ok(!Result, "SetStdHandleEx(-13) succeeded\n");
    ok(GetLastError() == ERROR_INVALID_HANDLE, "SetStdHandleEx(-13) failed with %lu\n", GetLastError());
}
