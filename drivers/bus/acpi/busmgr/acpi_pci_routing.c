/*
 * PROJECT:         LiberNT ACPI Bus Manager
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         ACPI-based PCI interrupt routing provider for the HAL
 */

#include <precomp.h>
#include <ntddk.h>
#define NDEBUG
#include <debug.h>

#define ACPI_PRT_POOL_TAG 'TRPA'

typedef struct _PRT_MAP_ENTRY
{
    ULONG Segment;
    UCHAR Bus;
    UCHAR Device;
    UCHAR Pin;      /* 1..4 (INTA#..INTD#) */
    ULONG Gsi;
    UCHAR Polarity; /* HAL_ACPI_POLARITY_* */
    UCHAR Trigger;  /* HAL_ACPI_TRIGGER_* */
} PRT_MAP_ENTRY, *PPRT_MAP_ENTRY;

C_ASSERT(sizeof(PRT_MAP_ENTRY) == sizeof(HAL_ACPI_PCI_ROUTE_ENTRY));
C_ASSERT(FIELD_OFFSET(PRT_MAP_ENTRY, Gsi) == FIELD_OFFSET(HAL_ACPI_PCI_ROUTE_ENTRY, Gsi));
C_ASSERT(FIELD_OFFSET(PRT_MAP_ENTRY, Polarity) == FIELD_OFFSET(HAL_ACPI_PCI_ROUTE_ENTRY, Polarity));
C_ASSERT(FIELD_OFFSET(PRT_MAP_ENTRY, Trigger) == FIELD_OFFSET(HAL_ACPI_PCI_ROUTE_ENTRY, TriggerMode));

static PPRT_MAP_ENTRY PrtCache;
static ULONG PrtCacheCount;
static ULONG PrtCacheCapacity;

static
VOID
AcpiPrtResetCache(VOID)
{
    if (PrtCache)
    {
        ExFreePoolWithTag(PrtCache, ACPI_PRT_POOL_TAG);
        PrtCache = NULL;
    }

    PrtCacheCount = 0;
    PrtCacheCapacity = 0;
}

static
BOOLEAN
AcpiPrtEnsureCapacity(
    _In_ ULONG Additional)
{
    ULONG Required;
    ULONG NewCapacity;
    PPRT_MAP_ENTRY NewBuffer;

    if ((PrtCacheCount + Additional) <= PrtCacheCapacity)
    {
        return TRUE;
    }

    Required = PrtCacheCount + Additional;
    NewCapacity = (PrtCacheCapacity != 0) ? PrtCacheCapacity : 64;
    while (NewCapacity < Required)
    {
        NewCapacity *= 2;
    }

    NewBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                      NewCapacity * sizeof(PRT_MAP_ENTRY),
                                      ACPI_PRT_POOL_TAG);
    if (!NewBuffer)
    {
        DPRINT1("ACPI: Failed to grow PCI routing cache to %lu entries\n", NewCapacity);
        return FALSE;
    }

    if (PrtCacheCount)
    {
        RtlCopyMemory(NewBuffer, PrtCache, PrtCacheCount * sizeof(PRT_MAP_ENTRY));
    }

    if (PrtCache)
    {
        ExFreePoolWithTag(PrtCache, ACPI_PRT_POOL_TAG);
    }

    PrtCache = NewBuffer;
    PrtCacheCapacity = NewCapacity;
    return TRUE;
}

static
UCHAR
AcpiPrtMapPolarity(
    _In_ UCHAR AcpiPolarity)
{
    switch (AcpiPolarity)
    {
        case ACPI_ACTIVE_HIGH:
            return HAL_ACPI_POLARITY_HIGH;
        case ACPI_ACTIVE_LOW:
            return HAL_ACPI_POLARITY_LOW;
        case ACPI_ACTIVE_BOTH:
            return HAL_ACPI_POLARITY_BOTH;
        default:
            return HAL_ACPI_POLARITY_LOW;
    }
}

