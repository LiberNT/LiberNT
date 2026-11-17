/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     In-memory PnP resource database
 * COPYRIGHT:   Copyright LiberNT contributors
 */

#include <ntoskrnl.h>
#include <reactos/drivers/cmreslist.h>
#define NDEBUG
#include <debug.h>

typedef enum _PI_RES_CLASS
{
    PiResClassMemory = 0,
    PiResClassPort,
    PiResClassInterrupt,
    PiResClassDma,
    PiResClassBusNumber,
    PiResClassMax
} PI_RES_CLASS;

typedef struct _PI_RES_ENTRY
{
    LIST_ENTRY ListEntry;
    PDEVICE_NODE Owner;
    PVOID Claimant;
    CM_PARTIAL_RESOURCE_DESCRIPTOR Desc;
} PI_RES_ENTRY, *PPI_RES_ENTRY;

typedef struct _PI_RES_BUCKET
{
    LIST_ENTRY List;
    KSPIN_LOCK Lock;
} PI_RES_BUCKET;

/* Assignment -> bucket spin lock. Never wait for registry seeding while held. */
KMUTEX IopResourceAssignmentLock;

static PI_RES_BUCKET IopResBuckets[PiResClassMax];
static UCHAR IopResDbHalReport;
static UCHAR IopResDbBootClaim;
static BOOLEAN IopResDbReady = FALSE;
static volatile LONG IopResDbSeedState = 0;
static KEVENT IopResDbSeededEvent;

static VOID IopResDbSeedFromRegistry(VOID);

VOID
IopResDbInitialize(VOID)
{
    ULONG i;
    KeInitializeMutex(&IopResourceAssignmentLock, 0);
    for (i = 0; i < PiResClassMax; i++)
    {
        InitializeListHead(&IopResBuckets[i].List);
        KeInitializeSpinLock(&IopResBuckets[i].Lock);
    }
    KeInitializeEvent(&IopResDbSeededEvent, NotificationEvent, FALSE);
    IopResDbReady = TRUE;
}

VOID
IopResDbEnsureSeeded(VOID)
{
    if (InterlockedCompareExchange(&IopResDbSeedState, 1, 0) == 0)
    {
        IopResDbSeedFromRegistry();
        InterlockedExchange(&IopResDbSeedState, 2);
        KeSetEvent(&IopResDbSeededEvent, IO_NO_INCREMENT, FALSE);
    }
    else if (IopResDbSeedState != 2)
    {
        /* The seeding thread needs the assignment lock to record what it reads */
        ASSERT(IopResourceAssignmentLock.OwnerThread != KeGetCurrentThread());

        /* Another thread is seeding; wait so we never query a partial database */
        KeWaitForSingleObject(&IopResDbSeededEvent, Executive, KernelMode, FALSE, NULL);
    }
}

static
BOOLEAN
IopResClassFromType(_In_ UCHAR Type, _Out_ PI_RES_CLASS *Class)
{
    switch (Type)
    {
        case CmResourceTypeMemory:      *Class = PiResClassMemory; return TRUE;
        case CmResourceTypeMemoryLarge: *Class = PiResClassMemory; return TRUE;
        case CmResourceTypePort:        *Class = PiResClassPort; return TRUE;
        case CmResourceTypeInterrupt:   *Class = PiResClassInterrupt; return TRUE;
        case CmResourceTypeDma:         *Class = PiResClassDma; return TRUE;
        case CmResourceTypeBusNumber:   *Class = PiResClassBusNumber; return TRUE;
        default:                        return FALSE;
    }
}

/* Pairwise overlap test, mirroring IopCheckResourceDescriptor (pnpres.c). */
static
BOOLEAN
IopResDbIsAncestorOrSelf(_In_ PDEVICE_NODE Ancestor, _In_ PDEVICE_NODE DeviceNode)
{
    while (DeviceNode)
    {
        if (DeviceNode == Ancestor)
            return TRUE;
        DeviceNode = DeviceNode->Parent;
    }
    return FALSE;
}

