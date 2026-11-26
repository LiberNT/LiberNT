/*
 * PROJECT:     LiberNT D3DKMT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Allocation entries read by the allocation creation and resource open system calls
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"

typedef NTSTATUS (APIENTRY *PFN_KMT1)(PVOID);
typedef NTSTATUS (APIENTRY *PFN_KMTSHARE)(UINT, const D3DKMT_HANDLE *, POBJECT_ATTRIBUTES, DWORD, HANDLE *);

#define ALLOCABI_SENTINEL 0xA5

typedef struct _ALLOCABI_CONTEXT
{
    PUCHAR Page;
    PUCHAR Guard;
    PVOID Memory;
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hDevice2;
} ALLOCABI_CONTEXT;

typedef enum _ALLOCABI_RECIPE
{
    AllocAbiExistingHeap,
    AllocAbiPlain,
    AllocAbiRecipeCount
} ALLOCABI_RECIPE;

static PFN_KMT1
AllocAbiWin32u(
    _In_ PCSTR Name)
{
    return (PFN_KMT1)GetProcAddress(LoadLibraryA("win32u.dll"), Name);
}

static PFN_KMT1
AllocAbiGdi32(
    _In_ PCSTR Name)
{
    return (PFN_KMT1)GetProcAddress(LoadLibraryA("gdi32.dll"), Name);
}

static NTSTATUS
AllocAbiGuarded(
    _In_ PFN_KMT1 Function,
    _In_opt_ PVOID Argument,
    _Out_ BOOLEAN *Raised)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    *Raised = FALSE;
    _SEH2_TRY
    {
        Status = Function(Argument);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        *Raised = TRUE;
    }
    _SEH2_END;
    return Status;
}

/* Entries of Size bytes that end where an inaccessible page starts */
static PUCHAR
AllocAbiEntries(
    _In_ const ALLOCABI_CONTEXT *Context,
    _In_ SIZE_T Size)
{
    memset(Context->Page, 0, 0x1000);
    return Context->Guard - Size;
}

static BOOLEAN
AllocAbiUntouched(
    _In_reads_bytes_(Size) const UCHAR *Bytes,
    _In_ SIZE_T Size)
{
    SIZE_T Index;

    for (Index = 0; Index < Size; ++Index)
    {
        if (Bytes[Index] != ALLOCABI_SENTINEL)
            return FALSE;
    }
    return TRUE;
}

static VOID
AllocAbiDestroy(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DKMT_HANDLE hResource,
    _In_reads_opt_(Count) const D3DKMT_HANDLE *Allocations,
    _In_ UINT Count)
{
    PFN_KMT1 pDestroyAllocation = AllocAbiWin32u("NtGdiDdDDIDestroyAllocation");
    D3DKMT_DESTROYALLOCATION Destroy;

    if (pDestroyAllocation == NULL)
        return;
    ZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.hDevice = hDevice;
    Destroy.hResource = hResource;
    if (hResource == 0)
    {
        Destroy.phAllocationList = Allocations;
        Destroy.AllocationCount = Count;
    }
    pDestroyAllocation(&Destroy);
}

/* The first entry starts at Entries, the second at Entries + Stride; both are D3DDDI_ALLOCATIONINFO2-shaped up to Stride */
static NTSTATUS
AllocAbiCreate(
    _In_ PFN_KMT1 pCreate,
    _In_ const ALLOCABI_CONTEXT *Context,
    _In_ ALLOCABI_RECIPE Recipe,
    _In_ UINT Count,
    _In_ BOOLEAN Shared,
    _Inout_ PUCHAR Entries,
    _In_ SIZE_T Stride,
    _Out_ D3DKMT_CREATEALLOCATION *Create)
{
    static D3DKMT_CREATESTANDARDALLOCATION Standard;
    UINT Index;

    ZeroMemory(&Standard, sizeof(Standard));
    ZeroMemory(Create, sizeof(*Create));
    for (Index = 0; Index < Count; ++Index)
    {
        D3DDDI_ALLOCATIONINFO2 *Info = (D3DDDI_ALLOCATIONINFO2 *)(Entries + Index * Stride);

        Info->hAllocation = 0;
        if (Recipe == AllocAbiExistingHeap)
            Info->pSystemMem = Context->Memory;
    }
    Create->hDevice = Context->hDevice;
    Create->NumAllocations = Count;
    Create->pAllocationInfo2 = (D3DDDI_ALLOCATIONINFO2 *)Entries;
    if (Recipe == AllocAbiExistingHeap)
    {
        Standard.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
        Standard.ExistingHeapData.Size = 0x10000;
        Create->pStandardAllocation = &Standard;
        Create->Flags.ExistingSysMem = 1;
        Create->Flags.StandardAllocation = 1;
    }
    if (Shared)
    {
        Create->Flags.CreateResource = 1;
        Create->Flags.CreateShared = 1;
    }
    return pCreate(Create);
}