static
UCHAR
AcpiPrtMapTrigger(
    _In_ UCHAR AcpiTrigger)
{
    switch (AcpiTrigger)
    {
        case ACPI_LEVEL_SENSITIVE:
            return HAL_ACPI_TRIGGER_LEVEL;
        case ACPI_EDGE_SENSITIVE:
        default:
            return HAL_ACPI_TRIGGER_EDGE;
    }
}

static
VOID
AcpiPrtInsertOrUpdate(
    _In_ const PRT_MAP_ENTRY *Entry)
{
    for (ULONG i = 0; i < PrtCacheCount; ++i)
    {
        if ((PrtCache[i].Segment == Entry->Segment) &&
            (PrtCache[i].Bus == Entry->Bus) &&
            (PrtCache[i].Device == Entry->Device) &&
            (PrtCache[i].Pin == Entry->Pin))
        {
            PrtCache[i] = *Entry;
            return;
        }
    }

    if (!AcpiPrtEnsureCapacity(1))
    {
        return;
    }

    PrtCache[PrtCacheCount++] = *Entry;
}

static
BOOLEAN
AcpiPrtResolveLink(
    _In_ ACPI_HANDLE LinkHandle,
    _In_ UINT32 SourceIndex,
    _Inout_ PPRT_MAP_ENTRY Entry)
{
    ACPI_BUFFER ResourceBuffer = { ACPI_ALLOCATE_BUFFER, NULL };
    ACPI_STATUS Status;
    BOOLEAN Resolved = FALSE;

    Status = BuspGetCurrentResources(LinkHandle, &ResourceBuffer);
    if (ACPI_FAILURE(Status) || !ResourceBuffer.Pointer)
    {
        if (Status != AE_NOT_FOUND)
        {
            DPRINT1("ACPI: _CRS for PCI link %p failed (0x%X)\n", LinkHandle, Status);
        }
        return FALSE;
    }

    for (ACPI_RESOURCE *Resource = (ACPI_RESOURCE *)ResourceBuffer.Pointer;
         Resource && Resource->Type != ACPI_RESOURCE_TYPE_END_TAG;
         Resource = ACPI_NEXT_RESOURCE(Resource))
    {
        if (Resource->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ)
        {
            const ACPI_RESOURCE_EXTENDED_IRQ *ExtIrq = &Resource->Data.ExtendedIrq;
            if (ExtIrq->InterruptCount == 0)
            {
                continue;
            }

            UINT32 Index = (SourceIndex < ExtIrq->InterruptCount) ? SourceIndex : 0;
            Entry->Gsi = (ULONG)ExtIrq->Interrupts[Index];
            Entry->Polarity = AcpiPrtMapPolarity(ExtIrq->Polarity);
            Entry->Trigger = AcpiPrtMapTrigger(ExtIrq->Triggering);
            Resolved = TRUE;
            break;
        }
        else if (Resource->Type == ACPI_RESOURCE_TYPE_IRQ)
        {
            const ACPI_RESOURCE_IRQ *Irq = &Resource->Data.Irq;
            if (Irq->InterruptCount == 0)
            {
                continue;
            }

            UINT32 Index = (SourceIndex < Irq->InterruptCount) ? SourceIndex : 0;
            Entry->Gsi = (ULONG)Irq->Interrupts[Index];
            Entry->Polarity = AcpiPrtMapPolarity(Irq->Polarity);
            Entry->Trigger = AcpiPrtMapTrigger(Irq->Triggering);
            Resolved = TRUE;
            break;
        }
        else if (Resource->Type == ACPI_RESOURCE_TYPE_ADDRESS32)
        {
            /* ACPI resource type 4 (ADDRESS32) can encode GSIs in _CRS for root bridges. */
            const ACPI_RESOURCE_ADDRESS32 *Addr = &Resource->Data.Address32;
            if (Addr->ResourceType == ACPI_MEMORY_RANGE || Addr->ResourceType == ACPI_IO_RANGE)
                continue;

            /* Use Minimum as GSI when Source is empty. */
            Entry->Gsi = (ULONG)Addr->Address.Minimum;
            Entry->Polarity = HAL_ACPI_POLARITY_LOW;
            Entry->Trigger = HAL_ACPI_TRIGGER_LEVEL;
            Resolved = TRUE;
            break;
        }
    }

    ACPI_FREE(ResourceBuffer.Pointer);
    return Resolved;
}

