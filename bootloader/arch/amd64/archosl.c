/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64 OS loader hooks: address space, descriptor tables and kernel entry
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>
#include "../../winload/winload.h"

#define AMD64_LOADER_PAGES   0x100000
#define AMD64_GDT_ENTRIES    128
#define AMD64_IDT_ENTRIES    256
#define AMD64_TABLE_MASK     0x1FF
#define AMD64_TABLE_BITS     9
#define AMD64_PML4_SHIFT     39
#define AMD64_LARGE_PAGE     0x200000
#define AMD64_APIC_FEATURE   0x200
#define AMD64_APIC_MSR       0x1B
#define AMD64_COM1           0x3F8
#define AMD64_COM_STATUS     5
#define AMD64_COM_READY      0x20
#define AMD64_COM_SPINS      0x10000

#define AMD64_KERNEL_CODE    0x00209B0000000000ULL
#define AMD64_KERNEL_DATA    0x00CF93000000FFFFULL
#define AMD64_USER_CODE32    0x00CFFB000000FFFFULL
#define AMD64_USER_DATA      0x00CFF3000000FFFFULL
#define AMD64_USER_CODE      0x0020FB0000000000ULL
#define AMD64_USER_TEB32     0xFF40F3FD50003C00ULL

typedef VOID (NTAPI *PAMD64_KERNEL_ENTRY)(PLOADER_PARAMETER_BLOCK LoaderBlock);

void _changestack(VOID);

VOID
Amd64LoadDescriptorTables(
    _In_ PVOID Gdtr,
    _In_ PVOID Idtr);

const PFN_NUMBER MmArchMaxLoaderPage = AMD64_LOADER_PAGES;
const PCSTR OslArchSystemIdentifier = "AT/AT COMPATIBLE";

static PHARDWARE_PTE Amd64Pml4;
static PUCHAR Amd64Gdt;
static PKTSS64 Amd64Tss;
static BOOLEAN Amd64Ready;

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

static
VOID
Amd64LateDebugPutChar(
    UCHAR Character)
{
    ULONG Spins;

    for (Spins = 0; Spins < AMD64_COM_SPINS; Spins++)
    {
        if (__inbyte(AMD64_COM1 + AMD64_COM_STATUS) & AMD64_COM_READY)
            break;
    }

    __outbyte(AMD64_COM1, Character);
}

static
PHARDWARE_PTE
Amd64GetPte(
    _In_ ULONG64 VirtualAddress)
{
    PHARDWARE_PTE Table = Amd64Pml4;
    PHARDWARE_PTE Entry, Next;
    ULONG Shift;

    for (Shift = AMD64_PML4_SHIFT; Shift > MM_PAGE_SHIFT; Shift -= AMD64_TABLE_BITS)
    {
        Entry = &Table[(VirtualAddress >> Shift) & AMD64_TABLE_MASK];
        if (!Entry->Valid)
        {
            Next = MmAllocateMemoryWithType(MM_PAGE_SIZE, LoaderMemoryData);
            if (!Next)
                return NULL;

            RtlZeroMemory(Next, MM_PAGE_SIZE);
            Entry->Valid = 1;
            Entry->Write = 1;
            Entry->PageFrameNumber = (ULONG_PTR)Next >> MM_PAGE_SHIFT;
        }

        Table = (PHARDWARE_PTE)((ULONG_PTR)Entry->PageFrameNumber << MM_PAGE_SHIFT);
    }

    return &Table[(VirtualAddress >> MM_PAGE_SHIFT) & AMD64_TABLE_MASK];
}

static
BOOLEAN
Amd64MapPages(
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG64 PhysicalAddress,
    _In_ PFN_NUMBER Pages)
{
    PHARDWARE_PTE Pte;

    for (; Pages; Pages--, VirtualAddress += MM_PAGE_SIZE, PhysicalAddress += MM_PAGE_SIZE)
    {
        Pte = Amd64GetPte(VirtualAddress);
        if (!Pte)
            return FALSE;

        Pte->Valid = 1;
        Pte->Write = 1;
        Pte->PageFrameNumber = PhysicalAddress >> MM_PAGE_SHIFT;
    }

    return TRUE;
}

BOOLEAN
MempSetupPaging(
    _In_ PFN_NUMBER StartPage,
    _In_ PFN_NUMBER NumberOfPages,
    _In_ BOOLEAN KernelMapping)
{
    ULONG64 Address = (ULONG64)StartPage << MM_PAGE_SHIFT;

    if (!Amd64MapPages(Address, Address, NumberOfPages))
        return FALSE;

    return !KernelMapping || Amd64MapPages(KSEG0_BASE + Address, Address, NumberOfPages);
}

