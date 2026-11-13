/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LoongArch64 NT loader handoff and four-level bootstrap tables
 * COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */


#include <bl.h>
#include "../../winload/winload.h"
#include "loongarch64.h"

DBG_DEFAULT_CHANNEL(WINDOWS);

#define LOONGARCH64_PAGE_TABLES        5

C_ASSERT(FIELD_OFFSET(LOADER_PARAMETER_BLOCK, LoadOrderListHead) == 16);
C_ASSERT(FIELD_OFFSET(LOADER_PARAMETER_BLOCK, KernelStack) == 128);
C_ASSERT(FIELD_OFFSET(LOADER_PARAMETER_BLOCK, RegistryBase) == 168);
C_ASSERT(FIELD_OFFSET(LOADER_PARAMETER_BLOCK, ArcDiskInformation) == 232);
C_ASSERT(FIELD_OFFSET(LOADER_PARAMETER_BLOCK, Extension) == 240);
C_ASSERT(FIELD_OFFSET(LOONGARCH64_LOADER_BLOCK, Dmw) == 80);
C_ASSERT(FIELD_OFFSET(LOONGARCH64_LOADER_BLOCK, StlbPs) == 112);
C_ASSERT(FIELD_OFFSET(LOONGARCH64_LOADER_BLOCK, DirectMapBase) == 120);
C_ASSERT(FIELD_OFFSET(LOONGARCH64_LOADER_BLOCK, ExceptionStubPhysical) == 192);
C_ASSERT(FIELD_OFFSET(LOONGARCH64_LOADER_BLOCK, EarlyDeviceRangeCount) == 228);
C_ASSERT(FIELD_OFFSET(LOONGARCH64_LOADER_BLOCK, EarlyDeviceRanges) == 232);
C_ASSERT(FIELD_OFFSET(LOONGARCH64_HANDOFF_VALUES, Dmw) == 0);
C_ASSERT(FIELD_OFFSET(LOONGARCH64_HANDOFF_VALUES, Pgdl) == 32);
C_ASSERT(FIELD_OFFSET(LOONGARCH64_HANDOFF_VALUES, Pgdh) == 40);
C_ASSERT(FIELD_OFFSET(LOONGARCH64_HANDOFF_VALUES, Eentry) == 48);
C_ASSERT(FIELD_OFFSET(LOONGARCH64_HANDOFF_VALUES, Tlbrentry) == 56);
C_ASSERT(FIELD_OFFSET(LOONGARCH64_HANDOFF_VALUES, EntryPoint) == 64);
C_ASSERT(FIELD_OFFSET(LOONGARCH64_HANDOFF_VALUES, KernelStack) == 72);
C_ASSERT(FIELD_OFFSET(LOONGARCH64_HANDOFF_VALUES, LoaderBlock) == 80);
C_ASSERT(LOONGARCH64_PWCL_4LEVEL_4K == 0x13E4D52C);
C_ASSERT(LOONGARCH64_PWCH_4LEVEL_4K == 0x267);
C_ASSERT(LOONGARCH64_PGD_INDEX(KI_USER_SHARED_DATA) ==
         LOONGARCH64_LOADER_SHARED_USER_DATA_PGD_INDEX);
C_ASSERT(LOONGARCH64_DIR2_INDEX(KI_USER_SHARED_DATA) == 0);
C_ASSERT(LOONGARCH64_DIR1_INDEX(KI_USER_SHARED_DATA) == 0);
C_ASSERT(LOONGARCH64_PTE_INDEX(KI_USER_SHARED_DATA) == 0);

typedef struct _LOONGARCH64_LOADER_STATE
{
    PULONG64 RootTable;
    PULONG64 EmptyTable;
    PVOID KernelStack;
    PVOID DpcStack;
    PVOID Pcr;
    PVOID Process;
    PVOID Thread;
    PVOID SharedUserDataPage;
    PVOID ExceptionStub;
    ULONG64 CpuId;
    ULONG64 CpuCfg1;
    ULONG64 CpuCfg2;
    ULONG64 PhysicalLimit;
    ULONG64 HighestMappedPhysicalAddress;
    BOOLEAN SetupSucceeded;
    BOOLEAN TablesFinalized;
} LOONGARCH64_LOADER_STATE;

