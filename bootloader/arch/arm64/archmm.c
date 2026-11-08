/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 loader address space limits
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>
#include <arm64.h>
#include <reactos/arm64/early_uart.h>
#include "../../winload/winload.h"

BOOLEAN Arm64IsKernelVaMapped(ULONGLONG Va);

#include <reactos/arm64/earlyfont.h>

#define OSL_FB_CELL_HEIGHT 14

extern VOID (*Arm64TrapScreenRoutine)(PCSTR Text);
extern UINT8 arm64_exception_vectors[];
extern ULONG_PTR VramAddress;

static volatile ULONG *OslFbBase;
static ULONG OslFbWidth;
static ULONG OslFbHeight;
static ULONG OslFbStride;
static ULONG OslFbScale;
static ULONG OslFbColumns;
static ULONG OslFbRows;
static ULONG OslFbRow;
static ULONG OslFbColumn;
static ULONG OslFbColor;
static BOOLEAN OslFbRowCleared;

static
BOOLEAN
OslFbTranslates(
    _In_ ULONG_PTR Address)
{
    ULONG64 Result;

    if (((ARM64_READ_SYSREG(CURRENTEL) >> 2) & 3) == 2)
        ARM64_AT(S1E2W, Address);
    else
        ARM64_AT(S1E1W, Address);
    ARM64_ISB();
    Result = ARM64_READ_SYSREG(PAR_EL1);
    return (Result & 1) == 0;
}

static
VOID
OslFbFill(
    _In_ volatile ULONG *Pixels,
    _In_ ULONG Count,
    _In_ ULONG Color)
{
    ULONG_PTR Line;
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
        Pixels[Index] = Color;
    for (Line = (ULONG_PTR)Pixels & ~63ULL; Line < (ULONG_PTR)(Pixels + Count); Line += 64)
        ARM64_DC(CIVAC, Line);
}

static
VOID
OslFbPuts(
    _In_ PCSTR Text)
{
    ULONG Y, X, SubY, SubX;

    if (!OslFbBase)
        return;

    if (!OslFbTranslates((ULONG_PTR)OslFbBase) ||
        !OslFbTranslates((ULONG_PTR)(OslFbBase + (SIZE_T)(OslFbHeight - 1) * OslFbStride)))
    {
        volatile ULONG *Alias = (volatile ULONG *)(ARM64_PHYS_MAP_BASE | (ULONG_PTR)OslFbBase);

        if (!OslFbTranslates((ULONG_PTR)Alias) ||
            !OslFbTranslates((ULONG_PTR)(Alias + (SIZE_T)(OslFbHeight - 1) * OslFbStride)))
        {
            return;
        }
        OslFbBase = Alias;
    }

    for (; *Text; Text++)
    {
        if (*Text == '\n' || OslFbColumn >= OslFbColumns)
        {
            OslFbColumn = 0;
            OslFbRow = (OslFbRow + 1) % OslFbRows;
            OslFbRowCleared = FALSE;
            if (*Text == '\n')
                continue;
        }

        if (!OslFbRowCleared)
        {
            for (Y = 0; Y < OSL_FB_CELL_HEIGHT * OslFbScale; Y++)
                OslFbFill(OslFbBase + (SIZE_T)(OslFbRow * OSL_FB_CELL_HEIGHT * OslFbScale + Y) * OslFbStride, OslFbWidth, 0);
            OslFbRowCleared = TRUE;
        }

        for (Y = 0; Y < EARLY_FONT_HEIGHT; Y++)
        {
            UCHAR Bits = ((UCHAR)*Text >= EARLY_FONT_FIRST_CHAR && (UCHAR)*Text <= EARLY_FONT_LAST_CHAR) ?
                         EarlyFont8x13[(UCHAR)*Text - EARLY_FONT_FIRST_CHAR][Y] : 0xFF;

            for (SubY = 0; SubY < OslFbScale; SubY++)
            {
                volatile ULONG *Pixels = OslFbBase +
                                         (SIZE_T)(OslFbRow * OSL_FB_CELL_HEIGHT * OslFbScale + Y * OslFbScale + SubY) * OslFbStride +
                                         OslFbColumn * EARLY_FONT_WIDTH * OslFbScale;

                for (X = 0; X < EARLY_FONT_WIDTH; X++)
                {
                    for (SubX = 0; SubX < OslFbScale; SubX++)
                        Pixels[X * OslFbScale + SubX] = (Bits & (0x80 >> X)) ? OslFbColor : 0;
                }
                for (X = 0; X < EARLY_FONT_WIDTH * OslFbScale; X += 16)
                    ARM64_DC(CIVAC, (ULONG_PTR)(Pixels + X));
            }
        }
        OslFbColumn++;
    }
    ARM64_DSB(SY);
}

static
VOID
OslFbPrepare(VOID)
{
    if (!FrameBufferData || !VramAddress || FrameBufferData->BitsPerPixel != 32 ||
        FrameBufferData->ScreenWidth < 320 || FrameBufferData->ScreenHeight < 200 ||
        FrameBufferData->PixelsPerScanLine < FrameBufferData->ScreenWidth)
    {
        return;
    }

    OslFbWidth = FrameBufferData->ScreenWidth;
    OslFbHeight = FrameBufferData->ScreenHeight;
    OslFbStride = FrameBufferData->PixelsPerScanLine;
    OslFbScale = (OslFbWidth >= 2048) ? 2 : 1;
    OslFbColumns = OslFbWidth / (EARLY_FONT_WIDTH * OslFbScale);
    OslFbRows = OslFbHeight / (OSL_FB_CELL_HEIGHT * OslFbScale);
    OslFbRow = 0;
    OslFbRowCleared = FALSE;
    OslFbColumn = 0;
    OslFbColor = FrameBufferData->PixelMasks.RedMask | FrameBufferData->PixelMasks.GreenMask |
                 FrameBufferData->PixelMasks.BlueMask;
    if (!OslFbColor)
        OslFbColor = 0x00FFFFFF;
    OslFbBase = (volatile ULONG *)(VramAddress + FrameBufferData->FrameBufferOffset);
    Arm64TrapScreenRoutine = OslFbPuts;
}