VOID
MempDump(VOID)
{
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

static
BOOLEAN
Amd64MapSpecialPages(
    _In_ PVOID SharedData)
{
    PVOID ImageBase;
    SIZE_T ImageSize;
    int Info[4];

    if (!Amd64MapPages(KI_USER_SHARED_DATA, (ULONG_PTR)SharedData, 1) ||
        !Amd64GetPte(MM_HAL_VA_START) ||
        !Amd64GetPte(MM_HAL_VA_START + AMD64_LARGE_PAGE))
    {
        return FALSE;
    }

    __cpuid(Info, 1);
    if ((Info[3] & AMD64_APIC_FEATURE) &&
        !Amd64MapPages(APIC_BASE, __readmsr(AMD64_APIC_MSR) & ~(ULONG64)MM_PAGE_MASK, 1))
    {
        return FALSE;
    }

    return !FwGetLoaderImage(&ImageBase, &ImageSize) ||
           MempSetupPaging((PFN_NUMBER)((ULONG_PTR)ImageBase >> MM_PAGE_SHIFT),
                           MM_SIZE_TO_PAGES(ImageSize),
                           FALSE);
}

VOID
WinLdrSetupMachineDependent(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    SIZE_T TableSize = AMD64_GDT_ENTRIES * sizeof(KGDTENTRY64) +
                       AMD64_IDT_ENTRIES * sizeof(KIDTENTRY64);
    PVOID SharedData;

    LoaderBlock->u.I386.CommonDataArea = (PVOID)DbgPrint;
    LoaderBlock->u.I386.MachineType = MACHINE_TYPE_ISA;

    MmAllocateMemoryAtAddress(MM_PAGE_SIZE, NULL, LoaderFirmwarePermanent);

    Amd64Pml4 = MmAllocateMemoryWithType(MM_PAGE_SIZE, LoaderMemoryData);
    Amd64Tss = MmAllocateMemoryWithType(sizeof(KTSS64), LoaderMemoryData);
    Amd64Gdt = MmAllocateMemoryWithType(TableSize, LoaderMemoryData);
    SharedData = MmAllocateMemoryWithType(MM_PAGE_SIZE, LoaderStartupPcrPage);
    if (!Amd64Pml4 || !Amd64Tss || !Amd64Gdt || !SharedData)
        return;

    RtlZeroMemory(Amd64Pml4, MM_PAGE_SIZE);
    RtlZeroMemory(Amd64Tss, sizeof(KTSS64));
    RtlZeroMemory(Amd64Gdt, TableSize);
    RtlZeroMemory(SharedData, MM_PAGE_SIZE);

    Amd64Pml4[(PXE_BASE >> AMD64_PML4_SHIFT) & AMD64_TABLE_MASK].Valid = 1;
    Amd64Pml4[(PXE_BASE >> AMD64_PML4_SHIFT) & AMD64_TABLE_MASK].Write = 1;
    Amd64Pml4[(PXE_BASE >> AMD64_PML4_SHIFT) & AMD64_TABLE_MASK].PageFrameNumber =
        (ULONG_PTR)Amd64Pml4 >> MM_PAGE_SHIFT;

    Amd64Ready = Amd64MapSpecialPages(SharedData);
}

static
VOID
Amd64SetSegment(
    _In_ USHORT Selector,
    _In_ ULONG64 Descriptor)
{
    *(PULONG64)(Amd64Gdt + Selector) = Descriptor;
}

VOID
WinLdrSetProcessorContext(
    _In_ USHORT OperatingSystemVersion)
{
    PKGDTENTRY64 TssEntry = (PKGDTENTRY64)(Amd64Gdt + KGDT64_SYS_TSS);
    ULONG64 TssBase = (ULONG64)PaToVa(Amd64Tss);
    KDESCRIPTOR Gdtr, Idtr;

    UNREFERENCED_PARAMETER(OperatingSystemVersion);

    Amd64SetSegment(KGDT64_R0_CODE, AMD64_KERNEL_CODE);
    Amd64SetSegment(KGDT64_R0_DATA, AMD64_KERNEL_DATA);
    Amd64SetSegment(KGDT64_R3_CMCODE, AMD64_USER_CODE32);
    Amd64SetSegment(KGDT64_R3_DATA, AMD64_USER_DATA);
    Amd64SetSegment(KGDT64_R3_CODE, AMD64_USER_CODE);
    Amd64SetSegment(KGDT64_R3_CMTEB, AMD64_USER_TEB32);

    TssEntry->LimitLow = sizeof(KTSS64) - 1;
    TssEntry->BaseLow = (USHORT)TssBase;
    TssEntry->Bits.BaseMiddle = (UCHAR)(TssBase >> 16);
    TssEntry->Bits.Type = I386_TSS;
    TssEntry->Bits.Present = 1;
    TssEntry->Bits.BaseHigh = (UCHAR)(TssBase >> 24);
    TssEntry->BaseUpper = (ULONG)(TssBase >> 32);

    Gdtr.Limit = AMD64_GDT_ENTRIES * sizeof(KGDTENTRY64) - 1;
    Gdtr.Base = PaToVa(Amd64Gdt);
    Idtr.Limit = AMD64_IDT_ENTRIES * sizeof(KIDTENTRY64) - 1;
    Idtr.Base = PaToVa(Amd64Gdt + AMD64_GDT_ENTRIES * sizeof(KGDTENTRY64));

    _disable();
    __writeeflags(0);
    __writecr3((ULONG64)Amd64Pml4);
    Amd64LoadDescriptorTables(&Gdtr.Limit, &Idtr.Limit);
}

VOID
OslArchInitialize(VOID)
{
    FwLateDebugPutChar = Amd64LateDebugPutChar;
}

VOID
OslArchSwitchStack(VOID)
{
    _changestack();
}

VOID
FwExitBootServices(VOID)
{
    UefiExitBootServices();
}

BOOLEAN
OslArchPrepareHandoff(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    WinLdrSetupMachineDependent(LoaderBlock);

    if (!Amd64Ready || !WinLdrSetupMemoryLayout(LoaderBlock))
        return FALSE;

    FwExitBootServices();
    WinLdrSetProcessorContext(OSL_NT_VERSION);
    return TRUE;
}

DECLSPEC_NORETURN
VOID
OslArchStartKernel(
    _In_ PVOID EntryPoint,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ((PAMD64_KERNEL_ENTRY)EntryPoint)(LoaderBlock);

    for (;;)
        ;
}