static VOID
AllocAbiCreateTests(
    _In_ const ALLOCABI_CONTEXT *Context)
{
    PFN_KMT1 pCreate = AllocAbiWin32u("NtGdiDdDDICreateAllocation");
    PFN_KMT1 pGdiCreate = AllocAbiGdi32("D3DKMTCreateAllocation");
    PFN_KMT1 pGdiCreate2 = AllocAbiGdi32("D3DKMTCreateAllocation2");
    D3DKMT_CREATEALLOCATION Create;
    D3DKMT_HANDLE Handles[2];
    PUCHAR Entries;
    NTSTATUS Status;
    ALLOCABI_RECIPE Recipe;

    for (Recipe = 0; Recipe < AllocAbiRecipeCount; ++Recipe)
    {
        Entries = AllocAbiEntries(Context, sizeof(D3DDDI_ALLOCATIONINFO2));
        Status = AllocAbiCreate(pCreate, Context, Recipe, 1, FALSE, Entries, sizeof(D3DDDI_ALLOCATIONINFO2), &Create);
        if (NT_SUCCESS(Status))
            break;
        trace("recipe %d: entry ending at an inaccessible page: status 0x%08lx\n", Recipe, Status);
    }
    if (Recipe == AllocAbiRecipeCount)
    {
        skip("no allocation can be created on this adapter\n");
        return;
    }
    ok(((D3DDDI_ALLOCATIONINFO2 *)Entries)->hAllocation != 0, "recipe %d: no allocation handle\n", Recipe);
    AllocAbiDestroy(Context->hDevice, 0, &((D3DDDI_ALLOCATIONINFO2 *)Entries)->hAllocation, 1);

    Entries = AllocAbiEntries(Context, sizeof(D3DDDI_ALLOCATIONINFO));
    Status = AllocAbiCreate(pCreate, Context, Recipe, 1, FALSE, Entries, sizeof(D3DDDI_ALLOCATIONINFO), &Create);
    ok(!NT_SUCCESS(Status), "recipe %d: a D3DDDI_ALLOCATIONINFO-sized entry was accepted (0x%08lx)\n", Recipe, Status);
    if (NT_SUCCESS(Status))
        AllocAbiDestroy(Context->hDevice, 0, &((D3DDDI_ALLOCATIONINFO *)Entries)->hAllocation, 1);

    if (pGdiCreate != NULL)
    {
        Entries = AllocAbiEntries(Context, sizeof(D3DDDI_ALLOCATIONINFO));
        Status = AllocAbiCreate(pGdiCreate, Context, Recipe, 1, FALSE, Entries, sizeof(D3DDDI_ALLOCATIONINFO), &Create);
        ok(Status == STATUS_SUCCESS, "recipe %d: gdi32 v1 entry: status 0x%08lx\n", Recipe, Status);
        ok(!NT_SUCCESS(Status) || ((D3DDDI_ALLOCATIONINFO *)Entries)->hAllocation != 0, "recipe %d: gdi32 v1 entry: no handle\n", Recipe);
        if (NT_SUCCESS(Status))
            AllocAbiDestroy(Context->hDevice, 0, &((D3DDDI_ALLOCATIONINFO *)Entries)->hAllocation, 1);
    }
    if (pGdiCreate2 != NULL)
    {
        Entries = AllocAbiEntries(Context, sizeof(D3DDDI_ALLOCATIONINFO2));
        Status = AllocAbiCreate(pGdiCreate2, Context, Recipe, 1, FALSE, Entries, sizeof(D3DDDI_ALLOCATIONINFO2), &Create);
        ok(Status == STATUS_SUCCESS, "recipe %d: gdi32 v2 entry: status 0x%08lx\n", Recipe, Status);
        ok(!NT_SUCCESS(Status) || ((D3DDDI_ALLOCATIONINFO2 *)Entries)->hAllocation != 0, "recipe %d: gdi32 v2 entry: no handle\n", Recipe);
        if (NT_SUCCESS(Status))
            AllocAbiDestroy(Context->hDevice, 0, &((D3DDDI_ALLOCATIONINFO2 *)Entries)->hAllocation, 1);
    }

    /* Standard allocations take one entry; two entries need plain allocations */
    Entries = AllocAbiEntries(Context, 2 * sizeof(D3DDDI_ALLOCATIONINFO2));
    Status = AllocAbiCreate(pCreate, Context, AllocAbiPlain, 2, FALSE, Entries, sizeof(D3DDDI_ALLOCATIONINFO2), &Create);
    if (!NT_SUCCESS(Status))
    {
        skip("two plain allocations cannot be created on this adapter (0x%08lx)\n", Status);
        return;
    }
    Handles[0] = ((D3DDDI_ALLOCATIONINFO2 *)Entries)[0].hAllocation;
    Handles[1] = ((D3DDDI_ALLOCATIONINFO2 *)Entries)[1].hAllocation;
    ok(Handles[0] != 0 && Handles[1] != 0 && Handles[0] != Handles[1], "two entries: handles 0x%x 0x%x\n", Handles[0], Handles[1]);
    AllocAbiDestroy(Context->hDevice, 0, Handles, 2);

    if (pGdiCreate != NULL)
    {
        Entries = AllocAbiEntries(Context, 2 * sizeof(D3DDDI_ALLOCATIONINFO));
        Status = AllocAbiCreate(pGdiCreate, Context, AllocAbiPlain, 2, FALSE, Entries, sizeof(D3DDDI_ALLOCATIONINFO), &Create);
        ok(Status == STATUS_SUCCESS, "gdi32 v1 two entries: status 0x%08lx\n", Status);
        if (NT_SUCCESS(Status))
        {
            Handles[0] = ((D3DDDI_ALLOCATIONINFO *)Entries)[0].hAllocation;
            Handles[1] = ((D3DDDI_ALLOCATIONINFO *)Entries)[1].hAllocation;
            ok(Handles[0] != 0 && Handles[1] != 0 && Handles[0] != Handles[1], "gdi32 v1 two entries: handles 0x%x 0x%x\n", Handles[0], Handles[1]);
            AllocAbiDestroy(Context->hDevice, 0, Handles, 2);
        }
    }
}

