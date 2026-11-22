/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 OS loader hooks: LPAE address space and kernel entry
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <blefi.h>
#include <fdtacpi.h>
#include <Fdt.h>
#include <libernt/fdtlib.h>
#include "../../winload/winload.h"

#define ARM_PTE_VALID        0x0000000000000001ULL
#define ARM_PTE_TABLE        0x0000000000000002ULL
#define ARM_PTE_PAGE         0x0000000000000002ULL
#define ARM_PTE_ATTR_DEVICE  (1ULL << 2)
#define ARM_PTE_ATTR_NORMAL  (4ULL << 2)
#define ARM_PTE_SH_INNER     (3ULL << 8)
#define ARM_PTE_AF           (1ULL << 10)
#define ARM_PTE_PXN          (1ULL << 53)
#define ARM_PTE_XN           (1ULL << 54)
#define ARM_PTE_SW_WRITE     (1ULL << 55)
#define ARM_PTE_FRAME        0x000000FFFFFFF000ULL

#define ARM_PTE_KERNEL       (ARM_PTE_VALID | ARM_PTE_PAGE | ARM_PTE_ATTR_NORMAL | ARM_PTE_SH_INNER | \
                              ARM_PTE_AF | ARM_PTE_SW_WRITE)
#define ARM_PTE_DATA         (ARM_PTE_KERNEL | ARM_PTE_XN | ARM_PTE_PXN)
#define ARM_PTE_DEVICE       (ARM_PTE_VALID | ARM_PTE_PAGE | ARM_PTE_ATTR_DEVICE | ARM_PTE_AF | \
                              ARM_PTE_XN | ARM_PTE_PXN | ARM_PTE_SW_WRITE)
#define ARM_PTE_HANDOFF      (ARM_PTE_VALID | ARM_PTE_PAGE | ARM_PTE_ATTR_NORMAL | ARM_PTE_SH_INNER | \
                              ARM_PTE_AF)
#define ARM_PTE_TABLE_ENTRY  (ARM_PTE_VALID | ARM_PTE_TABLE)
#define ARM_BLOCK_SIZE       0x200000UL
#define ARM_BLOCK_DATA       (ARM_PTE_DATA & ~ARM_PTE_PAGE)
#define ARM_BLOCK_DEVICE     (ARM_PTE_DEVICE & ~ARM_PTE_PAGE)

#define ARM_ROOT_ENTRIES     4
#define ARM_TABLE_ENTRIES    512
#define ARM_KSEG0_PAGES      (ARM_LOADER_KSEG0_SIZE >> MM_PAGE_SHIFT)
#define ARM_KSEG0_ALIGNMENT  0x200000ULL
#define ARM_MAX_PHYSICAL     0x100000000ULL
#define ARM_EXTRA_TABLES     18

#define ARM_MAIR             0x444400FFUL
#define ARM_TTBCR            0x80803500UL

typedef struct _ARM_LOADER_STATE
{
    ULONGLONG Kseg0PhysicalBase;
    PULONGLONG Root;
    PULONGLONG Pool;
    ULONG PoolCount;
    ULONG PoolUsed;
    PVOID KernelStack;
    PVOID DpcStack;
    PVOID PanicStack;
    PVOID Pcr;
    PVOID Process;
    PVOID Thread;
    PVOID SharedPage;
    PVOID Handoff;
    ULONG DeviceWindowUsed;
    ULONGLONG EarlyUartPhysical;
    ULONG EarlyUartAddress;
    const VOID *DeviceTree;
    ULONG DeviceTreeSize;
    BOOLEAN Ready;
} ARM_LOADER_STATE;

DECLSPEC_NORETURN
VOID
ArmEnterKernel(
    _In_ ULONG EntryPoint,
    _In_ ULONG LoaderBlock,
    _In_ ULONG KernelStack,
    _In_ ULONG Root,
    _In_ ULONG Stub);

extern UCHAR ArmEnterKernelStart[];
extern UCHAR ArmEnterKernelEnd[];

void _changestack(VOID);
void _exituefi(VOID);

