/*
 * PROJECT:     LiberNT WDDM software GPU miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Generic root-enumerated firmware framebuffer provider
 */

#include "softgpu.h"
#include "softgpu_2d_core.h"

#define SOFTGPU_ROOT_WORKING_SURFACE_COUNT 12UL

#define SOFTGPU_VBE_INFO_SIZE           512UL
#define SOFTGPU_VBE_MODE_INFO_SIZE      256UL
#define SOFTGPU_VBE_MAX_LISTED_MODES    256UL
#define SOFTGPU_VBE_SUCCESS             0x004F
#define SOFTGPU_VBE_MODE_REQUIRED       0x0099
#define SOFTGPU_VBE_DIRECT_COLOR        6
#define SOFTGPU_VBE_LINEAR_FRAMEBUFFER  0x4000
#define SOFTGPU_VBE_PRESERVE_MEMORY     0x8000
#define SOFTGPU_VBE_MIN_WIDTH           640UL
#define SOFTGPU_VBE_MIN_HEIGHT          480UL

typedef struct _SOFTGPU_X86_BIOS_REGISTERS
{
    ULONG Eax;
    ULONG Ecx;
    ULONG Edx;
    ULONG Ebx;
    ULONG Ebp;
    ULONG Esi;
    ULONG Edi;
    USHORT SegDs;
    USHORT SegEs;
} SOFTGPU_X86_BIOS_REGISTERS, *PSOFTGPU_X86_BIOS_REGISTERS;

typedef NTSTATUS (NTAPI *PSOFTGPU_X86_BIOS_ALLOCATE_BUFFER)(ULONG *, USHORT *, USHORT *);
typedef NTSTATUS (NTAPI *PSOFTGPU_X86_BIOS_FREE_BUFFER)(USHORT, USHORT);
typedef NTSTATUS (NTAPI *PSOFTGPU_X86_BIOS_READ_MEMORY)(USHORT, USHORT, PVOID, ULONG);
typedef NTSTATUS (NTAPI *PSOFTGPU_X86_BIOS_WRITE_MEMORY)(USHORT, USHORT, PVOID, ULONG);
typedef BOOLEAN (NTAPI *PSOFTGPU_X86_BIOS_CALL)(ULONG, PSOFTGPU_X86_BIOS_REGISTERS);
typedef FIRMWARE_TYPE (NTAPI *PSOFTGPU_GET_FIRMWARE_TYPE)(VOID);
typedef NTSTATUS (NTAPI *PSOFTGPU_SET_VIRTUAL_FRAMEBUFFER)(PVOID);

static PSOFTGPU_X86_BIOS_CALL SoftGpuX86BiosCall;
static ULONGLONG SoftGpuBootScreenSize;

#if defined(_M_IX86) || defined(_M_AMD64)
#define SOFTGPU_VGA_INPUT_STATUS_1       ((PUCHAR)(ULONG_PTR)0x3da)
#define SOFTGPU_VGA_VERTICAL_RETRACE     0x08
#define SOFTGPU_VBLANK_POLL_US           20
#define SOFTGPU_VBLANK_TIMEOUT_US        20000

static BOOLEAN
SoftGpuPlatformWaitForRetraceState(
    _In_ BOOLEAN InRetrace)
{
    ULONG Elapsed;

    for (Elapsed = 0;
         Elapsed < SOFTGPU_VBLANK_TIMEOUT_US;
         Elapsed += SOFTGPU_VBLANK_POLL_US)
    {
        BOOLEAN Current =
            (READ_PORT_UCHAR(SOFTGPU_VGA_INPUT_STATUS_1) &
             SOFTGPU_VGA_VERTICAL_RETRACE) != 0;

        if (Current == InRetrace)
            return TRUE;

        KeStallExecutionProcessor(SOFTGPU_VBLANK_POLL_US);
    }

    return FALSE;
}
#endif

