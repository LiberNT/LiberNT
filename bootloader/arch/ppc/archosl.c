/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PowerPC OS loader hooks: page tables, HTAB, BATs and kernel entry
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <ofw.h>
#include "../../winload/winload.h"

#define PPC_PTE_VALID          0x001ULL
#define PPC_PTE_WRITE          0x002ULL
#define PPC_PTE_ACCESSED       0x020ULL
#define PPC_PTE_DIRTY          0x040ULL
#define PPC_PTE_FRAME_MASK     0x000000FFFFFFF000ULL
#define PPC_PTE_TABLE          (PPC_PTE_VALID | PPC_PTE_WRITE)
#define PPC_PTE_KERNEL_LEAF    (PPC_PTE_VALID | PPC_PTE_WRITE | PPC_PTE_ACCESSED | PPC_PTE_DIRTY)

#define PPC_TABLE_ENTRIES      512
#define PPC_ROOT_SHIFT         30
#define PPC_L1_SHIFT           21
#define PPC_L0_SHIFT           12
#define PPC_BOOT_ROOT_INDEX    (PPC_LOADER_KSEG0_BASE >> PPC_ROOT_SHIFT)
#define PPC_TABLE_SLACK        6

#define PPC_HTAB_SIZE          0x40000UL
#define PPC_HTAB_MASK          ((PPC_HTAB_SIZE >> 16) - 1)
#define PPC_SR_KP              0x20000000UL
#define PPC_BAT_VS             0x2UL
#define PPC_BAT_PP_RW          0x2UL
#define PPC_BAT_I              0x20UL
#define PPC_BAT_G              0x08UL
#define PPC_BAT_BL_256M        (0x7FFUL << 2)
#define PPC_BAT_BL_16M         (0x07FUL << 2)
#define PPC_BAT_COUNT          8
#define PPC_SEGMENTS           16
#define PPC_USER_SEGMENTS      8
#define PPC_STACK_LINKAGE      64
#define PPC_STACK_ALIGN        16
#define PPC_CONSOLE_BAUD       115200
#define PPC_KEYBOARD_IRQ       1
#define PPC_POINTER_IRQ        12
#define PPC_I8042_DATA_PORT    0x60
#define PPC_I8042_COMMAND_PORT 0x64
#define PPC_KEYBOARD_TYPE      4

#define PPC_KERNEL_MSR (PPC_MSR_ME | PPC_MSR_IR | PPC_MSR_DR | PPC_MSR_ILE | PPC_MSR_LE | PPC_MSR_FP | PPC_MSR_RI)

typedef struct _PPC_HANDOFF
{
    ULONG Sdr1;
    ULONG Sr[PPC_SEGMENTS];
    ULONG Ibat[PPC_BAT_COUNT];
    ULONG Dbat[PPC_BAT_COUNT];
    ULONG Msr;
    ULONG Entry;
    ULONG Toc;
    ULONG Stack;
    ULONG Argument;
} PPC_HANDOFF, *PPPC_HANDOFF;

C_ASSERT(FIELD_OFFSET(PPC_HANDOFF, Sr) == 4);
C_ASSERT(FIELD_OFFSET(PPC_HANDOFF, Ibat) == 68);
C_ASSERT(FIELD_OFFSET(PPC_HANDOFF, Dbat) == 100);
C_ASSERT(FIELD_OFFSET(PPC_HANDOFF, Msr) == 132);
C_ASSERT(sizeof(PPC_HANDOFF) == 152);

typedef struct _PPC_LOADER_STATE
{
    PULONGLONG RootTable;
    PULONGLONG BootL1Table;
    PUCHAR TablePool;
    ULONG TablesAvailable;
    ULONG TablesUsed;
    PVOID Htab;
    PVOID KernelStack;
    PVOID DpcStack;
    PVOID PanicStack;
    PVOID Pcr;
    PVOID Process;
    PVOID Thread;
    PVOID SharedUserDataPage;
    ULONG HighestMappedPhysicalAddress;
    BOOLEAN SetupSucceeded;
    BOOLEAN TablesFinalized;
    PPC_HANDOFF Handoff;
} PPC_LOADER_STATE;

DECLSPEC_NORETURN
VOID
PpcEnterKernel(
    _In_ PPPC_HANDOFF Handoff);