/* The last address of a range; one that would wrap around runs to the top of the address space */
static
UINT64
IopResDbLast(_In_ UINT64 Start, _In_ UINT64 Length)
{
    return (Length - 1 > MAXULONGLONG - Start) ? MAXULONGLONG : Start + (Length - 1);
}

/* A window holds a range inside it that belongs to its owner or to a device below it; a nested bridge's window may equal its parent's */
static
BOOLEAN
IopResDbWindowHolds(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Window,
    _In_ USHORT WindowFlag,
    _In_ UINT64 WindowStart,
    _In_ UINT64 WindowLast,
    _In_opt_ PDEVICE_NODE WindowOwner,
    _In_ UINT64 Start,
    _In_ UINT64 Last,
    _In_opt_ PDEVICE_NODE Owner)
{
    ASSERT(WindowStart <= WindowLast);
    ASSERT(Start <= Last);

    if (!(Window->Flags & WindowFlag) || WindowStart > Start || WindowLast < Last)
        return FALSE;
    return WindowOwner == NULL || Owner == NULL || IopResDbIsAncestorOrSelf(WindowOwner, Owner);
}

static
BOOLEAN
IopResDbConflict(_In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR a, _In_opt_ PDEVICE_NODE aOwner, _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR b, _In_opt_ PDEVICE_NODE bOwner)
{
    PI_RES_CLASS aClass, bClass;

    if (!IopResClassFromType(a->Type, &aClass) || !IopResClassFromType(b->Type, &bClass) || aClass != bClass)
        return FALSE;
    if (a->ShareDisposition == CmResourceShareShared && b->ShareDisposition == CmResourceShareShared)
        return FALSE;

    switch (a->Type)
    {
        /* A large window is memory too, with its scaled length */
        case CmResourceTypeMemory:
        case CmResourceTypeMemoryLarge:
        {
            UINT64 aStart, bStart;
            UINT64 aLength = RtlCmDecodeMemIoResource(a, &aStart);
            UINT64 bLength = RtlCmDecodeMemIoResource(b, &bStart);
            UINT64 aLast, bLast;
            if (aLength == 0 || bLength == 0)
                return FALSE;
            aLast = IopResDbLast(aStart, aLength);
            bLast = IopResDbLast(bStart, bLength);
            if (aStart > bLast || bStart > aLast)
                return FALSE;
            return !IopResDbWindowHolds(a, CM_RESOURCE_MEMORY_WINDOW_DECODE, aStart, aLast, aOwner, bStart, bLast, bOwner) &&
                   !IopResDbWindowHolds(b, CM_RESOURCE_MEMORY_WINDOW_DECODE, bStart, bLast, bOwner, aStart, aLast, aOwner);
        }
        case CmResourceTypePort:
        {
            UINT64 aStart = (UINT64)a->u.Port.Start.QuadPart, aLast;
            UINT64 bStart = (UINT64)b->u.Port.Start.QuadPart, bLast;
            if (a->u.Port.Length == 0 || b->u.Port.Length == 0)
                return FALSE;
            aLast = IopResDbLast(aStart, a->u.Port.Length);
            bLast = IopResDbLast(bStart, b->u.Port.Length);
            if (aStart > bLast || bStart > aLast)
                return FALSE;
            return !IopResDbWindowHolds(a, CM_RESOURCE_PORT_WINDOW_DECODE, aStart, aLast, aOwner, bStart, bLast, bOwner) &&
                   !IopResDbWindowHolds(b, CM_RESOURCE_PORT_WINDOW_DECODE, bStart, bLast, bOwner, aStart, aLast, aOwner);
        }
        case CmResourceTypeInterrupt:
            /* A message interrupt holds a vector the HAL handed out, not an interrupt line */
            if ((a->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) || (b->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
                return FALSE;
            return (a->u.Interrupt.Vector == b->u.Interrupt.Vector);
        case CmResourceTypeBusNumber:
        {
            UINT32 aStart = a->u.BusNumber.Start, aEnd = aStart + a->u.BusNumber.Length;
            UINT32 bStart = b->u.BusNumber.Start, bEnd = bStart + b->u.BusNumber.Length;
            if (!(aStart < bEnd && bStart < aEnd))
                return FALSE;
            if (aOwner && bOwner && aStart <= bStart && aEnd >= bEnd && IopResDbIsAncestorOrSelf(aOwner, bOwner))
                return FALSE;
            if (aOwner && bOwner && bStart <= aStart && bEnd >= aEnd && IopResDbIsAncestorOrSelf(bOwner, aOwner))
                return FALSE;
            if ((a->ShareDisposition == CmResourceShareShared &&
                 aStart < bStart && aEnd >= bEnd) ||
                (b->ShareDisposition == CmResourceShareShared &&
                 bStart < aStart && bEnd >= aEnd))
            {
                return FALSE;
            }
            return TRUE;
        }
        case CmResourceTypeDma:
            return IopDmaDescriptorsConflict(a, b);
        default:
            return FALSE;
    }
}

/* Conflicts are logged but still recorded (non-enforcing). */
static
NTSTATUS
IopResDbReserveList(_In_opt_ PDEVICE_NODE DeviceNode, _In_opt_ PVOID Claimant, _In_opt_ PCM_RESOURCE_LIST ResourceList, _In_ BOOLEAN SkipInternalInterrupts, _In_ BOOLEAN AddressesOnly, _Out_opt_ PBOOLEAN Conflict)
{
    PCM_FULL_RESOURCE_DESCRIPTOR full;
    ULONG i, j;

    if (Conflict)
        *Conflict = FALSE;
    if (!IopResDbReady || ResourceList == NULL || ResourceList->Count == 0)
        return STATUS_SUCCESS;

    KeWaitForSingleObject(&IopResourceAssignmentLock, Executive, KernelMode, FALSE, NULL);
    full = &ResourceList->List[0];
    for (i = 0; i < ResourceList->Count; i++)
    {
        PCM_PARTIAL_RESOURCE_LIST partial = &full->PartialResourceList;
        PCM_PARTIAL_RESOURCE_DESCRIPTOR desc = &partial->PartialDescriptors[0];

        for (j = 0; j < partial->Count; j++, desc = CmiGetNextPartialDescriptor(desc))
        {
            PI_RES_CLASS cls;
            PPI_RES_ENTRY entry, existing;
            PLIST_ENTRY le;
            KIRQL irql;
            BOOLEAN Conflicted = FALSE;

            if (!IopResClassFromType(desc->Type, &cls))
                continue;
            if (AddressesOnly && cls != PiResClassMemory && cls != PiResClassPort)
                continue;
            if (SkipInternalInterrupts && full->InterfaceType == Internal && desc->Type == CmResourceTypeInterrupt)
                continue;

            entry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*entry), TAG_IO);
            if (entry == NULL)
                continue;
            entry->Owner = DeviceNode;
            entry->Claimant = Claimant;
            entry->Desc = *desc;

            KeAcquireSpinLock(&IopResBuckets[cls].Lock, &irql);
            for (le = IopResBuckets[cls].List.Flink; le != &IopResBuckets[cls].List; le = le->Flink)
            {
                existing = CONTAINING_RECORD(le, PI_RES_ENTRY, ListEntry);
                if (existing->Owner == DeviceNode)
                    continue;
                if (IopResDbConflict(desc, DeviceNode, &existing->Desc, existing->Owner))
                {
                    if (Conflict)
                        *Conflict = TRUE;
                    Conflicted = TRUE;
                    break;
                }
            }
            InsertTailList(&IopResBuckets[cls].List, &entry->ListEntry);
            KeReleaseSpinLock(&IopResBuckets[cls].Lock, irql);

            if (Conflicted && DeviceNode)
                DPRINT("PnP: resource conflict (type %u) involving %wZ\n", desc->Type, &DeviceNode->InstancePath);
        }

        full = CmiGetNextResourceDescriptor(full);
    }

    KeReleaseMutex(&IopResourceAssignmentLock, FALSE);
    return STATUS_SUCCESS;
}

