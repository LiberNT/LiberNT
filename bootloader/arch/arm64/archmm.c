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
    EarlyUartInitialize(0);
    FwLateDebugPutChar = Arm64LateDebugPutChar;
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
    FwExitBootServices();
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
