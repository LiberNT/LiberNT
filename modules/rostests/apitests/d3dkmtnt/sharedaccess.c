/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Granted access of shared resource and shared sync object NT handles
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"

typedef NTSTATUS (APIENTRY *PFN_KMT1)(PVOID);
typedef NTSTATUS (APIENTRY *PFN_KMTSHARE)(UINT, const D3DKMT_HANDLE *, POBJECT_ATTRIBUTES, DWORD, HANDLE *);
typedef NTSTATUS (NTAPI *PFN_QUERYOBJECT)(HANDLE, ULONG, PVOID, ULONG, PULONG);

typedef struct _SHAREDACCESS_BASIC_INFORMATION
{
    ULONG Attributes;
    ACCESS_MASK GrantedAccess;
    ULONG HandleCount;
    ULONG PointerCount;
    ULONG Reserved[10];
} SHAREDACCESS_BASIC_INFORMATION;

typedef struct _SHAREDACCESS_CASE
{
    ACCESS_MASK Desired;
    ACCESS_MASK Resource;
    ACCESS_MASK Sync;
} SHAREDACCESS_CASE;

static const SHAREDACCESS_CASE SharedAccessCases[] =
{
    { 0, 0, 0 },
    { 0x00000001, 0x00000001, 0x00000001 },
    { 0x00000002, 0x00000000, 0x00000002 },
    { 0x00000004, 0x00000000, 0x00000000 },
    { DELETE, DELETE, DELETE },
    { READ_CONTROL, READ_CONTROL, READ_CONTROL },
    { WRITE_DAC, WRITE_DAC, WRITE_DAC },
    { WRITE_OWNER, WRITE_OWNER, WRITE_OWNER },
    { SYNCHRONIZE, SYNCHRONIZE, SYNCHRONIZE },
    { GENERIC_READ, 0x00020000, 0x00020001 },
    { GENERIC_WRITE, 0x00020001, 0x00020002 },
    { GENERIC_EXECUTE, 0x00020000, 0x00120000 },
    { GENERIC_ALL, 0x000F0001, 0x001F0003 },
    { MAXIMUM_ALLOWED, 0x000F0001, 0x001F0003 },
};

static PVOID
SharedAccessResolve(
    _In_ PCSTR Name)
{
    CHAR Win32uName[96];
    PVOID Function;

    Function = (PVOID)GetProcAddress(LoadLibraryA("gdi32.dll"), Name);
    if (Function != NULL)
        return Function;
    lstrcpyA(Win32uName, "NtGdiDdDDI");
    lstrcpynA(Win32uName + 10, Name + 6, sizeof(Win32uName) - 10);
    return (PVOID)GetProcAddress(LoadLibraryA("win32u.dll"), Win32uName);
}

static ACCESS_MASK
SharedAccessGranted(
    _In_ HANDLE Handle)
{
    PFN_QUERYOBJECT pNtQueryObject = (PFN_QUERYOBJECT)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryObject");
    SHAREDACCESS_BASIC_INFORMATION Basic;
    ULONG Length = 0;

    if (pNtQueryObject == NULL || pNtQueryObject(Handle, 0, &Basic, sizeof(Basic), &Length) != STATUS_SUCCESS)
        return 0xFFFFFFFF;
    return Basic.GrantedAccess;
}

static BOOLEAN
SharedAccessCreateResource(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ PVOID Memory,
    _Out_ D3DKMT_HANDLE *Resource)
{
    PFN_KMT1 pCreateAllocation = (PFN_KMT1)SharedAccessResolve("D3DKMTCreateAllocation");
    D3DKMT_CREATESTANDARDALLOCATION Standard;
    D3DDDI_ALLOCATIONINFO2 Info;
    D3DKMT_CREATEALLOCATION Create;

    *Resource = 0;
    if (pCreateAllocation == NULL)
        return FALSE;

    ZeroMemory(&Standard, sizeof(Standard));
    ZeroMemory(&Info, sizeof(Info));
    ZeroMemory(&Create, sizeof(Create));
    Standard.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
    Standard.ExistingHeapData.Size = 0x10000;
    Info.pSystemMem = Memory;
    Create.hDevice = hDevice;
    Create.pStandardAllocation = &Standard;
    Create.NumAllocations = 1;
    Create.pAllocationInfo2 = &Info;
    Create.Flags.CreateResource = 1;
    Create.Flags.CreateShared = 1;
    Create.Flags.NtSecuritySharing = 1;
    Create.Flags.ExistingSysMem = 1;
    Create.Flags.StandardAllocation = 1;
    if (NT_SUCCESS(pCreateAllocation(&Create)))
    {
        *Resource = Create.hResource;
        return TRUE;
    }

    /* TODO: drop the plain allocation once LiberNT creates standard existing-heap allocations */
    ZeroMemory(&Info, sizeof(Info));
    ZeroMemory(&Create, sizeof(Create));
    Create.hDevice = hDevice;
    Create.NumAllocations = 1;
    Create.pAllocationInfo2 = &Info;
    Create.Flags.CreateResource = 1;
    Create.Flags.CreateShared = 1;
    Create.Flags.NtSecuritySharing = 1;
    if (!NT_SUCCESS(pCreateAllocation(&Create)))
        return FALSE;
    *Resource = Create.hResource;
    return TRUE;
}