static LOONGARCH64_LOADER_STATE La64LoaderState;
static LOONGARCH64_HANDOFF_VALUES La64HandoffValues;

static
ULONG64
La64ReadCpucfg(
    _In_ ULONG Index)
{
    ULONG64 Value;

    __asm__ __volatile__("cpucfg %0, %1" : "=r"(Value) : "r"(Index));
    return Value;
}

static
ULONG64
La64ReadCrmd(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("csrrd %0, %1" : "=r"(Value) : "i"(LOONGARCH64_CSR_CRMD));
    return Value;
}

static
ULONG64
La64ReadCpuid(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("csrrd %0, %1" : "=r"(Value) : "i"(LOONGARCH64_CSR_CPUID));
    return Value;
}

static
ULONG64
La64DirectMapAddress(
    _In_ ULONG64 PhysicalAddress)
{
    return (PhysicalAddress & LOONGARCH64_ADDRESS_MASK) |
           LOONGARCH64_LOADER_DIRECT_MAP_BASE;
}

static
BOOLEAN
La64IsDirectMappedAddress(
    _In_ ULONG_PTR Address)
{
    ULONG64 Segment = (ULONG64)Address >> 60;

    if (Address < LOONGARCH64_LOADER_PHYSICAL_LIMIT)
        return TRUE;
    return (Segment == 0x8) || (Segment == 0x9) || (Segment == 0xA);
}

static
BOOLEAN
La64PhysicalRangeSupported(
    _In_ ULONG64 PhysicalAddress,
    _In_ ULONG64 Size)
{
    return (Size != 0) &&
           (PhysicalAddress < La64LoaderState.PhysicalLimit) &&
           (Size <= La64LoaderState.PhysicalLimit - PhysicalAddress);
}

static
VOID
La64RecordHighestPhysicalAddress(
    _In_ ULONG64 PhysicalAddress,
    _In_ ULONG64 Size)
{
    if (Size == 0)
        return;
    if (PhysicalAddress + Size - 1 > La64LoaderState.HighestMappedPhysicalAddress)
        La64LoaderState.HighestMappedPhysicalAddress = PhysicalAddress + Size - 1;
}

static
PVOID
La64AllocateResidentMemory(
    _In_ SIZE_T Size,
    _In_ TYPE_OF_MEMORY MemoryType,
    _In_z_ PCSTR Name)
{
    SIZE_T AllocationSize;
    PVOID Address;

    AllocationSize = ROUND_UP(Size, LOONGARCH64_PAGE_SIZE);
    Address = MmAllocateMemoryWithType(AllocationSize, MemoryType);
    if (!Address)
    {
        ERR("LoongArch64: unable to allocate %s (%Iu bytes)\n", Name, AllocationSize);
        return NULL;
    }

    if (((ULONG_PTR)Address & (LOONGARCH64_PAGE_SIZE - 1)) ||
        !La64PhysicalRangeSupported((ULONG_PTR)Address, AllocationSize))
    {
        ERR("LoongArch64: %s allocation %p..%p is outside the direct map\n",
            Name,
            Address,
            (PVOID)((ULONG_PTR)Address + AllocationSize - 1));
        return NULL;
    }

    RtlZeroMemory(Address, AllocationSize);
    La64RecordHighestPhysicalAddress((ULONG_PTR)Address, AllocationSize);
    return Address;
}