static
ACPI_STATUS
AcpiPrtBuildForHandle(
    _In_ ACPI_HANDLE Handle,
    _In_ ULONG Segment,
    _In_ UCHAR RootBus)
{
    ACPI_BUFFER CrsBuffer = { ACPI_ALLOCATE_BUFFER, NULL };
    ACPI_BUFFER Buffer = { ACPI_ALLOCATE_BUFFER, NULL };
    ACPI_STATUS Status;
    ACPI_RESOURCE *CrsResource;
    ULONG MaxGsi = 0;

    /* Pre-scan _CRS for type-4 IRQ encodings so we can map them later. */
    Status = BuspGetCurrentResources(Handle, &CrsBuffer);
    if (ACPI_SUCCESS(Status) && CrsBuffer.Pointer)
    {
        for (CrsResource = (ACPI_RESOURCE *)CrsBuffer.Pointer;
             CrsResource && CrsResource->Type != ACPI_RESOURCE_TYPE_END_TAG;
             CrsResource = ACPI_NEXT_RESOURCE(CrsResource))
        {
            if (CrsResource->Type == ACPI_RESOURCE_TYPE_ADDRESS32)
            {
                const ACPI_RESOURCE_ADDRESS32 *Addr = &CrsResource->Data.Address32;
                if ((Addr->ResourceType == ACPI_MEMORY_RANGE) ||
                    (Addr->ResourceType == ACPI_IO_RANGE))
                    continue;

                if (Addr->Address.Minimum > MaxGsi)
                    MaxGsi = (ULONG)Addr->Address.Minimum;
            }
        }
    }

    Status = BuspGetIrqRoutingTable(Handle, &Buffer);
    if (ACPI_FAILURE(Status))
    {
        if (Status != AE_NOT_FOUND)
        {
            DPRINT1("ACPI: _PRT query for root %p failed (0x%X)\n", Handle, Status);
        }
        if (CrsBuffer.Pointer)
            ACPI_FREE(CrsBuffer.Pointer);
        return Status;
    }

    /* If _PRT is absent but _CRS had a type-4 IRQ, synthesize a single entry */
    if (Buffer.Pointer == NULL && MaxGsi != 0)
    {
        PRT_MAP_ENTRY Entry = {0};
        Entry.Segment = Segment;
        Entry.Bus = RootBus;
        Entry.Device = 0;
        Entry.Pin = 1;
        Entry.Gsi = MaxGsi;
        Entry.Polarity = HAL_ACPI_POLARITY_LOW;
        Entry.Trigger = HAL_ACPI_TRIGGER_LEVEL;
        AcpiPrtInsertOrUpdate(&Entry);
    }

    for (ACPI_PCI_ROUTING_TABLE *Route = (ACPI_PCI_ROUTING_TABLE *)Buffer.Pointer;
         Route && Route->Length;
         Route = (ACPI_PCI_ROUTING_TABLE *)((PUCHAR)Route + Route->Length))
    {
        PRT_MAP_ENTRY Entry = {0};
        ACPI_HANDLE LinkHandle;

        Entry.Segment = Segment;
        Entry.Bus = RootBus;
        Entry.Device = (UCHAR)((Route->Address >> 16) & 0xFF);
        Entry.Pin = (UCHAR)((Route->Pin & 0x3) + 1); /* INTA#..INTD# */
        Entry.Polarity = HAL_ACPI_POLARITY_LOW;
        Entry.Trigger = HAL_ACPI_TRIGGER_LEVEL;

        if (!Route->Source[0])
        {
            Entry.Gsi = Route->SourceIndex;
            AcpiPrtInsertOrUpdate(&Entry);
            continue;
        }

        Status = AcpiGetHandle(Handle, Route->Source, &LinkHandle);
        if (ACPI_FAILURE(Status))
        {
            DPRINT1("ACPI: Failed to resolve PCI link %s (0x%X)\n", Route->Source, Status);
            continue;
        }

        if (AcpiPrtResolveLink(LinkHandle, Route->SourceIndex, &Entry))
        {
            AcpiPrtInsertOrUpdate(&Entry);
        }
        else
        {
            DPRINT1("ACPI: Unresolved PCI link %s for device %u pin %u\n",
                    Route->Source,
                    Entry.Device,
                    Entry.Pin);
        }
    }

    if (MaxGsi != 0)
    {
        /* HalpRecordPciMaxGsi(&MaxEntry); -- HAL hook not implemented (cherry-pick stub). */
        DPRINT("ACPI: Discovered PCI max GSI %u for seg %u bus %u (HAL recording skipped)\n",
               (unsigned)MaxGsi, (unsigned)Segment, (unsigned)RootBus);
    }

    if (CrsBuffer.Pointer)
        ACPI_FREE(CrsBuffer.Pointer);
    ACPI_FREE(Buffer.Pointer);
    return AE_OK;
}