static BOOLEAN
AllocAbiCreateShared(
    _In_ const ALLOCABI_CONTEXT *Context,
    _In_ UINT Count,
    _In_ BOOLEAN NtShared,
    _Out_ D3DKMT_CREATEALLOCATION *Create,
    _Out_ ALLOCABI_RECIPE *Created)
{
    PFN_KMT1 pCreate = AllocAbiWin32u("NtGdiDdDDICreateAllocation");
    static D3DDDI_ALLOCATIONINFO2 Info[2];
    ALLOCABI_RECIPE Recipe;

    for (Recipe = (Count == 1) ? AllocAbiExistingHeap : AllocAbiPlain; Recipe < AllocAbiRecipeCount; ++Recipe)
    {
        static D3DKMT_CREATESTANDARDALLOCATION Standard;
        UINT Index;

        ZeroMemory(Info, sizeof(Info));
        ZeroMemory(&Standard, sizeof(Standard));
        ZeroMemory(Create, sizeof(*Create));
        Create->hDevice = Context->hDevice;
        Create->NumAllocations = Count;
        Create->pAllocationInfo2 = Info;
        Create->Flags.CreateResource = 1;
        Create->Flags.CreateShared = 1;
        Create->Flags.NtSecuritySharing = NtShared;
        if (Recipe == AllocAbiExistingHeap)
        {
            for (Index = 0; Index < Count; ++Index)
                Info[Index].pSystemMem = Context->Memory;
            Standard.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
            Standard.ExistingHeapData.Size = 0x10000;
            Create->pStandardAllocation = &Standard;
            Create->Flags.ExistingSysMem = 1;
            Create->Flags.StandardAllocation = 1;
        }
        if (NT_SUCCESS(pCreate(Create)))
        {
            *Created = Recipe;
            return TRUE;
        }
    }
    return FALSE;
}