static
BOOLEAN
La64BuildPageTables(
    _In_ PVOID SharedUserDataPage)
{
    PULONG64 Pool;
    PULONG64 Dir2;
    PULONG64 Dir1;
    PULONG64 Pte;
    ULONG Index;
    ULONG64 EmptyEntry;
    ULONG64 Leaf;

    Pool = La64AllocateResidentMemory(LOONGARCH64_PAGE_TABLES << LOONGARCH64_PAGE_SHIFT,
                                      LoaderMemoryData,
                                      "page tables");
    if (!Pool)
        return FALSE;

    La64LoaderState.RootTable = Pool;
    Dir2 = Pool + 1 * LOONGARCH64_PT_ENTRIES;
    Dir1 = Pool + 2 * LOONGARCH64_PT_ENTRIES;
    Pte = Pool + 3 * LOONGARCH64_PT_ENTRIES;
    La64LoaderState.EmptyTable = Pool + 4 * LOONGARCH64_PT_ENTRIES;

    EmptyEntry = (ULONG64)(ULONG_PTR)La64LoaderState.EmptyTable;

    for (Index = 0; Index < LOONGARCH64_PT_ENTRIES; ++Index)
    {
        La64LoaderState.RootTable[Index] = EmptyEntry;
        Dir2[Index] = EmptyEntry;
        Dir1[Index] = EmptyEntry;
        Pte[Index] = EmptyEntry;
    }

    /* TODO: G-2 - directory entries carry P; both readings are covered, verify Pill/Pis/Pif on hardware. */
    La64LoaderState.RootTable[LOONGARCH64_PGD_INDEX(KI_USER_SHARED_DATA)] =
        (ULONG64)(ULONG_PTR)Dir2 | LOONGARCH64_DIRECTORY_ENTRY;
    Dir2[LOONGARCH64_DIR2_INDEX(KI_USER_SHARED_DATA)] =
        (ULONG64)(ULONG_PTR)Dir1 | LOONGARCH64_DIRECTORY_ENTRY;
    Dir1[LOONGARCH64_DIR1_INDEX(KI_USER_SHARED_DATA)] =
        (ULONG64)(ULONG_PTR)Pte | LOONGARCH64_DIRECTORY_ENTRY;

    Leaf = ((ULONG64)(ULONG_PTR)SharedUserDataPage &
            LOONGARCH64_PTE_PFN_MASK) | LOONGARCH64_PTE_DATA_LEAF;
    Pte[LOONGARCH64_PTE_INDEX(KI_USER_SHARED_DATA)] = Leaf;

    return TRUE;
}

static
BOOLEAN
La64TranslateAddress(
    _In_ ULONG64 VirtualAddress,
    _Out_ PULONG64 PhysicalAddress)
{
    PULONG64 Table = La64LoaderState.RootTable;
    ULONG64 Indices[4];
    ULONG Level;
    ULONG64 Entry;

    Indices[0] = LOONGARCH64_PGD_INDEX(VirtualAddress);
    Indices[1] = LOONGARCH64_DIR2_INDEX(VirtualAddress);
    Indices[2] = LOONGARCH64_DIR1_INDEX(VirtualAddress);
    Indices[3] = LOONGARCH64_PTE_INDEX(VirtualAddress);

    for (Level = 0; Level < 3; ++Level)
    {
        Entry = Table[Indices[Level]];
        if (!(Entry & LOONGARCH64_DIRECTORY_ENTRY))
            return FALSE;
        Table = (PULONG64)(ULONG_PTR)(Entry & LOONGARCH64_PTE_PFN_MASK);
        if (!Table)
            return FALSE;
    }

    Entry = Table[Indices[3]];
    if (!(Entry & LOONGARCH64_PTE_VALID))
        return FALSE;

    *PhysicalAddress = (Entry & LOONGARCH64_PTE_PFN_MASK) |
                       (VirtualAddress & (LOONGARCH64_PAGE_SIZE - 1));
    return TRUE;
}

