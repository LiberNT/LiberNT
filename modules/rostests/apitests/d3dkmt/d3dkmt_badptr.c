/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     D3DKMT calls given an invalid or kernel argument pointer fail instead of using it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef NTSTATUS (APIENTRY *PFN_D3DKMT_ONE_ARGUMENT)(PVOID);

static const char * const BadPointerEntries[] =
{
    "CreateAllocation", "CheckMonitorPowerState", "CloseAdapter", "CreateContext", "CreateDevice",
    "CreateOverlay", "CreateSynchronizationObject", "DestroyAllocation", "DestroyContext", "DestroyDevice",
    "DestroyOverlay", "DestroySynchronizationObject", "Escape", "FlipOverlay", "GetContextSchedulingPriority",
    "GetDeviceState", "GetDisplayModeList", "GetMultisampleMethodList", "GetPresentHistory", "GetScanLine",
    "GetSharedPrimaryHandle", "InvalidateActiveVidPn", "Lock", "OpenResource", "PollDisplayChildren",
    "QueryAdapterInfo", "QueryAllocationResidency", "QueryResourceInfo", "QueryStatistics", "Render",
    "SetAllocationPriority", "SetContextSchedulingPriority", "SetDisplayMode", "SetDisplayPrivateDriverFormat",
    "SetGammaRamp", "SetQueuedLimit", "SetVidPnSourceOwner", "Unlock", "CreateAllocation2", "OpenResource2",
    "CreateSynchronizationObject2", "OpenSynchronizationObject", "WaitForSynchronizationObject2",
    "SignalSynchronizationObject2", "UpdateOverlay", "WaitForIdle", "WaitForSynchronizationObject",
    "WaitForVerticalBlankEvent", "SignalSynchronizationObject", "OfferAllocations", "ReclaimAllocations",
    "SetVidPnSourceOwner1", "WaitForVerticalBlankEvent2", "MakeResident", "Evict", "QueryVideoMemoryInfo",
    "CreatePagingQueue", "DestroyPagingQueue", "ReserveGpuVirtualAddress", "MapGpuVirtualAddress",
    "FreeGpuVirtualAddress", "UpdateGpuVirtualAddress", "WaitForSynchronizationObjectFromCpu",
    "SignalSynchronizationObjectFromCpu", "SubmitCommand", "GetAllocationPriority", "CreateContextVirtual",
    "GetDWMVerticalBlankEvent", "SetSyncRefreshCountWaitTarget", "SignalSynchronizationObjectFromGpu",
    "WaitForSynchronizationObjectFromGpu", "SignalSynchronizationObjectFromGpu2",
};

static NTSTATUS
CallGuarded(PFN_D3DKMT_ONE_ARGUMENT Function, PVOID Argument, BOOL *Faulted)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    *Faulted = FALSE;
    _SEH2_TRY
    {
        Status = Function(Argument);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        *Faulted = TRUE;
    }
    _SEH2_END;
    return Status;
}

static const char * const NotExportedByWindows[] =
{
    "SetDisplayPrivateDriverFormat", "CreateAllocation2", "OpenResource2", "CreateSynchronizationObject2",
    "WaitForSynchronizationObject2", "SignalSynchronizationObject2", "SetVidPnSourceOwner1",
};

static BOOL
IsNotExportedByWindows(const char *Entry)
{
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(NotExportedByWindows); ++Index)
    {
        if (strcmp(Entry, NotExportedByWindows[Index]) == 0)
            return TRUE;
    }
    return FALSE;
}

START_TEST(badptr)
{
    HMODULE Win32u = LoadLibraryA("win32u.dll");
    PVOID KernelAddress = (PVOID)(sizeof(PVOID) == 8 ? (ULONG_PTR)0xFFFF800000000000ULL : (ULONG_PTR)0x80000000UL);
    PVOID NoAccessPage = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    char Name[96];
    ULONG Index;

    if (Win32u == NULL)
    {
        skip("win32u.dll is not available\n");
        return;
    }
    for (Index = 0; Index < ARRAYSIZE(BadPointerEntries); ++Index)
    {
        PFN_D3DKMT_ONE_ARGUMENT Function;
        NTSTATUS Unmapped, Kernel, NoAccess, Expected;
        BOOL UnmappedFaulted, KernelFaulted, NoAccessFaulted;

        _snprintf(Name, ARRAYSIZE(Name) - 1, "NtGdiDdDDI%s", BadPointerEntries[Index]);
        Name[ARRAYSIZE(Name) - 1] = 0;
        Function = (PFN_D3DKMT_ONE_ARGUMENT)GetProcAddress(Win32u, Name);
        if (Function == NULL)
        {
            ok(IsNotExportedByWindows(BadPointerEntries[Index]), "%s is not exported\n", Name);
            continue;
        }
        Expected = strcmp(BadPointerEntries[Index], "GetPresentHistory") == 0 ? STATUS_ACCESS_DENIED : STATUS_INVALID_PARAMETER;
        Unmapped = CallGuarded(Function, (PVOID)(ULONG_PTR)0x10, &UnmappedFaulted);
        ok(!UnmappedFaulted && Unmapped == Expected, "%s(unmapped): %#lx%s, expected %#lx\n", Name, Unmapped,
           UnmappedFaulted ? " raised" : "", Expected);
        Kernel = CallGuarded(Function, KernelAddress, &KernelFaulted);
        ok(!KernelFaulted && Kernel == Expected, "%s(kernel address): %#lx%s, expected %#lx\n", Name, Kernel,
           KernelFaulted ? " raised" : "", Expected);
        if (NoAccessPage != NULL)
        {
            NoAccess = CallGuarded(Function, NoAccessPage, &NoAccessFaulted);
            if (NoAccessFaulted || NoAccess != STATUS_INVALID_PARAMETER)
                trace("%s(no-access page): %#lx%s\n", BadPointerEntries[Index], NoAccess, NoAccessFaulted ? " raised" : "");
        }
    }
    if (NoAccessPage != NULL)
        VirtualFree(NoAccessPage, 0, MEM_RELEASE);
}