PFN_NUMBER MmArchMaxLoaderPage;
const PFN_NUMBER MmArchMaxRamdiskPage = (PFN_NUMBER)(ARM_MAX_PHYSICAL >> MM_PAGE_SHIFT);
const PCSTR OslArchSystemIdentifier = "ARM processor family";

static ARM_LOADER_STATE ArmLoader;

PVOID
VaToPa(
    PVOID Va)
{
    ULONG_PTR Address = (ULONG_PTR)Va;

    if (Address >= ARM_LOADER_KSEG0_BASE && Address < ARM_LOADER_KSEG0_BASE + ARM_LOADER_KSEG0_SIZE)
        return (PVOID)(ULONG_PTR)(Address - ARM_LOADER_KSEG0_BASE + ArmLoader.Kseg0PhysicalBase);
    return Va;
}

PVOID
PaToVa(
    PVOID Pa)
{
    ULONGLONG Address = (ULONG_PTR)Pa;

    if (!Pa)
        return NULL;
    ASSERT(Address >= ArmLoader.Kseg0PhysicalBase &&
           Address < ArmLoader.Kseg0PhysicalBase + ARM_LOADER_KSEG0_SIZE);
    return (PVOID)(ULONG_PTR)(Address - ArmLoader.Kseg0PhysicalBase + ARM_LOADER_KSEG0_BASE);
}

static
BOOLEAN
ArmIsRamType(
    _In_ TYPE_OF_MEMORY Type)
{
    return Type != LoaderReserve && Type != LoaderBad && Type != LoaderMaximum;
}

VOID
OslArchInitialize(VOID)
{
    PFREELDR_MEMORY_DESCRIPTOR Map;
    ULONGLONG Lowest = ARM_MAX_PHYSICAL;
    ULONGLONG Limit;
    ULONG Count, Index;

    Map = FwGetMemoryMap(&Count);
    for (Index = 0; Map && Index < Count; Index++)
    {
        if (ArmIsRamType(Map[Index].MemoryType) && Map[Index].PageCount != 0)
            Lowest = min(Lowest, (ULONGLONG)Map[Index].BasePage << MM_PAGE_SHIFT);
    }

    if (Lowest >= ARM_MAX_PHYSICAL)
        Lowest = 0;

    ArmLoader.Kseg0PhysicalBase = Lowest & ~(ARM_KSEG0_ALIGNMENT - 1);
    Limit = min(ArmLoader.Kseg0PhysicalBase + ARM_LOADER_KSEG0_SIZE, ARM_MAX_PHYSICAL);
    MmArchMaxLoaderPage = (PFN_NUMBER)(Limit >> MM_PAGE_SHIFT);
    OslTrace("OS loader: KSEG0 maps physical %I64x-%I64x",
             ArmLoader.Kseg0PhysicalBase,
             Limit - 1);

    FwDescribeDeviceTreePlatform();
}

VOID
OslArchAddConfiguration(
    _In_ PCONFIGURATION_COMPONENT_DATA System)
{
    UNREFERENCED_PARAMETER(System);
}

static
PULONGLONG
ArmAllocateTable(VOID)
{
    PULONGLONG Table;

    if (ArmLoader.PoolUsed >= ArmLoader.PoolCount)
        return NULL;

    Table = ArmLoader.Pool + (SIZE_T)ArmLoader.PoolUsed++ * ARM_TABLE_ENTRIES;
    RtlZeroMemory(Table, MM_PAGE_SIZE);
    return Table;
}

static
PULONGLONG
ArmGetLeaf(
    _In_ ULONG VirtualAddress)
{
    PULONGLONG Entry = &ArmLoader.Root[VirtualAddress >> 30];
    PULONGLONG Table;
    ULONG Level;

    for (Level = 0; Level < 2; Level++)
    {
        if (!(*Entry & ARM_PTE_VALID))
        {
            Table = ArmAllocateTable();
            if (!Table)
                return NULL;
            *Entry = ARM_PTE_TABLE_ENTRY | ((ULONG_PTR)Table & ARM_PTE_FRAME);
        }

        ASSERT((*Entry & (ARM_PTE_VALID | ARM_PTE_TABLE)) == ARM_PTE_TABLE_ENTRY);
        Table = (PULONGLONG)(ULONG_PTR)(*Entry & ARM_PTE_FRAME);
        Entry = &Table[(VirtualAddress >> (Level == 0 ? 21 : 12)) & (ARM_TABLE_ENTRIES - 1)];
    }

    return Entry;
}

