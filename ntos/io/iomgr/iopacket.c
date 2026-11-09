/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Where an I/O request packet comes from and how it is given back
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

GENERAL_LOOKASIDE IoSmallIrpLookaside;
GENERAL_LOOKASIDE IoLargeIrpLookaside;
GENERAL_LOOKASIDE IoCompletionPacketLookaside;
GENERAL_LOOKASIDE IopMdlLookasideList;

RESERVE_IRP_ALLOCATOR IopReserveIrpAllocator;

ULONG IoPageReadIrpAllocationFailure = 0;
ULONG IoPageReadNonPagefileIrpAllocationFailure = 0;

/* PRIVATE FUNCTIONS **********************************************************/

static
PIRP
IopAllocateIrpFromLookaside(
    _In_ CCHAR StackSize,
    _Out_ PUCHAR AllocationFlags)
{
    PGENERAL_LOOKASIDE Lookaside;
    PVOID Entry;
    USHORT Size;

    if (StackSize <= 1)
    {
        Lookaside = &IoSmallIrpLookaside;
        Size = IoSizeOfIrp(1);
    }
    else
    {
        Lookaside = &IoLargeIrpLookaside;
        Size = IoSizeOfIrp(IOP_FIXED_STACK_SIZE);
    }

    Lookaside->TotalAllocates++;
    Entry = InterlockedPopEntrySList(&Lookaside->ListHead);

    if (Entry == NULL)
    {
        Lookaside->AllocateMisses++;
        Entry = ExAllocatePoolWithTag(Lookaside->Type, Size, TAG_IRP);
        if (Entry == NULL) return NULL;
        *AllocationFlags = IRP_ALLOCATED_FIXED_SIZE;
        return (PIRP)Entry;
    }

    *AllocationFlags = IRP_ALLOCATED_FIXED_SIZE | IRP_LOOKASIDE_ALLOCATION;
    return (PIRP)Entry;
}

static
VOID
IopFreeIrpToLookaside(
    _In_ PIRP Irp)
{
    PGENERAL_LOOKASIDE Lookaside;

    Lookaside = (Irp->StackCount <= 1) ? &IoSmallIrpLookaside
                                       : &IoLargeIrpLookaside;

    Lookaside->TotalFrees++;

    if (ExQueryDepthSList(&Lookaside->ListHead) >= Lookaside->Depth)
    {
        Lookaside->FreeMisses++;
        ExFreePoolWithTag(Irp, TAG_IRP);
        return;
    }

    InterlockedPushEntrySList(&Lookaside->ListHead, (PSLIST_ENTRY)Irp);
}

PIOP_IRP_EXTENSION
IopGetIrpExtension(
    _In_ PIRP Irp,
    _In_ BOOLEAN Allocate)
{
    PIOP_IRP_EXTENSION Extension;

    Extension = Irp->Tail.Overlay.IrpExtension;
    if (Extension != NULL || !Allocate) return Extension;

    Extension = ExAllocatePoolZero(NonPagedPool,
                                   sizeof(IOP_IRP_EXTENSION),
                                   TAG_IRP_EXTENSION);
    if (Extension == NULL) return NULL;

    Extension->Allocated = TRUE;
    Irp->Tail.Overlay.IrpExtension = Extension;

    return Extension;
}

