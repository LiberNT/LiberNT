/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     i386 OS loader hooks: address space, descriptor tables and kernel entry
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>
#include "../../winload/winload.h"

#define I386_LOADER_PAGES    0x40000
#define I386_IDENTITY_PAGES  (KSEG0_BASE >> MM_PAGE_SHIFT)
#define I386_PTE_BASE        0xC0000000
#define I386_GDT_ENTRIES     128
#define I386_IDT_ENTRIES     256
#define I386_TABLE_MASK      0x3FF
#define I386_PDE_SHIFT       22
#define I386_APIC_FEATURE    0x200
#define I386_APIC_MSR        0x1B
#define I386_COM1            0x3F8
#define I386_COM_STATUS      5
#define I386_COM_READY       0x20
#define I386_COM_SPINS       0x10000

#define I386_FLAT_LIMIT      0xFFFFF
#define I386_FLAT_FLAGS      0xC0
#define I386_BYTE_FLAGS      0x40
#define I386_KERNEL_CODE     0x9B
#define I386_KERNEL_DATA     0x93
#define I386_USER_CODE       0xFB
#define I386_USER_DATA       0xF3
#define I386_TSS_ACCESS      0x89
#define I386_TEB_LIMIT       0xFFF
#define I386_VDM_BASE        0x400
#define I386_VDM_LIMIT       0xFFFF

typedef VOID (*PI386_TRANSFER)(VOID);

void _changestack(VOID);

extern UCHAR I386TransferStart[];
extern UCHAR I386TransferEnd[];

DECLSPEC_NORETURN
VOID
I386EnterKernel(
    _In_ PI386_TRANSFER Transfer,
    _In_ PHARDWARE_PTE Pde,
    _In_ PVOID Gdtr,
    _In_ PVOID Idtr,
    _In_ PVOID EntryPoint,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock);

const PFN_NUMBER MmArchMaxLoaderPage = I386_LOADER_PAGES;
const PCSTR OslArchSystemIdentifier = "AT/AT COMPATIBLE";

static PHARDWARE_PTE I386Pde;
static PUCHAR I386Gdt;
static PKTSS I386Tss;
static PI386_TRANSFER I386Transfer;
static BOOLEAN I386Ready;

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
I386LateDebugPutChar(
    UCHAR Character)
{
    ULONG Spins;

    for (Spins = 0; Spins < I386_COM_SPINS; Spins++)
    {
        if (__inbyte(I386_COM1 + I386_COM_STATUS) & I386_COM_READY)
            break;
    }

    __outbyte(I386_COM1, Character);
}

static
PHARDWARE_PTE
I386GetPte(
    _In_ ULONG VirtualAddress)
{
    PHARDWARE_PTE Entry = &I386Pde[VirtualAddress >> I386_PDE_SHIFT];
    PHARDWARE_PTE Table;

    if (!Entry->Valid)
    {
        Table = MmAllocateMemoryWithType(MM_PAGE_SIZE, LoaderMemoryData);
        if (!Table)
            return NULL;

        RtlZeroMemory(Table, MM_PAGE_SIZE);
        Entry->Valid = 1;
        Entry->Write = 1;
        Entry->PageFrameNumber = (ULONG_PTR)Table >> MM_PAGE_SHIFT;
    }

    Table = (PHARDWARE_PTE)((ULONG_PTR)Entry->PageFrameNumber << MM_PAGE_SHIFT);
    return &Table[(VirtualAddress >> MM_PAGE_SHIFT) & I386_TABLE_MASK];
}

