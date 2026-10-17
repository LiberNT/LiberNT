/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V OS loader hooks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>
#include <riscv64.h>
#include "../../winload/winload.h"

void _changestack(VOID);
void _exituefi(VOID);

const PFN_NUMBER MmArchMaxLoaderPage = MM_MAX_PAGE_LOADER;
const PFN_NUMBER MmArchMaxRamdiskPage = MM_MAX_PAGE_LOADER;
const PCSTR OslArchSystemIdentifier = "RISC-V processor family";

PVOID
VaToPa(
    PVOID Va)
{
    return (PVOID)((ULONG_PTR)Va & ~(ULONG_PTR)KSEG0_BASE);
}

PVOID
PaToVa(
    PVOID Pa)
{
    return Pa ? (PVOID)((ULONG_PTR)Pa | (ULONG_PTR)KSEG0_BASE) : NULL;
}

VOID
OslArchAddConfiguration(
    _In_ PCONFIGURATION_COMPONENT_DATA System)
{
    UNREFERENCED_PARAMETER(System);
}

BOOLEAN
OslArchMapFirmwareRuntime(
    _In_ ULONGLONG PhysicalStart,
    _In_ ULONGLONG PageCount,
    _In_ BOOLEAN Device,
    _In_ BOOLEAN Executable,
    _Out_ PULONGLONG VirtualStart)
{
    UNREFERENCED_PARAMETER(PhysicalStart);
    UNREFERENCED_PARAMETER(PageCount);
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Executable);
    *VirtualStart = 0;
    return FALSE;
}

TYPE_OF_MEMORY
OslArchDescriptorType(
    _In_ PFN_NUMBER Page,
    _In_ TYPE_OF_MEMORY Type)
{
    UNREFERENCED_PARAMETER(Page);
    return Type;
}

VOID
OslArchInitialize(VOID)
{
}

VOID
OslArchSwitchStack(VOID)
{
    _changestack();
}

VOID
FwExitBootServices(VOID)
{
    _exituefi();
}

BOOLEAN
OslArchPrepareHandoff(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    WinLdrSetupMachineDependent(LoaderBlock);

    if (!RiscvLoaderSetupSucceeded() ||
        !WinLdrSetupMemoryLayout(LoaderBlock) ||
        !RiscvFinalizePageTables(LoaderBlock))
    {
        return FALSE;
    }

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

    RiscvJumpToKernel((ULONG_PTR)EntryPoint, (ULONG_PTR)LoaderBlock, Physical->KernelStack);

    for (;;)
        ;
}