typedef struct _ALLOCABI_OPEN
{
    UINT NumAllocations;
    UINT TotalSize;
    UINT RuntimeSize;
    UINT ResourceSize;
} ALLOCABI_OPEN;

static UCHAR AllocAbiRuntimeData[0x400];
static UCHAR AllocAbiResourceData[0x400];
static UCHAR AllocAbiTotalData[0x1000];

static BOOLEAN
AllocAbiQuery(
    _In_ const ALLOCABI_CONTEXT *Context,
    _In_ D3DKMT_HANDLE hGlobalShare,
    _In_opt_ HANDLE hNtHandle,
    _Out_ ALLOCABI_OPEN *Info)
{
    NTSTATUS Status;

    if (hNtHandle != NULL)
    {
        PFN_KMT1 pQuery = AllocAbiWin32u("NtGdiDdDDIQueryResourceInfoFromNtHandle");
        D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE Query;

        ZeroMemory(&Query, sizeof(Query));
        Query.hDevice = Context->hDevice2;
        Query.hNtHandle = hNtHandle;
        Query.pPrivateRuntimeData = AllocAbiRuntimeData;
        Query.PrivateRuntimeDataSize = sizeof(AllocAbiRuntimeData);
        Status = pQuery != NULL ? pQuery(&Query) : STATUS_PROCEDURE_NOT_FOUND;
        Info->NumAllocations = Query.NumAllocations;
        Info->TotalSize = Query.TotalPrivateDriverDataSize;
        Info->RuntimeSize = Query.PrivateRuntimeDataSize;
        Info->ResourceSize = Query.ResourcePrivateDriverDataSize;
    }
    else
    {
        PFN_KMT1 pQuery = AllocAbiWin32u("NtGdiDdDDIQueryResourceInfo");
        D3DKMT_QUERYRESOURCEINFO Query;

        ZeroMemory(&Query, sizeof(Query));
        Query.hDevice = Context->hDevice2;
        Query.hGlobalShare = hGlobalShare;
        Query.pPrivateRuntimeData = AllocAbiRuntimeData;
        Query.PrivateRuntimeDataSize = sizeof(AllocAbiRuntimeData);
        Status = pQuery != NULL ? pQuery(&Query) : STATUS_PROCEDURE_NOT_FOUND;
        Info->NumAllocations = Query.NumAllocations;
        Info->TotalSize = Query.TotalPrivateDriverDataSize;
        Info->RuntimeSize = Query.PrivateRuntimeDataSize;
        Info->ResourceSize = Query.ResourcePrivateDriverDataSize;
    }
    ok(Status == STATUS_SUCCESS, "query of the shared resource: status 0x%08lx\n", Status);
    return NT_SUCCESS(Status) && Info->TotalSize <= sizeof(AllocAbiTotalData) &&
           Info->RuntimeSize <= sizeof(AllocAbiRuntimeData) && Info->ResourceSize <= sizeof(AllocAbiResourceData);
}