static
BOOLEAN
I386MapPages(
    _In_ ULONG VirtualAddress,
    _In_ ULONG PhysicalAddress,
    _In_ PFN_NUMBER Pages,
    _In_ BOOLEAN Uncached)
{
    PHARDWARE_PTE Pte;

    for (; Pages; Pages--, VirtualAddress += MM_PAGE_SIZE, PhysicalAddress += MM_PAGE_SIZE)
    {
        Pte = I386GetPte(VirtualAddress);
        if (!Pte)
            return FALSE;

        Pte->Valid = 1;
        Pte->Write = 1;
        Pte->WriteThrough = Uncached;
        Pte->CacheDisable = Uncached;
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
    ULONG Address = (ULONG)StartPage << MM_PAGE_SHIFT;

    if (KernelMapping && StartPage + NumberOfPages > I386_LOADER_PAGES)
        return FALSE;

    if (StartPage + NumberOfPages <= I386_IDENTITY_PAGES &&
        !I386MapPages(Address, Address, NumberOfPages, FALSE))
    {
        return FALSE;
    }

    return !KernelMapping || I386MapPages(KSEG0_BASE + Address, Address, NumberOfPages, FALSE);
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
    if (Page >= I386_LOADER_PAGES &&
        (Type == LoaderFirmwareTemporary || Type == LoaderLoadedProgram))
    {
        return LoaderFree;
    }

    return Type;
}

static
BOOLEAN
I386MapSpecialPages(
    _In_ PVOID Pcr,
    _In_ PVOID SharedData)
{
    int Info[4];

    if (!I386MapPages(KIP0PCRADDRESS, (ULONG_PTR)Pcr, KIPCR_BOOT_SIZE / MM_PAGE_SIZE, FALSE) ||
        !I386MapPages(KI_USER_SHARED_DATA, (ULONG_PTR)SharedData, 1, FALSE) ||
        !I386GetPte(MM_HAL_VA_START))
    {
        return FALSE;
    }

    __cpuid(Info, 1);
    return !(Info[3] & I386_APIC_FEATURE) ||
           I386MapPages(APIC_BASE, (ULONG)__readmsr(I386_APIC_MSR) & ~(ULONG)MM_PAGE_MASK, 1, TRUE);
}

VOID
WinLdrSetupMachineDependent(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    SIZE_T TableSize = I386_GDT_ENTRIES * sizeof(KGDTENTRY) +
                       I386_IDT_ENTRIES * sizeof(KIDTENTRY);
    PVOID Pcr, SharedData;

    LoaderBlock->u.I386.CommonDataArea = NULL;
    LoaderBlock->u.I386.MachineType = MACHINE_TYPE_ISA;

    MmAllocateMemoryAtAddress(MM_PAGE_SIZE, NULL, LoaderFirmwarePermanent);

    I386Pde = MmAllocateMemoryWithType(MM_PAGE_SIZE, LoaderMemoryData);
    I386Tss = MmAllocateMemoryWithType(sizeof(KTSS), LoaderMemoryData);
    I386Gdt = MmAllocateMemoryWithType(TableSize, LoaderMemoryData);
    Pcr = MmAllocateMemoryWithType(KIPCR_BOOT_SIZE, LoaderStartupPcrPage);
    SharedData = MmAllocateMemoryWithType(MM_PAGE_SIZE, LoaderStartupPcrPage);
    I386Transfer = MmAllocateMemoryWithType(MM_PAGE_SIZE, LoaderLoadedProgram);
    if (!I386Pde || !I386Tss || !I386Gdt || !Pcr || !SharedData || !I386Transfer)
        return;

    RtlZeroMemory(I386Pde, MM_PAGE_SIZE);
    RtlZeroMemory(I386Tss, sizeof(KTSS));
    RtlZeroMemory(I386Gdt, TableSize);
    RtlZeroMemory(Pcr, KIPCR_BOOT_SIZE);
    RtlZeroMemory(SharedData, MM_PAGE_SIZE);
    RtlCopyMemory((PVOID)I386Transfer, I386TransferStart, I386TransferEnd - I386TransferStart);

    I386Pde[I386_PTE_BASE >> I386_PDE_SHIFT].Valid = 1;
    I386Pde[I386_PTE_BASE >> I386_PDE_SHIFT].Write = 1;
    I386Pde[I386_PTE_BASE >> I386_PDE_SHIFT].PageFrameNumber = (ULONG_PTR)I386Pde >> MM_PAGE_SHIFT;

    I386Ready = I386MapSpecialPages(Pcr, SharedData);
}

static
VOID
I386SetSegment(
    _In_ USHORT Selector,
    _In_ ULONG Base,
    _In_ ULONG Limit,
    _In_ UCHAR Access,
    _In_ UCHAR Flags)
{
    PKGDTENTRY Entry = (PKGDTENTRY)(I386Gdt + Selector);

    Entry->LimitLow = (USHORT)Limit;
    Entry->BaseLow = (USHORT)Base;
    Entry->HighWord.Bytes.BaseMid = (UCHAR)(Base >> 16);
    Entry->HighWord.Bytes.Flags1 = Access;
    Entry->HighWord.Bytes.Flags2 = (UCHAR)(Flags | ((Limit >> 16) & 0xF));
    Entry->HighWord.Bytes.BaseHi = (UCHAR)(Base >> 24);
}

VOID
WinLdrSetProcessorContext(
    _In_ USHORT OperatingSystemVersion)
{
    UNREFERENCED_PARAMETER(OperatingSystemVersion);

    I386SetSegment(KGDT_R0_CODE, 0, I386_FLAT_LIMIT, I386_KERNEL_CODE, I386_FLAT_FLAGS);
    I386SetSegment(KGDT_R0_DATA, 0, I386_FLAT_LIMIT, I386_KERNEL_DATA, I386_FLAT_FLAGS);
    I386SetSegment(KGDT_R3_CODE, 0, I386_FLAT_LIMIT, I386_USER_CODE, I386_FLAT_FLAGS);
    I386SetSegment(KGDT_R3_DATA, 0, I386_FLAT_LIMIT, I386_USER_DATA, I386_FLAT_FLAGS);
    I386SetSegment(KGDT_TSS, (ULONG_PTR)PaToVa(I386Tss), sizeof(KTSS) - 1, I386_TSS_ACCESS, 0);
    I386SetSegment(KGDT_R0_PCR, KIP0PCRADDRESS, KIPCR_BOOT_SIZE - 1, I386_KERNEL_DATA, I386_BYTE_FLAGS);
    I386SetSegment(KGDT_R3_TEB, 0, I386_TEB_LIMIT, I386_USER_DATA, I386_BYTE_FLAGS);
    I386SetSegment(KGDT_VDM_TILE, I386_VDM_BASE, I386_VDM_LIMIT, I386_USER_DATA, 0);

    _disable();
}

VOID
OslArchInitialize(VOID)
{
    FwLateDebugPutChar = I386LateDebugPutChar;
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

    if (!I386Ready ||
        !WinLdrSetupMemoryLayout(LoaderBlock) ||
        !MempSetupPaging((PFN_NUMBER)((ULONG_PTR)I386Transfer >> MM_PAGE_SHIFT), 1, FALSE))
    {
        return FALSE;
    }

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
    KDESCRIPTOR Gdtr, Idtr;

    Gdtr.Limit = I386_GDT_ENTRIES * sizeof(KGDTENTRY) - 1;
    Gdtr.Base = (ULONG_PTR)PaToVa(I386Gdt);
    Idtr.Limit = I386_IDT_ENTRIES * sizeof(KIDTENTRY) - 1;
    Idtr.Base = (ULONG_PTR)PaToVa(I386Gdt + I386_GDT_ENTRIES * sizeof(KGDTENTRY));

    I386EnterKernel(I386Transfer, I386Pde, &Gdtr.Limit, &Idtr.Limit, EntryPoint, LoaderBlock);
}