static
VOID
IopResDbReleaseOwnerClaim(_In_ PDEVICE_NODE DeviceNode, _In_ PVOID Claimant)
{
    ULONG cls;

    ASSERT(DeviceNode != NULL);
    ASSERT(Claimant != NULL);
    ASSERT(IopResourceAssignmentLock.OwnerThread == KeGetCurrentThread());

    for (cls = 0; cls < PiResClassMax; cls++)
    {
        PLIST_ENTRY le;
        KIRQL irql;

        KeAcquireSpinLock(&IopResBuckets[cls].Lock, &irql);
        le = IopResBuckets[cls].List.Flink;
        while (le != &IopResBuckets[cls].List)
        {
            PPI_RES_ENTRY entry = CONTAINING_RECORD(le, PI_RES_ENTRY, ListEntry);
            le = le->Flink;
            if (entry->Owner == DeviceNode && entry->Claimant == Claimant)
            {
                RemoveEntryList(&entry->ListEntry);
                ExFreePoolWithTag(entry, TAG_IO);
            }
        }
        KeReleaseSpinLock(&IopResBuckets[cls].Lock, irql);
    }
}

NTSTATUS
IopResDbReserve(_In_ PDEVICE_NODE DeviceNode, _In_opt_ PCM_RESOURCE_LIST ResourceList, _Out_opt_ PBOOLEAN Conflict)
{
    NTSTATUS Status;

    ASSERT(DeviceNode != NULL);
    if (!IopResDbReady)
        return IopResDbReserveList(DeviceNode, NULL, ResourceList, FALSE, FALSE, Conflict);

    KeWaitForSingleObject(&IopResourceAssignmentLock, Executive, KernelMode, FALSE, NULL);
    IopResDbReleaseOwnerClaim(DeviceNode, &IopResDbBootClaim);
    Status = IopResDbReserveList(DeviceNode, NULL, ResourceList, FALSE, FALSE, Conflict);
    KeReleaseMutex(&IopResourceAssignmentLock, FALSE);
    return Status;
}

