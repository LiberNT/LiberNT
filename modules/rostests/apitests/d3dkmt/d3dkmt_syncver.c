/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The win32u synchronization object signal and wait services read the version 2 descriptor
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"

typedef NTSTATUS (APIENTRY *PFN_SYNCVER_SERVICE)(PVOID);

/* Calls the service with the first Size bytes of Descriptor placed so that they end where an inaccessible page begins */
static NTSTATUS
SyncVerCallAtPageEnd(
    _In_ PFN_SYNCVER_SERVICE Service,
    _In_reads_bytes_(Size) const VOID *Descriptor,
    _In_ SIZE_T Size)
{
    SYSTEM_INFO Info;
    NTSTATUS Status = STATUS_NO_MEMORY;
    PBYTE Pages;
    DWORD Old;

    GetSystemInfo(&Info);
    Pages = VirtualAlloc(NULL, 2 * Info.dwPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (Pages == NULL)
        return Status;
    memcpy(Pages + Info.dwPageSize - Size, Descriptor, Size);
    VirtualProtect(Pages + Info.dwPageSize, Info.dwPageSize, PAGE_NOACCESS, &Old);
    _SEH2_TRY
    {
        Status = Service(Pages + Info.dwPageSize - Size);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    VirtualFree(Pages, 0, MEM_RELEASE);
    return Status;
}

START_TEST(syncver)
{
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    D3DKMT_CREATECONTEXTVIRTUAL Context;
    D3DKMT_DESTROYCONTEXT DestroyContext;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Sync;
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 Signal;
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2 Wait;
    PFN_SYNCVER_SERVICE pService, pWaitService;
    D3DKMT_HANDLE hAdapter, hDevice;
    HANDLE Event;
    NTSTATUS Status;
    BOOL Wow64 = FALSE;

    LOADFN(PFND3DKMT_CREATECONTEXTVIRTUAL, pCreateContext, "D3DKMTCreateContextVirtual");
    LOADFN(PFND3DKMT_DESTROYCONTEXT, pDestroyContext, "D3DKMTDestroyContext");
    LOADFN(PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2, pCreate, "D3DKMTCreateSynchronizationObject2");
    LOADFN(PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT, pDestroy, "D3DKMTDestroySynchronizationObject");
    pService = (PFN_SYNCVER_SERVICE)GetProcAddress(LoadLibraryW(L"win32u.dll"), "NtGdiDdDDISignalSynchronizationObject");
    pWaitService = (PFN_SYNCVER_SERVICE)GetProcAddress(LoadLibraryW(L"win32u.dll"), "NtGdiDdDDIWaitForSynchronizationObject");
    if (pService == NULL || pWaitService == NULL)
    {
        skip("win32u lacks the synchronization object signal or wait service\n");
        return;
    }

    IsWow64Process(GetCurrentProcess(), &Wow64);
    hAdapter = OpenRenderAdapter();
    if (!hAdapter) { skip("No render adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed on the render adapter\n"); CloseAdapter(hAdapter); return; }
    memset(&Context, 0, sizeof(Context));
    Context.hDevice = hDevice;
    Context.EngineAffinity = 1;
    Context.Flags.NullRendering = 1;
    Status = pCreateContext(&Context);
    if (!NT_SUCCESS(Status))
    {
        skip("CreateContextVirtual refused 0x%08lX\n", (long)Status);
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
        return;
    }

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    memset(&Sync, 0, sizeof(Sync));
    Sync.hDevice = hDevice;
    Sync.Info.Type = D3DDDI_CPU_NOTIFICATION;
    Sync.Info.CPUNotification.Event = Event;
    Status = pCreate(&Sync);
    ok_succeeded(Status, "Create CPU notification: 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        memset(&Signal, 0, sizeof(Signal));
        Signal.hContext = Context.hContext;
        Signal.ObjectCount = 1;
        Signal.ObjectHandleArray[0] = Sync.hSyncObject;
        Signal.Fence.FenceValue = 1;

        Status = SyncVerCallAtPageEnd(pService, &Signal, sizeof(Signal));
        ok(Status == STATUS_SUCCESS, "signal service with a v2 descriptor returned 0x%08lX\n", (long)Status);
        if (Status == STATUS_SUCCESS)
            ok(WaitForSingleObject(Event, 5000) == WAIT_OBJECT_0, "the v2 descriptor did not signal the CPU notification\n");

        /* A service that reads the v2 descriptor touches the bytes past a v1-sized one */
        Status = SyncVerCallAtPageEnd(pService, &Signal, sizeof(D3DKMT_SIGNALSYNCHRONIZATIONOBJECT));
        /* TODO: INFO_NEEDED: the status Windows 11 WoW64 returns when the 32-bit descriptor stops at a page end (measured natively only) */
        if (Wow64)
            trace("WoW64: signal service with a v1-sized descriptor at a page end returned 0x%08lX\n", (long)Status);
        else
            ok(Status == STATUS_INVALID_PARAMETER, "signal service with a v1-sized descriptor at a page end returned 0x%08lX\n", (long)Status);

        /* Only a v2 reader sees BroadcastContextCount, which may not exceed D3DDDI_MAX_BROADCAST_CONTEXT */
        ResetEvent(Event);
        Signal.BroadcastContextCount = 0xFFFFFFFF;
        Status = SyncVerCallAtPageEnd(pService, &Signal, sizeof(Signal));
        ok(Status == STATUS_INVALID_PARAMETER, "signal service with an out-of-range v2 BroadcastContextCount returned 0x%08lX\n", (long)Status);
        ok(WaitForSingleObject(Event, 0) == WAIT_TIMEOUT, "a refused signal set the CPU notification\n");
        Signal.BroadcastContextCount = 0;

        memset(&DestroySync, 0, sizeof(DestroySync));
        DestroySync.hSyncObject = Sync.hSyncObject;
        pDestroy(&DestroySync);
    }
    if (Event != NULL)
        CloseHandle(Event);

    memset(&Sync, 0, sizeof(Sync));
    Sync.hDevice = hDevice;
    Sync.Info.Type = D3DDDI_FENCE;
    Sync.Info.Fence.FenceValue = 0;
    Status = pCreate(&Sync);
    ok_succeeded(Status, "Create fence: 0x%08lX\n", (long)Status);
    if (NT_SUCCESS(Status))
    {
        memset(&Wait, 0, sizeof(Wait));
        Wait.hContext = Context.hContext;
        Wait.ObjectCount = 1;
        Wait.ObjectHandleArray[0] = Sync.hSyncObject;
        Wait.Fence.FenceValue = 0;

        Status = SyncVerCallAtPageEnd(pWaitService, &Wait, sizeof(Wait));
        ok(Status == STATUS_SUCCESS, "wait service with a v2 fence descriptor returned 0x%08lX\n", (long)Status);
        Status = SyncVerCallAtPageEnd(pWaitService, &Wait, sizeof(D3DKMT_WAITFORSYNCHRONIZATIONOBJECT));
        ok(Status == STATUS_INVALID_PARAMETER, "wait service with a v1-sized descriptor at a page end returned 0x%08lX\n", (long)Status);
        Wait.Reserved[7] = ~(UINT64)0;
        Status = SyncVerCallAtPageEnd(pWaitService, &Wait, sizeof(Wait));
        ok(Status == STATUS_SUCCESS, "wait service with a nonzero last v2 reserved word returned 0x%08lX\n", (long)Status);

        memset(&DestroySync, 0, sizeof(DestroySync));
        DestroySync.hSyncObject = Sync.hSyncObject;
        pDestroy(&DestroySync);
    }
    memset(&DestroyContext, 0, sizeof(DestroyContext));
    DestroyContext.hContext = Context.hContext;
    pDestroyContext(&DestroyContext);
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
#else
    skip("Virtual contexts need WDDM 2.0 headers\n");
#endif
}