static
BOOLEAN
ArmMapPages(
    _In_ ULONG VirtualAddress,
    _In_ ULONGLONG PhysicalAddress,
    _In_ ULONG Pages,
    _In_ ULONGLONG Attributes)
{
    PULONGLONG Leaf;

    for (; Pages; Pages--, VirtualAddress += MM_PAGE_SIZE, PhysicalAddress += MM_PAGE_SIZE)
    {
        Leaf = ArmGetLeaf(VirtualAddress);
        if (!Leaf)
            return FALSE;
        if (*Leaf != 0 && (*Leaf & ARM_PTE_FRAME) != (PhysicalAddress & ARM_PTE_FRAME))
            return FALSE;
        *Leaf = Attributes | (PhysicalAddress & ARM_PTE_FRAME);
    }

    return TRUE;
}

BOOLEAN
MempSetupPaging(
    _In_ PFN_NUMBER StartPage,
    _In_ PFN_NUMBER NumberOfPages,
    _In_ BOOLEAN KernelMapping)
{
    ULONGLONG Start = (ULONGLONG)StartPage << MM_PAGE_SHIFT;
    ULONGLONG End = Start + ((ULONGLONG)NumberOfPages << MM_PAGE_SHIFT);

    if (!KernelMapping)
        return TRUE;

    return Start >= ArmLoader.Kseg0PhysicalBase &&
           End <= ArmLoader.Kseg0PhysicalBase + ARM_LOADER_KSEG0_SIZE;
}

VOID
MempDump(VOID)
{
}

static
ULONG
ArmReserveDeviceWindow(
    _In_ ULONGLONG PhysicalAddress,
    _In_ ULONG Pages,
    _In_ ULONGLONG Attributes)
{
    ULONG Address = ARM_LOADER_DEVICE_WINDOW_BASE + (ArmLoader.DeviceWindowUsed << MM_PAGE_SHIFT);

    if (Pages > (ARM_LOADER_DEVICE_WINDOW_SIZE >> MM_PAGE_SHIFT) - ArmLoader.DeviceWindowUsed)
        return 0;
    if (!ArmMapPages(Address, PhysicalAddress & ~(ULONGLONG)(MM_PAGE_SIZE - 1), Pages, Attributes))
        return 0;

    ArmLoader.DeviceWindowUsed += Pages;
    return Address + (ULONG)(PhysicalAddress & (MM_PAGE_SIZE - 1));
}