static PVOID
SoftGpuGetHalRoutine(
    _In_ PCWSTR Name)
{
    UNICODE_STRING RoutineName;

    RtlInitUnicodeString(&RoutineName, Name);
    return MmGetSystemRoutineAddress(&RoutineName);
}

static PVOID
SoftGpuDetachBootScreen(VOID)
{
    PSOFTGPU_SET_VIRTUAL_FRAMEBUFFER SetVirtualFrameBuffer;
    PVOID Shadow;

    if (SoftGpuBootScreenSize == 0 || SoftGpuBootScreenSize > MAXULONG_PTR)
        return NULL;

    SetVirtualFrameBuffer = (PSOFTGPU_SET_VIRTUAL_FRAMEBUFFER)SoftGpuGetHalRoutine(L"InbvSetVirtualFrameBuffer");
    if (SetVirtualFrameBuffer == NULL)
        return NULL;

    Shadow = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)SoftGpuBootScreenSize, SOFTGPU_POOL_TAG);
    if (Shadow == NULL)
        return NULL;

    if (!NT_SUCCESS(SetVirtualFrameBuffer(Shadow)))
    {
        ExFreePoolWithTag(Shadow, SOFTGPU_POOL_TAG);
        return NULL;
    }

    return Shadow;
}

static VOID
SoftGpuAttachBootScreen(
    _In_opt_ PVOID Shadow)
{
    PSOFTGPU_SET_VIRTUAL_FRAMEBUFFER SetVirtualFrameBuffer;

    if (Shadow == NULL)
        return;

    SetVirtualFrameBuffer = (PSOFTGPU_SET_VIRTUAL_FRAMEBUFFER)SoftGpuGetHalRoutine(L"InbvSetVirtualFrameBuffer");
    SetVirtualFrameBuffer(NULL);
    ExFreePoolWithTag(Shadow, SOFTGPU_POOL_TAG);
}

static USHORT
SoftGpuReadUshort(
    _In_reads_bytes_(Offset + sizeof(USHORT)) const UCHAR *Buffer,
    _In_ ULONG Offset)
{
    return (USHORT)(Buffer[Offset] | (Buffer[Offset + 1] << 8));
}

static NTSTATUS
SoftGpuVbeSetMode(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const SOFTGPU_DISPLAY_MODE *Mode)
{
    SOFTGPU_X86_BIOS_REGISTERS Registers;
    PVOID Shadow;
    BOOLEAN Success;

    UNREFERENCED_PARAMETER(Device);

    if (SoftGpuX86BiosCall == NULL)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(&Registers, sizeof(Registers));
    Registers.Eax = 0x4F02;
    Registers.Ebx = Mode->PlatformMode | SOFTGPU_VBE_LINEAR_FRAMEBUFFER | SOFTGPU_VBE_PRESERVE_MEMORY;
    Shadow = SoftGpuDetachBootScreen();
    Success = SoftGpuX86BiosCall(0x10, &Registers) && (Registers.Eax & 0xFFFF) == SOFTGPU_VBE_SUCCESS;
    SoftGpuAttachBootScreen(Shadow);
    if (!Success)
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;

    return STATUS_SUCCESS;
}

