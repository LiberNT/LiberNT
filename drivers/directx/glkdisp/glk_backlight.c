/*
 * PROJECT:     LiberNT Gemini Lake Display Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Integrated panel backlight through the display engine PWM
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "softgpu.h"

#define GLK_PCI_BAR0                0x10UL
#define GLK_BLC_PAGE                0xC8000ULL
#define GLK_BLC_PWM_CTL             0x250UL
#define GLK_BLC_PWM_FREQ            0x254UL
#define GLK_BLC_PWM_DUTY            0x258UL
#define GLK_BLC_CONTROLLER_STRIDE   0x100UL
#define GLK_BLC_CONTROLLER_COUNT    2UL
#define GLK_BLC_PWM_ENABLE          0x80000000UL
#define GLK_BLC_LEVEL_COUNT         100UL
#define GLK_BLC_TAG                 'lBlG'

#define GLK_PCI_ASLS                0xFCUL
#define GLK_OPREGION_SIZE           0x2000UL
#define GLK_OPREGION_MINOR          0x16UL
#define GLK_OPREGION_MAJOR          0x17UL
#define GLK_OPREGION_MBOXES         0x58UL
#define GLK_OPREGION_MBOX_ASLE      0x04UL
#define GLK_OPREGION_MBOX_ASLE_EXT  0x10UL
#define GLK_OPREGION_RVDA           0x3BAUL
#define GLK_OPREGION_RVDS           0x3C2UL
#define GLK_OPREGION_VBT            0x400UL
#define GLK_OPREGION_ASLE_EXT       0x1C00UL

#define GLK_VBT_HEADER_SIZE         48UL
#define GLK_VBT_SIZE                24UL
#define GLK_VBT_BDB_OFFSET          28UL
#define GLK_BDB_HEADER_SIZE         22UL
#define GLK_BDB_VERSION             16UL
#define GLK_BDB_HEADER_LENGTH       18UL
#define GLK_BDB_SIZE                20UL
#define GLK_BDB_LFP_OPTIONS         40
#define GLK_BDB_LFP_BACKLIGHT       43
#define GLK_BDB_MIPI_SEQUENCE       53
#define GLK_VBT_PANEL_TYPES         16UL
#define GLK_VBT_ENTRY_SIZE          6UL
#define GLK_VBT_ENTRY               1UL
#define GLK_VBT_CONTROL             113UL
#define GLK_VBT_LEVEL               129UL
#define GLK_VBT_MIN_LEVEL           193UL
#define GLK_VBT_PRECISION           257UL
#define GLK_VBT_TYPE_PWM            2
#define GLK_VBT_MINIMUM_LIMIT       64UL
#define GLK_VBT_MINIMUM_SCALE       255UL

typedef struct _GLK_BACKLIGHT
{
    PUCHAR Registers;
    ULONG Controller;
    ULONG Maximum;
    ULONG Minimum;
    BOOLEAN VbtPresent;
    ULONG VbtController;
    ULONG VbtMinimum;
    EX_RUNDOWN_REF Rundown;
} GLK_BACKLIGHT, *PGLK_BACKLIGHT;

static const GUID GlkGuidBrightness =
    { 0xFDE5BBA4, 0xB3F9, 0x46FB, { 0xBD, 0xAA, 0x07, 0x28, 0xCE, 0x31, 0x00, 0xB4 } };

static PULONG
GlkBacklightRegister(
    _In_ PGLK_BACKLIGHT Backlight,
    _In_ ULONG Controller,
    _In_ ULONG Offset)
{
    return (PULONG)(Backlight->Registers + Controller * GLK_BLC_CONTROLLER_STRIDE + Offset);
}

static ULONG
GlkRead8(
    _In_reads_bytes_(Size) const UCHAR *Data,
    _In_ ULONG Size,
    _In_ ULONG Offset)
{
    return Offset < Size ? Data[Offset] : 0;
}

static ULONG
GlkRead16(
    _In_reads_bytes_(Size) const UCHAR *Data,
    _In_ ULONG Size,
    _In_ ULONG Offset)
{
    return GlkRead8(Data, Size, Offset) | (GlkRead8(Data, Size, Offset + 1) << 8);
}

static ULONG
GlkRead32(
    _In_reads_bytes_(Size) const UCHAR *Data,
    _In_ ULONG Size,
    _In_ ULONG Offset)
{
    return GlkRead16(Data, Size, Offset) | (GlkRead16(Data, Size, Offset + 2) << 16);
}

static const UCHAR *
GlkVbtFindBlock(
    _In_reads_bytes_(BdbSize) const UCHAR *Bdb,
    _In_ ULONG BdbSize,
    _In_ UCHAR Id,
    _Out_ PULONG BlockSize)
{
    ULONG Index = GlkRead16(Bdb, BdbSize, GLK_BDB_HEADER_LENGTH);
    ULONG Size;
    UCHAR Current;

    *BlockSize = 0;
    while (Index + 3 < BdbSize)
    {
        Current = Bdb[Index];
        if (Current == GLK_BDB_MIPI_SEQUENCE && GlkRead8(Bdb, BdbSize, Index + 3) >= 3)
            Size = GlkRead32(Bdb, BdbSize, Index + 4);
        else
            Size = GlkRead16(Bdb, BdbSize, Index + 1);
        Index += 3;
        if (Size > BdbSize - Index)
            return NULL;
        if (Current == Id)
        {
            *BlockSize = Size;
            return Bdb + Index;
        }
        Index += Size;
    }
    return NULL;
}

static BOOLEAN
GlkVbtValid(
    _In_reads_bytes_(Size) const UCHAR *Vbt,
    _In_ ULONG Size)
{
    ULONG VbtSize;
    ULONG BdbOffset;

    if (Size < GLK_VBT_HEADER_SIZE || !RtlEqualMemory(Vbt, "$VBT", 4))
        return FALSE;

    VbtSize = GlkRead16(Vbt, Size, GLK_VBT_SIZE);
    BdbOffset = GlkRead32(Vbt, Size, GLK_VBT_BDB_OFFSET);
    return VbtSize <= Size &&
           BdbOffset <= VbtSize &&
           GLK_BDB_HEADER_SIZE <= VbtSize - BdbOffset &&
           GlkRead16(Vbt + BdbOffset, VbtSize - BdbOffset, GLK_BDB_SIZE) <= VbtSize - BdbOffset;
}

static VOID
GlkBacklightParseVbt(
    _Inout_ PGLK_BACKLIGHT Backlight,
    _In_reads_bytes_(Size) const UCHAR *Vbt,
    _In_ ULONG Size)
{
    const UCHAR *Bdb = Vbt + GlkRead32(Vbt, Size, GLK_VBT_BDB_OFFSET);
    const UCHAR *Block;
    ULONG BdbSize = GlkRead16(Bdb, GLK_BDB_HEADER_SIZE, GLK_BDB_SIZE);
    ULONG Version = GlkRead16(Bdb, GLK_BDB_HEADER_SIZE, GLK_BDB_VERSION);
    ULONG BlockSize;
    ULONG PanelType = 0;
    ULONG Entry;
    ULONG Minimum;

    Block = GlkVbtFindBlock(Bdb, BdbSize, GLK_BDB_LFP_OPTIONS, &BlockSize);
    if (Block != NULL && GlkRead8(Block, BlockSize, 0) < GLK_VBT_PANEL_TYPES)
        PanelType = GlkRead8(Block, BlockSize, 0);

    Block = GlkVbtFindBlock(Bdb, BdbSize, GLK_BDB_LFP_BACKLIGHT, &BlockSize);
    if (Block == NULL || GlkRead8(Block, BlockSize, 0) != GLK_VBT_ENTRY_SIZE)
        return;

    Entry = GLK_VBT_ENTRY + PanelType * GLK_VBT_ENTRY_SIZE;
    if ((GlkRead8(Block, BlockSize, Entry) & 0x3) != GLK_VBT_TYPE_PWM)
        return;

    Backlight->VbtController = 0;
    if (Version >= 191)
        Backlight->VbtController = GlkRead8(Block, BlockSize, GLK_VBT_CONTROL + PanelType) >> 4;

    if (Version >= 234)
    {
        Minimum = GlkRead16(Block, BlockSize, GLK_VBT_MIN_LEVEL + PanelType * 4);
        if (Version >= 236 ? GlkRead8(Block, BlockSize, GLK_VBT_PRECISION + PanelType) == 16
                           : GlkRead16(Block, BlockSize, GLK_VBT_LEVEL + PanelType * 4) > GLK_VBT_MINIMUM_SCALE)
        {
            Minimum /= GLK_VBT_MINIMUM_SCALE;
        }
    }
    else
    {
        Minimum = GlkRead8(Block, BlockSize, Entry + 3);
    }

    Backlight->VbtMinimum = min(Minimum, GLK_VBT_MINIMUM_LIMIT);
    Backlight->VbtPresent = TRUE;
}

static PVOID
GlkMapMemory(
    _In_ ULONGLONG Address,
    _In_ ULONG Size)
{
    PHYSICAL_ADDRESS Physical;
    MEMORY_CACHING_TYPE CacheType;

    Physical.QuadPart = (LONGLONG)Address;
    if (!NT_SUCCESS(HalGetMemoryCachingRequirements(Physical, Size, &CacheType)))
        CacheType = MmNonCached;
    return MmMapIoSpace(Physical, Size, CacheType);
}

static VOID
GlkBacklightReadVbt(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PGLK_BACKLIGHT Backlight)
{
    const UCHAR *OpRegion;
    PUCHAR Raw = NULL;
    ULONGLONG Rvda;
    ULONG Asls = 0;
    ULONG BytesRead = 0;
    ULONG Mboxes;
    ULONG Rvds;

    Backlight->VbtPresent = FALSE;
    if (!NT_SUCCESS(Device->DxgkInterface.DxgkCbReadDeviceSpace(Device->DxgkInterface.DeviceHandle,
                                                                DXGK_WHICHSPACE_CONFIG,
                                                                &Asls,
                                                                GLK_PCI_ASLS,
                                                                sizeof(Asls),
                                                                &BytesRead)) ||
        BytesRead != sizeof(Asls) ||
        Asls == 0)
    {
        return;
    }

    OpRegion = GlkMapMemory(Asls, GLK_OPREGION_SIZE);
    if (OpRegion == NULL)
        return;

    if (RtlEqualMemory(OpRegion, "IntelGraphicsMem", 16))
    {
        Mboxes = GlkRead32(OpRegion, GLK_OPREGION_SIZE, GLK_OPREGION_MBOXES);
        if (OpRegion[GLK_OPREGION_MAJOR] >= 2 && (Mboxes & GLK_OPREGION_MBOX_ASLE))
        {
            Rvda = GlkRead32(OpRegion, GLK_OPREGION_SIZE, GLK_OPREGION_RVDA) |
                   ((ULONGLONG)GlkRead32(OpRegion, GLK_OPREGION_SIZE, GLK_OPREGION_RVDA + 4) << 32);
            Rvds = GlkRead32(OpRegion, GLK_OPREGION_SIZE, GLK_OPREGION_RVDS);
            if (Rvda != 0 && Rvds != 0)
            {
                if (OpRegion[GLK_OPREGION_MAJOR] > 2 || OpRegion[GLK_OPREGION_MINOR] >= 1)
                    Rvda += Asls;
                Raw = GlkMapMemory(Rvda, Rvds);
                if (Raw != NULL && GlkVbtValid(Raw, Rvds))
                    GlkBacklightParseVbt(Backlight, Raw, Rvds);
                if (Raw != NULL)
                    MmUnmapIoSpace(Raw, Rvds);
            }
        }

        if (!Backlight->VbtPresent &&
            GlkVbtValid(OpRegion + GLK_OPREGION_VBT,
                        ((Mboxes & GLK_OPREGION_MBOX_ASLE_EXT) ? GLK_OPREGION_ASLE_EXT : GLK_OPREGION_SIZE) -
                            GLK_OPREGION_VBT))
        {
            GlkBacklightParseVbt(Backlight,
                                 OpRegion + GLK_OPREGION_VBT,
                                 ((Mboxes & GLK_OPREGION_MBOX_ASLE_EXT) ? GLK_OPREGION_ASLE_EXT : GLK_OPREGION_SIZE) -
                                     GLK_OPREGION_VBT);
        }
    }

    MmUnmapIoSpace((PVOID)OpRegion, GLK_OPREGION_SIZE);
}

static BOOLEAN
GlkBacklightResourceOwned(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ ULONGLONG Address)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_FULL_RESOURCE_DESCRIPTOR Full;
    DXGK_DEVICE_INFO DeviceInfo;
    ULONG FullIndex;
    ULONG Index;

    if (Device->DxgkInterface.DxgkCbGetDeviceInformation == NULL ||
        !NT_SUCCESS(Device->DxgkInterface.DxgkCbGetDeviceInformation(Device->DxgkInterface.DeviceHandle,
                                                                     &DeviceInfo)) ||
        DeviceInfo.TranslatedResourceList == NULL)
    {
        return FALSE;
    }

    Full = DeviceInfo.TranslatedResourceList->List;
    for (FullIndex = 0; FullIndex < DeviceInfo.TranslatedResourceList->Count; FullIndex++)
    {
        for (Index = 0; Index < Full->PartialResourceList.Count; Index++)
        {
            Descriptor = &Full->PartialResourceList.PartialDescriptors[Index];
            if (Descriptor->Type == CmResourceTypeMemory &&
                (ULONGLONG)Descriptor->u.Memory.Start.QuadPart <= Address &&
                Address + PAGE_SIZE <= (ULONGLONG)Descriptor->u.Memory.Start.QuadPart + Descriptor->u.Memory.Length)
            {
                return TRUE;
            }
        }
        Full = (PCM_FULL_RESOURCE_DESCRIPTOR)&Full->PartialResourceList.PartialDescriptors[Index];
    }
    return FALSE;
}

static VOID
GlkBacklightDisableFirmwareControl(
    _In_ PSOFTGPU_DEVICE Device)
{
    ULONG InputStorage[(FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) +
                        ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG)) + sizeof(ULONG) - 1) / sizeof(ULONG)];
    PACPI_EVAL_INPUT_BUFFER_COMPLEX Input = (PACPI_EVAL_INPUT_BUFFER_COMPLEX)InputStorage;

    if (Device->DxgkInterface.DxgkCbEvalAcpiMethod == NULL)
        return;

    RtlZeroMemory(InputStorage, sizeof(InputStorage));
    Input->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    Input->MethodNameAsUlong = ACPI_METHOD_DISPLAY_DOS;
    Input->Size = sizeof(InputStorage) - FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument);
    Input->ArgumentCount = 1;
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Input->Argument,
                                     ACPI_ARG_ENABLE_SWITCH_EVENT | ACPI_ARG_DISABLE_AUTO_LCD_BRIGHTNESS);
    (VOID)Device->DxgkInterface.DxgkCbEvalAcpiMethod(Device->DxgkInterface.DeviceHandle,
                                                     DISPLAY_ADAPTER_HW_ID,
                                                     Input,
                                                     sizeof(InputStorage),
                                                     NULL,
                                                     0);
}

VOID
SoftGpuPlatformAdapterStarted(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PGLK_BACKLIGHT Backlight = Device->PlatformServicesContext;
    PHYSICAL_ADDRESS Address;
    ULONGLONG Bar = 0;
    ULONG BytesRead = 0;
    ULONG Controller;
    ULONG Index;

    PAGED_CODE();

    if (Backlight == NULL)
    {
        Backlight = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Backlight), GLK_BLC_TAG);
        if (Backlight == NULL)
            return;
        ExInitializeRundownProtection(&Backlight->Rundown);
        Device->PlatformServicesContext = Backlight;
    }
    else
    {
        ExReInitializeRundownProtection(&Backlight->Rundown);
    }

    if (Device->DxgkInterface.DxgkCbReadDeviceSpace == NULL ||
        !NT_SUCCESS(Device->DxgkInterface.DxgkCbReadDeviceSpace(Device->DxgkInterface.DeviceHandle,
                                                                DXGK_WHICHSPACE_CONFIG,
                                                                &Bar,
                                                                GLK_PCI_BAR0,
                                                                sizeof(Bar),
                                                                &BytesRead)) ||
        BytesRead != sizeof(Bar))
    {
        return;
    }

    Address.QuadPart = (LONGLONG)((Bar & ~0xFULL) + GLK_BLC_PAGE);
    if (!GlkBacklightResourceOwned(Device, (ULONGLONG)Address.QuadPart))
        return;

    Backlight->Registers = MmMapIoSpace(Address, PAGE_SIZE, MmNonCached);
    if (Backlight->Registers == NULL)
        return;

    GlkBacklightReadVbt(Device, Backlight);

    Controller = GLK_BLC_CONTROLLER_COUNT;
    if (Backlight->VbtPresent &&
        Backlight->VbtController < GLK_BLC_CONTROLLER_COUNT &&
        (READ_REGISTER_ULONG(GlkBacklightRegister(Backlight, Backlight->VbtController, GLK_BLC_PWM_CTL)) &
         GLK_BLC_PWM_ENABLE))
    {
        Controller = Backlight->VbtController;
    }
    for (Index = 0; Controller == GLK_BLC_CONTROLLER_COUNT && Index < GLK_BLC_CONTROLLER_COUNT; Index++)
    {
        if (READ_REGISTER_ULONG(GlkBacklightRegister(Backlight, Index, GLK_BLC_PWM_CTL)) & GLK_BLC_PWM_ENABLE)
            Controller = Index;
    }
    Backlight->Maximum = 0;
    if (Controller < GLK_BLC_CONTROLLER_COUNT)
        Backlight->Maximum = READ_REGISTER_ULONG(GlkBacklightRegister(Backlight, Controller, GLK_BLC_PWM_FREQ));
    if (Controller == GLK_BLC_CONTROLLER_COUNT || Backlight->Maximum == 0)
    {
        DPRINT1("The panel backlight PWM is not enabled\n");
        MmUnmapIoSpace(Backlight->Registers, PAGE_SIZE);
        Backlight->Registers = NULL;
        return;
    }

    Backlight->Controller = Controller;
    Backlight->Minimum = Backlight->VbtPresent ?
        (ULONG)(((ULONGLONG)Backlight->VbtMinimum * Backlight->Maximum + GLK_VBT_MINIMUM_SCALE / 2) /
                GLK_VBT_MINIMUM_SCALE) :
        0;
    DPRINT1("Panel backlight on PWM controller %lu, period %lu, minimum %lu%s\n",
            Controller, Backlight->Maximum, Backlight->Minimum,
            Backlight->VbtPresent ? "" : " (no VBT backlight data)");
    GlkBacklightDisableFirmwareControl(Device);
}

VOID
SoftGpuPlatformAdapterStopping(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PGLK_BACKLIGHT Backlight = Device->PlatformServicesContext;

    PAGED_CODE();

    if (Backlight == NULL)
        return;

    ExWaitForRundownProtectionRelease(&Backlight->Rundown);
    if (Backlight->Registers == NULL)
        return;

    MmUnmapIoSpace(Backlight->Registers, PAGE_SIZE);
    Backlight->Registers = NULL;
}

VOID
SoftGpuPlatformAdapterRemoved(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    PAGED_CODE();

    if (Device->PlatformServicesContext == NULL)
        return;

    ExFreePoolWithTag(Device->PlatformServicesContext, GLK_BLC_TAG);
    Device->PlatformServicesContext = NULL;
}

static PGLK_BACKLIGHT
GlkBacklightAcquire(
    _In_ PVOID Context)
{
    PGLK_BACKLIGHT Backlight = ((PSOFTGPU_DEVICE)Context)->PlatformServicesContext;

    if (Backlight == NULL || !ExAcquireRundownProtection(&Backlight->Rundown))
        return NULL;
    if (Backlight->Registers == NULL)
    {
        ExReleaseRundownProtection(&Backlight->Rundown);
        return NULL;
    }
    return Backlight;
}

static VOID
NTAPI
GlkBacklightReference(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
}

static NTSTATUS
GlkBacklightGetPossible(
    _In_ PVOID Context,
    _In_ ULONG BufferSize,
    _Out_ PUCHAR LevelCount,
    _Out_writes_bytes_to_(BufferSize, *LevelCount) PUCHAR BrightnessLevels)
{
    ULONG Index;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(Context);

    *LevelCount = 0;
    if (BufferSize < GLK_BLC_LEVEL_COUNT)
        return STATUS_BUFFER_TOO_SMALL;

    for (Index = 0; Index < GLK_BLC_LEVEL_COUNT; Index++)
        BrightnessLevels[Index] = (UCHAR)(Index + 1);
    *LevelCount = (UCHAR)GLK_BLC_LEVEL_COUNT;
    return STATUS_SUCCESS;
}

static NTSTATUS
GlkBacklightSet(
    _In_ PVOID Context,
    _In_ UCHAR Brightness)
{
    PGLK_BACKLIGHT Backlight;

    PAGED_CODE();

    if (Brightness == 0 || Brightness > GLK_BLC_LEVEL_COUNT)
        return STATUS_INVALID_PARAMETER;

    Backlight = GlkBacklightAcquire(Context);
    if (Backlight == NULL)
        return STATUS_DEVICE_NOT_READY;

    WRITE_REGISTER_ULONG(GlkBacklightRegister(Backlight, Backlight->Controller, GLK_BLC_PWM_DUTY),
                         Backlight->Minimum +
                             (ULONG)(((ULONGLONG)(Backlight->Maximum - Backlight->Minimum) * Brightness +
                                      GLK_BLC_LEVEL_COUNT / 2) / GLK_BLC_LEVEL_COUNT));
    ExReleaseRundownProtection(&Backlight->Rundown);
    return STATUS_SUCCESS;
}

static NTSTATUS
GlkBacklightGet(
    _In_ PVOID Context,
    _Out_ PUCHAR Brightness)
{
    PGLK_BACKLIGHT Backlight;
    ULONGLONG Level;

    PAGED_CODE();

    *Brightness = 0;
    Backlight = GlkBacklightAcquire(Context);
    if (Backlight == NULL)
        return STATUS_DEVICE_NOT_READY;

    Level = READ_REGISTER_ULONG(GlkBacklightRegister(Backlight, Backlight->Controller, GLK_BLC_PWM_DUTY));
    Level = Level > Backlight->Minimum ? Level - Backlight->Minimum : 0;
    Level = (Level * GLK_BLC_LEVEL_COUNT + (Backlight->Maximum - Backlight->Minimum) / 2) /
            (Backlight->Maximum - Backlight->Minimum);
    *Brightness = (UCHAR)min(max(Level, 1), GLK_BLC_LEVEL_COUNT);
    ExReleaseRundownProtection(&Backlight->Rundown);
    return STATUS_SUCCESS;
}

NTSTATUS
SoftGpuPlatformQueryInterface(
    _Inout_ PSOFTGPU_DEVICE Device,
    _Inout_ PQUERY_INTERFACE QueryInterface)
{
    PDXGK_BRIGHTNESS_INTERFACE Interface;
    PGLK_BACKLIGHT Backlight = Device->PlatformServicesContext;

    PAGED_CODE();

    if (!IsEqualGUID(QueryInterface->InterfaceType, &GlkGuidBrightness) ||
        QueryInterface->Version != DXGK_BRIGHTNESS_INTERFACE_VERSION_1 ||
        Backlight == NULL ||
        Backlight->Registers == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }
    if (QueryInterface->Size < sizeof(*Interface))
        return STATUS_BUFFER_TOO_SMALL;

    Interface = (PDXGK_BRIGHTNESS_INTERFACE)QueryInterface->Interface;
    Interface->Size = sizeof(*Interface);
    Interface->Version = DXGK_BRIGHTNESS_INTERFACE_VERSION_1;
    Interface->Context = Device;
    Interface->InterfaceReference = GlkBacklightReference;
    Interface->InterfaceDereference = GlkBacklightReference;
    Interface->GetPossibleBrightness = GlkBacklightGetPossible;
    Interface->SetBrightness = GlkBacklightSet;
    Interface->GetBrightness = GlkBacklightGet;
    return STATUS_SUCCESS;
}