BOOLEAN
OslArchMapFirmwareRuntime(
    _In_ ULONGLONG PhysicalStart,
    _In_ ULONGLONG PageCount,
    _In_ BOOLEAN Device,
    _In_ BOOLEAN Executable,
    _Out_ PULONGLONG VirtualStart)
{
    ULONG Address;

    if (!Device &&
        PhysicalStart >= ArmLoader.Kseg0PhysicalBase &&
        PhysicalStart + (PageCount << MM_PAGE_SHIFT) <= ArmLoader.Kseg0PhysicalBase + ARM_LOADER_KSEG0_SIZE)
    {
        *VirtualStart = (ULONG_PTR)PaToVa((PVOID)(ULONG_PTR)PhysicalStart);
        return TRUE;
    }

    if (PageCount > MAXULONG)
        return FALSE;

    Address = ArmReserveDeviceWindow(PhysicalStart,
                                     (ULONG)PageCount,
                                     Device ? ARM_PTE_DEVICE : (Executable ? ARM_PTE_KERNEL : ARM_PTE_DATA));
    *VirtualStart = Address;
    return Address != 0;
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
OslArchPlaceRamdisk(VOID)
{
}

VOID
OslArchReserveFirmwareMemory(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
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

static
const VOID *
ArmFindDeviceTree(
    _Out_ PULONG Size)
{
    EFI_GUID DeviceTreeGuid = FDT_TABLE_GUID;
    UINTN Index;

    *Size = 0;
    if (!GlobalSystemTable || !GlobalSystemTable->ConfigurationTable)
        return NULL;

    for (Index = 0; Index < GlobalSystemTable->NumberOfTableEntries; Index++)
    {
        EFI_CONFIGURATION_TABLE *Entry = &GlobalSystemTable->ConfigurationTable[Index];

        if (!memcmp(&Entry->VendorGuid, &DeviceTreeGuid, sizeof(DeviceTreeGuid)))
        {
            if (!FdtValidateHeader(Entry->VendorTable, FDT_MAXIMUM_SIZE, Size))
                return NULL;
            return Entry->VendorTable;
        }
    }

    return NULL;
}

static
VOID
ArmDiscoverEarlyUart(VOID)
{
    const CHAR *Path, *Compatible;
    ULONGLONG Address, Length;
    ULONG Chosen, Node, Parent, PathLength, CompatibleLength;
    FDT Fdt;

    ArmLoader.DeviceTree = ArmFindDeviceTree(&ArmLoader.DeviceTreeSize);
    if (ArmLoader.DeviceTree &&
        ((ULONG_PTR)ArmLoader.DeviceTree < ArmLoader.Kseg0PhysicalBase ||
         (ULONGLONG)(ULONG_PTR)ArmLoader.DeviceTree + ArmLoader.DeviceTreeSize >
             ArmLoader.Kseg0PhysicalBase + ARM_LOADER_KSEG0_SIZE))
    {
        /* The kernel reads the tree through KSEG0, which does not reach where the firmware left it. */
        PVOID Copy = MmAllocateMemoryWithType(ArmLoader.DeviceTreeSize, LoaderMemoryData);

        if (Copy)
            RtlCopyMemory(Copy, ArmLoader.DeviceTree, ArmLoader.DeviceTreeSize);
        ArmLoader.DeviceTree = Copy;
    }

    if (!ArmLoader.DeviceTree || !FdtOpen(ArmLoader.DeviceTree, ArmLoader.DeviceTreeSize, &Fdt))
        return;
    if (!FdtFindNode(&Fdt, "/chosen", &Chosen, NULL))
        return;

    Path = (const CHAR *)FdtGetProperty(&Fdt, Chosen, "stdout-path", &PathLength);
    if (!Path || PathLength == 0 || Path[PathLength - 1] != '\0' ||
        !FdtResolvePath(&Fdt, Path, &Node, &Parent, NULL))
    {
        return;
    }

    Compatible = (const CHAR *)FdtGetProperty(&Fdt, Node, "compatible", &CompatibleLength);
    if (!Compatible || !FdtStringListContains(Compatible, CompatibleLength, "arm,pl011") ||
        !FdtReadReg(&Fdt, Node, Parent, 0, &Address, &Length) ||
        !FdtTranslateAddress(&Fdt, Parent, &Address) ||
        Address + MM_PAGE_SIZE > ARM_MAX_PHYSICAL)
    {
        return;
    }

    ArmLoader.EarlyUartAddress = ArmReserveDeviceWindow(Address, 1, ARM_PTE_DEVICE);
    if (ArmLoader.EarlyUartAddress)
        ArmLoader.EarlyUartPhysical = Address;
}

static
PVOID
ArmAllocateResident(
    _In_ SIZE_T Size,
    _In_ TYPE_OF_MEMORY Type)
{
    PVOID Memory = MmAllocateMemoryWithType(Size, Type);

    if (Memory)
        RtlZeroMemory(Memory, ROUND_UP(Size, MM_PAGE_SIZE));
    return Memory;
}

static
BOOLEAN
ArmAllocateTables(VOID)
{
    PFREELDR_MEMORY_DESCRIPTOR Map;
    ULONGLONG Highest = ArmLoader.Kseg0PhysicalBase;
    ULONG Count, Index, Tables;

    Map = FwGetMemoryMap(&Count);
    for (Index = 0; Map && Index < Count; Index++)
    {
        ULONGLONG End = ((ULONGLONG)Map[Index].BasePage + Map[Index].PageCount) << MM_PAGE_SHIFT;

        if (ArmIsRamType(Map[Index].MemoryType))
            Highest = max(Highest, End);
    }

    Highest = min(Highest, ArmLoader.Kseg0PhysicalBase + ARM_LOADER_KSEG0_SIZE);
    Tables = (ULONG)((Highest - ArmLoader.Kseg0PhysicalBase + (MM_PAGE_SIZE * ARM_TABLE_ENTRIES) - 1) /
                     (MM_PAGE_SIZE * ARM_TABLE_ENTRIES));
    ArmLoader.PoolCount = Tables + 1 + ARM_EXTRA_TABLES;
    ArmLoader.Pool = ArmAllocateResident((SIZE_T)ArmLoader.PoolCount * MM_PAGE_SIZE, LoaderMemoryData);
    if (!ArmLoader.Pool)
        return FALSE;

    ArmLoader.Root = ArmAllocateTable();
    return ArmLoader.Root != NULL;
}

VOID
WinLdrSetupMachineDependent(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PARM_LOADER_BLOCK Payload = &LoaderBlock->u.Arm;

    RtlZeroMemory(Payload, sizeof(*Payload));
    if (!ArmAllocateTables())
        return;

    ArmLoader.KernelStack = ArmAllocateResident(KERNEL_STACK_SIZE, LoaderStartupKernelStack);
    ArmLoader.DpcStack = ArmAllocateResident(KERNEL_STACK_SIZE, LoaderStartupDpcStack);
    ArmLoader.PanicStack = ArmAllocateResident(KERNEL_STACK_SIZE, LoaderStartupPanicStack);
    ArmLoader.Pcr = ArmAllocateResident(ROUND_UP(sizeof(KIPCR), MM_PAGE_SIZE), LoaderStartupPcrPage);
    ArmLoader.Process = ArmAllocateResident(MM_PAGE_SIZE, LoaderMemoryData);
    ArmLoader.Thread = ArmAllocateResident(MM_PAGE_SIZE, LoaderMemoryData);
    ArmLoader.SharedPage = ArmAllocateResident(MM_PAGE_SIZE, LoaderStartupPcrPage);
    ArmLoader.Handoff = ArmAllocateResident(MM_PAGE_SIZE, LoaderLoadedProgram);
    if (!ArmLoader.KernelStack || !ArmLoader.DpcStack || !ArmLoader.PanicStack || !ArmLoader.Pcr ||
        !ArmLoader.Process || !ArmLoader.Thread || !ArmLoader.SharedPage || !ArmLoader.Handoff)
    {
        return;
    }

    if (!ArmMapPages(KI_USER_SHARED_DATA, (ULONG_PTR)ArmLoader.SharedPage, 1, ARM_PTE_DATA))
        return;

    ArmDiscoverEarlyUart();

    LoaderBlock->KernelStack = (ULONG_PTR)PaToVa((PUCHAR)ArmLoader.KernelStack + KERNEL_STACK_SIZE);
    LoaderBlock->Prcb = (ULONG_PTR)PaToVa((PUCHAR)ArmLoader.Pcr + FIELD_OFFSET(KIPCR, Prcb));
    LoaderBlock->Process = (ULONG_PTR)PaToVa(ArmLoader.Process);
    LoaderBlock->Thread = (ULONG_PTR)PaToVa(ArmLoader.Thread);
#if (NTDDI_VERSION >= NTDDI_WIN8)
    LoaderBlock->KernelStackSize = KERNEL_STACK_SIZE;
#endif

    Payload->Version = ARM_LOADER_BLOCK_VERSION;
    Payload->Size = sizeof(*Payload);
    Payload->Kseg0PhysicalBase = ArmLoader.Kseg0PhysicalBase;
    Payload->PageTableRoot = (ULONG_PTR)ArmLoader.Root;
    Payload->PcrPage = (ULONG_PTR)PaToVa(ArmLoader.Pcr);
    Payload->InterruptStack = (ULONG_PTR)PaToVa((PUCHAR)ArmLoader.DpcStack + KERNEL_STACK_SIZE);
    Payload->PanicStack = (ULONG_PTR)PaToVa((PUCHAR)ArmLoader.PanicStack + KERNEL_STACK_SIZE);
    Payload->DpcStack = Payload->InterruptStack;
    Payload->SharedUserDataPage = (ULONG_PTR)ArmLoader.SharedPage;
    Payload->EarlyUartPhysical = ArmLoader.EarlyUartPhysical;
    Payload->EarlyUartAddress = ArmLoader.EarlyUartAddress;
    Payload->EarlyUartInterface = ArmLoader.EarlyUartAddress ? ARM_EARLY_UART_PL011 : ARM_EARLY_UART_NONE;
    if (ArmLoader.DeviceTree)
    {
        Payload->DeviceTree = (ULONG_PTR)PaToVa((PVOID)ArmLoader.DeviceTree);
        Payload->DeviceTreeSize = ArmLoader.DeviceTreeSize;
    }

    ArmLoader.Ready = TRUE;
}

static
BOOLEAN
ArmMapKseg0(VOID)
{
    PPAGE_LOOKUP_TABLE_ITEM Table;
    PFN_NUMBER Count, Index, First, Last;

    Table = MmGetMemoryMap(&Count);
    if (!Table)
        return FALSE;

    First = (PFN_NUMBER)(ArmLoader.Kseg0PhysicalBase >> MM_PAGE_SHIFT);
    Last = First + ARM_KSEG0_PAGES;
    for (Index = 0; Index < Count; Index++)
    {
        PFN_NUMBER Page = MmLowestPhysicalPage + Index;

        if (Page < First || Page >= Last || !ArmIsRamType(Table[Index].PageAllocated))
            continue;
        if (!ArmMapPages(ARM_LOADER_KSEG0_BASE + (ULONG)((Page - First) << MM_PAGE_SHIFT),
                         (ULONGLONG)Page << MM_PAGE_SHIFT,
                         1,
                         ARM_PTE_KERNEL))
        {
            return FALSE;
        }
    }

    return TRUE;
}

/* The stub turns translation on where it runs, so its page is mapped at its physical address. */
static
BOOLEAN
ArmMapHandoff(VOID)
{
    ULONG_PTR Stub = (ULONG_PTR)ArmLoader.Handoff;
    /* The address of a label in Thumb code has its low bit set. */
    PUCHAR Code = (PUCHAR)((ULONG_PTR)ArmEnterKernelStart & ~(ULONG_PTR)1);
    SIZE_T Length = (SIZE_T)(((ULONG_PTR)ArmEnterKernelEnd & ~(ULONG_PTR)1) - (ULONG_PTR)Code);

    ASSERT(Length != 0 && Length <= MM_PAGE_SIZE);
    ASSERT((Stub & (MM_PAGE_SIZE - 1)) == 0);

    /* TODO: a machine whose memory starts at or above the KSEG0 base has no page below it for the stub */
    if (Stub + MM_PAGE_SIZE > ARM_LOADER_KSEG0_BASE)
        return FALSE;

    RtlCopyMemory(ArmLoader.Handoff, Code, Length);
    return ArmMapPages((ULONG)Stub, Stub, 1, ARM_PTE_HANDOFF);
}

/* Until the HAL has mapped its devices and firmware tables it reaches them at their physical addresses. */
static
BOOLEAN
ArmMapIdentity(VOID)
{
    PPAGE_LOOKUP_TABLE_ITEM Lookup;
    PULONGLONG Entry, Table;
    PFN_NUMBER Count, Page, First;
    ULONG Address, Index;
    BOOLEAN Ram;

    Lookup = MmGetMemoryMap(&Count);
    if (!Lookup)
        return FALSE;

    for (Address = 0; Address < ARM_LOADER_KSEG0_BASE; Address += ARM_BLOCK_SIZE)
    {
        Entry = &ArmLoader.Root[Address >> 30];
        if (!(*Entry & ARM_PTE_VALID))
        {
            Table = ArmAllocateTable();
            if (!Table)
                return FALSE;
            *Entry = ARM_PTE_TABLE_ENTRY | ((ULONG_PTR)Table & ARM_PTE_FRAME);
        }

        ASSERT((*Entry & (ARM_PTE_VALID | ARM_PTE_TABLE)) == ARM_PTE_TABLE_ENTRY);
        Table = (PULONGLONG)(ULONG_PTR)(*Entry & ARM_PTE_FRAME);
        Entry = &Table[(Address >> 21) & (ARM_TABLE_ENTRIES - 1)];
        if ((*Entry & (ARM_PTE_VALID | ARM_PTE_TABLE)) == ARM_PTE_VALID)
            continue;

        Ram = FALSE;
        First = Address >> MM_PAGE_SHIFT;
        for (Page = First; !Ram && Page < First + (ARM_BLOCK_SIZE >> MM_PAGE_SHIFT); Page++)
        {
            Ram = Page >= MmLowestPhysicalPage && Page - MmLowestPhysicalPage < Count &&
                  ArmIsRamType(Lookup[Page - MmLowestPhysicalPage].PageAllocated);
        }

        if (!(*Entry & ARM_PTE_VALID))
        {
            *Entry = (Ram ? ARM_BLOCK_DATA : ARM_BLOCK_DEVICE) | Address;
            continue;
        }

        Table = (PULONGLONG)(ULONG_PTR)(*Entry & ARM_PTE_FRAME);
        for (Index = 0; Index < ARM_TABLE_ENTRIES; Index++)
        {
            if (!(Table[Index] & ARM_PTE_VALID))
                Table[Index] = (Ram ? ARM_PTE_DATA : ARM_PTE_DEVICE) | (Address + (Index << MM_PAGE_SHIFT));
        }
    }

    return TRUE;
}

static
VOID
ArmCleanRange(
    _In_ ULONG_PTR Start,
    _In_ SIZE_T Length)
{
    ULONG Ctr = _MoveFromCoprocessor(15, 0, 0, 0, 1);
    ULONG Line = 4UL << ((Ctr >> 16) & 0xF);
    ULONG_PTR Address = Start & ~(ULONG_PTR)(Line - 1);

    for (; Address < Start + Length; Address += Line)
        _MoveToCoprocessor((ULONG)Address, 15, 0, 7, 10, 1);
    __dsb(_ARM_BARRIER_SY);
}

static
VOID
ArmCleanLoadedImages(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLIST_ENTRY Head = VaToPa(&LoaderBlock->LoadOrderListHead);
    PLIST_ENTRY Entry;

    for (Entry = VaToPa(Head->Flink); Entry != Head; Entry = VaToPa(Entry->Flink))
    {
        PKLDR_DATA_TABLE_ENTRY Module = CONTAINING_RECORD(Entry, KLDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        ArmCleanRange((ULONG_PTR)VaToPa(Module->DllBase), Module->SizeOfImage);
    }
}

BOOLEAN
OslArchPrepareHandoff(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    WinLdrSetupMachineDependent(LoaderBlock);

    if (!ArmLoader.Ready || !WinLdrSetupMemoryLayout(LoaderBlock) || !ArmMapKseg0() || !ArmMapHandoff() ||
        !ArmMapIdentity())
    {
        return FALSE;
    }

    ArmCleanLoadedImages(LoaderBlock);
    FwExitBootServices();
    OslFinishRamdiskMove();
    return TRUE;
}

DECLSPEC_NORETURN
VOID
OslArchStartKernel(
    _In_ PVOID EntryPoint,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLOADER_PARAMETER_BLOCK Physical = VaToPa(LoaderBlock);

    ArmCleanRange((ULONG_PTR)ArmLoader.Pool, (SIZE_T)ArmLoader.PoolCount * MM_PAGE_SIZE);
    ArmCleanRange((ULONG_PTR)ArmLoader.Handoff, MM_PAGE_SIZE);
    ArmEnterKernel((ULONG)(ULONG_PTR)EntryPoint,
                   (ULONG)(ULONG_PTR)LoaderBlock,
                   (ULONG)Physical->KernelStack,
                   (ULONG)(ULONG_PTR)ArmLoader.Root,
                   (ULONG)(ULONG_PTR)ArmLoader.Handoff);
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
        __wfi();
}