static
BOOLEAN
La64InitializeHandoff(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG64 PcrAddress;
    ULONG64 Valen;
    ULONG64 Palen;

    RtlZeroMemory(&La64LoaderState, sizeof(La64LoaderState));
    if (!LoaderBlock)
        return FALSE;

    La64LoaderState.CpuId = La64ReadCpuid();
    La64LoaderState.CpuCfg1 = La64ReadCpucfg(1);
    La64LoaderState.CpuCfg2 = La64ReadCpucfg(2);

    Valen = (La64LoaderState.CpuCfg1 >> 12) & 0xFF;
    Palen = (La64LoaderState.CpuCfg1 >> 4) & 0xFF;
    /* TODO: G-1 - a VALEN or PALEN other than 48 needs a second address-space design. */
    if (Valen != LOONGARCH64_VALEN_48 || Palen != LOONGARCH64_PALEN_48)
    {
        ERR("LoongArch64: VALEN 0x%llx / PALEN 0x%llx are not the supported 48-bit layout\n",
            Valen,
            Palen);
        return FALSE;
    }

    if ((La64ReadCrmd() & (LOONGARCH64_CRMD_PG | LOONGARCH64_CRMD_DA)) !=
        LOONGARCH64_CRMD_PG)
    {
        ERR("LoongArch64: firmware did not leave mapping mode active\n");
        return FALSE;
    }

    La64LoaderState.PhysicalLimit = LOONGARCH64_LOADER_PHYSICAL_LIMIT;

    La64LoaderState.KernelStack = La64AllocateResidentMemory(
        KERNEL_STACK_SIZE,
        LoaderStartupKernelStack,
        "kernel stack");
    La64LoaderState.DpcStack = La64AllocateResidentMemory(
        KERNEL_STACK_SIZE,
        LoaderStartupDpcStack,
        "DPC/interrupt stack");
    La64LoaderState.Pcr = La64AllocateResidentMemory(
        ROUND_UP(sizeof(KPCR), LOONGARCH64_PAGE_SIZE),
        LoaderStartupPcrPage,
        "PCR");
    La64LoaderState.Process = La64AllocateResidentMemory(
        LOONGARCH64_PAGE_SIZE,
        LoaderMemoryData,
        "initial process");
    La64LoaderState.Thread = La64AllocateResidentMemory(
        LOONGARCH64_PAGE_SIZE,
        LoaderMemoryData,
        "initial thread");
    La64LoaderState.SharedUserDataPage = La64AllocateResidentMemory(
        LOONGARCH64_PAGE_SIZE,
        LoaderStartupPcrPage,
        "shared user data");
    La64LoaderState.ExceptionStub = La64AllocateResidentMemory(
        LOONGARCH64_PAGE_SIZE,
        LoaderMemoryData,
        "fail-stop stub");
    if (!La64LoaderState.KernelStack || !La64LoaderState.DpcStack ||
        !La64LoaderState.Pcr ||
        !La64LoaderState.Process || !La64LoaderState.Thread ||
        !La64LoaderState.SharedUserDataPage || !La64LoaderState.ExceptionStub)
    {
        return FALSE;
    }

    {
        PULONG Stub = La64LoaderState.ExceptionStub;
        ULONG Index;

        /* TODO: G-7 - the page is filled whole because the EENTRY offset formula conflicts with VS=0. */
        for (Index = 0; Index < LOONGARCH64_PAGE_SIZE / sizeof(ULONG); ++Index)
            Stub[Index] = LOONGARCH64_EXCEPTION_STUB_WORD;
    }

    if (!La64BuildPageTables(La64LoaderState.SharedUserDataPage))
        return FALSE;

    LoaderBlock->KernelStack = (ULONG_PTR)PaToVa(
        (PVOID)(((ULONG_PTR)La64LoaderState.KernelStack + KERNEL_STACK_SIZE) & ~15ULL));
    PcrAddress = (ULONG_PTR)La64LoaderState.Pcr;
    LoaderBlock->Prcb = (ULONG_PTR)PaToVa((PVOID)(PcrAddress +
                                                   FIELD_OFFSET(KPCR, Prcb)));
    LoaderBlock->Process = (ULONG_PTR)PaToVa(La64LoaderState.Process);
    LoaderBlock->Thread = (ULONG_PTR)PaToVa(La64LoaderState.Thread);
#if (NTDDI_VERSION >= NTDDI_WIN8)
    LoaderBlock->KernelStackSize = KERNEL_STACK_SIZE;
#endif

    La64LoaderState.SetupSucceeded = TRUE;
    return TRUE;
}