static BOOLEAN
SharedAccessCreateSync(
    _In_ D3DKMT_HANDLE hDevice,
    _Out_ D3DKMT_HANDLE *SyncObject)
{
    PFN_KMT1 pCreateSync2 = (PFN_KMT1)GetProcAddress(LoadLibraryA("gdi32.dll"), "D3DKMTCreateSynchronizationObject2");
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create;

    *SyncObject = 0;
    if (pCreateSync2 == NULL)
        pCreateSync2 = (PFN_KMT1)GetProcAddress(LoadLibraryA("win32u.dll"), "NtGdiDdDDICreateSynchronizationObject");
    if (pCreateSync2 == NULL)
        return FALSE;
    ZeroMemory(&Create, sizeof(Create));
    Create.hDevice = hDevice;
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.Flags.Shared = 1;
    Create.Info.Flags.NtSecuritySharing = 1;
    if (!NT_SUCCESS(pCreateSync2(&Create)))
        return FALSE;
    *SyncObject = Create.hSyncObject;
    return TRUE;
}

static VOID
SharedAccessCheck(
    _In_ PFN_KMTSHARE pShareObjects,
    _In_ D3DKMT_HANDLE Object,
    _In_ BOOLEAN IsSync)
{
    OBJECT_ATTRIBUTES Attributes;
    UINT Index;

    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
    for (Index = 0; Index < ARRAYSIZE(SharedAccessCases); ++Index)
    {
        const SHAREDACCESS_CASE *Case = &SharedAccessCases[Index];
        ACCESS_MASK Expected = IsSync ? Case->Sync : Case->Resource;
        HANDLE Shared = NULL;
        NTSTATUS Status;

        Status = pShareObjects(1, &Object, &Attributes, Case->Desired, &Shared);
        ok(Status == STATUS_SUCCESS, "%s share 0x%08lx: status 0x%08lx\n", IsSync ? "sync" : "resource", Case->Desired, Status);
        if (Status != STATUS_SUCCESS)
            continue;
        ok(SharedAccessGranted(Shared) == Expected, "%s share 0x%08lx: granted 0x%08lx, expected 0x%08lx\n",
           IsSync ? "sync" : "resource", Case->Desired, SharedAccessGranted(Shared), Expected);
        CloseHandle(Shared);
    }
}

START_TEST(sharedaccess)
{
    PFN_KMT1 pEnumAdapters2;
    PFN_KMT1 pCreateDevice;
    PFN_KMTSHARE pShareObjects;
    D3DKMT_ADAPTERINFO Adapters[16];
    D3DKMT_ENUMADAPTERS2 Enum;
    PVOID Memory;
    UINT Index;
    BOOLEAN Tested = FALSE;

    LoadLibraryA("gdi32.dll");
    pEnumAdapters2 = (PFN_KMT1)SharedAccessResolve("D3DKMTEnumAdapters2");
    pCreateDevice = (PFN_KMT1)SharedAccessResolve("D3DKMTCreateDevice");
    pShareObjects = (PFN_KMTSHARE)SharedAccessResolve("D3DKMTShareObjects");
    if (pEnumAdapters2 == NULL || pCreateDevice == NULL || pShareObjects == NULL)
    {
        skip("D3DKMT entry points are not available\n");
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

    Memory = VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Memory != NULL, "VirtualAlloc failed\n");
    if (Memory == NULL)
        return;

    for (Index = Enum.NumAdapters; Index > 0 && !Tested; --Index)
    {
        D3DKMT_CREATEDEVICE Device;
        D3DKMT_HANDLE Resource;
        D3DKMT_HANDLE SyncObject;

        ZeroMemory(&Device, sizeof(Device));
        Device.hAdapter = Adapters[Index - 1].hAdapter;
        if (!NT_SUCCESS(pCreateDevice(&Device)))
            continue;
        if (!SharedAccessCreateResource(Device.hDevice, Memory, &Resource))
            continue;
        SharedAccessCheck(pShareObjects, Resource, FALSE);
        ok(SharedAccessCreateSync(Device.hDevice, &SyncObject), "shared monitored fence was not created\n");
        if (SyncObject != 0)
            SharedAccessCheck(pShareObjects, SyncObject, TRUE);
        Tested = TRUE;
    }
    if (!Tested)
        skip("no adapter creates a shareable resource\n");
}