/* Opens the resource through pOpen with NumAllocations entries of EntrySize bytes at Stride; returns the opened resource */
static NTSTATUS
AllocAbiOpen(
    _In_ PFN_KMT1 pOpen,
    _In_ const ALLOCABI_CONTEXT *Context,
    _In_ D3DKMT_HANDLE hGlobalShare,
    _In_opt_ HANDLE hNtHandle,
    _In_ const ALLOCABI_OPEN *Info,
    _In_ PUCHAR Entries,
    _Out_ D3DKMT_HANDLE *hResource,
    _Out_ BOOLEAN *Raised)
{
    NTSTATUS Status;

    if (hNtHandle != NULL)
    {
        D3DKMT_OPENRESOURCEFROMNTHANDLE Open;

        ZeroMemory(&Open, sizeof(Open));
        Open.hDevice = Context->hDevice2;
        Open.hNtHandle = hNtHandle;
        Open.NumAllocations = Info->NumAllocations;
        Open.pOpenAllocationInfo2 = (D3DDDI_OPENALLOCATIONINFO2 *)Entries;
        Open.pPrivateRuntimeData = AllocAbiRuntimeData;
        Open.PrivateRuntimeDataSize = Info->RuntimeSize;
        Open.pResourcePrivateDriverData = AllocAbiResourceData;
        Open.ResourcePrivateDriverDataSize = Info->ResourceSize;
        Open.pTotalPrivateDriverDataBuffer = AllocAbiTotalData;
        Open.TotalPrivateDriverDataBufferSize = Info->TotalSize;
        Status = AllocAbiGuarded(pOpen, &Open, Raised);
        *hResource = Open.hResource;
    }
    else
    {
        D3DKMT_OPENRESOURCE Open;

        ZeroMemory(&Open, sizeof(Open));
        Open.hDevice = Context->hDevice2;
        Open.hGlobalShare = hGlobalShare;
        Open.NumAllocations = Info->NumAllocations;
        Open.pOpenAllocationInfo = (D3DDDI_OPENALLOCATIONINFO *)Entries;
        Open.pPrivateRuntimeData = AllocAbiRuntimeData;
        Open.PrivateRuntimeDataSize = Info->RuntimeSize;
        Open.pResourcePrivateDriverData = AllocAbiResourceData;
        Open.ResourcePrivateDriverDataSize = Info->ResourceSize;
        Open.pTotalPrivateDriverDataBuffer = AllocAbiTotalData;
        Open.TotalPrivateDriverDataBufferSize = Info->TotalSize;
        Status = AllocAbiGuarded(pOpen, &Open, Raised);
        *hResource = Open.hResource;
    }
    return Status;
}

static PUCHAR
AllocAbiOpenEntries(
    _In_ const ALLOCABI_CONTEXT *Context,
    _In_ UINT Count,
    _In_ SIZE_T Stride,
    _In_ SIZE_T Size)
{
    PUCHAR Entries = AllocAbiEntries(Context, Size);
    UINT Index;

    memset(Entries, ALLOCABI_SENTINEL, Size);
    for (Index = 0; Index < Count; ++Index)
    {
        D3DDDI_OPENALLOCATIONINFO *Entry = (D3DDDI_OPENALLOCATIONINFO *)(Entries + Index * Stride);

        Entry->hAllocation = 0;
        Entry->pPrivateDriverData = NULL;
        Entry->PrivateDriverDataSize = 0;
    }
    return Entries;
}