static BOOLEAN
SoftGpuVbeAddMode(
    _Inout_ PSOFTGPU_PLATFORM_CONFIG Config,
    _In_ USHORT ModeNumber,
    _In_ const UCHAR *ModeInfo,
    _In_ USHORT VbeVersion,
    _In_ ULONGLONG VideoMemorySize)
{
    SOFTGPU_DISPLAY_MODE *Mode;
    ULONG Width = SoftGpuReadUshort(ModeInfo, 0x12);
    ULONG Height = SoftGpuReadUshort(ModeInfo, 0x14);
    ULONG Pitch = SoftGpuReadUshort(ModeInfo, VbeVersion >= 0x0300 ? 0x32 : 0x10);
    ULONG Base = ModeInfo[0x28] | (ModeInfo[0x29] << 8) | (ModeInfo[0x2A] << 16) | ((ULONG)ModeInfo[0x2B] << 24);
    ULONG Index;

    if ((SoftGpuReadUshort(ModeInfo, 0) & SOFTGPU_VBE_MODE_REQUIRED) != SOFTGPU_VBE_MODE_REQUIRED ||
        ModeInfo[0x19] != 32 ||
        ModeInfo[0x1B] != SOFTGPU_VBE_DIRECT_COLOR ||
        Base != Config->ScanoutPhysicalAddress.QuadPart ||
        Width < SOFTGPU_VBE_MIN_WIDTH ||
        Height < SOFTGPU_VBE_MIN_HEIGHT ||
        Width > SOFTGPU_MAX_DISPLAY_WIDTH ||
        Height > SOFTGPU_MAX_DISPLAY_HEIGHT ||
        Pitch < Width * SOFTGPU_DISPLAY_BYTES_PER_PIXEL ||
        Pitch > SOFTGPU_MAX_DISPLAY_PITCH ||
        (ULONGLONG)Pitch * Height > VideoMemorySize ||
        Config->ModeCount >= SOFTGPU_MAX_DISPLAY_MODES)
    {
        return FALSE;
    }

    for (Index = 0; Index < Config->ModeCount; Index++)
    {
        if (Config->Modes[Index].Width == Width && Config->Modes[Index].Height == Height)
            return FALSE;
    }

    Mode = &Config->Modes[Config->ModeCount++];
    Mode->Width = Width;
    Mode->Height = Height;
    Mode->Pitch = Pitch;
    Mode->PlatformMode = ModeNumber;
    return TRUE;
}