#define ACPI_PRT_MAX_BRIDGE_DEPTH 32

static
VOID
AcpiPrtBuildForBridges(
    _In_ ACPI_HANDLE Parent,
    _In_ ULONG Segment,
    _In_ UCHAR Bus,
    _In_ ULONG Depth)
{
    ACPI_HANDLE Child = NULL;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Parent != NULL);

    if (Depth >= ACPI_PRT_MAX_BRIDGE_DEPTH)
        return;

    while (ACPI_SUCCESS(AcpiGetNextObject(ACPI_TYPE_DEVICE, Parent, Child, &Child)))
    {
        ACPI_OBJECT Result;
        ACPI_BUFFER Buffer = { sizeof(Result), &Result };
        ACPI_PCI_ID PciId;
        UINT64 Value;
        UCHAR SecondaryBus;

        if (ACPI_FAILURE(AcpiEvaluateObjectTyped(Child, METHOD_NAME__ADR, NULL, &Buffer, ACPI_TYPE_INTEGER)))
            continue;

        PciId.Segment = (UINT16)Segment;
        PciId.Bus = Bus;
        PciId.Device = (UINT16)((Result.Integer.Value >> 16) & 0xFFFF);
        PciId.Function = (UINT16)(Result.Integer.Value & 0xFFFF);
        if (PciId.Device >= PCI_MAX_DEVICES || PciId.Function >= PCI_MAX_FUNCTION)
            continue;

        if (ACPI_FAILURE(AcpiOsReadPciConfiguration(&PciId, FIELD_OFFSET(PCI_COMMON_CONFIG, VendorID), &Value, 16)) ||
            Value == PCI_INVALID_VENDORID)
        {
            continue;
        }
        if (ACPI_FAILURE(AcpiOsReadPciConfiguration(&PciId, FIELD_OFFSET(PCI_COMMON_CONFIG, HeaderType), &Value, 8)))
            continue;
        Value &= ~PCI_MULTIFUNCTION;
        if (Value != PCI_BRIDGE_TYPE && Value != PCI_CARDBUS_BRIDGE_TYPE)
            continue;
        if (ACPI_FAILURE(AcpiOsReadPciConfiguration(&PciId, FIELD_OFFSET(PCI_COMMON_CONFIG, u.type1.SecondaryBus), &Value, 8)))
            continue;

        SecondaryBus = (UCHAR)Value;
        if (SecondaryBus <= Bus)
        {
            /* TODO: bridges the firmware left unnumbered get no _PRT once the PCI driver numbers them */
            continue;
        }

        (VOID)AcpiPrtBuildForHandle(Child, Segment, SecondaryBus);
        AcpiPrtBuildForBridges(Child, Segment, SecondaryBus, Depth + 1);
    }
}