/* The firmware's addresses for a device are held from its enumeration until it gets its own, so a sibling started first cannot take them */
NTSTATUS
IopResDbReserveBoot(_In_ PDEVICE_NODE DeviceNode, _In_opt_ PCM_RESOURCE_LIST BootResources)
{
    NTSTATUS Status;

    ASSERT(DeviceNode != NULL);
    if (!IopResDbReady || BootResources == NULL)
        return STATUS_SUCCESS;

    KeWaitForSingleObject(&IopResourceAssignmentLock, Executive, KernelMode, FALSE, NULL);
    IopResDbReleaseOwnerClaim(DeviceNode, &IopResDbBootClaim);
    Status = IopResDbReserveList(DeviceNode, &IopResDbBootClaim, BootResources, FALSE, TRUE, NULL);
    KeReleaseMutex(&IopResourceAssignmentLock, FALSE);
    return Status;
}

/* Check one candidate descriptor against the database; returns the conflicting entry. */
static
BOOLEAN
IopResDbCheck(_In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Candidate, _In_opt_ PDEVICE_NODE CandidateOwner, _In_opt_ PVOID Claimant,
              _In_ BOOLEAN BootSetting, _Out_opt_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Conflicting)
{
    PI_RES_CLASS cls;
    PLIST_ENTRY le;
    KIRQL irql;
    BOOLEAN found = FALSE;
    UINT64 Start, Length;
    BOOLEAN Report = FALSE;
    CM_PARTIAL_RESOURCE_DESCRIPTOR Other;
    PDEVICE_OBJECT OtherPdo = NULL;
    PVOID OtherClaimant = NULL;

    if (Candidate->Type == CmResourceTypeMemory || Candidate->Type == CmResourceTypeMemoryLarge)
    {
        Length = RtlCmDecodeMemIoResource(Candidate, &Start);
        if (Length != 0 && IopResDbLast(Start, Length) >= MmGetPhysicalAddressLimit())
        {
            if (Conflicting)
                *Conflicting = *Candidate;
            return TRUE;
        }
    }

    if (!IopResDbReady || !IopResClassFromType(Candidate->Type, &cls))
        return FALSE;

    KeAcquireSpinLock(&IopResBuckets[cls].Lock, &irql);
    for (le = IopResBuckets[cls].List.Flink; le != &IopResBuckets[cls].List; le = le->Flink)
    {
        PPI_RES_ENTRY entry = CONTAINING_RECORD(le, PI_RES_ENTRY, ListEntry);
        if (CandidateOwner != NULL && entry->Owner == CandidateOwner)
            continue;
        if (Claimant != NULL && entry->Claimant == Claimant)
            continue;
        /* A firmware setting is not refused for what the HAL reports or another device's firmware setting holds */
        if (BootSetting && (entry->Claimant == &IopResDbHalReport || entry->Claimant == &IopResDbBootClaim))
            continue;
        if (IopResDbConflict(Candidate, CandidateOwner, &entry->Desc, entry->Owner))
        {
            if (BootSetting && (Candidate->Type == CmResourceTypeMemory || Candidate->Type == CmResourceTypeMemoryLarge))
            {
                Report = TRUE;
                Other = entry->Desc;
                OtherClaimant = entry->Claimant;

                /* The owner's path is paged and outlives the lock only while its device object does */
                if (entry->Owner != NULL &&
                    entry->Owner->PhysicalDeviceObject != NULL &&
                    ObReferenceObjectSafe(entry->Owner->PhysicalDeviceObject))
                {
                    OtherPdo = entry->Owner->PhysicalDeviceObject;
                }
            }
            if (Conflicting)
                *Conflicting = entry->Desc;
            found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&IopResBuckets[cls].Lock, irql);

    if (Report)
    {
        UINT64 OtherStart;
        UINT64 OtherLength = RtlCmDecodeMemIoResource(&Other, &OtherStart);
        PDEVICE_NODE OtherOwner = OtherPdo != NULL ? IopGetDeviceNode(OtherPdo) : NULL;

        DPRINT1("PnP: boot memory 0x%I64x length 0x%I64x of %wZ conflicts with 0x%I64x length 0x%I64x flags=0x%x owner=%wZ claimant=%p\n",
                Start, Length, CandidateOwner != NULL ? &CandidateOwner->InstancePath : NULL,
                OtherStart, OtherLength, Other.Flags, OtherOwner != NULL ? &OtherOwner->InstancePath : NULL, OtherClaimant);

        if (OtherPdo != NULL)
            ObDereferenceObjectDeferDelete(OtherPdo);
    }
    return found;
}

BOOLEAN
IopResDbCheckDescriptor(_In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Candidate, _In_opt_ PDEVICE_NODE CandidateOwner, _In_opt_ PVOID Claimant, _Out_opt_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Conflicting)
{
    return IopResDbCheck(Candidate, CandidateOwner, Claimant, FALSE, Conflicting);
}

/* Windows 11 gives a device the boot configuration its firmware describes even where the HAL reports the same range */
BOOLEAN
IopResDbCheckBootDescriptor(_In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Candidate, _In_opt_ PDEVICE_NODE CandidateOwner, _In_opt_ PVOID Claimant)
{
    return IopResDbCheck(Candidate, CandidateOwner, Claimant, TRUE, NULL);
}

static
BOOLEAN
IopResDbIsWindow(_In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc)
{
    switch (Desc->Type)
    {
        case CmResourceTypeMemory:
        case CmResourceTypeMemoryLarge:
            return (Desc->Flags & CM_RESOURCE_MEMORY_WINDOW_DECODE) != 0;
        case CmResourceTypePort:
            return (Desc->Flags & CM_RESOURCE_PORT_WINDOW_DECODE) != 0;
        default:
            return FALSE;
    }
}

/*
 * The windows of the nearest ancestor that decodes any, the way a bridge or
 * a root bus forwards addresses to the devices below it. Returns FALSE when
 * no ancestor decodes windows.
 */
BOOLEAN
IopResDbGetDecodingWindows(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ UCHAR Type,
    _Out_writes_to_(MaxWindows, *WindowCount) PCM_PARTIAL_RESOURCE_DESCRIPTOR Windows,
    _In_ ULONG MaxWindows,
    _Out_ PULONG WindowCount)
{
    static const PI_RES_CLASS Classes[] = { PiResClassMemory, PiResClassPort };
    PDEVICE_NODE Ancestor;
    PI_RES_CLASS Wanted;
    BOOLEAN Decodes = FALSE;
    BOOLEAN Truncated = FALSE;
    ULONG Index;

    ASSERT(DeviceNode != NULL);
    ASSERT(Windows != NULL && MaxWindows != 0);
    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    *WindowCount = 0;
    if (!IopResDbReady || !IopResClassFromType(Type, &Wanted) ||
        (Wanted != PiResClassMemory && Wanted != PiResClassPort))
    {
        return FALSE;
    }

    for (Ancestor = DeviceNode->Parent; Ancestor != NULL && !Decodes; Ancestor = Ancestor->Parent)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(Classes); Index++)
        {
            PI_RES_CLASS cls = Classes[Index];
            PLIST_ENTRY le;
            KIRQL irql;

            KeAcquireSpinLock(&IopResBuckets[cls].Lock, &irql);
            for (le = IopResBuckets[cls].List.Flink; le != &IopResBuckets[cls].List; le = le->Flink)
            {
                PPI_RES_ENTRY entry = CONTAINING_RECORD(le, PI_RES_ENTRY, ListEntry);

                if (entry->Owner != Ancestor || entry->Claimant == &IopResDbBootClaim || !IopResDbIsWindow(&entry->Desc))
                    continue;

                Decodes = TRUE;
                if (cls != Wanted)
                    continue;
                if (*WindowCount < MaxWindows)
                    Windows[(*WindowCount)++] = entry->Desc;
                else
                    Truncated = TRUE;
            }
            KeReleaseSpinLock(&IopResBuckets[cls].Lock, irql);
        }
    }

    if (Truncated)
        DPRINT1("PnP: more than %lu windows above %wZ, the rest are not searched\n", MaxWindows, &DeviceNode->InstancePath);

    ASSERT(*WindowCount <= MaxWindows);
    ASSERT(Decodes || *WindowCount == 0);
    return Decodes;
}