static
VOID
La64FillLoaderBlock(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLOONGARCH64_LOADER_BLOCK Payload = &LoaderBlock->u.LoongArch64;
    ULONG64 Root = (ULONG64)(ULONG_PTR)La64LoaderState.RootTable;

    RtlZeroMemory(Payload, sizeof(*Payload));
    Payload->Version = LOONGARCH64_LOADER_BLOCK_VERSION;
    Payload->Size = sizeof(*Payload);
    Payload->Flags = LOONGARCH64_LOADER_REQUIRED_FLAGS;
    Payload->CpuId = La64LoaderState.CpuId;
    Payload->CpuCfg1 = La64LoaderState.CpuCfg1;
    Payload->CpuCfg2 = La64LoaderState.CpuCfg2;
    Payload->PageDirectoryRoot = Root;
    Payload->Pgdl = Root;
    Payload->Pgdh = Root;
    Payload->Pwcl = LOONGARCH64_PWCL_4LEVEL_4K;
    Payload->Pwch = LOONGARCH64_PWCH_4LEVEL_4K;
    Payload->Dmw[0] = LOONGARCH64_DMW0_VALUE;
    Payload->Dmw[1] = LOONGARCH64_DMW1_VALUE;
    Payload->Dmw[2] = LOONGARCH64_DMW2_VALUE;
    Payload->Dmw[3] = LOONGARCH64_DMW3_VALUE;
    Payload->StlbPs = LOONGARCH64_STLBPS_4K;
    Payload->Asid = LOONGARCH64_ASID_BOOT;
    Payload->DirectMapBase = LOONGARCH64_LOADER_DIRECT_MAP_BASE;
    Payload->UncachedBase = LOONGARCH64_LOADER_UNCACHED_BASE;
    Payload->WriteCombineBase = LOONGARCH64_LOADER_WRITE_COMBINE_BASE;
    Payload->PhysicalLimit = La64LoaderState.PhysicalLimit;
    Payload->HighestMappedPhysicalAddress =
        La64LoaderState.HighestMappedPhysicalAddress;
    Payload->PcrPage = (ULONG64)(ULONG_PTR)PaToVa(La64LoaderState.Pcr);
    Payload->DpcStack = (ULONG64)(ULONG_PTR)PaToVa(
        (PUCHAR)La64LoaderState.DpcStack + KERNEL_STACK_SIZE);
    Payload->SharedUserDataPage =
        (ULONG64)(ULONG_PTR)La64LoaderState.SharedUserDataPage;
    Payload->ExceptionStub = (ULONG64)(ULONG_PTR)PaToVa(La64LoaderState.ExceptionStub);
    Payload->ExceptionStubPhysical =
        (ULONG64)(ULONG_PTR)La64LoaderState.ExceptionStub;
    /* TODO: no LoongArch early-console discovery exists; the EarlyConsole* fields stay zero. */
    Payload->EarlyConsoleInterface = LOONGARCH64_EARLY_CONSOLE_NONE;
}

BOOLEAN
MempSetupPaging(
    _In_ PFN_NUMBER StartPage,
    _In_ PFN_NUMBER NumberOfPages,
    _In_ BOOLEAN KernelMapping)
{
    ULONG64 PhysicalAddress;
    ULONG64 Size;

    UNREFERENCED_PARAMETER(KernelMapping);

    if (NumberOfPages == 0)
        return TRUE;
    if (!La64LoaderState.SetupSucceeded)
    {
        ERR("LoongArch64: page range 0x%llx requested before the handoff was initialized\n",
            (ULONGLONG)StartPage);
        return FALSE;
    }

    PhysicalAddress = (ULONG64)StartPage << LOONGARCH64_PAGE_SHIFT;
    Size = (ULONG64)NumberOfPages << LOONGARCH64_PAGE_SHIFT;
    if (!La64PhysicalRangeSupported(PhysicalAddress, Size))
    {
        ERR("LoongArch64: page range 0x%llx + 0x%llx is outside the direct map\n",
            (ULONGLONG)StartPage,
            (ULONGLONG)NumberOfPages);
        return FALSE;
    }

    La64RecordHighestPhysicalAddress(PhysicalAddress, Size);
    return TRUE;
}