VOID
ExecuteLoaderCleanly(
    PVOID PreviousStack);

const PFN_NUMBER MmArchMaxLoaderPage = PPC_LOADER_KSEG0_SIZE >> MM_PAGE_SHIFT;
const PFN_NUMBER MmArchMaxRamdiskPage = PPC_LOADER_KSEG0_SIZE >> MM_PAGE_SHIFT;
const PCSTR OslArchSystemIdentifier = "PowerPC";

static PPC_LOADER_STATE PpcLoaderState;

PVOID
VaToPa(
    PVOID Va)
{
    return (PVOID)((ULONG_PTR)Va & ~(ULONG_PTR)PPC_LOADER_KSEG0_BASE);
}

PVOID
PaToVa(
    PVOID Pa)
{
    return Pa ? (PVOID)((ULONG_PTR)Pa | (ULONG_PTR)PPC_LOADER_KSEG0_BASE) : NULL;
}

static
PVOID
PpcAllocateResident(
    _In_ SIZE_T Size,
    _In_ TYPE_OF_MEMORY Type)
{
    PVOID Memory = MmAllocateMemoryWithType(Size, Type);

    if (!Memory || (ULONG_PTR)Memory + Size > PPC_LOADER_KSEG0_SIZE)
        return NULL;

    RtlZeroMemory(Memory, Size);
    return Memory;
}

static
PULONGLONG
PpcAllocateTable(VOID)
{
    PULONGLONG Table;

    if (PpcLoaderState.TablesUsed >= PpcLoaderState.TablesAvailable)
        return NULL;

    Table = (PULONGLONG)(PpcLoaderState.TablePool + PpcLoaderState.TablesUsed * MM_PAGE_SIZE);
    PpcLoaderState.TablesUsed++;
    RtlZeroMemory(Table, MM_PAGE_SIZE);
    return Table;
}

static
PULONGLONG
PpcGetLeafSlot(
    _In_ ULONG VirtualAddress,
    _In_ BOOLEAN Create)
{
    ULONG RootIndex = VirtualAddress >> PPC_ROOT_SHIFT;
    ULONG L1Index = (VirtualAddress >> PPC_L1_SHIFT) & (PPC_TABLE_ENTRIES - 1);
    ULONG L0Index = (VirtualAddress >> PPC_L0_SHIFT) & (PPC_TABLE_ENTRIES - 1);
    PULONGLONG L0Table;

    if (RootIndex != PPC_BOOT_ROOT_INDEX || !PpcLoaderState.BootL1Table)
        return NULL;

    if (!(PpcLoaderState.BootL1Table[L1Index] & PPC_PTE_VALID))
    {
        if (!Create)
            return NULL;

        L0Table = PpcAllocateTable();
        if (!L0Table)
            return NULL;

        PpcLoaderState.BootL1Table[L1Index] = ((ULONG_PTR)L0Table & PPC_PTE_FRAME_MASK) | PPC_PTE_TABLE;
    }

    L0Table = (PULONGLONG)(ULONG_PTR)(PpcLoaderState.BootL1Table[L1Index] & PPC_PTE_FRAME_MASK);
    return &L0Table[L0Index];
}

static
BOOLEAN
PpcMapPage(
    _In_ ULONG VirtualAddress,
    _In_ ULONG PhysicalAddress)
{
    PULONGLONG Slot = PpcGetLeafSlot(VirtualAddress, TRUE);

    if (!Slot)
        return FALSE;

    *Slot = ((ULONGLONG)PhysicalAddress & PPC_PTE_FRAME_MASK) | PPC_PTE_KERNEL_LEAF;
    return TRUE;
}

BOOLEAN
MempSetupPaging(
    _In_ PFN_NUMBER StartPage,
    _In_ PFN_NUMBER NumberOfPages,
    _In_ BOOLEAN KernelMapping)
{
    PFN_NUMBER Page;
    ULONG Physical;

    if (!KernelMapping)
        return TRUE;

    if (!PpcLoaderState.SetupSucceeded)
        return FALSE;

    for (Page = StartPage; Page < StartPage + NumberOfPages; Page++)
    {
        Physical = (ULONG)Page << MM_PAGE_SHIFT;
        if (Physical >= PPC_LOADER_KSEG0_SIZE || !PpcMapPage(PPC_LOADER_KSEG0_BASE + Physical, Physical))
            return FALSE;

        if (Physical + MM_PAGE_SIZE - 1 > PpcLoaderState.HighestMappedPhysicalAddress)
            PpcLoaderState.HighestMappedPhysicalAddress = Physical + MM_PAGE_SIZE - 1;
    }

    return TRUE;
}

