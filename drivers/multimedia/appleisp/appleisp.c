/*
 * PROJECT:     LiberNT Apple ISP Camera Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry, device tree description and Plug and Play of the ISP camera
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "appleisp.h"
#include "ispguid.h"

static const ISP_HARDWARE IspHardware[] =
{
    /* TODO: t8103, t8112, t8122, t6000, t6020 and t6031 constants; only the M3 Pro ISP is described */
    {
        "apple,t6030-isp",
        0x350704000ULL,
        0x200f14000ULL, 0x1000,
        0x3503d0920ULL, 0, 0, 8,
        0x3503d0980ULL, 0, 0, 8,
        0x08,
        0x4a40
    },
};

static
BOOLEAN
IspReadCells(
    _In_ PISP_DEVICE Isp,
    _In_ ULONG Node,
    _In_z_ PCSTR Name,
    _Out_writes_(Count) PULONG Values,
    _In_ ULONG Count)
{
    const VOID *Property;
    ULONG Length, Index;
    ULONG64 Value;

    Property = FdtGetProperty(&Isp->Tree.Fdt, Node, Name, &Length);
    if (!Property || Length < Count * sizeof(ULONG))
        return FALSE;
    for (Index = 0; Index < Count; ++Index)
    {
        if (!FdtReadCells(Property, Length, Index, 1, &Value))
            return FALSE;
        Values[Index] = (ULONG)Value;
    }
    return TRUE;
}