VOID
MempDump(VOID)
{
    TRACE("LoongArch64: root %p, highest PA 0x%llx\n",
          La64LoaderState.RootTable,
          La64LoaderState.HighestMappedPhysicalAddress);
}

BOOLEAN
LoongArch64LoaderSetupSucceeded(VOID)
{
    return La64LoaderState.SetupSucceeded;
}

VOID
LoongArch64ZeroSharedUserData(VOID)
{
    if (La64LoaderState.SharedUserDataPage)
        RtlZeroMemory(La64LoaderState.SharedUserDataPage, LOONGARCH64_PAGE_SIZE);
}

BOOLEAN
LoongArch64MapFirmwareRuntime(
    _In_ ULONGLONG PhysicalStart,
    _In_ ULONGLONG PageCount,
    _In_ BOOLEAN Device,
    _In_ BOOLEAN Executable,
    _Out_ PULONGLONG VirtualStart)
{
    ULONGLONG Size;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Executable);

    *VirtualStart = 0;
    if (!La64LoaderState.SetupSucceeded ||
        (PhysicalStart & (LOONGARCH64_PAGE_SIZE - 1)) ||
        PageCount == 0)
    {
        ERR("LoongArch64: firmware runtime range 0x%llx + 0x%llx pages is not representable\n",
            PhysicalStart,
            PageCount);
        return FALSE;
    }

    Size = PageCount << LOONGARCH64_PAGE_SHIFT;
    if (!La64PhysicalRangeSupported(PhysicalStart, Size))
    {
        ERR("LoongArch64: firmware runtime range 0x%llx + 0x%llx is outside the direct map\n",
            PhysicalStart,
            Size);
        return FALSE;
    }

    /* TODO: G-4 - firmware runtime VAs and their cache coherency are unverified; DMW1 is used. */
    *VirtualStart = La64DirectMapAddress(PhysicalStart);
    La64RecordHighestPhysicalAddress(PhysicalStart, Size);
    return TRUE;
}

BOOLEAN
LoongArch64FinalizePageTables(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG64 Physical;

    if (!La64LoaderState.SetupSucceeded || !LoaderBlock)
        return FALSE;

    if (!La64LoaderState.RootTable ||
        ((ULONG_PTR)La64LoaderState.RootTable & (LOONGARCH64_PAGE_SIZE - 1)))
    {
        ERR("LoongArch64: the page-table root is not 4 KiB aligned\n");
        return FALSE;
    }

    if (((ULONG_PTR)La64LoaderState.RootTable >> LOONGARCH64_PAGE_SHIFT) > MAXULONG)
    {
        ERR("LoongArch64: the page-table root frame does not fit a 32-bit frame number\n");
        return FALSE;
    }

    if (!La64TranslateAddress(KI_USER_SHARED_DATA, &Physical) ||
        Physical != (ULONG64)(ULONG_PTR)La64LoaderState.SharedUserDataPage)
    {
        ERR("LoongArch64: the shared user data page is not mapped\n");
        return FALSE;
    }

    La64FillLoaderBlock(LoaderBlock);

    La64LoaderState.TablesFinalized = TRUE;
    return TRUE;
}

VOID
WinLdrSetupMachineDependent(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    if (!La64InitializeHandoff(LoaderBlock))
        UiMessageBox("LoongArch64 loader handoff initialization failed.");
}

VOID
WinLdrSetProcessorContext(
    _In_ USHORT OperatingSystemVersion)
{
    UNREFERENCED_PARAMETER(OperatingSystemVersion);

    if (!La64LoaderState.TablesFinalized)
    {
        for (;;)
            __asm__ __volatile__("idle 0");
    }

    /* TODO: G-3 - the TLB is invalidated only after the jump to the direct-map alias, so a miss cannot strand the loader. */
    __asm__ __volatile__("dbar 0" ::: "memory");
}