VOID
MempDump(VOID)
{
}

static
VOID
PpcAddInputDevice(
    _In_ PCONFIGURATION_COMPONENT_DATA Bus,
    _In_ BOOLEAN Keyboard)
{
    PCM_PARTIAL_RESOURCE_LIST List;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_KEYBOARD_DEVICE_DATA KeyboardData;
    PCONFIGURATION_COMPONENT_DATA Controller;
    ULONG Count = Keyboard ? 3 : 1;
    ULONG Size, Index;

    List = OslAllocateResourceList(Count, 0, &Size);
    if (!List)
        return;

    RtlZeroMemory(List, Size);
    List->Version = 1;
    List->Revision = 1;
    List->Count = Count;

    Descriptor = &List->PartialDescriptors[0];
    Descriptor->Type = CmResourceTypeInterrupt;
    Descriptor->Flags = CM_RESOURCE_INTERRUPT_LATCHED;
    Descriptor->u.Interrupt.Level = Keyboard ? PPC_KEYBOARD_IRQ : PPC_POINTER_IRQ;
    Descriptor->u.Interrupt.Vector = Descriptor->u.Interrupt.Level;
    Descriptor->u.Interrupt.Affinity = (KAFFINITY)-1;

    for (Index = 1; Index < Count; Index++)
    {
        Descriptor = &List->PartialDescriptors[Index];
        Descriptor->Type = CmResourceTypePort;
        Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
        Descriptor->Flags = CM_RESOURCE_PORT_IO;
        Descriptor->u.Port.Start.QuadPart = (Index == 1) ? PPC_I8042_DATA_PORT : PPC_I8042_COMMAND_PORT;
        Descriptor->u.Port.Length = 1;
    }

    Controller = OslCreateComponent(Bus,
                                    ControllerClass,
                                    Keyboard ? KeyboardController : PointerController,
                                    Input,
                                    "",
                                    List,
                                    Size);
    if (!Controller)
        return;

    List = OslAllocateResourceList(Keyboard ? 1 : 0, Keyboard ? sizeof(*KeyboardData) : 0, &Size);
    if (!List)
        return;

    RtlZeroMemory(List, Size);
    List->Version = 1;
    List->Revision = 1;

    if (Keyboard)
    {
        List->Count = 1;
        Descriptor = &List->PartialDescriptors[0];
        Descriptor->Type = CmResourceTypeDeviceSpecific;
        Descriptor->u.DeviceSpecificData.DataSize = sizeof(*KeyboardData);
        KeyboardData = (PCM_KEYBOARD_DEVICE_DATA)(Descriptor + 1);
        KeyboardData->Version = 1;
        KeyboardData->Revision = 1;
        KeyboardData->Type = PPC_KEYBOARD_TYPE;
    }

    OslCreateComponent(Controller,
                       PeripheralClass,
                       Keyboard ? KeyboardPeripheral : PointerPeripheral,
                       Input,
                       Keyboard ? "PCAT_ENHANCED" : "MICROSOFT PS2 MOUSE",
                       List,
                       Size);
}