VOID
NTAPI
IopFreeIrpExtension(
    _Inout_ PIRP Irp)
{
    PIOP_IRP_EXTENSION Extension = Irp->Tail.Overlay.IrpExtension;

    if (Extension == NULL) return;

    Irp->Tail.Overlay.IrpExtension = NULL;

    if (Extension->Allocated)
    {
        ExFreePoolWithTag(Extension, TAG_IRP_EXTENSION);
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
IoInitializeIrp(
    _Inout_ PIRP Irp,
    _In_ USHORT PacketSize,
    _In_ CCHAR StackSize)
{
    ASSERT(Irp != NULL);
    ASSERT(StackSize > 0);

    RtlZeroMemory(Irp, PacketSize);

    Irp->Type = IO_TYPE_IRP;
    Irp->Size = PacketSize;
    Irp->StackCount = StackSize;
    Irp->CurrentLocation = StackSize + 1;
    Irp->ApcEnvironment = KeGetCurrentThread()->ApcStateIndex;
    Irp->Tail.Overlay.CurrentStackLocation = (PIO_STACK_LOCATION)
        ((PUCHAR)Irp + sizeof(IRP) + StackSize * sizeof(IO_STACK_LOCATION));

    InitializeListHead(&Irp->ThreadListEntry);
}

PIRP
NTAPI
IoAllocateIrp(
    _In_ CCHAR StackSize,
    _In_ BOOLEAN ChargeQuota)
{
    UCHAR AllocationFlags = 0;
    USHORT PacketSize;
    PIRP Irp;

    if (StackSize <= 0) return NULL;

    if (StackSize <= IOP_FIXED_STACK_SIZE && !ChargeQuota)
    {
        Irp = IopAllocateIrpFromLookaside(StackSize, &AllocationFlags);
        if (Irp != NULL)
        {
            PacketSize = (StackSize <= 1) ? IoSizeOfIrp(1)
                                          : IoSizeOfIrp(IOP_FIXED_STACK_SIZE);
            IoInitializeIrp(Irp, PacketSize, StackSize);
            Irp->AllocationFlags = AllocationFlags;
            return Irp;
        }

        return NULL;
    }

    PacketSize = IoSizeOfIrp(StackSize);

    if (ChargeQuota)
    {
        Irp = ExAllocatePoolWithQuotaTag(NonPagedPool | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE,
                                         PacketSize,
                                         TAG_IRP);
        if (Irp == NULL) return NULL;
        AllocationFlags = IRP_QUOTA_CHARGED;
    }
    else
    {
        Irp = ExAllocatePoolWithTag(NonPagedPool, PacketSize, TAG_IRP);
        if (Irp == NULL) return NULL;
    }

    IoInitializeIrp(Irp, PacketSize, StackSize);
    Irp->AllocationFlags = AllocationFlags;

    return Irp;
}

VOID
NTAPI
IoFreeIrp(
    _In_ PIRP Irp)
{
    ASSERT(Irp != NULL);
    ASSERT(Irp->Type == IO_TYPE_IRP);
    ASSERT(IsListEmpty(&Irp->ThreadListEntry));
    ASSERT(Irp->CurrentLocation >= Irp->StackCount);

    if (Irp->Tail.Overlay.IrpExtension != NULL) IopFreeIrpExtension(Irp);

    if (Irp == IopReserveIrpAllocator.ReserveIrp)
    {
        IopFreeReserveIrp(IO_NO_INCREMENT);
        return;
    }

    if (Irp->AllocationFlags & IRP_LOOKASIDE_ALLOCATION)
    {
        IopFreeIrpToLookaside(Irp);
        return;
    }

    if (Irp->AllocationFlags & IRP_QUOTA_CHARGED)
    {
        ExFreePoolWithTag(Irp, TAG_IRP);
        return;
    }

    ExFreePoolWithTag(Irp, TAG_IRP);
}

VOID
NTAPI
IoReuseIrp(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status)
{
    UCHAR AllocationFlags;
    USHORT PacketSize;
    CCHAR StackSize;

    ASSERT(Irp != NULL);
    ASSERT(Irp->Type == IO_TYPE_IRP);

    AllocationFlags = Irp->AllocationFlags;
    PacketSize = Irp->Size;
    StackSize = Irp->StackCount;

    if (Irp->Tail.Overlay.IrpExtension != NULL) IopFreeIrpExtension(Irp);

    IoInitializeIrp(Irp, PacketSize, StackSize);

    Irp->AllocationFlags = AllocationFlags;
    Irp->IoStatus.Status = Status;
}

VOID
NTAPI
IoCleanupIrp(
    _Inout_ PIRP Irp)
{
    PMDL Mdl;

    if (Irp->Flags & IRP_DEALLOCATE_BUFFER)
    {
        ExFreePool(Irp->AssociatedIrp.SystemBuffer);
        Irp->AssociatedIrp.SystemBuffer = NULL;
        Irp->Flags &= ~IRP_DEALLOCATE_BUFFER;
    }

    while (Irp->MdlAddress != NULL)
    {
        Mdl = Irp->MdlAddress;
        Irp->MdlAddress = Mdl->Next;

        if (Mdl->MdlFlags & MDL_PAGES_LOCKED) MmUnlockPages(Mdl);
        IoFreeMdl(Mdl);
    }

    if (Irp->Tail.Overlay.AuxiliaryBuffer != NULL)
    {
        ExFreePool(Irp->Tail.Overlay.AuxiliaryBuffer);
        Irp->Tail.Overlay.AuxiliaryBuffer = NULL;
    }

    if (Irp->Tail.Overlay.IrpExtension != NULL) IopFreeIrpExtension(Irp);
}

VOID
NTAPI
IopCleanupIrp(
    _In_ PIRP Irp,
    _In_opt_ PFILE_OBJECT FileObject)
{
    IoCleanupIrp(Irp);

    if (FileObject != NULL) ObDereferenceObject(FileObject);

    IoFreeIrp(Irp);
}

PIRP
NTAPI
IoMakeAssociatedIrp(
    _In_ PIRP Irp,
    _In_ CCHAR StackSize)
{
    PIRP AssociatedIrp;

    AssociatedIrp = IoAllocateIrp(StackSize, FALSE);
    if (AssociatedIrp == NULL) return NULL;

    AssociatedIrp->Flags |= IRP_ASSOCIATED_IRP;
    AssociatedIrp->AssociatedIrp.MasterIrp = Irp;
    AssociatedIrp->Tail.Overlay.Thread = Irp->Tail.Overlay.Thread;
    AssociatedIrp->RequestorMode = KernelMode;

    return AssociatedIrp;
}

PIRP
NTAPI
IopAllocateIrpMustSucceed(
    IN CCHAR StackSize)
{
    LARGE_INTEGER Interval;
    PIRP Irp;

    Irp = IoAllocateIrp(StackSize, FALSE);
    if (Irp != NULL) return Irp;

    Interval.QuadPart = -10 * 1000 * 10;

    while (TRUE)
    {
        Irp = IoAllocateIrp(StackSize, FALSE);
        if (Irp != NULL)
        {
            Irp->AllocationFlags |= IRP_ALLOCATED_MUST_SUCCEED;
            return Irp;
        }

        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }
}

/* RESERVE IRP ****************************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
IopInitializeReserveIrp(
    IN PRESERVE_IRP_ALLOCATOR ReserveIrpAllocator)
{
    ReserveIrpAllocator->StackSize = IOP_FIXED_STACK_SIZE;

    ReserveIrpAllocator->ReserveIrp =
        IoAllocateIrp(ReserveIrpAllocator->StackSize, FALSE);
    if (ReserveIrpAllocator->ReserveIrp == NULL) return FALSE;

    ReserveIrpAllocator->ReserveIrpInUse = 0;
    KeInitializeEvent(&ReserveIrpAllocator->WaitEvent,
                      SynchronizationEvent,
                      FALSE);

    return TRUE;
}

PIRP
NTAPI
IopAllocateReserveIrp(
    IN CCHAR StackSize)
{
    if (StackSize > IopReserveIrpAllocator.StackSize) return NULL;

    while (InterlockedCompareExchange(&IopReserveIrpAllocator.ReserveIrpInUse,
                                      1,
                                      0) != 0)
    {
        KeWaitForSingleObject(&IopReserveIrpAllocator.WaitEvent,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }

    IoInitializeIrp(IopReserveIrpAllocator.ReserveIrp,
                    IoSizeOfIrp(IopReserveIrpAllocator.StackSize),
                    StackSize);

    return IopReserveIrpAllocator.ReserveIrp;
}

VOID
IopFreeReserveIrp(
    _In_ CCHAR PriorityBoost)
{
    InterlockedExchange(&IopReserveIrpAllocator.ReserveIrpInUse, 0);
    KeSetEvent(&IopReserveIrpAllocator.WaitEvent, PriorityBoost, FALSE);
}

/* MEMORY DESCRIPTOR LISTS ****************************************************/

PMDL
NTAPI
IoAllocateMdl(
    _In_opt_ PVOID VirtualAddress,
    _In_ ULONG Length,
    _In_ BOOLEAN SecondaryBuffer,
    _In_ BOOLEAN ChargeQuota,
    _Inout_opt_ PIRP Irp)
{
    ULONG PageCount;
    ULONG Size;
    PMDL Mdl;
    PMDL Tail;

    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(VirtualAddress, Length);
    if (PageCount == 0) return NULL;
    if (PageCount > (MAXUSHORT - sizeof(MDL)) / sizeof(PFN_NUMBER)) return NULL;

    Size = (ULONG)(sizeof(MDL) + PageCount * sizeof(PFN_NUMBER));

    if (Size <= IopMdlLookasideList.Size)
    {
        Mdl = (PMDL)InterlockedPopEntrySList(&IopMdlLookasideList.ListHead);
        IopMdlLookasideList.TotalAllocates++;

        if (Mdl == NULL)
        {
            IopMdlLookasideList.AllocateMisses++;
            Mdl = ExAllocatePoolWithTag(NonPagedPool,
                                        IopMdlLookasideList.Size,
                                        TAG_MDL);
        }
    }
    else if (ChargeQuota)
    {
        Mdl = ExAllocatePoolWithQuotaTag(NonPagedPool | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE,
                                         Size,
                                         TAG_MDL);
    }
    else
    {
        Mdl = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_MDL);
    }

    if (Mdl == NULL) return NULL;

    MmInitializeMdl(Mdl, VirtualAddress, Length);

    if (Size <= IopMdlLookasideList.Size)
    {
        Mdl->MdlFlags |= MDL_ALLOCATED_FIXED_SIZE;
    }

    if (Irp == NULL) return Mdl;

    if (!SecondaryBuffer)
    {
        Irp->MdlAddress = Mdl;
        return Mdl;
    }

    Tail = Irp->MdlAddress;
    if (Tail == NULL)
    {
        Irp->MdlAddress = Mdl;
        return Mdl;
    }

    while (Tail->Next != NULL) Tail = Tail->Next;
    Tail->Next = Mdl;

    return Mdl;
}

VOID
NTAPI
IoFreeMdl(
    _In_ PMDL Mdl)
{
    ASSERT(Mdl != NULL);
    ASSERT(!(Mdl->MdlFlags & MDL_PAGES_LOCKED));
    ASSERT(!(Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA));

    if (Mdl->MdlFlags & MDL_PARTIAL_HAS_BEEN_MAPPED)
    {
        MmUnmapLockedPages(Mdl->MappedSystemVa, Mdl);
    }

    if (Mdl->MdlFlags & MDL_ALLOCATED_FIXED_SIZE)
    {
        IopMdlLookasideList.TotalFrees++;

        if (ExQueryDepthSList(&IopMdlLookasideList.ListHead) <
            IopMdlLookasideList.Depth)
        {
            InterlockedPushEntrySList(&IopMdlLookasideList.ListHead,
                                      (PSLIST_ENTRY)Mdl);
            return;
        }

        IopMdlLookasideList.FreeMisses++;
    }

    ExFreePoolWithTag(Mdl, TAG_MDL);
}

VOID
NTAPI
IoBuildPartialMdl(
    _In_ PMDL SourceMdl,
    _Inout_ PMDL TargetMdl,
    _In_ PVOID VirtualAddress,
    _In_ ULONG Length)
{
    PPFN_NUMBER SourcePages;
    PPFN_NUMBER TargetPages;
    ULONG_PTR SourceBase;
    ULONG_PTR Offset;
    ULONG PageCount;
    ULONG Available;
    ULONG Index;

    SourceBase = (ULONG_PTR)MmGetMdlVirtualAddress(SourceMdl);
    Offset = (ULONG_PTR)VirtualAddress - SourceBase;

    ASSERT((ULONG_PTR)VirtualAddress >= SourceBase);
    ASSERT(Offset < MmGetMdlByteCount(SourceMdl));

    Available = MmGetMdlByteCount(SourceMdl) - (ULONG)Offset;
    if (Length == 0 || Length > Available) Length = Available;

    TargetMdl->StartVa = (PVOID)PAGE_ALIGN(VirtualAddress);
    TargetMdl->ByteOffset = BYTE_OFFSET(VirtualAddress);
    TargetMdl->ByteCount = Length;
    TargetMdl->Process = SourceMdl->Process;
    TargetMdl->MdlFlags &= ~(MDL_MAPPED_TO_SYSTEM_VA |
                             MDL_PARTIAL_HAS_BEEN_MAPPED |
                             MDL_SOURCE_IS_NONPAGED_POOL |
                             MDL_ALLOCATED_MUST_SUCCEED);
    TargetMdl->MdlFlags |= MDL_PARTIAL;
    TargetMdl->MappedSystemVa =
        (PUCHAR)SourceMdl->MappedSystemVa + Offset;

    if (SourceMdl->MdlFlags & MDL_IO_SPACE)
    {
        TargetMdl->MdlFlags |= MDL_IO_SPACE;
    }

    SourcePages = MmGetMdlPfnArray(SourceMdl);
    TargetPages = MmGetMdlPfnArray(TargetMdl);

    SourcePages += ((ULONG_PTR)TargetMdl->StartVa -
                    (ULONG_PTR)SourceMdl->StartVa) >> PAGE_SHIFT;

    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(VirtualAddress, Length);

    for (Index = 0; Index < PageCount; Index++)
    {
        TargetPages[Index] = SourcePages[Index];
    }
}

/* PRIORITY AND ACTIVITY ******************************************************/

IO_PAGING_PRIORITY
FASTCALL
IoGetPagingIoPriority(
    _In_ PIRP Irp)
{
    if (!(Irp->Flags & IRP_PAGING_IO)) return IoPagingPriorityInvalid;

    if (Irp->Flags & IRP_SYNCHRONOUS_PAGING_IO) return IoPagingPriorityHigh;

    return IoPagingPriorityNormal;
}

NTSTATUS
NTAPI
IoRetrievePriorityInfo(
    _In_opt_ PIRP Irp,
    _In_opt_ PFILE_OBJECT FileObject,
    _In_opt_ PETHREAD Thread,
    _Inout_ PIO_PRIORITY_INFO PriorityInfo)
{
    if (PriorityInfo == NULL) return STATUS_INVALID_PARAMETER;
    if (PriorityInfo->Size < sizeof(IO_PRIORITY_INFO))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    PriorityInfo->ThreadPriority = 0xFFFF;
    PriorityInfo->IoPriority = IoPriorityNormal;
    /* TODO: no Mm page-priority query; report the normal band */
    PriorityInfo->PagePriority = 0;

    if (Thread != NULL)
    {
        PriorityInfo->ThreadPriority = KeQueryPriorityThread(&Thread->Tcb);
    }

    if (FileObject != NULL)
    {
        PFILE_OBJECT_EXTENSION Extension;

        if (FileObject->Flags & FO_FILE_OBJECT_HAS_EXTENSION)
        {
            Extension = FileObject->FileObjectExtension;
            if (Extension != NULL &&
                (Extension->FoExtFlags & IOP_FILE_OBJECT_EXT_PRIORITY_HINT))
            {
                PriorityInfo->IoPriority = Extension->PriorityHint;
            }
        }
    }

    if (Irp != NULL)
    {
        if (Irp->Flags & IRP_PAGING_IO)
        {
            PriorityInfo->IoPriority =
                (Irp->Flags & IRP_SYNCHRONOUS_PAGING_IO) ? IoPriorityHigh
                                                         : IoPriorityNormal;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoGetActivityIdIrp(
    _In_ PIRP Irp,
    _Out_ LPGUID Guid)
{
    PIOP_IRP_EXTENSION Extension;

    Extension = IopGetIrpExtension(Irp, FALSE);
    if (Extension == NULL ||
        !(Extension->TypesAllocated & IOP_IRP_EXTENSION_ACTIVITY_ID))
    {
        return STATUS_NOT_FOUND;
    }

    *Guid = Extension->ActivityId;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoSetActivityIdIrp(
    _Inout_ PIRP Irp,
    _In_opt_ LPCGUID Guid)
{
    PIOP_IRP_EXTENSION Extension;

    if (Guid == NULL)
    {
        Extension = IopGetIrpExtension(Irp, FALSE);
        if (Extension != NULL)
        {
            Extension->TypesAllocated &= ~IOP_IRP_EXTENSION_ACTIVITY_ID;
            RtlZeroMemory(&Extension->ActivityId, sizeof(GUID));
        }

        return STATUS_SUCCESS;
    }

    Extension = IopGetIrpExtension(Irp, TRUE);
    if (Extension == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Extension->ActivityId = *Guid;
    Extension->TypesAllocated |= IOP_IRP_EXTENSION_ACTIVITY_ID;

    return STATUS_SUCCESS;
}

LPCGUID
NTAPI
IoSetActivityIdThread(
    _In_ LPCGUID ActivityId)
{
    PETHREAD Thread = PsGetCurrentThread();
    LPCGUID Previous;

    Previous = Thread->ActivityId;
    Thread->ActivityId = ActivityId;

    return Previous;
}

VOID
NTAPI
IoClearActivityIdThread(
    _In_ LPCGUID OriginalId)
{
    PsGetCurrentThread()->ActivityId = OriginalId;
}

LPCGUID
NTAPI
IoGetActivityIdThread(VOID)
{
    return PsGetCurrentThread()->ActivityId;
}

NTSTATUS
NTAPI
IoPropagateActivityIdToThread(
    _In_ PIRP Irp,
    _Out_ LPGUID PropagatedId,
    _Outptr_ LPCGUID *OriginalId)
{
    NTSTATUS Status;

    *OriginalId = NULL;

    Status = IoGetActivityIdIrp(Irp, PropagatedId);
    if (!NT_SUCCESS(Status)) return Status;

    *OriginalId = IoSetActivityIdThread(PropagatedId);

    return STATUS_SUCCESS;
}

IO_PRIORITY_HINT
NTAPI
IoGetIoPriorityHint(
    _In_ PIRP Irp)
{
    PFILE_OBJECT_EXTENSION Extension;
    PFILE_OBJECT FileObject;

    if (Irp == NULL) return IoPriorityNormal;

    if (Irp->Flags & IRP_PAGING_IO)
    {
        return (Irp->Flags & IRP_SYNCHRONOUS_PAGING_IO) ? IoPriorityHigh
                                                        : IoPriorityNormal;
    }

    /* TODO: Windows 11 keeps a per-request hint whose IRP field is not published */
    FileObject = Irp->Tail.Overlay.OriginalFileObject;
    if (FileObject == NULL) return IoPriorityNormal;
    if (!(FileObject->Flags & FO_FILE_OBJECT_HAS_EXTENSION))
    {
        return IoPriorityNormal;
    }

    Extension = FileObject->FileObjectExtension;
    if (Extension == NULL) return IoPriorityNormal;
    if (!(Extension->FoExtFlags & IOP_FILE_OBJECT_EXT_PRIORITY_HINT))
    {
        return IoPriorityNormal;
    }

    return Extension->PriorityHint;
}

/* The severity the status code declares about itself, 0 through 3 */
static
ULONG
IopStatusSeverity(
    _In_ NTSTATUS Status)
{
    return ((ULONG)Status >> 30) & 3;
}

VOID
NTAPI
IoSetMasterIrpStatus(
    _Inout_ PIRP MasterIrp,
    _In_ NTSTATUS Status)
{
    NTSTATUS Current;

    if (MasterIrp == NULL) return;

    Current = MasterIrp->IoStatus.Status;

    /* TODO: no published ordering inside one severity class; the first wins */
    if (IopStatusSeverity(Status) <= IopStatusSeverity(Current)) return;

    MasterIrp->IoStatus.Status = Status;
}