static VOID
AllocAbiOpenOne(
    _In_ PCSTR Label,
    _In_ PFN_KMT1 pOpen,
    _In_ const ALLOCABI_CONTEXT *Context,
    _In_ D3DKMT_HANDLE hGlobalShare,
    _In_opt_ HANDLE hNtHandle,
    _In_ SIZE_T EntrySize,
    _In_ ALLOCABI_RECIPE Recipe)
{
    SIZE_T Reserved = FIELD_OFFSET(D3DDDI_OPENALLOCATIONINFO2, Reserved);
    ALLOCABI_OPEN Info;
    D3DDDI_OPENALLOCATIONINFO *Entry;
    D3DKMT_HANDLE hResource = 0;
    PUCHAR Entries;
    NTSTATUS Status;
    BOOLEAN Raised;
    SIZE_T Fields = FIELD_OFFSET(D3DDDI_OPENALLOCATIONINFO2, GpuVirtualAddress);

    if (!AllocAbiQuery(Context, hGlobalShare, hNtHandle, &Info))
        return;
    ok(Info.NumAllocations == 1, "%s: %u allocations\n", Label, Info.NumAllocations);
    Info.NumAllocations = 1;
    Entries = AllocAbiOpenEntries(Context, 1, EntrySize, EntrySize);
    Entry = (D3DDDI_OPENALLOCATIONINFO *)Entries;
    Status = AllocAbiOpen(pOpen, Context, hGlobalShare, hNtHandle, &Info, Entries, &hResource, &Raised);
    ok(Status == STATUS_SUCCESS && !Raised, "%s, %lu-byte entry: status 0x%08lx%s\n", Label, (ULONG)EntrySize, Status, Raised ? " raised" : "");
    if (!NT_SUCCESS(Status) || Raised)
        return;
    ok(hResource != 0, "%s, %lu-byte entry: no resource handle\n", Label, (ULONG)EntrySize);
    ok(Entry->hAllocation != 0, "%s, %lu-byte entry: no allocation handle\n", Label, (ULONG)EntrySize);
    ok(Entry->PrivateDriverDataSize == Info.TotalSize, "%s, %lu-byte entry: private data size %u, expected %u\n", Label,
       (ULONG)EntrySize, Entry->PrivateDriverDataSize, Info.TotalSize);
    ok(Info.TotalSize == 0 || Entry->pPrivateDriverData == AllocAbiTotalData, "%s, %lu-byte entry: private data %p, expected %p\n",
       Label, (ULONG)EntrySize, Entry->pPrivateDriverData, AllocAbiTotalData);
    if (EntrySize > Reserved)
        ok(AllocAbiUntouched(Entries + Reserved, EntrySize - Reserved), "%s: Reserved written\n", Label);
    /* TODO: pin GpuVirtualAddress for an opened allocation that has a GPU virtual address */
    if (EntrySize > Fields && Recipe == AllocAbiExistingHeap)
        ok(AllocAbiUntouched(Entries + Fields, Reserved - Fields), "%s: GpuVirtualAddress written\n", Label);
    AllocAbiDestroy(Context->hDevice2, hResource, NULL, 0);
}

static VOID
AllocAbiOpenTwo(
    _In_ PCSTR Label,
    _In_ PFN_KMT1 pOpen,
    _In_ const ALLOCABI_CONTEXT *Context,
    _In_ D3DKMT_HANDLE hGlobalShare,
    _In_ SIZE_T Stride)
{
    ALLOCABI_OPEN Info;
    D3DKMT_HANDLE hResource = 0;
    D3DKMT_HANDLE Handles[2];
    PUCHAR Entries;
    NTSTATUS Status;
    BOOLEAN Raised;

    if (!AllocAbiQuery(Context, hGlobalShare, NULL, &Info))
        return;
    ok(Info.NumAllocations == 2, "%s: %u allocations\n", Label, Info.NumAllocations);
    Info.NumAllocations = 2;
    Entries = AllocAbiOpenEntries(Context, 2, Stride, 2 * Stride);
    Status = AllocAbiOpen(pOpen, Context, hGlobalShare, NULL, &Info, Entries, &hResource, &Raised);
    ok(Status == STATUS_SUCCESS && !Raised, "%s: status 0x%08lx%s\n", Label, Status, Raised ? " raised" : "");
    if (!NT_SUCCESS(Status) || Raised)
        return;
    Handles[0] = ((D3DDDI_OPENALLOCATIONINFO *)Entries)->hAllocation;
    Handles[1] = ((D3DDDI_OPENALLOCATIONINFO *)(Entries + Stride))->hAllocation;
    ok(Handles[0] != 0 && Handles[1] != 0 && Handles[0] != Handles[1], "%s: handles 0x%x 0x%x\n", Label, Handles[0], Handles[1]);
    AllocAbiDestroy(Context->hDevice2, hResource, NULL, 0);
}