static VOID
SoftGpuVbeReadEdid(
    _Inout_ PSOFTGPU_PLATFORM_CONFIG Config,
    _In_ PSOFTGPU_X86_BIOS_CALL Call,
    _In_ PSOFTGPU_X86_BIOS_READ_MEMORY ReadMemory,
    _In_ USHORT Segment,
    _In_ USHORT Offset)
{
    static const UCHAR Header[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    SOFTGPU_X86_BIOS_REGISTERS Registers;
    UCHAR Edid[SOFTGPU_EDID_SIZE];
    UCHAR Sum = 0;
    ULONG Index;

    RtlZeroMemory(&Registers, sizeof(Registers));
    Registers.Eax = 0x4F15;
    Registers.Ebx = 0x01;
    Registers.SegEs = Segment;
    Registers.Edi = Offset;
    if (!Call(0x10, &Registers) ||
        (Registers.Eax & 0xFFFF) != SOFTGPU_VBE_SUCCESS ||
        !NT_SUCCESS(ReadMemory(Segment, Offset, Edid, sizeof(Edid))) ||
        !RtlEqualMemory(Edid, Header, sizeof(Header)))
    {
        return;
    }

    for (Index = 0; Index < sizeof(Edid); Index++)
        Sum += Edid[Index];
    if (Sum != 0)
        return;

    RtlCopyMemory(Config->Edid, Edid, sizeof(Edid));
    Config->EdidLength = sizeof(Edid);
}

static BOOLEAN
SoftGpuEdidListsMode(
    _In_reads_bytes_(SOFTGPU_EDID_SIZE) const UCHAR *Edid,
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    static const struct
    {
        UCHAR Byte;
        UCHAR Bit;
        USHORT Width;
        USHORT Height;
    } Established[] =
    {
        { 35, 7, 720, 400 }, { 35, 6, 720, 400 }, { 35, 5, 640, 480 }, { 35, 4, 640, 480 },
        { 35, 3, 640, 480 }, { 35, 2, 640, 480 }, { 35, 1, 800, 600 }, { 35, 0, 800, 600 },
        { 36, 7, 800, 600 }, { 36, 6, 800, 600 }, { 36, 5, 832, 624 }, { 36, 3, 1024, 768 },
        { 36, 2, 1024, 768 }, { 36, 1, 1024, 768 }, { 36, 0, 1280, 1024 }, { 37, 7, 1152, 870 },
    };
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(Established); Index++)
    {
        if ((Edid[Established[Index].Byte] & (1 << Established[Index].Bit)) &&
            Established[Index].Width == Width && Established[Index].Height == Height)
        {
            return TRUE;
        }
    }

    for (Index = 38; Index < 54; Index += 2)
    {
        ULONG StandardWidth;
        ULONG StandardHeight;

        if (Edid[Index] <= 0x01)
            continue;

        StandardWidth = (Edid[Index] + 31) * 8;
        switch (Edid[Index + 1] >> 6)
        {
            case 0:
                StandardHeight = (Edid[0x12] == 1 && Edid[0x13] < 3) ? StandardWidth : StandardWidth * 10 / 16;
                break;
            case 1:
                StandardHeight = StandardWidth * 3 / 4;
                break;
            case 2:
                StandardHeight = StandardWidth * 4 / 5;
                break;
            default:
                StandardHeight = StandardWidth * 9 / 16;
                break;
        }

        if (StandardWidth == Width && StandardHeight == Height)
            return TRUE;
    }

    for (Index = 54; Index < 126; Index += 18)
    {
        if (Edid[Index] == 0 && Edid[Index + 1] == 0)
            continue;

        if ((Edid[Index + 2] | ((Edid[Index + 4] & 0xF0) << 4)) == Width &&
            (Edid[Index + 5] | ((Edid[Index + 7] & 0xF0) << 4)) == Height)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static VOID
SoftGpuVbePruneModes(
    _Inout_ PSOFTGPU_PLATFORM_CONFIG Config)
{
    ULONG Index;
    ULONG Kept = 0;

    if (Config->EdidLength == 0)
        return;

    for (Index = 0; Index < Config->ModeCount; Index++)
    {
        const SOFTGPU_DISPLAY_MODE *Mode = &Config->Modes[Index];

        if ((Mode->Width == Config->Width && Mode->Height == Config->Height) ||
            SoftGpuEdidListsMode(Config->Edid, Mode->Width, Mode->Height))
        {
            Config->Modes[Kept++] = *Mode;
        }
    }

    Config->ModeCount = Kept;
}

static VOID
SoftGpuVbeQueryModes(
    _Inout_ PSOFTGPU_PLATFORM_CONFIG Config)
{
    PSOFTGPU_X86_BIOS_ALLOCATE_BUFFER AllocateBuffer;
    PSOFTGPU_X86_BIOS_FREE_BUFFER FreeBuffer;
    PSOFTGPU_X86_BIOS_READ_MEMORY ReadMemory;
    PSOFTGPU_X86_BIOS_WRITE_MEMORY WriteMemory;
    PSOFTGPU_X86_BIOS_CALL Call;
    PSOFTGPU_GET_FIRMWARE_TYPE GetFirmwareType;
    SOFTGPU_X86_BIOS_REGISTERS Registers;
    UCHAR Info[SOFTGPU_VBE_INFO_SIZE];
    UCHAR ModeInfo[SOFTGPU_VBE_MODE_INFO_SIZE];
    USHORT ModeList[SOFTGPU_VBE_MAX_LISTED_MODES];
    ULONG ModeListCount = 0;
    ULONGLONG VideoMemorySize;
    ULONG Size = SOFTGPU_VBE_INFO_SIZE + SOFTGPU_VBE_MODE_INFO_SIZE;
    USHORT Segment;
    USHORT Offset;
    USHORT VbeVersion;
    USHORT ListSegment;
    USHORT ListOffset;
    ULONG Index;
    PVOID Shadow;

    AllocateBuffer = (PSOFTGPU_X86_BIOS_ALLOCATE_BUFFER)SoftGpuGetHalRoutine(L"x86BiosAllocateBuffer");
    FreeBuffer = (PSOFTGPU_X86_BIOS_FREE_BUFFER)SoftGpuGetHalRoutine(L"x86BiosFreeBuffer");
    ReadMemory = (PSOFTGPU_X86_BIOS_READ_MEMORY)SoftGpuGetHalRoutine(L"x86BiosReadMemory");
    WriteMemory = (PSOFTGPU_X86_BIOS_WRITE_MEMORY)SoftGpuGetHalRoutine(L"x86BiosWriteMemory");
    Call = (PSOFTGPU_X86_BIOS_CALL)SoftGpuGetHalRoutine(L"x86BiosCall");
    GetFirmwareType = (PSOFTGPU_GET_FIRMWARE_TYPE)SoftGpuGetHalRoutine(L"ExGetFirmwareType");
    if (AllocateBuffer == NULL || FreeBuffer == NULL || ReadMemory == NULL || WriteMemory == NULL || Call == NULL ||
        GetFirmwareType == NULL || GetFirmwareType() != FirmwareTypeBios)
    {
        return;
    }

    if (!NT_SUCCESS(AllocateBuffer(&Size, &Segment, &Offset)))
        return;

    Shadow = SoftGpuDetachBootScreen();

    RtlZeroMemory(Info, sizeof(Info));
    RtlCopyMemory(Info, "VBE2", 4);
    RtlZeroMemory(&Registers, sizeof(Registers));
    Registers.Eax = 0x4F00;
    Registers.SegEs = Segment;
    Registers.Edi = Offset;
    if (NT_SUCCESS(WriteMemory(Segment, Offset, Info, sizeof(Info))) &&
        Call(0x10, &Registers) &&
        (Registers.Eax & 0xFFFF) == SOFTGPU_VBE_SUCCESS &&
        NT_SUCCESS(ReadMemory(Segment, Offset, Info, sizeof(Info))) &&
        RtlEqualMemory(Info, "VESA", 4))
    {
        VbeVersion = SoftGpuReadUshort(Info, 0x04);
        ListOffset = SoftGpuReadUshort(Info, 0x0E);
        ListSegment = SoftGpuReadUshort(Info, 0x10);
        VideoMemorySize = (ULONGLONG)SoftGpuReadUshort(Info, 0x12) * 0x10000;
        while (ModeListCount < SOFTGPU_VBE_MAX_LISTED_MODES &&
               NT_SUCCESS(ReadMemory(ListSegment,
                                     (USHORT)(ListOffset + ModeListCount * sizeof(USHORT)),
                                     &ModeList[ModeListCount],
                                     sizeof(USHORT))) &&
               ModeList[ModeListCount] != 0xFFFF)
        {
            ModeListCount++;
        }

        for (Index = 0; Index < ModeListCount; Index++)
        {
            RtlZeroMemory(&Registers, sizeof(Registers));
            Registers.Eax = 0x4F01;
            Registers.Ecx = ModeList[Index];
            Registers.SegEs = Segment;
            Registers.Edi = Offset + SOFTGPU_VBE_INFO_SIZE;
            if (Call(0x10, &Registers) &&
                (Registers.Eax & 0xFFFF) == SOFTGPU_VBE_SUCCESS &&
                NT_SUCCESS(ReadMemory(Segment, (USHORT)(Offset + SOFTGPU_VBE_INFO_SIZE), ModeInfo, sizeof(ModeInfo))))
            {
                SoftGpuVbeAddMode(Config, ModeList[Index], ModeInfo, VbeVersion, VideoMemorySize);
            }
        }

        SoftGpuVbeReadEdid(Config, Call, ReadMemory, Segment, Offset);
        SoftGpuVbePruneModes(Config);

        for (Index = 0; Index < Config->ModeCount; Index++)
        {
            if (Config->Modes[Index].Width == Config->Width &&
                Config->Modes[Index].Height == Config->Height &&
                Config->Modes[Index].Pitch == Config->ScanoutPitch)
            {
                break;
            }
        }

        if (Config->ModeCount > 1 &&
            Index < Config->ModeCount &&
            VideoMemorySize <= SOFTGPU_MAX_SURFACE_SIZE)
        {
            Config->CurrentMode = Index;
            Config->ScanoutSize = max(Config->ScanoutSize, VideoMemorySize);
            Config->SetMode = SoftGpuVbeSetMode;
            SoftGpuX86BiosCall = Call;
            DPRINT("SOFTGPU: VBE %x.%x offers %lu display modes\n",
                    VbeVersion >> 8, VbeVersion & 0xFF, Config->ModeCount);
        }
        else
        {
            Config->ModeCount = 0;
        }

    }

    SoftGpuAttachBootScreen(Shadow);
    FreeBuffer(Segment, Offset);
}

NTSTATUS
SoftGpuPlatformValidatePdo(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    return PhysicalDeviceObject != NULL ?
               STATUS_SUCCESS :
               STATUS_INVALID_PARAMETER;
}

NTSTATUS
SoftGpuPlatformQueryStart(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ PDXGK_INTERFACE DxgkInterface,
    _Out_ PSOFTGPU_PLATFORM_CONFIG Config)
{
    SIZE_T WorkingSetSize;
    ULONG Index;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Device);

    if (DxgkInterface == NULL || Config == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Config, sizeof(*Config));
    Config->Width = SOFTGPU_DEFAULT_WIDTH;
    Config->Height = SOFTGPU_DEFAULT_HEIGHT;
    Config->Format = SOFTGPU_DEFAULT_FORMAT;

#if (REACTOS_WDDM_TARGET_LEVEL < 1200)
    {
        SOFTGPU_LOADER_FRAMEBUFFER LoaderFrameBuffer;
        ULONG LoaderPitch;
        ULONGLONG LoaderVisibleLength;

        /*
         * WDDM 1.0/1.1 has no POST-ownership callback. Use the loader handoff
         * when present; a genuinely headless boot retains the explicit
         * 1024x768 software-only fallback.
         */
        RtlZeroMemory(&LoaderFrameBuffer, sizeof(LoaderFrameBuffer));
        if (InbvGetGopFrameBufferInfo(&LoaderFrameBuffer))
        {
            if (!SoftGpuDecodeLoaderGop(&LoaderFrameBuffer,
                                        &LoaderPitch,
                                        &LoaderVisibleLength))
            {
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }

            Config->Width = LoaderFrameBuffer.HorizontalResolution;
            Config->Height = LoaderFrameBuffer.VerticalResolution;
            Config->Format = SOFTGPU_DEFAULT_FORMAT;
            Config->ScanoutPhysicalAddress =
                LoaderFrameBuffer.FrameBufferBase;
            Config->ScanoutPitch = LoaderPitch;
            Config->ScanoutSize = LoaderVisibleLength;
            SoftGpuBootScreenSize = LoaderVisibleLength;
        }
    }
#else
    {
        DXGK_DISPLAY_INFORMATION PostDisplayInfo;
        ULONGLONG PostVisibleLength;
        INTERFACE_TYPE BusType;
        ULONG ResultLength;
        /*
         * This root adapter is a Basic Display fallback. Use the firmware
         * framebuffer when dxgkrnl transfers one. A successful zero-width
         * descriptor means no POST display information is available, so keep
         * the default software-only output and start in headless mode.
         */
        Status = SoftGpuAcquirePostDisplay(DxgkInterface,
                                           &PostDisplayInfo,
                                           &PostVisibleLength);
        if (!NT_SUCCESS(Status))
            return Status;

        if (PostDisplayInfo.Width != 0)
        {
            Config->Width = PostDisplayInfo.Width;
            Config->Height = PostDisplayInfo.Height;
            Config->Format = PostDisplayInfo.ColorFormat;
            Config->ScanoutPhysicalAddress =
                PostDisplayInfo.PhysicAddress;
            Config->ScanoutPitch = PostDisplayInfo.Pitch;
            Config->ScanoutSize = PostVisibleLength;
            SoftGpuBootScreenSize = PostVisibleLength;
        }
        else if (Device != NULL &&
                 Device->PhysicalDeviceObject != NULL &&
                 NT_SUCCESS(IoGetDeviceProperty(Device->PhysicalDeviceObject,
                                                DevicePropertyLegacyBusType,
                                                sizeof(BusType),
                                                &BusType,
                                                &ResultLength)) &&
                 BusType == PCIBus)
        {
            DPRINT1("SOFTGPU: PCI display device has no POST framebuffer; "
                    "not starting\n");
            return STATUS_UNSUCCESSFUL;
        }
        else
        {
            DPRINT1("SOFTGPU: no POST framebuffer; starting headless "
                    "%lux%lu software output\n",
                    Config->Width,
                    Config->Height);
        }
    }
#endif

    if (Config->ScanoutPhysicalAddress.QuadPart != 0)
        SoftGpuVbeQueryModes(Config);

    for (Index = 0; Index < Config->ModeCount; Index++)
    {
        Status = SoftGpu2dComputeAllocationSlabSize(
                     Config->Modes[Index].Width,
                     Config->Modes[Index].Height,
                     SOFTGPU_ROOT_WORKING_SURFACE_COUNT,
                     MAXULONG_PTR,
                     &WorkingSetSize);
        if (!NT_SUCCESS(Status))
            return Status;
        Config->Modes[Index].AllocationSlabSize =
            min(WorkingSetSize, SOFTGPU_MAX_ALLOCATION_SLAB_SIZE);
    }

    /*
     * The common four-surface minimum covers scanout work, while the root
     * adapter also keeps compositor buffers and redirected windows resident.
     * Scale that desktop working set with the current mode.  The global cap
     * remains authoritative for large modes and avoids an excessive
     * contiguous allocation on either 32-bit or 64-bit systems.
     */
    Status = SoftGpu2dComputeAllocationSlabSize(
                 Config->Width,
                 Config->Height,
                 SOFTGPU_ROOT_WORKING_SURFACE_COUNT,
                 MAXULONG_PTR,
                 &WorkingSetSize);
    if (!NT_SUCCESS(Status))
        return Status;
    Config->MinimumAllocationSlabSize =
        min(WorkingSetSize, SOFTGPU_MAX_ALLOCATION_SLAB_SIZE);

    return STATUS_SUCCESS;
}

ULONG
SoftGpuPlatformDmaBufferPrivateDataSize(VOID)
{
    return 0;
}

ULONG
SoftGpuPlatformDmaBufferSegmentSet(VOID)
{
    return 0;
}

NTSTATUS
SoftGpuPlatformStartScanout(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    UNREFERENCED_PARAMETER(Device);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
SoftGpuPlatformStopScanout(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    UNREFERENCED_PARAMETER(Device);
    return STATUS_SUCCESS;
}

NTSTATUS
SoftGpuPlatformSetPrimary(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ PHYSICAL_ADDRESS PrimaryAddress,
    _In_ ULONG Pitch,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ BOOLEAN Visible)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(PrimaryAddress);
    UNREFERENCED_PARAMETER(Pitch);
    UNREFERENCED_PARAMETER(Width);
    UNREFERENCED_PARAMETER(Height);
    UNREFERENCED_PARAMETER(Visible);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
SoftGpuPlatformPresentDisplayOnly(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const DXGKARG_PRESENT_DISPLAYONLY *PresentDisplayOnly)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(PresentDisplayOnly);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
SoftGpuPlatformUpdatePointer(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    UNREFERENCED_PARAMETER(Device);
    return STATUS_NOT_SUPPORTED;
}

VOID
SoftGpuPlatformScanoutWritten(
    _Inout_ PSOFTGPU_DEVICE Device,
    _In_ const RECT *Rect)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Rect);
}