BOOLEAN
IopResDbConflictsWithChosen(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Candidate,
    _In_opt_ PDEVICE_NODE CandidateOwner,
    _In_reads_opt_(ChosenCount) PCM_PARTIAL_RESOURCE_DESCRIPTOR Chosen,
    _In_ ULONG ChosenCount)
{
    ULONG Index;

    ASSERT(Candidate != NULL);
    ASSERT(ChosenCount == 0 || Chosen != NULL);

    for (Index = 0; Index < ChosenCount; Index++)
    {
        ASSERT(&Chosen[Index] != Candidate);
        if (IopResDbConflict(Candidate, CandidateOwner, &Chosen[Index], CandidateOwner))
            return TRUE;
    }

    return FALSE;
}

/* Check every descriptor in a list against the database. */
BOOLEAN
IopResDbCheckList(_In_opt_ PCM_RESOURCE_LIST ResourceList)
{
    PCM_FULL_RESOURCE_DESCRIPTOR full;
    ULONG i, j;

    if (!IopResDbReady || ResourceList == NULL || ResourceList->Count == 0)
        return FALSE;

    full = &ResourceList->List[0];
    for (i = 0; i < ResourceList->Count; i++)
    {
        PCM_PARTIAL_RESOURCE_LIST partial = &full->PartialResourceList;
        PCM_PARTIAL_RESOURCE_DESCRIPTOR desc = &partial->PartialDescriptors[0];

        for (j = 0; j < partial->Count; j++, desc = CmiGetNextPartialDescriptor(desc))
        {
            if (IopResDbCheckDescriptor(desc, NULL, NULL, NULL))
                return TRUE;
        }

        full = CmiGetNextResourceDescriptor(full);
    }

    return FALSE;
}