static VOID
AllocAbiOpenTests(
    _In_ const ALLOCABI_CONTEXT *Context)
{
    PFN_KMT1 pOpen = AllocAbiWin32u("NtGdiDdDDIOpenResource");
    PFN_KMT1 pOpenNt = AllocAbiWin32u("NtGdiDdDDIOpenResourceFromNtHandle");
    PFN_KMTSHARE pShare = (PFN_KMTSHARE)AllocAbiWin32u("NtGdiDdDDIShareObjects");
    PFN_KMT1 pGdiOpen = AllocAbiGdi32("D3DKMTOpenResource");
    PFN_KMT1 pGdiOpen2 = AllocAbiGdi32("D3DKMTOpenResource2");
    D3DKMT_CREATEALLOCATION Create;
    OBJECT_ATTRIBUTES Attributes;
    ALLOCABI_RECIPE Recipe;
    HANDLE hNtHandle = NULL;
    NTSTATUS Status;

    if (!AllocAbiCreateShared(Context, 1, FALSE, &Create, &Recipe))
    {
        skip("no shared resource can be created on this adapter\n");
        return;
    }
    AllocAbiOpenOne("win32u open", pOpen, Context, Create.hGlobalShare, NULL, FIELD_OFFSET(D3DDDI_OPENALLOCATIONINFO2, GpuVirtualAddress), Recipe);
    AllocAbiOpenOne("win32u open", pOpen, Context, Create.hGlobalShare, NULL, sizeof(D3DDDI_OPENALLOCATIONINFO2), Recipe);
    if (pGdiOpen != NULL)
        AllocAbiOpenOne("gdi32 v1 open", pGdiOpen, Context, Create.hGlobalShare, NULL, sizeof(D3DDDI_OPENALLOCATIONINFO), Recipe);
    if (pGdiOpen2 != NULL)
        AllocAbiOpenOne("gdi32 v2 open", pGdiOpen2, Context, Create.hGlobalShare, NULL, sizeof(D3DDDI_OPENALLOCATIONINFO2), Recipe);
    AllocAbiDestroy(Context->hDevice, Create.hResource, NULL, 0);

    if (pShare != NULL && pOpenNt != NULL && AllocAbiCreateShared(Context, 1, TRUE, &Create, &Recipe))
    {
        InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
        Status = pShare(1, &Create.hResource, &Attributes, GENERIC_ALL, &hNtHandle);
        ok(Status == STATUS_SUCCESS, "share: status 0x%08lx\n", Status);
        if (NT_SUCCESS(Status))
        {
            AllocAbiOpenOne("win32u open from NT handle", pOpenNt, Context, 0, hNtHandle, FIELD_OFFSET(D3DDDI_OPENALLOCATIONINFO2, GpuVirtualAddress), Recipe);
            AllocAbiOpenOne("win32u open from NT handle", pOpenNt, Context, 0, hNtHandle, sizeof(D3DDDI_OPENALLOCATIONINFO2), Recipe);
            CloseHandle(hNtHandle);
        }
        AllocAbiDestroy(Context->hDevice, Create.hResource, NULL, 0);
    }

    if (!AllocAbiCreateShared(Context, 2, FALSE, &Create, &Recipe))
    {
        skip("no shared resource with two allocations can be created on this adapter\n");
        return;
    }
    AllocAbiOpenTwo("win32u open, two entries", pOpen, Context, Create.hGlobalShare, sizeof(D3DDDI_OPENALLOCATIONINFO2));
    if (pGdiOpen != NULL)
        AllocAbiOpenTwo("gdi32 v1 open, two entries", pGdiOpen, Context, Create.hGlobalShare, sizeof(D3DDDI_OPENALLOCATIONINFO));
    if (pGdiOpen2 != NULL)
        AllocAbiOpenTwo("gdi32 v2 open, two entries", pGdiOpen2, Context, Create.hGlobalShare, sizeof(D3DDDI_OPENALLOCATIONINFO2));
    AllocAbiDestroy(Context->hDevice, Create.hResource, NULL, 0);
}