const PFN_NUMBER MmArchMaxLoaderPage = MM_MAX_PAGE_LOADER;
const PFN_NUMBER MmArchMaxRamdiskPage = MM_MAX_PAGE_LOADER;

void _exituefi(VOID);
void _changestack(VOID);

BOOLEAN
EarlyUartInitialize(
    UINT64 UartBaseOverride);

const PCSTR OslArchSystemIdentifier = "ARM processor family";

static
VOID
Arm64LateDebugPutChar(
    UCHAR Character)
{
    EarlyUartPutc(Character);
}

VOID
OslArchAddConfiguration(
    _In_ PCONFIGURATION_COMPONENT_DATA System)
{
    UNREFERENCED_PARAMETER(System);
}

TYPE_OF_MEMORY
OslArchDescriptorType(
    _In_ PFN_NUMBER Page,
    _In_ TYPE_OF_MEMORY Type)
{
    UNREFERENCED_PARAMETER(Page);
    return Type;
}

BOOLEAN
OslArchMapFirmwareRuntime(
    _In_ ULONGLONG PhysicalStart,
    _In_ ULONGLONG PageCount,
    _In_ BOOLEAN Device,
    _In_ BOOLEAN Executable,
    _Out_ PULONGLONG VirtualStart)
{
    ULONGLONG Size = PageCount << MM_PAGE_SHIFT;
    ULONG Attributes;

    if (Device)
        Attributes = ARM64_MAP_ATTR_DEVICE;
    else
        Attributes = ARM64_MAP_ATTR_NORMAL | (Executable ? ARM64_MAP_ATTR_EXECUTE : 0);

    *VirtualStart = ARM64_KSEG0_BASE + PhysicalStart;
    return Arm64MapVirtualMemory(PhysicalStart, PhysicalStart, Size, Attributes) &&
           Arm64MapVirtualMemory(*VirtualStart, PhysicalStart, Size, Attributes);
}

VOID
OslArchInitialize(VOID)
{
    Arm64DescribeDeviceTreePlatform();
    EarlyUartInitialize(0);
    FwLateDebugPutChar = Arm64LateDebugPutChar;
    Arm64InstallAppleSmbios();
}

VOID
OslArchPlaceRamdisk(VOID)
{
}

VOID
FwExitBootServices(VOID)
{
    _exituefi();
}

VOID
OslArchSwitchStack(VOID)
{
    _changestack();
}

BOOLEAN
OslArchPrepareHandoff(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    WinLdrSetupMachineDependent(LoaderBlock);

    if (!WinLdrSetupMemoryLayout(LoaderBlock))
        return FALSE;

    Arm64PreparePageTables();
    if (FrameBufferData && VramAddress)
    {
        ULONGLONG FrameBase = VramAddress + FrameBufferData->FrameBufferOffset;
        ULONGLONG FrameEnd = FrameBase + (ULONGLONG)FrameBufferData->PixelsPerScanLine * FrameBufferData->ScreenHeight * sizeof(ULONG);

        FrameBase &= ~((ULONGLONG)MM_PAGE_SIZE - 1);
        FrameEnd = (FrameEnd + MM_PAGE_SIZE - 1) & ~((ULONGLONG)MM_PAGE_SIZE - 1);
        if (!Arm64IsKernelVaMapped(ARM64_PHYS_MAP_BASE | FrameBase) &&
            !Arm64MapVirtualMemory(ARM64_PHYS_MAP_BASE | FrameBase, FrameBase, FrameEnd - FrameBase, ARM64_MEM_ATTR_NORMAL_WC))
            ERR("winload: framebuffer alias mapping failed\n");
    }
    OslFbPrepare();
    FwExitBootServices();
    Arm64StopFirmwareWatchdog();
    Arm64SetVbarAuto((ULONG_PTR)arm64_exception_vectors);
    WinLdrSetProcessorContext(OSL_NT_VERSION);
    RtlZeroMemory((PVOID)KI_USER_SHARED_DATA, MM_PAGE_SIZE);
    return TRUE;
}

DECLSPEC_NORETURN
VOID
OslArchStartKernel(
    _In_ PVOID EntryPoint,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLOADER_PARAMETER_BLOCK Physical = VaToPa(LoaderBlock);

    Arm64JumpToKernel((ULONGLONG)(ULONG_PTR)EntryPoint,
                      (ULONGLONG)(ULONG_PTR)LoaderBlock,
                      (ULONGLONG)Physical->KernelStack);

    for (;;)
        ;
}

BOOLEAN
OslArchPrepareResume(
    _In_ PLNT_HIBER_HEADER Header,
    _In_ ULONG64 HighestPage)
{
    UNREFERENCED_PARAMETER(Header);
    UNREFERENCED_PARAMETER(HighestPage);
    return FALSE;
}

DECLSPEC_NORETURN
VOID
OslArchResume(
    _In_ ULONG64 Operations)
{
    UNREFERENCED_PARAMETER(Operations);

    for (;;)
        ;
}