static
NTSTATUS
IspReadPresets(
    _Inout_ PISP_DEVICE Isp)
{
    PFDT Fdt = &Isp->Tree.Fdt;
    ULONG Presets, Child, NameLength, Size[2], Output[2], Crop[4];
    PISP_PRESET Preset;
    const CHAR *Name;

    for (Presets = FdtFirstChild(Fdt, Isp->Tree.Node); Presets != FDT_NO_NODE; Presets = FdtNextSibling(Fdt, Presets))
    {
        Name = FdtNodeName(Fdt, Presets, &NameLength);
        if (Name && NameLength == sizeof("sensor-presets") - 1 && RtlEqualMemory(Name, "sensor-presets", NameLength))
            break;
    }
    if (Presets == FDT_NO_NODE)
    {
        DbgPrint("appleisp: the ISP node has no sensor-presets\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    for (Child = FdtFirstChild(Fdt, Presets); Child != FDT_NO_NODE; Child = FdtNextSibling(Fdt, Child))
    {
        if (Isp->PresetCount == ISP_MAX_PRESETS)
        {
            DbgPrint("appleisp: only the first %lu sensor presets are used\n", ISP_MAX_PRESETS);
            break;
        }
        Preset = &Isp->Presets[Isp->PresetCount];
        RtlZeroMemory(Preset, sizeof(*Preset));
        if (!FdtReadU32(Fdt, Child, "apple,config-index", &Preset->ConfigIndex) ||
            !IspReadCells(Isp, Child, "apple,input-size", Size, 2) ||
            !IspReadCells(Isp, Child, "apple,output-size", Output, 2) ||
            !IspReadCells(Isp, Child, "apple,crop", Crop, 4))
        {
            DbgPrint("appleisp: sensor preset %lu is incomplete\n", Isp->PresetCount);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        Preset->InputWidth = Size[0];
        Preset->InputHeight = Size[1];
        Preset->OutputWidth = Output[0];
        Preset->OutputHeight = Output[1];
        Preset->CropX = Crop[0];
        Preset->CropY = Crop[1];
        Preset->CropWidth = Crop[2];
        Preset->CropHeight = Crop[3];
        if (!Preset->OutputWidth || !Preset->OutputHeight || Preset->OutputWidth > 8192 || Preset->OutputHeight > 8192 ||
            (Preset->OutputWidth | Preset->OutputHeight) & 1 || Preset->CropX + Preset->CropWidth > Preset->InputWidth ||
            Preset->CropY + Preset->CropHeight > Preset->InputHeight)
        {
            DbgPrint("appleisp: sensor preset %lu (%lux%lu) is not usable\n", Isp->PresetCount, Preset->OutputWidth,
                     Preset->OutputHeight);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        Preset->Stride = ALIGN_UP_BY(Preset->OutputWidth, ISP_STRIDE_ALIGNMENT);
        Preset->LumaSize = Preset->Stride * Preset->OutputHeight;
        Preset->FrameSize = Preset->LumaSize + Preset->Stride * Preset->OutputHeight / 2;
        Preset->ImageSize = Preset->OutputWidth * Preset->OutputHeight * 3 / 2;
        DbgPrint("appleisp: preset %lu: %lux%lu from sensor mode %lu (%lux%lu, crop %lu,%lu %lux%lu)\n",
                 Isp->PresetCount, Preset->OutputWidth, Preset->OutputHeight, Preset->ConfigIndex, Preset->InputWidth,
                 Preset->InputHeight, Preset->CropX, Preset->CropY, Preset->CropWidth, Preset->CropHeight);
        Isp->PresetCount++;
    }
    return Isp->PresetCount ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

static
NTSTATUS
IspReadRegions(
    _Inout_ PISP_DEVICE Isp)
{
    PFDT Fdt = &Isp->Tree.Fdt;
    const VOID *Regions, *Names, *Addresses;
    ULONG RegionsLength, NamesLength, AddressesLength, Cursor = 0, NameLength, Index, Entry, Memory;
    ULONG Phandle, Parent, AddressCells, SizeCells, Stride;
    ULONG64 Value, Owner, Physical, Size, Iova, IovaSize;
    BOOLEAN Found, Heap = FALSE;
    PISP_REGION Region;
    const CHAR *Name;

    if (!FdtReadU32(Fdt, Isp->Tree.Node, "phandle", &Phandle) ||
        (Parent = FdtDriverFindParentNode(&Isp->Tree, Isp->Tree.Node)) == FDT_NO_NODE)
    {
        DbgPrint("appleisp: the ISP node has no phandle; the boot loader did not prepare it\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    FdtGetCellCounts(Fdt, Parent, &AddressCells, &SizeCells);
    if (!AddressCells || AddressCells > 2 || !SizeCells || SizeCells > 2)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Stride = 1 + AddressCells + SizeCells;
    Regions = FdtGetProperty(Fdt, Isp->Tree.Node, "memory-region", &RegionsLength);
    Names = FdtGetProperty(Fdt, Isp->Tree.Node, "memory-region-names", &NamesLength);
    for (Index = 0; Regions && FdtReadCells(Regions, RegionsLength, Index, 1, &Value); ++Index)
    {
        Name = FdtNextString(Names, NamesLength, &Cursor, &NameLength);
        if (Isp->RegionCount == ISP_MAX_REGIONS || (Name && NameLength >= sizeof(Region->Name)))
        {
            DbgPrint("appleisp: firmware region %lu has too long a name or is one too many\n", Index);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        Region = &Isp->Regions[Isp->RegionCount];
        RtlZeroMemory(Region, sizeof(*Region));
        if (Name && NameLength)
            RtlCopyMemory(Region->Name, Name, NameLength);
        else
            RtlStringCbPrintfA(Region->Name, sizeof(Region->Name), "region%lu", Index);
        Memory = FdtDriverFindPhandle(&Isp->Tree, (ULONG)Value);
        if (Memory == FDT_NO_NODE || !FdtDriverReadReg(&Isp->Tree, Memory, 0, &Physical, &Size) || !Physical || !Size)
        {
            DbgPrint("appleisp: firmware region %s has no memory; the boot loader did not prepare the ISP\n",
                     Region->Name);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        Addresses = FdtGetProperty(Fdt, Memory, "iommu-addresses", &AddressesLength);
        Found = FALSE;
        Iova = IovaSize = 0;
        for (Entry = 0; Addresses && (Entry + 1) * Stride * sizeof(ULONG) <= AddressesLength && !Found; ++Entry)
        {
            if (FdtReadCells(Addresses, AddressesLength, Entry * Stride, 1, &Owner) && Owner == Phandle &&
                FdtReadCells(Addresses, AddressesLength, Entry * Stride + 1, AddressCells, &Iova) &&
                FdtReadCells(Addresses, AddressesLength, Entry * Stride + 1 + AddressCells, SizeCells, &IovaSize))
            {
                Found = TRUE;
            }
        }
        if (!Found || !IovaSize || IovaSize > Size ||
            ((Physical | Iova | IovaSize) & (APPLE_DART_PAGE_SIZE - 1)))
        {
            DbgPrint("appleisp: firmware region %s at %I64x+%I64x has no usable device address (%I64x+%I64x)\n",
                     Region->Name, Physical, Size, Iova, IovaSize);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        Region->Physical = Physical;
        Region->Iova = Iova;
        Region->Size = IovaSize;
        if (!strcmp(Region->Name, "heap"))
        {
            Isp->HeapTop = Iova + IovaSize;
            Heap = TRUE;
        }
        DbgPrint("appleisp: firmware region %s: %I64x+%I64x at device address %I64x\n", Region->Name, Physical,
                 IovaSize, Iova);
        Isp->RegionCount++;
    }
    if (!Heap)
    {
        /* TODO: derive the segments from the exported Apple device tree when the boot loader left no regions */
        DbgPrint("appleisp: no firmware heap region; the boot loader did not prepare the ISP\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
IspReadTree(
    _Inout_ PISP_DEVICE Isp)
{
    PFDT Fdt = &Isp->Tree.Fdt;
    const VOID *Compatible, *Property;
    ULONG Length, Index;
    ULONG64 VmSize;

    Compatible = FdtGetProperty(Fdt, Isp->Tree.Node, "compatible", &Length);
    for (Index = 0; Index < RTL_NUMBER_OF(IspHardware) && !Isp->Hardware; ++Index)
    {
        if (FdtStringListContains(Compatible, Length, IspHardware[Index].Compatible))
            Isp->Hardware = &IspHardware[Index];
    }
    if (!Isp->Hardware)
    {
        DbgPrint("appleisp: this ISP is not one the driver knows\n");
        return STATUS_NOT_SUPPORTED;
    }
    if (!FdtReadU32(Fdt, Isp->Tree.Node, "apple,platform-id", &Isp->PlatformId))
    {
        DbgPrint("appleisp: the ISP node has no apple,platform-id\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (!FdtReadU32(Fdt, Isp->Tree.Node, "apple,temporal-filter", &Isp->TemporalFilter))
        Isp->TemporalFilter = 0;
    Property = FdtGetProperty(Fdt, Isp->Tree.Node, "apple,dart-vm-size", &Length);
    if (!Property || Length != 2 * sizeof(ULONG) || !FdtReadCells(Property, Length, 0, 2, &VmSize) || !VmSize)
    {
        DbgPrint("appleisp: the ISP node has no apple,dart-vm-size\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Isp->DartVmSize = VmSize;
    IspReadCells(Isp, Isp->Tree.Node, "apple,firmware-version", Isp->FirmwareVersion, 3);
    if (!IspReadCells(Isp, Isp->Tree.Node, "apple,firmware-compat", Isp->FirmwareCompat, 3))
    {
        DbgPrint("appleisp: the boot loader did not name the ISP firmware version\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (Isp->FirmwareCompat[0] < 13 || (Isp->FirmwareCompat[0] == 13 && Isp->FirmwareCompat[1] < 5))
    {
        DbgPrint("appleisp: ISP firmware %lu.%lu.%lu predates the command layout the driver uses\n",
                 Isp->FirmwareCompat[0], Isp->FirmwareCompat[1], Isp->FirmwareCompat[2]);
        return STATUS_NOT_SUPPORTED;
    }
    if (!NT_SUCCESS(IspReadPresets(Isp)))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    return IspReadRegions(Isp);
}

static
VOID
IspPublishFriendlyName(
    _In_ PISP_DEVICE Isp,
    _In_ const GUID *Category,
    _In_z_ PCWSTR Name)
{
    UNICODE_STRING Value = RTL_CONSTANT_STRING(L"FriendlyName");
    UNICODE_STRING LinkName;
    PWSTR Links = NULL, Link;
    HANDLE Key;

    if (!NT_SUCCESS(IoGetDeviceInterfaces(Category, Isp->KsDevice->PhysicalDeviceObject,
                                          DEVICE_INTERFACE_INCLUDE_NONACTIVE, &Links)))
    {
        return;
    }
    for (Link = Links; *Link; Link += wcslen(Link) + 1)
    {
        RtlInitUnicodeString(&LinkName, Link);
        if (NT_SUCCESS(IoOpenDeviceInterfaceRegistryKey(&LinkName, KEY_SET_VALUE, &Key)))
        {
            ZwSetValueKey(Key, &Value, 0, REG_SZ, (PVOID)Name, (ULONG)(wcslen(Name) + 1) * sizeof(WCHAR));
            ZwClose(Key);
        }
    }
    ExFreePool(Links);
}

static
VOID
IspInitialize(
    _Inout_ PISP_DEVICE Isp,
    _In_ PKSDEVICE KsDevice)
{
    Isp->KsDevice = KsDevice;
    KeInitializeMutex(&Isp->SurfaceLock, 0);
    KeInitializeMutex(&Isp->CommandLock, 0);
    KeInitializeMutex(&Isp->ChannelLock, 0);
    KeInitializeSpinLock(&Isp->FrameLock);
    KeInitializeEvent(&Isp->ReplyEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Isp->WorkEvent, SynchronizationEvent, FALSE);
    InitializeListHead(&Isp->FirmwareSurfaces);
    Isp->FirmwareState = IspFirmwareOff;
}

static
NTSTATUS
NTAPI
IspPnpStart(
    _In_ PKSDEVICE KsDevice,
    _In_ PIRP Irp,
    _In_opt_ PCM_RESOURCE_LIST Translated,
    _In_opt_ PCM_RESOURCE_LIST Untranslated)
{
    static const WCHAR FriendlyName[] = L"FaceTime HD Camera";
    PISP_DEVICE Isp = KsDevice->Context;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Irp);
    UNREFERENCED_PARAMETER(Untranslated);
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (Isp)
    {
        Status = IspHardwareStart(Isp, Translated);
        if (!NT_SUCCESS(Status))
            IspHardwareStop(Isp);
        return Status;
    }
    Isp = ExAllocatePoolZero(NonPagedPool, sizeof(*Isp), ISP_TAG);
    if (!Isp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = KsAddItemToObjectBag(KsDevice->Bag, Isp, ExFreePool);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Isp, ISP_TAG);
        return Status;
    }
    IspInitialize(Isp, KsDevice);
    KsDevice->Context = Isp;

    Status = FdtDriverOpen(KsDevice->PhysicalDeviceObject, &Isp->Tree);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleisp: the device tree node of the ISP was not found: %lx\n", Status);
        return Status;
    }
    Isp->TreeOpen = TRUE;
    Status = IspReadTree(Isp);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = IspHardwareStart(Isp, Translated);
    if (NT_SUCCESS(Status))
        Status = IspDetect(Isp);
    if (NT_SUCCESS(Status))
        Status = IspBuildFilter(Isp);
    if (NT_SUCCESS(Status))
    {
        Status = KsCreateFilterFactory(KsDevice->FunctionalDeviceObject, Isp->FilterDescriptor, L"GLOBAL", NULL,
                                       KSCREATE_ITEM_FREEONSTOP, NULL, NULL, &Isp->FilterFactory);
    }
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleisp: the camera did not start: %lx\n", Status);
        IspHardwareStop(Isp);
        return Status;
    }
    IspPublishFriendlyName(Isp, &ISP_KSCATEGORY_CAPTURE, FriendlyName);
    IspPublishFriendlyName(Isp, &ISP_KSCATEGORY_VIDEO, FriendlyName);
    IspPublishFriendlyName(Isp, &ISP_KSCATEGORY_VIDEO_CAMERA, FriendlyName);
    DbgPrint("appleisp: FaceTime camera ready: sensor %lx, %lu NV12 sizes\n", Isp->SensorVersion, Isp->PresetCount);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
IspPnpStop(
    _In_ PKSDEVICE KsDevice,
    _In_ PIRP Irp)
{
    PISP_DEVICE Isp = KsDevice->Context;

    UNREFERENCED_PARAMETER(Irp);
    if (Isp)
        IspHardwareStop(Isp);
}

static
VOID
NTAPI
IspPnpRemove(
    _In_ PKSDEVICE KsDevice,
    _In_ PIRP Irp)
{
    PISP_DEVICE Isp = KsDevice->Context;

    UNREFERENCED_PARAMETER(Irp);
    if (!Isp)
        return;
    IspHardwareStop(Isp);
    if (Isp->TreeOpen)
        FdtDriverClose(&Isp->Tree);
    Isp->TreeOpen = FALSE;
}

/* TODO: device power IRPs; the ISP system domain stays on from start to stop and suspend is not handled */
static const KSDEVICE_DISPATCH IspDeviceDispatch =
{
    NULL,
    IspPnpStart,
    NULL,
    NULL,
    NULL,
    IspPnpStop,
    NULL,
    NULL,
    IspPnpRemove,
    NULL,
    IspPnpStop,
    NULL,
    NULL,
    NULL
};

static const KSDEVICE_DESCRIPTOR IspDeviceDescriptor =
{
    &IspDeviceDispatch,
    0,
    NULL
};

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    return KsInitializeDriver(DriverObject, RegistryPath, &IspDeviceDescriptor);
}