static
VOID
IopResDbReleaseClaim(_In_ PVOID Claimant)
{
    ULONG cls;

    ASSERT(Claimant != NULL);
    for (cls = 0; cls < PiResClassMax; cls++)
    {
        PLIST_ENTRY le;
        KIRQL irql;

        KeAcquireSpinLock(&IopResBuckets[cls].Lock, &irql);
        le = IopResBuckets[cls].List.Flink;
        while (le != &IopResBuckets[cls].List)
        {
            PPI_RES_ENTRY entry = CONTAINING_RECORD(le, PI_RES_ENTRY, ListEntry);
            le = le->Flink;
            if (entry->Claimant == Claimant)
            {
                RemoveEntryList(&entry->ListEntry);
                ExFreePoolWithTag(entry, TAG_IO);
            }
        }
        KeReleaseSpinLock(&IopResBuckets[cls].Lock, irql);
    }
}

NTSTATUS
IopResDbReplaceClaim(_In_opt_ PDEVICE_NODE DeviceNode, _In_ PVOID Claimant, _In_ PCM_RESOURCE_LIST ResourceList)
{
    ASSERT(Claimant != NULL);
    if (!IopResDbReady)
        return STATUS_SUCCESS;
    KeWaitForSingleObject(&IopResourceAssignmentLock, Executive, KernelMode, FALSE, NULL);
    IopResDbReleaseClaim(Claimant);
    IopResDbReserveList(DeviceNode, Claimant, ResourceList, FALSE, FALSE, NULL);
    KeReleaseMutex(&IopResourceAssignmentLock, FALSE);
    return STATUS_SUCCESS;
}

