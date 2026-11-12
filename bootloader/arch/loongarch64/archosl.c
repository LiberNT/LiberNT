/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LoongArch64 OS loader hooks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */


#include "../../winload/winload.h"
#include "loongarch64.h"

void _changestack(VOID);
void _exituefi(VOID);

const PFN_NUMBER MmArchMaxLoaderPage = MM_MAX_PAGE_LOADER;
const PFN_NUMBER MmArchMaxRamdiskPage = MM_MAX_PAGE_LOADER;
const PCSTR OslArchSystemIdentifier = "LoongArch processor family";

PVOID
VaToPa(
    PVOID Va)
{
    return (PVOID)((ULONG_PTR)Va & (ULONG_PTR)LOONGARCH64_ADDRESS_MASK);
}

PVOID
PaToVa(
    PVOID Pa)
{
    return Pa ? (PVOID)(((ULONG_PTR)Pa & (ULONG_PTR)LOONGARCH64_ADDRESS_MASK) |
                        (ULONG_PTR)LOONGARCH64_LOADER_DIRECT_MAP_BASE) : NULL;
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
    return LoongArch64MapFirmwareRuntime(PhysicalStart,
                                         PageCount,
                                         Device,
                                         Executable,
                                         VirtualStart);
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
OslArchPlaceRamdisk(VOID)
{
    /* TODO: no LoongArch DMA-address policy exists yet; the RISC-V DMA32 limit is used. */
    OslPlanRamdiskMove(LOONGARCH64_DMA32_LIMIT);
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

    if (!LoongArch64LoaderSetupSucceeded() ||
        !WinLdrSetupMemoryLayout(LoaderBlock) ||
        !LoongArch64FinalizePageTables(LoaderBlock))
    {
        return FALSE;
    }

    FwExitBootServices();
    OslFinishRamdiskMove();
    WinLdrSetProcessorContext(OSL_NT_VERSION);
    LoongArch64ZeroSharedUserData();
    return TRUE;
}

DECLSPEC_NORETURN
VOID
OslArchStartKernel(
    _In_ PVOID EntryPoint,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    LoongArch64JumpToKernel(EntryPoint, LoaderBlock);

    for (;;)
        __asm__ __volatile__("idle 0");
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
        __asm__ __volatile__("idle 0");
}