static
BOOLEAN
NTAPI
HalPciRouteProvider(
    _In_ ULONG Segment,
    _In_ UCHAR Bus,
    _In_ UCHAR Device,
    _In_ UCHAR Function,
    _In_ UCHAR Pin,
    _Out_ PULONG Gsi,
    _Out_opt_ PUCHAR Polarity,
    _Out_opt_ PUCHAR TriggerMode)
{
    UNREFERENCED_PARAMETER(Function);

    for (ULONG i = 0; i < PrtCacheCount; ++i)
    {
        if ((PrtCache[i].Segment == Segment) &&
            (PrtCache[i].Bus == Bus) &&
            (PrtCache[i].Device == Device) &&
            (PrtCache[i].Pin == Pin))
        {
            *Gsi = PrtCache[i].Gsi;
            if (Polarity) *Polarity = PrtCache[i].Polarity;
            if (TriggerMode) *TriggerMode = PrtCache[i].Trigger;
            return TRUE;
        }
    }

    return FALSE;
}

static
ACPI_STATUS
AcpiEnumRootBridgeCallback(
    _In_ ACPI_HANDLE ObjHandle,
    _In_ UINT32 NestingLevel,
    _Inout_opt_ PVOID Context,
    _Inout_opt_ PVOID *ReturnValue)
{
    ACPI_DEVICE_INFO *Info = NULL;
    ACPI_STATUS Status;
    ACPI_OBJECT Result;
    ACPI_BUFFER Buffer = { sizeof(Result), &Result };
    ULONG Segment = 0;
    ULONG Bus = 0;

    UNREFERENCED_PARAMETER(NestingLevel);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(ReturnValue);

    Status = AcpiGetObjectInfo(ObjHandle, &Info);
    if (ACPI_FAILURE(Status))
    {
        return AE_OK;
    }

    Status = AcpiEvaluateObjectTyped(ObjHandle, METHOD_NAME__SEG, NULL, &Buffer, ACPI_TYPE_INTEGER);
    if (ACPI_SUCCESS(Status))
    {
        Segment = (ULONG)Result.Integer.Value;
    }

    Status = AcpiEvaluateObjectTyped(ObjHandle, METHOD_NAME__BBN, NULL, &Buffer, ACPI_TYPE_INTEGER);
    if (ACPI_SUCCESS(Status))
    {
        Bus = (ULONG)Result.Integer.Value;
    }

    AcpiOsFree(Info);

    if (Bus > 0xFF)
    {
        DPRINT1("ACPI: Root bus number %lu out of range, truncating\n", Bus);
        Bus &= 0xFF;
    }

    AcpiPrtBuildForHandle(ObjHandle, Segment, (UCHAR)Bus);
    AcpiPrtBuildForBridges(ObjHandle, Segment, (UCHAR)Bus, 0);
    return AE_OK;
}

#define ACPI_PRT_MAX_LINK_REFERENCES 256

typedef struct _PRT_LINK_REFERENCES
{
    ACPI_HANDLE Handles[ACPI_PRT_MAX_LINK_REFERENCES];
    ULONG Count;
    BOOLEAN Incomplete;
} PRT_LINK_REFERENCES, *PPRT_LINK_REFERENCES;

static
BOOLEAN
AcpiPrtLinkReferenced(
    _In_ const PRT_LINK_REFERENCES *References,
    _In_ ACPI_HANDLE Link)
{
    ULONG Index;

    ASSERT(References->Count <= RTL_NUMBER_OF(References->Handles));
    for (Index = 0; Index < References->Count; Index++)
    {
        if (References->Handles[Index] == Link)
            return TRUE;
    }
    return FALSE;
}