VOID
SoftGpuPlatformFillNodeMetadata(
    _In_ ULONG NodeOrdinal,
    _Out_ DXGKARG_GETNODEMETADATA *GetNodeMetadata)
{
    static const WCHAR FriendlyName[] = L"3D";

    UNREFERENCED_PARAMETER(NodeOrdinal);
    GetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_3D;
    RtlCopyMemory(GetNodeMetadata->FriendlyName, FriendlyName, sizeof(FriendlyName));
}

VOID
SoftGpuPlatformInitializeTiming(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    if (Device == NULL)
        return;

    InterlockedExchange(&Device->ScanoutVBlankAvailable, 0);
#if defined(_M_IX86) || defined(_M_AMD64)
    /* A changing VGA status bit is a capability probe, not an assumption. */
    if (SoftGpuPlatformWaitForRetraceState(FALSE) &&
        SoftGpuPlatformWaitForRetraceState(TRUE))
    {
        InterlockedExchange(&Device->ScanoutVBlankAvailable, 1);
    }
#endif
}

BOOLEAN
SoftGpuPlatformWaitForVerticalBlank(
    _Inout_ PSOFTGPU_DEVICE Device)
{
    if (Device == NULL ||
        InterlockedCompareExchange(
            &Device->ScanoutVBlankAvailable, 0, 0) == 0)
    {
        return FALSE;
    }

#if defined(_M_IX86) || defined(_M_AMD64)
    /* Start at the leading edge, leaving a full scan period for the copy. */
    if (SoftGpuPlatformWaitForRetraceState(FALSE) &&
        SoftGpuPlatformWaitForRetraceState(TRUE))
    {
        return TRUE;
    }
#endif

    /* Do not impose a timeout on every later present if timing disappears. */
    InterlockedExchange(&Device->ScanoutVBlankAvailable, 0);
    return FALSE;
}

