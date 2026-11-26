/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WDDM services win32u exports that no graphics kernel export serves
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"

typedef NTSTATUS (APIENTRY *PFN_KMT1)(PVOID);
typedef NTSTATUS (APIENTRY *PFN_PRESENTQUEUEEVENT)(D3DKMT_HANDLE, HANDLE *);

static PVOID
W32kServedWin32u(
    _In_ PCSTR Name)
{
    return (PVOID)GetProcAddress(LoadLibraryA("win32u.dll"), Name);
}

static VOID
W32kServedSharedPrimary(
    _In_ LUID AdapterLuid)
{
    PFN_KMT1 pLock = (PFN_KMT1)W32kServedWin32u("NtGdiDdDDISharedPrimaryLockNotification");
    PFN_KMT1 pUnlock = (PFN_KMT1)W32kServedWin32u("NtGdiDdDDISharedPrimaryUnLockNotification");
    D3DKMT_SHAREDPRIMARYLOCKNOTIFICATION Lock;
    D3DKMT_SHAREDPRIMARYUNLOCKNOTIFICATION Unlock;
    NTSTATUS Status;

    ok(pLock != NULL && pUnlock != NULL, "win32u does not export the shared primary notifications\n");
    if (pLock == NULL || pUnlock == NULL)
        return;

    ZeroMemory(&Unlock, sizeof(Unlock));
    Unlock.AdapterLuid = AdapterLuid;
    Status = pUnlock(&Unlock);
    ok(Status == STATUS_INVALID_PARAMETER, "unlock without a lock: 0x%08lx\n", Status);

    ZeroMemory(&Lock, sizeof(Lock));
    Lock.AdapterLuid = AdapterLuid;
    Lock.LockRect.right = 16;
    Lock.LockRect.bottom = 16;
    Status = pLock(&Lock);
    ok(Status == STATUS_SUCCESS, "lock: 0x%08lx\n", Status);
    Status = pUnlock(&Unlock);
    ok(Status == STATUS_SUCCESS, "unlock after the lock: 0x%08lx\n", Status);
    Status = pUnlock(&Unlock);
    ok(Status == STATUS_SUCCESS, "second unlock: 0x%08lx\n", Status);

    Status = pLock(NULL);
    ok(Status == STATUS_INVALID_PARAMETER, "lock without a descriptor: 0x%08lx\n", Status);
    Status = pUnlock(NULL);
    ok(Status == STATUS_INVALID_PARAMETER, "unlock without a descriptor: 0x%08lx\n", Status);
}

static VOID
W32kServedPresentQueueEvent(
    _In_ D3DKMT_HANDLE hAdapter)
{
    PFN_PRESENTQUEUEEVENT pGetEvent = (PFN_PRESENTQUEUEEVENT)W32kServedWin32u("NtGdiDdDDIGetPresentQueueEvent");
    HANDLE Event;
    NTSTATUS Status;

    ok(pGetEvent != NULL, "win32u does not export NtGdiDdDDIGetPresentQueueEvent\n");
    if (pGetEvent == NULL)
        return;
    Event = (HANDLE)(ULONG_PTR)0x1234;
    Status = pGetEvent(hAdapter, &Event);
    ok(Status == STATUS_ACCESS_DENIED, "present queue event of a process without a present queue: 0x%08lx\n", Status);
    ok(Event == (HANDLE)(ULONG_PTR)0x1234, "present queue event written: %p\n", Event);
    Status = pGetEvent(hAdapter, NULL);
    ok(Status == STATUS_ACCESS_DENIED, "present queue event without an output: 0x%08lx\n", Status);
    Status = pGetEvent(0, &Event);
    ok(Status == STATUS_ACCESS_DENIED, "present queue event of no adapter: 0x%08lx\n", Status);
    Status = pGetEvent(0, NULL);
    ok(Status == STATUS_ACCESS_DENIED, "present queue event of no adapter without an output: 0x%08lx\n", Status);
    Status = pGetEvent(0x7ffffff0, &Event);
    ok(Status == STATUS_ACCESS_DENIED, "present queue event of an unknown adapter: 0x%08lx\n", Status);
    ok(Event == (HANDLE)(ULONG_PTR)0x1234, "present queue event written: %p\n", Event);
}

START_TEST(win32kserved)
{
    PFN_KMT1 pEnumAdapters2 = (PFN_KMT1)W32kServedWin32u("NtGdiDdDDIEnumAdapters2");
    D3DKMT_ADAPTERINFO Adapters[16];
    D3DKMT_ENUMADAPTERS2 Enum;
    UINT Index;

    LoadLibraryA("gdi32.dll");
    if (pEnumAdapters2 == NULL)
    {
        skip("win32u does not export NtGdiDdDDIEnumAdapters2\n");
        return;
    }
    ZeroMemory(&Enum, sizeof(Enum));
    Enum.NumAdapters = ARRAYSIZE(Adapters);
    Enum.pAdapters = Adapters;
    if (!NT_SUCCESS(pEnumAdapters2(&Enum)) || Enum.NumAdapters == 0)
    {
        skip("no D3DKMT adapter\n");
        return;
    }
    for (Index = 0; Index < Enum.NumAdapters; ++Index)
    {
        if (Adapters[Index].NumOfSources != 0)
            break;
    }
    if (Index == Enum.NumAdapters)
    {
        skip("no adapter with a display source\n");
        return;
    }
    W32kServedSharedPrimary(Adapters[Index].AdapterLuid);
    W32kServedPresentQueueEvent(Adapters[Index].hAdapter);
}
