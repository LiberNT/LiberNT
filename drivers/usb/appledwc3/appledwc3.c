/*
 * PROJECT:     LiberNT Apple Silicon USB Glue Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Powers an Apple silicon Type-C USB controller and enumerates its xHCI core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <initguid.h>
#include <wdmguid.h>
#include <ntstrsafe.h>
#include <reactos/drivers/fdtdrv.h>
#include <reactos/drivers/applepmgr.h>
#include <reactos/drivers/appledart.h>
#include <reactos/drivers/appleatcphy.h>
#include <reactos/drivers/appleacepd.h>

#define APPLE_DWC3_TAG              '3wDA'

#define DWC3_GCTL                   0xC110
#define DWC3_GCTL_PRTCAP_MASK       (3UL << 12)
#define DWC3_GCTL_PRTCAP_HOST       (1UL << 12)
#define DWC3_GSNPSID                0xC120
#define DWC3_GUSB2PHYCFG            0xC200
#define DWC3_GUSB2PHYCFG_SUSPHY     (1UL << 6)
#define DWC3_GUSB3PIPECTL           0xC2C0
#define DWC3_GUSB3PIPECTL_SUSPHY    (1UL << 17)
#define DWC3_DCTL                   0xC704
#define DWC3_DCTL_RUN_STOP          (1UL << 31)
#define DWC3_DCTL_CSFTRST           (1UL << 30)
#define DWC3_REGISTER_SPAN          0xC800
#define DWC3_SOFT_RESET_POLLS       250
#define DWC3_SOFT_RESET_SETTLE_MS   50

#define APPLE_DWC3_CIO_LFPS_OFFSET  0x38
#define APPLE_DWC3_CIO_BW_NGT       0x3C
#define APPLE_DWC3_CIO_LINK_TIMER   0x40
#define APPLE_DWC3_GLUE_SPAN        0x44

#define APPLE_DWC3_FDO              0x4F444641UL
#define APPLE_DWC3_PDO              0x4F445041UL

typedef struct _APPLE_DWC3_EXTENSION
{
    ULONG Type;
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT Child;
    struct _APPLE_DWC3_EXTENSION *Parent;
    BOOLEAN ChildReported;
    CM_PARTIAL_RESOURCE_DESCRIPTOR Memory;
    CM_PARTIAL_RESOURCE_DESCRIPTOR Interrupt;
    ULONG64 CoreBase;
    FAST_MUTEX StateLock;
    PIO_WORKITEM RoleWork;
    volatile LONG RoleWorkQueued;
    volatile LONG WantHost;
    volatile LONG Detach;
    BOOLEAN HostRunning;
    BOOLEAN RoleSwitch;
    PCALLBACK_OBJECT RoleCallback;
    PVOID RoleRegistration;
} APPLE_DWC3_EXTENSION, *PAPPLE_DWC3_EXTENSION;

DRIVER_INITIALIZE DriverEntry;

static VOID AppleDwc3QueueRoleWork(_Inout_ PAPPLE_DWC3_EXTENSION Extension);

static
NTSTATUS
AppleDwc3FindResources(
    _Inout_ PAPPLE_DWC3_EXTENSION Extension,
    _In_opt_ PCM_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    BOOLEAN Memory = FALSE, Interrupt = FALSE;
    ULONG Index;

    if (!Resources || !Resources->Count)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (Index = 0; Index < Resources->List[0].PartialResourceList.Count; ++Index)
    {
        Descriptor = &Resources->List[0].PartialResourceList.PartialDescriptors[Index];
        if (Descriptor->Type == CmResourceTypeMemory && !Memory &&
            (ULONG64)Descriptor->u.Memory.Start.QuadPart == Extension->CoreBase)
        {
            Extension->Memory = *Descriptor;
            Memory = TRUE;
        }
        else if (Descriptor->Type == CmResourceTypeInterrupt && !Interrupt)
        {
            Extension->Interrupt = *Descriptor;
            Interrupt = TRUE;
        }
    }
    if (!Memory || !Interrupt || Extension->Memory.u.Memory.Length < DWC3_REGISTER_SPAN)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    return STATUS_SUCCESS;
}

static
VOID
AppleDwc3DelayMs(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10000LL * Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
NTSTATUS
AppleDwc3SoftReset(
    _In_ PUCHAR Core)
{
    volatile ULONG *Dctl = (volatile ULONG *)(Core + DWC3_DCTL);
    ULONG Poll;

    WRITE_REGISTER_ULONG(Dctl, (READ_REGISTER_ULONG(Dctl) & ~DWC3_DCTL_RUN_STOP) | DWC3_DCTL_CSFTRST);
    for (Poll = 0; Poll < DWC3_SOFT_RESET_POLLS; ++Poll)
    {
        if (!(READ_REGISTER_ULONG(Dctl) & DWC3_DCTL_CSFTRST))
        {
            AppleDwc3DelayMs(DWC3_SOFT_RESET_SETTLE_MS);
            return STATUS_SUCCESS;
        }
        AppleDwc3DelayMs(1);
    }
    return STATUS_IO_TIMEOUT;
}

static
VOID
AppleDwc3SetupGlue(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ PAPPLE_DWC3_EXTENSION Extension)
{
    PUCHAR Glue;
    ULONG Size, Index;
    volatile ULONG *LinkTimer;

    Index = FdtDriverFindRegIndex(Tree, Tree->Node, "dwc3-apple");
    if (Index == MAXULONG)
        return;
    Glue = FdtDriverMapReg(Tree, Tree->Node, Index, NULL, &Size);
    if (!Glue)
        return;
    if (Size >= APPLE_DWC3_GLUE_SPAN)
    {
        WRITE_REGISTER_ULONG((volatile ULONG *)(Glue + APPLE_DWC3_CIO_LFPS_OFFSET), 0x0F800F80);
        WRITE_REGISTER_ULONG((volatile ULONG *)(Glue + APPLE_DWC3_CIO_BW_NGT), 0x0FC00FC0);
        LinkTimer = (volatile ULONG *)(Glue + APPLE_DWC3_CIO_LINK_TIMER);
        WRITE_REGISTER_ULONG(LinkTimer, (READ_REGISTER_ULONG(LinkTimer) & 0xFF000000UL) | 0x00140A10UL);
    }
    else
    {
        DbgPrint("appledwc3: %I64x glue registers are only %lu bytes\n", Extension->CoreBase, Size);
    }
    MmUnmapIoSpace(Glue, Size);
}

static
NTSTATUS
AppleDwc3Prepare(
    _Inout_ PAPPLE_DWC3_EXTENSION Extension,
    _In_opt_ PCM_RESOURCE_LIST Resources)
{
    FDT_DRIVER_TREE Tree;
    ULONG64 Size;
    ULONG Length;
    NTSTATUS Status;

    Status = FdtDriverOpen(Extension->PhysicalDevice, &Tree);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!FdtDriverReadReg(&Tree, Tree.Node, 0, &Extension->CoreBase, &Size))
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Done;
    }
    Extension->RoleSwitch = FdtGetProperty(&Tree.Fdt, Tree.Node, "usb-role-switch", &Length) != NULL;
    Status = AppleDwc3FindResources(Extension, Resources);
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = ApplePmgrPowerOnDevice(&Tree, Tree.Node);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appledwc3: %I64x power domains did not turn on: %lx\n", Extension->CoreBase, Status);
        goto Done;
    }
    Status = AppleDartMapPhysical(&Tree, Tree.Node, "appledwc3");
    if (!NT_SUCCESS(Status))
        DbgPrint("appledwc3: %I64x DMA translation is not set up: %lx\n", Extension->CoreBase, Status);

Done:
    FdtDriverClose(&Tree);
    return Status;
}

static
VOID
AppleDwc3PowerDown(
    _Inout_ PAPPLE_DWC3_EXTENSION Extension)
{
    FDT_DRIVER_TREE Tree;
    APPLE_ATCPHY Phy;
    NTSTATUS Status;

    ASSERT(!Extension->HostRunning);
    if (!NT_SUCCESS(FdtDriverOpen(Extension->PhysicalDevice, &Tree)))
        return;
    Status = AppleAtcphyOpen(&Tree, Tree.Node, &Phy);
    if (NT_SUCCESS(Status))
    {
        Status = ApplePmgrPowerOnDevice(&Tree, Phy.Node);
        if (NT_SUCCESS(Status))
            Status = AppleAtcphyStopUsb2Host(&Phy);
        AppleAtcphyClose(&Phy);
    }
    FdtDriverClose(&Tree);
    DbgPrint("appledwc3: %I64x controller held in reset, USB2 PHY off, Type-C PHY asleep: %lx\n", Extension->CoreBase,
             Status);
}

static
NTSTATUS
AppleDwc3PowerUp(
    _Inout_ PAPPLE_DWC3_EXTENSION Extension)
{
    FDT_DRIVER_TREE Tree;
    APPLE_ATCPHY Phy;
    PUCHAR Core = NULL;
    ULONG Identifier, Control;
    NTSTATUS Status, PhyStatus;
    BOOLEAN HavePhy = FALSE;

    RtlZeroMemory(&Phy, sizeof(Phy));
    Status = FdtDriverOpen(Extension->PhysicalDevice, &Tree);
    if (!NT_SUCCESS(Status))
        return Status;
    PhyStatus = AppleAtcphyOpen(&Tree, Tree.Node, &Phy);
    if (NT_SUCCESS(PhyStatus))
    {
        HavePhy = TRUE;
        PhyStatus = ApplePmgrPowerOnDevice(&Tree, Phy.Node);
        if (NT_SUCCESS(PhyStatus))
            PhyStatus = AppleAtcphyStartUsb2Host(&Tree, &Phy);
        DbgPrint("appledwc3: %I64x Type-C PHY USB2 host start %lx, tunables %lu/%lu/%lu\n", Extension->CoreBase,
                 PhyStatus, Phy.TunableEntries[0], Phy.TunableEntries[1], Phy.TunableEntries[2]);
        if (!NT_SUCCESS(PhyStatus))
        {
            Status = PhyStatus;
            goto Done;
        }
    }
    else if (PhyStatus != STATUS_NOT_FOUND)
    {
        DbgPrint("appledwc3: %I64x Type-C PHY is not usable: %lx\n", Extension->CoreBase, PhyStatus);
    }
    Core = MmMapIoSpace(Extension->Memory.u.Memory.Start, DWC3_REGISTER_SPAN, MmNonCached);
    if (!Core)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    Identifier = READ_REGISTER_ULONG((volatile ULONG *)(Core + DWC3_GSNPSID));
    Control = READ_REGISTER_ULONG((volatile ULONG *)(Core + DWC3_GCTL));
    DbgPrint("appledwc3: %I64x core %08lx, control %08lx, USB2 PHY %08lx, USB3 pipe %08lx, interrupt %lu\n",
             Extension->CoreBase, Identifier, Control,
             READ_REGISTER_ULONG((volatile ULONG *)(Core + DWC3_GUSB2PHYCFG)),
             READ_REGISTER_ULONG((volatile ULONG *)(Core + DWC3_GUSB3PIPECTL)), Extension->Interrupt.u.Interrupt.Vector);
    if ((Identifier & 0xFFFF0000UL) != 0x55330000UL && (Identifier & 0xFFFF0000UL) != 0x33310000UL &&
        (Identifier & 0xFFFF0000UL) != 0x33320000UL)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Done;
    }
    if (HavePhy)
    {
        Status = AppleDwc3SoftReset(Core);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("appledwc3: %I64x core soft reset did not finish\n", Extension->CoreBase);
            goto Done;
        }
        AppleDwc3SetupGlue(&Tree, Extension);
        Control = READ_REGISTER_ULONG((volatile ULONG *)(Core + DWC3_GCTL));
    }
    WRITE_REGISTER_ULONG((volatile ULONG *)(Core + DWC3_GCTL), (Control & ~DWC3_GCTL_PRTCAP_MASK) | DWC3_GCTL_PRTCAP_HOST);
    if (HavePhy)
    {
        WRITE_REGISTER_ULONG((volatile ULONG *)(Core + DWC3_GUSB2PHYCFG),
                             READ_REGISTER_ULONG((volatile ULONG *)(Core + DWC3_GUSB2PHYCFG)) | DWC3_GUSB2PHYCFG_SUSPHY);
        WRITE_REGISTER_ULONG((volatile ULONG *)(Core + DWC3_GUSB3PIPECTL),
                             READ_REGISTER_ULONG((volatile ULONG *)(Core + DWC3_GUSB3PIPECTL)) | DWC3_GUSB3PIPECTL_SUSPHY);
        PhyStatus = AppleAtcphySelectDummyPipe(&Phy);
        DbgPrint("appledwc3: %I64x USB3 pipe on the dummy PHY %lx, USB2 PHY %08lx, USB3 pipe %08lx\n", Extension->CoreBase,
                 PhyStatus, READ_REGISTER_ULONG((volatile ULONG *)(Core + DWC3_GUSB2PHYCFG)),
                 READ_REGISTER_ULONG((volatile ULONG *)(Core + DWC3_GUSB3PIPECTL)));
    }
    Status = STATUS_SUCCESS;

Done:
    if (Core)
        MmUnmapIoSpace(Core, DWC3_REGISTER_SPAN);
    AppleAtcphyClose(&Phy);
    FdtDriverClose(&Tree);
    return Status;
}

static
NTSTATUS
AppleDwc3CopyString(
    _In_reads_bytes_(Size) const WCHAR *Source,
    _In_ SIZE_T Size,
    _Out_ PULONG_PTR Information)
{
    PWCHAR Copy = ExAllocatePoolWithTag(PagedPool, Size, APPLE_DWC3_TAG);

    if (!Copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Copy, Source, Size);
    *Information = (ULONG_PTR)Copy;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AppleDwc3ChildResources(
    _In_ PAPPLE_DWC3_EXTENSION Extension,
    _Out_ PULONG_PTR Information)
{
    SIZE_T Size = FIELD_OFFSET(CM_RESOURCE_LIST, List[0].PartialResourceList.PartialDescriptors[2]);
    PCM_RESOURCE_LIST List = ExAllocatePoolZero(PagedPool, Size, APPLE_DWC3_TAG);

    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    List->Count = 1;
    List->List[0].InterfaceType = Internal;
    List->List[0].PartialResourceList.Version = 1;
    List->List[0].PartialResourceList.Revision = 1;
    List->List[0].PartialResourceList.Count = 2;
    List->List[0].PartialResourceList.PartialDescriptors[0] = Extension->Parent->Memory;
    List->List[0].PartialResourceList.PartialDescriptors[1] = Extension->Parent->Interrupt;
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AppleDwc3ChildRequirements(
    _In_ PAPPLE_DWC3_EXTENSION Extension,
    _Out_ PULONG_PTR Information)
{
    SIZE_T Size = FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors[2]);
    PIO_RESOURCE_REQUIREMENTS_LIST List = ExAllocatePoolZero(PagedPool, Size, APPLE_DWC3_TAG);
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Memory = &Extension->Parent->Memory;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Interrupt = &Extension->Parent->Interrupt;
    PIO_RESOURCE_DESCRIPTOR Resource;

    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    List->ListSize = (ULONG)Size;
    List->InterfaceType = Internal;
    List->AlternativeLists = 1;
    List->List[0].Version = 1;
    List->List[0].Revision = 1;
    List->List[0].Count = 2;
    Resource = &List->List[0].Descriptors[0];
    Resource->Type = CmResourceTypeMemory;
    Resource->ShareDisposition = CmResourceShareShared;
    Resource->Flags = Memory->Flags;
    Resource->u.Memory.Length = Memory->u.Memory.Length;
    Resource->u.Memory.Alignment = 1;
    Resource->u.Memory.MinimumAddress = Memory->u.Memory.Start;
    Resource->u.Memory.MaximumAddress.QuadPart = Memory->u.Memory.Start.QuadPart + Memory->u.Memory.Length - 1;
    Resource++;
    Resource->Type = CmResourceTypeInterrupt;
    Resource->ShareDisposition = CmResourceShareShared;
    Resource->Flags = Interrupt->Flags;
    Resource->u.Interrupt.MinimumVector = Interrupt->u.Interrupt.Vector;
    Resource->u.Interrupt.MaximumVector = Interrupt->u.Interrupt.Vector;
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AppleDwc3ChildPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_DWC3_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = Irp->IoStatus.Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            switch (Stack->Parameters.QueryId.IdType)
            {
                case BusQueryDeviceID:
                {
                    static const WCHAR Id[] = L"FDT\\snps_dwc3";
                    Status = AppleDwc3CopyString(Id, sizeof(Id), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryHardwareIDs:
                {
                    static const WCHAR Ids[] = L"FDT\\snps_dwc3\0";
                    Status = AppleDwc3CopyString(Ids, sizeof(Ids), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryInstanceID:
                {
                    WCHAR Id[20];

                    Status = RtlStringCbPrintfW(Id, sizeof(Id), L"%I64x", Extension->Parent ? Extension->Parent->CoreBase : 0);
                    if (NT_SUCCESS(Status))
                        Status = AppleDwc3CopyString(Id, (wcslen(Id) + 1) * sizeof(WCHAR), &Irp->IoStatus.Information);
                    break;
                }
                default:
                    break;
            }
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            if (Stack->Parameters.QueryDeviceText.DeviceTextType == DeviceTextDescription)
            {
                static const WCHAR Text[] = L"Apple silicon USB xHCI controller";
                Status = AppleDwc3CopyString(Text, sizeof(Text), &Irp->IoStatus.Information);
            }
            break;

        case IRP_MN_QUERY_RESOURCES:
            Status = Extension->Parent ? AppleDwc3ChildResources(Extension, &Irp->IoStatus.Information)
                                       : STATUS_NO_SUCH_DEVICE;
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Status = Extension->Parent ? AppleDwc3ChildRequirements(Extension, &Irp->IoStatus.Information)
                                       : STATUS_NO_SUCH_DEVICE;
            break;

        case IRP_MN_QUERY_BUS_INFORMATION:
        {
            PPNP_BUS_INFORMATION Information = ExAllocatePoolWithTag(PagedPool, sizeof(*Information), APPLE_DWC3_TAG);

            if (!Information)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            Information->BusTypeGuid = GUID_BUS_TYPE_INTERNAL;
            Information->LegacyBusType = Internal;
            Information->BusNumber = 0;
            Irp->IoStatus.Information = (ULONG_PTR)Information;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_CAPABILITIES:
        {
            PDEVICE_CAPABILITIES Capabilities = Stack->Parameters.DeviceCapabilities.Capabilities;

            if (!Capabilities || Capabilities->Version != 1 || Capabilities->Size < sizeof(*Capabilities))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Capabilities->SilentInstall = TRUE;
            Capabilities->Removable = FALSE;
            Capabilities->SurpriseRemovalOK = FALSE;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == TargetDeviceRelation)
            {
                PDEVICE_RELATIONS Relations = ExAllocatePoolWithTag(PagedPool, sizeof(*Relations), APPLE_DWC3_TAG);

                if (!Relations)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                Relations->Count = 1;
                Relations->Objects[0] = DeviceObject;
                ObReferenceObject(DeviceObject);
                Irp->IoStatus.Information = (ULONG_PTR)Relations;
                Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_START_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            if (!Extension->Parent || !Extension->Parent->ChildReported)
            {
                PAPPLE_DWC3_EXTENSION Parent = Extension->Parent;

                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                if (Parent)
                {
                    ExAcquireFastMutex(&Parent->StateLock);
                    if (!Parent->HostRunning)
                        AppleDwc3PowerDown(Parent);
                    Parent->Child = NULL;
                    ExReleaseFastMutex(&Parent->StateLock);
                }
                IoDeleteDevice(DeviceObject);
                if (Parent && InterlockedCompareExchange(&Parent->WantHost, 0, 0))
                    AppleDwc3QueueRoleWork(Parent);
                return Status;
            }
            break;

        default:
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
AppleDwc3CreateChild(
    _Inout_ PAPPLE_DWC3_EXTENSION Extension)
{
    PAPPLE_DWC3_EXTENSION ChildExtension;
    PDEVICE_OBJECT Child;
    NTSTATUS Status;

    if (Extension->Child)
        return STATUS_SUCCESS;
    Status = IoCreateDevice(Extension->Self->DriverObject, sizeof(APPLE_DWC3_EXTENSION), NULL,
                            FILE_DEVICE_CONTROLLER, FILE_AUTOGENERATED_DEVICE_NAME, FALSE, &Child);
    if (!NT_SUCCESS(Status))
        return Status;
    ChildExtension = Child->DeviceExtension;
    RtlZeroMemory(ChildExtension, sizeof(*ChildExtension));
    ChildExtension->Type = APPLE_DWC3_PDO;
    ChildExtension->Self = Child;
    ChildExtension->Parent = Extension;
    Child->Flags &= ~DO_DEVICE_INITIALIZING;
    Extension->Child = Child;
    Extension->ChildReported = TRUE;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
AppleDwc3RoleWork(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PAPPLE_DWC3_EXTENSION Extension = Context;
    LONG Want, Detach;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    for (;;)
    {
        Want = InterlockedCompareExchange(&Extension->WantHost, 0, 0);
        Detach = InterlockedExchange(&Extension->Detach, 0);
        ExAcquireFastMutex(&Extension->StateLock);
        if ((!Want || Detach) && Extension->HostRunning)
        {
            DbgPrint("appledwc3: %I64x cable detached, removing the xHCI controller\n", Extension->CoreBase);
            Extension->HostRunning = FALSE;
            Extension->ChildReported = FALSE;
            IoInvalidateDeviceRelations(Extension->PhysicalDevice, BusRelations);
        }
        else if (Want && !Extension->HostRunning && !Extension->Child)
        {
            Status = AppleDwc3PowerUp(Extension);
            if (NT_SUCCESS(Status))
                Status = AppleDwc3CreateChild(Extension);
            DbgPrint("appledwc3: %I64x cable attached as host, controller start %lx\n", Extension->CoreBase, Status);
            if (NT_SUCCESS(Status))
            {
                Extension->HostRunning = TRUE;
                IoInvalidateDeviceRelations(Extension->PhysicalDevice, BusRelations);
            }
        }
        ExReleaseFastMutex(&Extension->StateLock);
        InterlockedExchange(&Extension->RoleWorkQueued, 0);
        if ((InterlockedCompareExchange(&Extension->WantHost, 0, 0) == Want &&
             !InterlockedCompareExchange(&Extension->Detach, 0, 0)) ||
            InterlockedExchange(&Extension->RoleWorkQueued, 1))
            break;
    }
}

static
VOID
AppleDwc3QueueRoleWork(
    _Inout_ PAPPLE_DWC3_EXTENSION Extension)
{
    if (Extension->RoleWork && !InterlockedExchange(&Extension->RoleWorkQueued, 1))
        IoQueueWorkItem(Extension->RoleWork, AppleDwc3RoleWork, DelayedWorkQueue, Extension);
}

static
VOID
NTAPI
AppleDwc3RoleNotify(
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PAPPLE_DWC3_EXTENSION Extension = Context;
    ULONG_PTR Role = (ULONG_PTR)Argument2;

    if (!Extension || (ULONG64)(ULONG_PTR)Argument1 != Extension->CoreBase ||
        (Role != APPLE_TYPEC_ROLE_NONE && Role != APPLE_TYPEC_ROLE_HOST))
        return;
    if (Role == APPLE_TYPEC_ROLE_NONE)
        InterlockedExchange(&Extension->Detach, 1);
    InterlockedExchange(&Extension->WantHost, Role == APPLE_TYPEC_ROLE_HOST);
    AppleDwc3QueueRoleWork(Extension);
}

static
VOID
AppleDwc3ListenForRole(
    _Inout_ PAPPLE_DWC3_EXTENSION Extension)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(APPLE_TYPEC_CALLBACK_NAME);
    OBJECT_ATTRIBUTES Attributes;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE | OBJ_PERMANENT, NULL, NULL);
    if (!NT_SUCCESS(ExCreateCallback(&Extension->RoleCallback, &Attributes, TRUE, TRUE)))
    {
        Extension->RoleCallback = NULL;
        return;
    }
    Extension->RoleRegistration = ExRegisterCallback(Extension->RoleCallback, AppleDwc3RoleNotify, Extension);
    ExNotifyCallback(Extension->RoleCallback, (PVOID)(ULONG_PTR)Extension->CoreBase, (PVOID)APPLE_TYPEC_QUERY);
}

static
VOID
AppleDwc3StopListening(
    _Inout_ PAPPLE_DWC3_EXTENSION Extension)
{
    LARGE_INTEGER Interval;

    if (Extension->RoleRegistration)
    {
        ExUnregisterCallback(Extension->RoleRegistration);
        Extension->RoleRegistration = NULL;
    }
    if (Extension->RoleCallback)
    {
        ObDereferenceObject(Extension->RoleCallback);
        Extension->RoleCallback = NULL;
    }
    Interval.QuadPart = -10000LL;
    while (InterlockedCompareExchange(&Extension->RoleWorkQueued, 0, 0))
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
NTSTATUS
AppleDwc3FdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_DWC3_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->LowerDevice, Irp) ? Irp->IoStatus.Status
                                                                             : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                Status = AppleDwc3Prepare(Extension, Stack->Parameters.StartDevice.AllocatedResources);
            if (NT_SUCCESS(Status))
            {
                if (Extension->RoleSwitch)
                {
                    ExAcquireFastMutex(&Extension->StateLock);
                    AppleDwc3PowerDown(Extension);
                    ExReleaseFastMutex(&Extension->StateLock);
                    DbgPrint("appledwc3: %I64x waits for a USB-C data connection\n", Extension->CoreBase);
                    AppleDwc3ListenForRole(Extension);
                }
                else
                {
                    InterlockedExchange(&Extension->WantHost, 1);
                    AppleDwc3QueueRoleWork(Extension);
                }
            }
            else
            {
                DbgPrint("appledwc3: start failed %lx\n", Status);
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == BusRelations)
            {
                PDEVICE_RELATIONS Previous = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
                PDEVICE_RELATIONS Relations;
                ULONG Count = Previous ? Previous->Count : 0;
                ULONG Extra = (Extension->Child && Extension->ChildReported) ? 1 : 0;

                Relations = ExAllocatePoolWithTag(PagedPool, FIELD_OFFSET(DEVICE_RELATIONS, Objects[Count + Extra + 1]),
                                                  APPLE_DWC3_TAG);
                if (!Relations)
                {
                    Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }
                Relations->Count = Count;
                if (Count)
                    RtlCopyMemory(Relations->Objects, Previous->Objects, Count * sizeof(PDEVICE_OBJECT));
                if (Extra)
                {
                    Relations->Objects[Relations->Count++] = Extension->Child;
                    ObReferenceObject(Extension->Child);
                }
                if (Previous)
                    ExFreePool(Previous);
                Irp->IoStatus.Information = (ULONG_PTR)Relations;
                Irp->IoStatus.Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_REMOVE_DEVICE:
            AppleDwc3StopListening(Extension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            Extension->ChildReported = FALSE;
            if (Extension->RoleWork)
            {
                IoFreeWorkItem(Extension->RoleWork);
                Extension->RoleWork = NULL;
            }
            if (Extension->Child)
            {
                PAPPLE_DWC3_EXTENSION ChildExtension = Extension->Child->DeviceExtension;

                ChildExtension->Parent = NULL;
                IoDeleteDevice(Extension->Child);
                Extension->Child = NULL;
            }
            IoDetachDevice(Extension->LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
AppleDwc3DispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_DWC3_EXTENSION Extension = DeviceObject->DeviceExtension;

    if (Extension->Type == APPLE_DWC3_PDO)
        return AppleDwc3ChildPnp(DeviceObject, Irp);
    return AppleDwc3FdoPnp(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
AppleDwc3DispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_DWC3_EXTENSION Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    PoStartNextPowerIrp(Irp);
    if (Extension->Type == APPLE_DWC3_PDO)
    {
        Status = STATUS_SUCCESS;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
AppleDwc3DispatchOther(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_DWC3_EXTENSION Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    if (Extension->Type == APPLE_DWC3_PDO)
    {
        Status = Irp->IoStatus.Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
AppleDwc3AddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PAPPLE_DWC3_EXTENSION Extension;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(APPLE_DWC3_EXTENSION), NULL, FILE_DEVICE_BUS_EXTENDER,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension = Fdo->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Type = APPLE_DWC3_FDO;
    Extension->Self = Fdo;
    Extension->PhysicalDevice = PhysicalDeviceObject;
    ExInitializeFastMutex(&Extension->StateLock);
    Extension->RoleWork = IoAllocateWorkItem(Fdo);
    if (!Extension->RoleWork)
    {
        IoDeleteDevice(Fdo);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Extension->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (!Extension->LowerDevice)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }
    Fdo->Flags |= DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; ++Index)
        DriverObject->MajorFunction[Index] = AppleDwc3DispatchOther;
    DriverObject->MajorFunction[IRP_MJ_PNP] = AppleDwc3DispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = AppleDwc3DispatchPower;
    DriverObject->DriverExtension->AddDevice = AppleDwc3AddDevice;
    return STATUS_SUCCESS;
}