START_TEST(allocabi)
{
    PFN_KMT1 pEnumAdapters2;
    PFN_KMT1 pCreateDevice;
    PFN_KMT1 pGdiCreate;
    PFN_KMT1 pGdiOpen;
    D3DKMT_ADAPTERINFO Adapters[16];
    D3DKMT_ENUMADAPTERS2 Enum;
    ALLOCABI_CONTEXT Context;
    DWORD OldProtect;
    NTSTATUS Status;
    BOOLEAN Raised;
    UINT Adapter;

    LoadLibraryA("gdi32.dll");
    ok(AllocAbiWin32u("NtGdiDdDDICreateAllocation") != NULL, "win32u does not export NtGdiDdDDICreateAllocation\n");
    ok(AllocAbiWin32u("NtGdiDdDDIOpenResource") != NULL, "win32u does not export NtGdiDdDDIOpenResource\n");
    ok(AllocAbiWin32u("NtGdiDdDDICreateAllocation2") == NULL, "win32u exports NtGdiDdDDICreateAllocation2\n");
    ok(AllocAbiWin32u("NtGdiDdDDIOpenResource2") == NULL, "win32u exports NtGdiDdDDIOpenResource2\n");

    pGdiCreate = AllocAbiGdi32("D3DKMTCreateAllocation");
    pGdiOpen = AllocAbiGdi32("D3DKMTOpenResource");
    if (pGdiCreate != NULL)
    {
        Status = AllocAbiGuarded(pGdiCreate, NULL, &Raised);
        ok(Raised && Status == STATUS_ACCESS_VIOLATION, "gdi32 v1 create with no descriptor: 0x%08lx%s\n", Status, Raised ? " raised" : "");
    }
    if (pGdiOpen != NULL)
    {
        Status = AllocAbiGuarded(pGdiOpen, NULL, &Raised);
        ok(!Raised && Status == STATUS_INVALID_PARAMETER, "gdi32 v1 open with no descriptor: 0x%08lx%s\n", Status, Raised ? " raised" : "");
    }

    pEnumAdapters2 = AllocAbiWin32u("NtGdiDdDDIEnumAdapters2");
    pCreateDevice = AllocAbiWin32u("NtGdiDdDDICreateDevice");
    if (pEnumAdapters2 == NULL || pCreateDevice == NULL || AllocAbiWin32u("NtGdiDdDDICreateAllocation") == NULL)
        return;
    ZeroMemory(&Context, sizeof(Context));
    Context.Memory = VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Context.Page = VirtualAlloc(NULL, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (Context.Memory == NULL || Context.Page == NULL || !VirtualProtect(Context.Page + 0x1000, 0x1000, PAGE_NOACCESS, &OldProtect))
    {
        skip("no memory for the entries\n");
        return;
    }
    Context.Guard = Context.Page + 0x1000;

    ZeroMemory(&Enum, sizeof(Enum));
    Enum.NumAdapters = ARRAYSIZE(Adapters);
    Enum.pAdapters = Adapters;
    if (!NT_SUCCESS(pEnumAdapters2(&Enum)) || Enum.NumAdapters == 0)
    {
        skip("no D3DKMT adapter\n");
        return;
    }
    for (Adapter = 0; Adapter < Enum.NumAdapters; ++Adapter)
    {
        D3DKMT_CREATEDEVICE Device, Device2;

        ZeroMemory(&Device, sizeof(Device));
        Device.hAdapter = Adapters[Adapter].hAdapter;
        Device2 = Device;
        if (!NT_SUCCESS(pCreateDevice(&Device)) || !NT_SUCCESS(pCreateDevice(&Device2)))
        {
            trace("adapter %u: no device\n", Adapter);
            continue;
        }
        Context.hDevice = Device.hDevice;
        Context.hDevice2 = Device2.hDevice;
        AllocAbiCreateTests(&Context);
        AllocAbiOpenTests(&Context);
    }
}