DECLSPEC_NORETURN
VOID
LoongArch64JumpToKernel(
    _In_ PVOID EntryPoint,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG_PTR Root = (ULONG_PTR)La64LoaderState.RootTable;

    if (!La64LoaderState.TablesFinalized ||
        !La64IsDirectMappedAddress((ULONG_PTR)&La64HandoffValues) ||
        ((ULONG_PTR)EntryPoint >> 60) != 0x9 ||
        ((ULONG_PTR)LoaderBlock >> 60) != 0x9)
    {
        for (;;)
            __asm__ __volatile__("idle 0");
    }

    /* TODO: G-8 - CSR.PGD (0x1B) is neither written nor recorded; PGDL/PGDH are unambiguous. */
    RtlZeroMemory(&La64HandoffValues, sizeof(La64HandoffValues));
    La64HandoffValues.Dmw[0] = LOONGARCH64_DMW0_VALUE;
    La64HandoffValues.Dmw[1] = LOONGARCH64_DMW1_VALUE;
    La64HandoffValues.Dmw[2] = LOONGARCH64_DMW2_VALUE;
    La64HandoffValues.Dmw[3] = LOONGARCH64_DMW3_VALUE;
    La64HandoffValues.Pgdl = Root;
    La64HandoffValues.Pgdh = Root;
    La64HandoffValues.Eentry = (ULONG64)(ULONG_PTR)PaToVa(La64LoaderState.ExceptionStub);
    La64HandoffValues.Tlbrentry = (ULONG64)(ULONG_PTR)La64LoaderState.ExceptionStub;
    La64HandoffValues.EntryPoint = (ULONG64)(ULONG_PTR)EntryPoint;
    La64HandoffValues.KernelStack = (ULONG64)LoaderBlock->KernelStack;
    La64HandoffValues.LoaderBlock = (ULONG64)(ULONG_PTR)LoaderBlock;

    __asm__ __volatile__(
        "move      $t0, %0\n\t"
        "ld.d      $t1, $t0, 0\n\t"
        "csrwr     $t1, 0x180\n\t"
        "ld.d      $t1, $t0, 8\n\t"
        "csrwr     $t1, 0x181\n\t"
        "ld.d      $t1, $t0, 16\n\t"
        "csrwr     $t1, 0x182\n\t"
        "ld.d      $t1, $t0, 24\n\t"
        "csrwr     $t1, 0x183\n\t"
        "la.local  $t1, .Lla64_direct_map\n\t"
        "bstrpick.d $t1, $t1, 47, 0\n\t"
        "lu12i.w   $t2, 9\n\t"
        "slli.d    $t2, $t2, 48\n\t"
        "or        $t1, $t1, $t2\n\t"
        "jr        $t1\n\t"
        ".Lla64_direct_map:\n\t"
        "bstrpick.d $t0, $t0, 47, 0\n\t"
        "lu12i.w   $t2, 9\n\t"
        "slli.d    $t2, $t2, 48\n\t"
        "or        $t0, $t0, $t2\n\t"
        "dbar      0\n\t"
        "invtlb    0x0, $r0, $r0\n\t"
        "ibar      0\n\t"
        "ld.d      $t1, $t0, 32\n\t"
        "csrwr     $t1, 0x19\n\t"
        "csrrd     $t2, 0x19\n\t"
        "ld.d      $t1, $t0, 40\n\t"
        "csrwr     $t1, 0x1a\n\t"
        "csrrd     $t2, 0x1a\n\t"
        "li.d      $t1, %c1\n\t"
        "csrwr     $t1, 0x1c\n\t"
        "csrrd     $t2, 0x1c\n\t"
        "li.d      $t1, %c2\n\t"
        "csrwr     $t1, 0x1d\n\t"
        "csrrd     $t2, 0x1d\n\t"
        "li.d      $t1, %c3\n\t"
        "csrwr     $t1, 0x1e\n\t"
        "csrrd     $t2, 0x1e\n\t"
        "csrwr     $r0, 0x18\n\t"
        "csrrd     $t2, 0x18\n\t"
        "ld.d      $t1, $t0, 48\n\t"
        "csrwr     $t1, 0x0c\n\t"
        "csrrd     $t2, 0x0c\n\t"
        "ld.d      $t1, $t0, 56\n\t"
        "csrwr     $t1, 0x88\n\t"
        "csrrd     $t2, 0x88\n\t"
        "csrwr     $r0, 0x1\n\t"
        "csrrd     $t2, 0x1\n\t"
        "ld.d      $t1, $t0, 64\n\t"
        "csrwr     $t1, 0x6\n\t"
        "csrrd     $t2, 0x6\n\t"
        "csrwr     $r0, 0x4\n\t"
        "csrrd     $t2, 0x4\n\t"
        "li.d      $t1, %c4\n\t"
        "csrwr     $t1, 0x2\n\t"
        "csrrd     $t2, 0x2\n\t"
        "csrwr     $r0, 0x3\n\t"
        "csrrd     $t2, 0x3\n\t"
        "csrwr     $r0, 0x1f\n\t"
        "csrrd     $t2, 0x1f\n\t"
        "csrwr     $r0, 0x60\n\t"
        "csrrd     $t2, 0x60\n\t"
        "li.d      $t1, %c5\n\t"
        "csrwr     $t1, 0x44\n\t"
        "csrrd     $t2, 0x44\n\t"
        "li.d      $t1, %c6\n\t"
        "csrwr     $t1, 0x41\n\t"
        "csrrd     $t2, 0x41\n\t"
        "dbar      0\n\t"
        "li.d      $t1, %c7\n\t"
        "csrwr     $t1, 0x0\n\t"
        "csrrd     $t2, 0x0\n\t"
        "ld.d      $a0, $t0, 80\n\t"
        "ld.d      $sp, $t0, 72\n\t"
        "ld.d      $t1, $t0, 64\n\t"
        "move      $ra, $r0\n\t"
        "move      $tp, $r0\n\t"
        "move      $a1, $r0\n\t"
        "move      $a2, $r0\n\t"
        "move      $a3, $r0\n\t"
        "move      $a4, $r0\n\t"
        "move      $a5, $r0\n\t"
        "move      $a6, $r0\n\t"
        "move      $a7, $r0\n\t"
        "move      $t0, $r0\n\t"
        "move      $t2, $r0\n\t"
        "move      $t3, $r0\n\t"
        "move      $t4, $r0\n\t"
        "move      $t5, $r0\n\t"
        "move      $t6, $r0\n\t"
        "move      $t7, $r0\n\t"
        "move      $t8, $r0\n\t"
        "move      $r21, $r0\n\t"
        "move      $fp, $r0\n\t"
        "move      $s0, $r0\n\t"
        "move      $s1, $r0\n\t"
        "move      $s2, $r0\n\t"
        "move      $s3, $r0\n\t"
        "move      $s4, $r0\n\t"
        "move      $s5, $r0\n\t"
        "move      $s6, $r0\n\t"
        "move      $s7, $r0\n\t"
        "move      $s8, $r0\n\t"
        "dbar      0\n\t"
        "jr        $t1"
        :
        : "r"(&La64HandoffValues),
          "i"(LOONGARCH64_PWCL_4LEVEL_4K),
          "i"(LOONGARCH64_PWCH_4LEVEL_4K),
          "i"(LOONGARCH64_STLBPS_4K),
          "i"(LOONGARCH64_EUEN_BOOT),
          "i"(LOONGARCH64_TICLR_CLEAR),
          "i"(LOONGARCH64_TCFG_BOOT),
          "i"(LOONGARCH64_CRMD_BOOT)
        : "memory");

    __builtin_unreachable();
}