NTSTATUS
SoftGpuPlatformQueryScanLine(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PDXGKARG_GETSCANLINE GetScanLine)
{
    if (Device == NULL || GetScanLine == NULL ||
        GetScanLine->VidPnTargetId != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

#if defined(_M_IX86) || defined(_M_AMD64)
    if (InterlockedCompareExchange(
            &Device->ScanoutVBlankAvailable, 0, 0) != 0)
    {
        GetScanLine->ScanLine = 0;
        GetScanLine->InVerticalBlank =
            (READ_PORT_UCHAR(SOFTGPU_VGA_INPUT_STATUS_1) &
             SOFTGPU_VGA_VERTICAL_RETRACE) != 0;
        return STATUS_SUCCESS;
    }
#endif

    return STATUS_NOT_SUPPORTED;
}

MEMORY_CACHING_TYPE
SoftGpuPlatformSegmentCacheType(VOID)
{
    return MmCached;
}

NTSTATUS
SoftGpuPlatformQueryDescriptor(
    _In_ PSOFTGPU_DEVICE Device,
    _Inout_ PDXGK_DEVICE_DESCRIPTOR DeviceDescriptor)
{
    ULONG Length;

    if (Device->EdidLength == 0)
        return STATUS_MONITOR_NO_DESCRIPTOR;
    if (DeviceDescriptor->DescriptorOffset >= Device->EdidLength)
        return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    if (DeviceDescriptor->DescriptorBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

    Length = min(DeviceDescriptor->DescriptorLength, Device->EdidLength - DeviceDescriptor->DescriptorOffset);
    RtlCopyMemory(DeviceDescriptor->DescriptorBuffer, Device->Edid + DeviceDescriptor->DescriptorOffset, Length);
    DeviceDescriptor->DescriptorLength = Length;
    return STATUS_SUCCESS;
}