VOID
IopResDbRelease(_In_ PDEVICE_NODE DeviceNode)
{
    ULONG cls;

    if (!IopResDbReady)
        return;

    for (cls = 0; cls < PiResClassMax; cls++)
    {
        PLIST_ENTRY le;
        KIRQL irql;

        KeAcquireSpinLock(&IopResBuckets[cls].Lock, &irql);
        le = IopResBuckets[cls].List.Flink;
        while (le != &IopResBuckets[cls].List)
        {
            PPI_RES_ENTRY entry = CONTAINING_RECORD(le, PI_RES_ENTRY, ListEntry);
            le = le->Flink;
            if (entry->Owner == DeviceNode)
            {
                RemoveEntryList(&entry->ListEntry);
                ExFreePoolWithTag(entry, TAG_IO);
            }
        }
        KeReleaseSpinLock(&IopResBuckets[cls].Lock, irql);
    }
}

/* Scan the resource map once, recording every raw allocation under a NULL owner. */
static
VOID
IopResDbSeedFromRegistry(VOID)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING keyName;
    HANDLE mapKey = NULL, classKey = NULL, driverKey = NULL;
    PKEY_BASIC_INFORMATION basic;
    PKEY_VALUE_PARTIAL_INFORMATION value;
    PKEY_VALUE_BASIC_INFORMATION valueName;
    ULONG i1, i2, i3, need;
    NTSTATUS status;

    UNICODE_STRING systemResourcesName = RTL_CONSTANT_STRING(L"System Resources");
    UNICODE_STRING halName = RTL_CONSTANT_STRING(L"Hardware Abstraction Layer");
    BOOLEAN halKey;

    RtlInitUnicodeString(&keyName, L"\\Registry\\Machine\\HARDWARE\\RESOURCEMAP");
    InitializeObjectAttributes(&oa, &keyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!NT_SUCCESS(ZwOpenKey(&mapKey, KEY_ENUMERATE_SUB_KEYS, &oa)))
        return;

    for (i1 = 0; ; i1++)
    {
        status = ZwEnumerateKey(mapKey, i1, KeyBasicInformation, NULL, 0, &need);
        if (status == STATUS_NO_MORE_ENTRIES)
            break;
        if (status != STATUS_BUFFER_OVERFLOW && status != STATUS_BUFFER_TOO_SMALL)
            break;
        basic = ExAllocatePoolWithTag(PagedPool, need, TAG_IO);
        if (basic == NULL)
            break;
        status = ZwEnumerateKey(mapKey, i1, KeyBasicInformation, basic, need, &need);
        if (!NT_SUCCESS(status))
        {
            ExFreePoolWithTag(basic, TAG_IO);
            break;
        }

        keyName.Buffer = basic->Name;
        keyName.MaximumLength = keyName.Length = (USHORT)basic->NameLength;
        if (RtlEqualUnicodeString(&keyName, &systemResourcesName, TRUE))
        {
            ExFreePoolWithTag(basic, TAG_IO);
            continue;
        }
        halKey = RtlEqualUnicodeString(&keyName, &halName, TRUE);
        InitializeObjectAttributes(&oa, &keyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, mapKey, NULL);
        status = ZwOpenKey(&classKey, KEY_ENUMERATE_SUB_KEYS, &oa);
        ExFreePoolWithTag(basic, TAG_IO);
        if (!NT_SUCCESS(status))
            continue;

        for (i2 = 0; ; i2++)
        {
            status = ZwEnumerateKey(classKey, i2, KeyBasicInformation, NULL, 0, &need);
            if (status == STATUS_NO_MORE_ENTRIES)
                break;
            if (status != STATUS_BUFFER_OVERFLOW && status != STATUS_BUFFER_TOO_SMALL)
                break;
            basic = ExAllocatePoolWithTag(PagedPool, need, TAG_IO);
            if (basic == NULL)
                break;
            status = ZwEnumerateKey(classKey, i2, KeyBasicInformation, basic, need, &need);
            if (!NT_SUCCESS(status))
            {
                ExFreePoolWithTag(basic, TAG_IO);
                break;
            }

            keyName.Buffer = basic->Name;
            keyName.MaximumLength = keyName.Length = (USHORT)basic->NameLength;
            InitializeObjectAttributes(&oa, &keyName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, classKey, NULL);
            status = ZwOpenKey(&driverKey, KEY_QUERY_VALUE, &oa);
            ExFreePoolWithTag(basic, TAG_IO);
            if (!NT_SUCCESS(status))
                continue;

            for (i3 = 0; ; i3++)
            {
                PCM_RESOURCE_LIST list;

                status = ZwEnumerateValueKey(driverKey, i3, KeyValuePartialInformation, NULL, 0, &need);
                if (status == STATUS_NO_MORE_ENTRIES)
                    break;
                if (status != STATUS_BUFFER_TOO_SMALL)
                    break;
                value = ExAllocatePoolWithTag(PagedPool, need, TAG_IO);
                if (value == NULL)
                    break;
                status = ZwEnumerateValueKey(driverKey, i3, KeyValuePartialInformation, value, need, &need);
                if (!NT_SUCCESS(status))
                {
                    ExFreePoolWithTag(value, TAG_IO);
                    break;
                }

                status = ZwEnumerateValueKey(driverKey, i3, KeyValueBasicInformation, NULL, 0, &need);
                if (status != STATUS_BUFFER_TOO_SMALL)
                {
                    ExFreePoolWithTag(value, TAG_IO);
                    break;
                }
                valueName = ExAllocatePoolWithTag(PagedPool, need + sizeof(WCHAR), TAG_IO);
                if (valueName == NULL)
                {
                    ExFreePoolWithTag(value, TAG_IO);
                    break;
                }
                status = ZwEnumerateValueKey(driverKey, i3, KeyValueBasicInformation, valueName, need, &need);
                if (!NT_SUCCESS(status))
                {
                    ExFreePoolWithTag(valueName, TAG_IO);
                    ExFreePoolWithTag(value, TAG_IO);
                    break;
                }
                valueName->Name[valueName->NameLength / sizeof(WCHAR)] = UNICODE_NULL;

                /* Only raw lists; skip translated copies */
                list = (PCM_RESOURCE_LIST)value->Data;
                if (wcsstr(valueName->Name, L".Translated") == NULL && value->Type == REG_RESOURCE_LIST && IopIsResourceListValid(list, value->DataLength) && list->Count != 0)
                    IopResDbReserveList(NULL, halKey ? &IopResDbHalReport : NULL, list, halKey, FALSE, NULL);

                ExFreePoolWithTag(valueName, TAG_IO);
                ExFreePoolWithTag(value, TAG_IO);
            }

            ZwClose(driverKey);
        }

        ZwClose(classKey);
    }

    ZwClose(mapKey);
}