static
ACPI_STATUS
AcpiPrtCollectLinksCallback(
    ACPI_HANDLE ObjHandle,
    UINT32 NestingLevel,
    void *Context,
    void **ReturnValue)
{
    PPRT_LINK_REFERENCES References = Context;
    ACPI_BUFFER Buffer = { ACPI_ALLOCATE_BUFFER, NULL };
    ACPI_HANDLE PrtHandle;
    ACPI_HANDLE LinkHandle;
    ACPI_STATUS Status;

    UNREFERENCED_PARAMETER(NestingLevel);
    UNREFERENCED_PARAMETER(ReturnValue);

    ASSERT(References != NULL);
    ASSERT(References->Count <= RTL_NUMBER_OF(References->Handles));
    if (ACPI_FAILURE(AcpiGetHandle(ObjHandle, METHOD_NAME__PRT, &PrtHandle)))
        return AE_OK;

    Status = BuspGetIrqRoutingTable(ObjHandle, &Buffer);
    if (ACPI_FAILURE(Status) || Buffer.Pointer == NULL)
    {
        DPRINT1("ACPI: _PRT of %p unreadable (0x%X), interrupt links stay enabled\n", ObjHandle, Status);
        References->Incomplete = TRUE;
        return AE_OK;
    }

    for (ACPI_PCI_ROUTING_TABLE *Route = (ACPI_PCI_ROUTING_TABLE *)Buffer.Pointer;
         Route && Route->Length;
         Route = (ACPI_PCI_ROUTING_TABLE *)((PUCHAR)Route + Route->Length))
    {
        ASSERT((PUCHAR)Route + Route->Length <= (PUCHAR)Buffer.Pointer + Buffer.Length);
        if (!Route->Source[0])
            continue;
        if (ACPI_FAILURE(AcpiGetHandle(ObjHandle, Route->Source, &LinkHandle)))
        {
            References->Incomplete = TRUE;
            continue;
        }
        if (AcpiPrtLinkReferenced(References, LinkHandle))
            continue;
        if (References->Count == RTL_NUMBER_OF(References->Handles))
        {
            References->Incomplete = TRUE;
            continue;
        }
        ASSERT(LinkHandle != NULL);
        References->Handles[References->Count++] = LinkHandle;
    }

    ACPI_FREE(Buffer.Pointer);
    return AE_OK;
}

static
ACPI_STATUS
AcpiPrtDisableLinkCallback(
    ACPI_HANDLE ObjHandle,
    UINT32 NestingLevel,
    void *Context,
    void **ReturnValue)
{
    PPRT_LINK_REFERENCES References = Context;
    ACPI_HANDLE DisHandle;
    ACPI_STATUS Status;

    UNREFERENCED_PARAMETER(NestingLevel);
    UNREFERENCED_PARAMETER(ReturnValue);

    ASSERT(References != NULL);
    ASSERT(!References->Incomplete);
    if (AcpiPrtLinkReferenced(References, ObjHandle) ||
        ACPI_FAILURE(AcpiGetHandle(ObjHandle, METHOD_NAME__DIS, &DisHandle)))
    {
        return AE_OK;
    }

    Status = AcpiEvaluateObject(ObjHandle, METHOD_NAME__DIS, NULL, NULL);
    if (ACPI_FAILURE(Status))
        DPRINT1("ACPI: _DIS of unused PCI interrupt link %p failed (0x%X)\n", ObjHandle, Status);
    return AE_OK;
}

/* Windows 11 disables every PCI interrupt link that no _PRT names once the APIC model is selected */
static
VOID
AcpiPrtDisableUnusedLinks(VOID)
{
    PPRT_LINK_REFERENCES References;

    PAGED_CODE();

    References = ExAllocatePoolWithTag(PagedPool, sizeof(*References), ACPI_PRT_POOL_TAG);
    if (References == NULL)
        return;
    RtlZeroMemory(References, sizeof(*References));

    (void)AcpiWalkNamespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
                            AcpiPrtCollectLinksCallback, NULL, References, NULL);
    if (!References->Incomplete)
        (void)AcpiGetDevices("PNP0C0F", AcpiPrtDisableLinkCallback, References, NULL);

    ExFreePoolWithTag(References, ACPI_PRT_POOL_TAG);
}

int
acpi_pci_link_init(VOID)
{
    AcpiPrtResetCache();

    (void)AcpiGetDevices("PNP0A03", AcpiEnumRootBridgeCallback, NULL, NULL);
    (void)AcpiGetDevices("PNP0A08", AcpiEnumRootBridgeCallback, NULL, NULL);
    AcpiPrtDisableUnusedLinks();

    HalpRegisterPciRouteQuery(HalPciRouteProvider);
    DPRINT("ACPI: PCI routing provider registered (%lu entries available)\n",
            PrtCacheCount);

    return 0;
}

void
acpi_pci_link_exit(VOID)
{
    HalpRegisterPciRouteQuery(NULL);
    AcpiPrtResetCache();
}