VOID
OslArchAddConfiguration(
    _In_ PCONFIGURATION_COMPONENT_DATA System)
{
    PCM_PARTIAL_RESOURCE_LIST List;
    PCONFIGURATION_COMPONENT_DATA Bus;
    ULONG Size;

    if (OfwMachine.MachineType != PPC_MACHINE_PREP || OfwFindDevice("keyboard") == OFW_INVALID)
        return;

    List = OslAllocateResourceList(0, 0, &Size);
    if (!List)
        return;

    RtlZeroMemory(List, Size);
    List->Version = 1;
    List->Revision = 1;

    Bus = OslCreateComponent(System, AdapterClass, MultiFunctionAdapter, 0, "ISA", List, Size);
    if (!Bus)
        return;

    PpcAddInputDevice(Bus, TRUE);
    PpcAddInputDevice(Bus, FALSE);
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
VOID
PpcFillLoaderBlock(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PPPC_LOADER_BLOCK Block = &LoaderBlock->u.PowerPC;
    ULONG Pvr;

    __asm__ __volatile__("mfpvr %0" : "=r"(Pvr));

    RtlZeroMemory(Block, sizeof(*Block));
    Block->Version = PPC_LOADER_BLOCK_VERSION;
    Block->Size = sizeof(*Block);
    Block->Flags = PPC_LOADER_FLAG_HASH_TABLE;
    Block->MachineType = OfwMachine.MachineType;
    Block->ProcessorVersion = Pvr;
    Block->ProcessorFrequency = OfwMachine.ProcessorFrequency;
    Block->BusFrequency = OfwMachine.BusFrequency;
    Block->TimebaseFrequency = OfwMachine.TimebaseFrequency;
    Block->DcacheLineSize = OfwMachine.DcacheLineSize;
    Block->IcacheLineSize = OfwMachine.IcacheLineSize;
    Block->DcacheSize = OfwMachine.DcacheSize;
    Block->IcacheSize = OfwMachine.IcacheSize;
    Block->HashTable = (ULONG_PTR)PpcLoaderState.Htab;
    Block->HashTableSize = PPC_HTAB_SIZE;
    Block->PageTableRoot = (ULONG_PTR)PpcLoaderState.RootTable;
    Block->Kseg0Base = PPC_LOADER_KSEG0_BASE;
    Block->Kseg0Size = PPC_LOADER_KSEG0_SIZE;
    Block->PcrPage = (ULONG_PTR)PpcLoaderState.Pcr;
    Block->PanicStack = (ULONG_PTR)PaToVa((PUCHAR)PpcLoaderState.PanicStack + KERNEL_STACK_SIZE);
    Block->DpcStack = (ULONG_PTR)PaToVa((PUCHAR)PpcLoaderState.DpcStack + KERNEL_STACK_SIZE);
    Block->SharedUserDataPage = (ULONG_PTR)PpcLoaderState.SharedUserDataPage;
    Block->IsaIoPhysicalBase = OfwMachine.IsaIoPhysicalBase;
    Block->IsaIoVirtualBase = OfwMachine.IsaIoPhysicalBase ? PPC_LOADER_IO_WINDOW_BASE : 0;
    Block->PciMemoryPhysicalBase = OfwMachine.PciMemoryPhysicalBase;
    Block->PciDmaOffset = OfwMachine.PciDmaOffset;

    if (OfwMachine.ConsolePort)
    {
        Block->Flags |= PPC_LOADER_FLAG_EARLY_CONSOLE;
        Block->EarlyConsolePort = OfwMachine.ConsolePort;
        Block->EarlyConsoleBaud = PPC_CONSOLE_BAUD;
    }
}

VOID
WinLdrSetupMachineDependent(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG TablesNeeded;
    ULONG_PTR HtabRaw, KernelStackTop;

    RtlZeroMemory(&PpcLoaderState, sizeof(PpcLoaderState));

    TablesNeeded = (ULONG)((MmGetHighestPhysicalPage() + PPC_TABLE_ENTRIES - 1) / PPC_TABLE_ENTRIES);
    TablesNeeded = min(TablesNeeded, PPC_LOADER_KSEG0_SIZE >> PPC_L1_SHIFT) + PPC_TABLE_SLACK;

    PpcLoaderState.TablePool = PpcAllocateResident(TablesNeeded * MM_PAGE_SIZE, LoaderMemoryData);
    if (!PpcLoaderState.TablePool)
        return;

    PpcLoaderState.TablesAvailable = TablesNeeded;
    PpcLoaderState.RootTable = PpcAllocateTable();
    PpcLoaderState.BootL1Table = PpcAllocateTable();
    if (!PpcLoaderState.RootTable || !PpcLoaderState.BootL1Table)
        return;

    PpcLoaderState.RootTable[PPC_BOOT_ROOT_INDEX] =
        ((ULONG_PTR)PpcLoaderState.BootL1Table & PPC_PTE_FRAME_MASK) | PPC_PTE_TABLE;

    HtabRaw = (ULONG_PTR)PpcAllocateResident(2 * PPC_HTAB_SIZE, LoaderMemoryData);
    if (!HtabRaw)
        return;

    PpcLoaderState.Htab = (PVOID)ALIGN_UP_BY(HtabRaw, PPC_HTAB_SIZE);
    PpcLoaderState.KernelStack = PpcAllocateResident(KERNEL_STACK_SIZE, LoaderStartupKernelStack);
    PpcLoaderState.DpcStack = PpcAllocateResident(KERNEL_STACK_SIZE, LoaderStartupDpcStack);
    PpcLoaderState.PanicStack = PpcAllocateResident(KERNEL_STACK_SIZE, LoaderStartupPanicStack);
    PpcLoaderState.Pcr = PpcAllocateResident(ALIGN_UP_BY(sizeof(KPCR), MM_PAGE_SIZE), LoaderStartupPcrPage);
    PpcLoaderState.Process = PpcAllocateResident(MM_PAGE_SIZE, LoaderMemoryData);
    PpcLoaderState.Thread = PpcAllocateResident(MM_PAGE_SIZE, LoaderMemoryData);
    PpcLoaderState.SharedUserDataPage = PpcAllocateResident(MM_PAGE_SIZE, LoaderStartupPcrPage);
    if (!PpcLoaderState.KernelStack || !PpcLoaderState.DpcStack || !PpcLoaderState.PanicStack ||
        !PpcLoaderState.Pcr || !PpcLoaderState.Process || !PpcLoaderState.Thread ||
        !PpcLoaderState.SharedUserDataPage)
    {
        return;
    }

    KernelStackTop = ALIGN_DOWN_BY((ULONG_PTR)PpcLoaderState.KernelStack + KERNEL_STACK_SIZE - PPC_STACK_LINKAGE,
                                   PPC_STACK_ALIGN);
    LoaderBlock->KernelStack = (ULONG_PTR)PaToVa((PVOID)KernelStackTop);
    LoaderBlock->Prcb = (ULONG_PTR)PaToVa((PUCHAR)PpcLoaderState.Pcr + FIELD_OFFSET(KPCR, Prcb));
    LoaderBlock->Process = (ULONG_PTR)PaToVa(PpcLoaderState.Process);
    LoaderBlock->Thread = (ULONG_PTR)PaToVa(PpcLoaderState.Thread);
    LoaderBlock->KernelStackSize = KERNEL_STACK_SIZE;

    PpcLoaderState.SetupSucceeded = TRUE;
    PpcFillLoaderBlock(LoaderBlock);
}

static
BOOLEAN
PpcFinalizePageTables(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG i;

    if (!PpcMapPage(KI_USER_SHARED_DATA, (ULONG_PTR)PpcLoaderState.SharedUserDataPage))
        return FALSE;

    RtlZeroMemory(PpcLoaderState.SharedUserDataPage, MM_PAGE_SIZE);
    LoaderBlock->u.PowerPC.HighestMappedPhysicalAddress = PpcLoaderState.HighestMappedPhysicalAddress;
    RtlZeroMemory(PpcLoaderState.Htab, PPC_HTAB_SIZE);

    PpcLoaderState.Handoff.Sdr1 = (ULONG_PTR)PpcLoaderState.Htab | PPC_HTAB_MASK;
    for (i = 0; i < PPC_USER_SEGMENTS; i++)
        PpcLoaderState.Handoff.Sr[i] = PPC_SR_KP | PPC_BOOT_USER_VSID(i);
    for (i = PPC_USER_SEGMENTS; i < PPC_SEGMENTS; i++)
        PpcLoaderState.Handoff.Sr[i] = PPC_SR_KP | PPC_KERNEL_VSID(i);

    RtlZeroMemory(PpcLoaderState.Handoff.Ibat, sizeof(PpcLoaderState.Handoff.Ibat));
    RtlZeroMemory(PpcLoaderState.Handoff.Dbat, sizeof(PpcLoaderState.Handoff.Dbat));
    PpcLoaderState.Handoff.Ibat[0] = PPC_LOADER_KSEG0_BASE | PPC_BAT_BL_256M | PPC_BAT_VS;
    PpcLoaderState.Handoff.Ibat[1] = PPC_BAT_PP_RW;
    PpcLoaderState.Handoff.Dbat[0] = PPC_LOADER_KSEG0_BASE | PPC_BAT_BL_256M | PPC_BAT_VS;
    PpcLoaderState.Handoff.Dbat[1] = PPC_BAT_PP_RW;

    if (OfwMachine.IsaIoPhysicalBase)
    {
        PpcLoaderState.Handoff.Dbat[2] = PPC_LOADER_IO_WINDOW_BASE | PPC_BAT_BL_16M | PPC_BAT_VS;
        PpcLoaderState.Handoff.Dbat[3] = OfwMachine.IsaIoPhysicalBase | PPC_BAT_I | PPC_BAT_G | PPC_BAT_PP_RW;
    }

    PpcLoaderState.Handoff.Msr = PPC_KERNEL_MSR;
    PpcLoaderState.TablesFinalized = TRUE;
    return TRUE;
}

VOID
WinLdrSetProcessorContext(
    _In_ USHORT OperatingSystemVersion)
{
    UNREFERENCED_PARAMETER(OperatingSystemVersion);
}

VOID
OslArchInitialize(VOID)
{
}

VOID
OslArchSwitchStack(VOID)
{
    ExecuteLoaderCleanly(NULL);
}

VOID
FwExitBootServices(VOID)
{
}

BOOLEAN
FwPrepareRuntimeServices(
    _Inout_ PEFI_FIRMWARE_INFORMATION Information,
    _Inout_ PVIRTUAL_EFI_RUNTIME_SERVICES Services)
{
    UNREFERENCED_PARAMETER(Information);
    UNREFERENCED_PARAMETER(Services);
    return FALSE;
}

BOOLEAN
OslArchPrepareHandoff(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    WinLdrSetupMachineDependent(LoaderBlock);

    if (!PpcLoaderState.SetupSucceeded ||
        !WinLdrSetupMemoryLayout(LoaderBlock) ||
        !PpcFinalizePageTables(LoaderBlock))
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
    PLOADER_PARAMETER_BLOCK Block = VaToPa(LoaderBlock);
    PULONG Descriptor = VaToPa(EntryPoint);
    PLDR_DATA_TABLE_ENTRY Module;
    PLIST_ENTRY Entry;

    PpcLoaderState.Handoff.Entry = Descriptor[0];
    PpcLoaderState.Handoff.Toc = Descriptor[1];
    PpcLoaderState.Handoff.Stack = Block->KernelStack;
    PpcLoaderState.Handoff.Argument = (ULONG_PTR)LoaderBlock;

    for (Entry = VaToPa(Block->LoadOrderListHead.Flink);
         Entry != &Block->LoadOrderListHead;
         Entry = VaToPa(Entry->Flink))
    {
        Module = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        PpcFlushCacheRange(VaToPa(Module->DllBase), Module->SizeOfImage);
    }

    OfwAvailable = FALSE;
    PpcFlushCacheRange(&PpcLoaderState, sizeof(PpcLoaderState));
    PpcEnterKernel(&PpcLoaderState.Handoff);
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

BOOLEAN
FwHiberCheckMemoryMap(
    _In_ PLNT_HIBER_HEADER Header,
    _In_reads_(RangeCount) PLNT_HIBER_RANGE Ranges,
    _In_ ULONG64 RangeCount,
    _Out_ PULONG64 HighestPage)
{
    UNREFERENCED_PARAMETER(Header);
    UNREFERENCED_PARAMETER(Ranges);
    UNREFERENCED_PARAMETER(RangeCount);
    *HighestPage = 0;
    return FALSE;
}

VOID
FwHiberClaimFreeMemory(
    _In_reads_(RangeCount) PLNT_HIBER_RANGE Ranges,
    _In_ ULONG64 RangeCount)
{
    UNREFERENCED_PARAMETER(Ranges);
    UNREFERENCED_PARAMETER(RangeCount);
}

VOID
FwHiberClaimPool(
    _In_ ULONG64 PageCount)
{
    UNREFERENCED_PARAMETER(PageCount);
}

BOOLEAN
FwHiberExitBootServices(
    _In_ PLNT_HIBER_HEADER Header)
{
    UNREFERENCED_PARAMETER(Header);
    return FALSE;
}
