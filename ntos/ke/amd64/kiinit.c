/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     amd64 kernel entry from the loader: PCR, GDT, IDT, TSS, MSRs, features and processor startup
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define REQUIRED_FEATURE_BITS (KF_RDTSC|KF_CR4|KF_CMPXCHG8B|KF_XMMI|KF_XMMI64| \
                               KF_LARGE_PAGE|KF_FAST_SYSCALL|KF_GLOBAL_PAGE| \
                               KF_CMOV|KF_PAT|KF_MMX|KF_FXSR|KF_NX_BIT|KF_MTRR)

ULONG (*FrLdrDbgPrint)(const char *Format, ...);

KSPIN_LOCK KiFreezeExecutionLock;

KIPCR KiInitialPcr;

UCHAR DECLSPEC_ALIGN(16) KiP0BootStackData[KERNEL_STACK_SIZE] = {0};
UCHAR DECLSPEC_ALIGN(16) KiP0DoubleFaultStackData[KERNEL_STACK_SIZE] = {0};
UCHAR DECLSPEC_ALIGN(16) KiP0NmiStackData[KERNEL_STACK_SIZE] = {0};
PVOID KiP0BootStack = &KiP0BootStackData[KERNEL_STACK_SIZE];
PVOID KiP0DoubleFaultStack = &KiP0DoubleFaultStackData[KERNEL_STACK_SIZE];
PVOID KiP0NmiStack = &KiP0NmiStackData[KERNEL_STACK_SIZE];

ULONGLONG BootCycles, BootCyclesEnd;

BOOLEAN KiHvApicMsrs;
BOOLEAN KiX2ApicMsrs;

void KiInitializeSegments();
void KiSystemCallEntry64();
void KiSystemCallEntry32();

CODE_SEG("INIT")
VOID
NTAPI
KiInitMachineDependent(VOID)
{
    if (KeFeatureBits & KF_LARGE_PAGE)
    {
        DPRINT("Large Page support detected but not yet taken advantage of!\n");
    }

    if (KeFeatureBits & KF_GLOBAL_PAGE)
    {
        DPRINT("Global Page support detected but not yet taken advantage of!\n");
    }

    if (KeFeatureBits & KF_MTRR)
    {
        DPRINT("MTRR support detected but not yet taken advantage of!\n");
    }

    if (KeFeatureBits & KF_PAT)
    {
        DPRINT("PAT support detected but not yet taken advantage of!\n");
    }
}

static
VOID
KiInitializePcr(
    _Out_ PKIPCR Pcr,
    _In_ ULONG ProcessorNumber,
    _In_ PKGDTENTRY64 GdtBase,
    _In_ PKIDTENTRY64 IdtBase,
    _In_ PKTSS64 TssBase,
    _In_ PKTHREAD IdleThread,
    _In_ PVOID DpcStack)
{
    RtlZeroMemory(Pcr, sizeof(KIPCR));

    Pcr->Self = (PKPCR)Pcr;
    Pcr->CurrentPrcb = &Pcr->Prcb;
    Pcr->Prcb.Context = &Pcr->Prcb.ProcessorState.ContextFrame;

    Pcr->MajorVersion = PCR_MAJOR_VERSION;
    Pcr->MinorVersion = PCR_MINOR_VERSION;

    Pcr->Prcb.MajorVersion = PRCB_MAJOR_VERSION;
    Pcr->Prcb.MinorVersion = PRCB_MINOR_VERSION;

    Pcr->Prcb.BuildType = 0;
#if DBG
    Pcr->Prcb.BuildType |= PRCB_BUILD_DEBUG;
#endif

    Pcr->Prcb.Number = ProcessorNumber;
    Pcr->Prcb.LegacyNumber = (UCHAR)KiLegacyProcessorNumber(ProcessorNumber);
    Pcr->Prcb.SetMember = KI_SET_BIT(ProcessorNumber);

    Pcr->GdtBase = GdtBase;
    Pcr->IdtBase = IdtBase;

    Pcr->TssBase = TssBase;

    Pcr->Prcb.RspBase = Pcr->TssBase->Rsp0;

    Pcr->Prcb.DpcStack = DpcStack;

    Pcr->Prcb.MultiThreadProcessorSet = Pcr->Prcb.SetMember;
    Pcr->Prcb.MultiThreadSetMaster = &Pcr->Prcb;

    /* Drop any breakpoints the boot loader left armed */
    Pcr->Prcb.ProcessorState.SpecialRegisters.KernelDr6 = 0;
    Pcr->Prcb.ProcessorState.SpecialRegisters.KernelDr7 = 0;

    Pcr->Prcb.MxCsr = INITIAL_MXCSR;

    Pcr->Prcb.CurrentThread = IdleThread;

    Pcr->Irql = PASSIVE_LEVEL;
}

VOID
NTAPI
KiInitializeCpu(
    PKIPCR Pcr)
{
    ULONG64 Pat;
    ULONG64 FeatureBits;

    KiInitializeSegments();

    __writemsr(MSR_GS_BASE, (ULONG64)Pcr);
    __writemsr(MSR_GS_SWAP, (ULONG64)Pcr);

    KiSetProcessorType();

    FeatureBits = KiGetFeatureBits();

    if ((FeatureBits & REQUIRED_FEATURE_BITS) != REQUIRED_FEATURE_BITS)
    {
        FrLdrDbgPrint("CPU doesn't have needed features! Has: 0x%x, required: 0x%x\n",
                FeatureBits, REQUIRED_FEATURE_BITS);
        KeBugCheck(0);
    }

    FeatureBits |= KF_NX_ENABLED;

    Pcr->Prcb.FeatureBits = (ULONG)FeatureBits;
    Pcr->Prcb.FeatureBitsHigh = FeatureBits >> 32;

    __writecr4(__readcr4() | CR4_FXSR);

    __writecr4(__readcr4() | CR4_XMMEXCPT);

    __writecr0(__readcr0() | CR0_WP);

    __writecr0(__readcr0() & ~CR0_MP);

    __writecr0(__readcr0() | CR0_NE);

    /* CR0.AM lets user mode opt into alignment checking */
    __writecr0(__readcr0() | CR0_AM);

    if (FeatureBits & KF_XSTATE)
    {
        __writecr4(__readcr4() | CR4_XSAVE);
    }

    __lldt(0);

    __writemsr(MSR_LSTAR, (ULONG64)KiSystemCallEntry64);
    __writemsr(MSR_CSTAR, (ULONG64)KiSystemCallEntry32);

    __writemsr(MSR_STAR, ((ULONG64)KGDT64_R0_CODE << 32) |
                         ((ULONG64)(KGDT64_R3_CMCODE|RPL_MASK) << 48));

    __writemsr(MSR_SYSCALL_MASK, EFLAGS_IF_MASK | EFLAGS_TF | EFLAGS_DF | EFLAGS_NESTED_TASK);

    __writemsr(MSR_EFER, __readmsr(MSR_EFER) | MSR_SCE | MSR_NXE);

    Pat = (PAT_WB << 0)  | (PAT_WC << 8) | (PAT_UCM << 16) | (PAT_UC << 24) |
          (PAT_WB << 32) | (PAT_WC << 40) | (PAT_UCM << 48) | (PAT_UC << 56);
    __writemsr(MSR_PAT, Pat);

    _mm_setcsr(INITIAL_MXCSR);

    KeSetCurrentIrql(PASSIVE_LEVEL);
}

static
VOID
KiInitializeTss(
    _In_ PKIPCR Pcr,
    _Out_ PKTSS64 Tss,
    _In_ PVOID InitialStack,
    _In_ PVOID DoubleFaultStack,
    _In_ PVOID NmiStack)
{
    PKGDTENTRY64 TssEntry;

    TssEntry = KiGetGdtEntry(Pcr->GdtBase, KGDT64_SYS_TSS);

    KiInitGdtEntry(TssEntry, (ULONG64)Tss, sizeof(KTSS64), AMD64_TSS, 0);

    RtlZeroMemory(Tss, sizeof(KTSS64));

    Tss->IoMapBase = 0x68;

    Tss->Rsp0 = (ULONG64)InitialStack;

    Tss->Ist[1] = (ULONG64)DoubleFaultStack;

    Tss->Ist[2] = (ULONG64)DoubleFaultStack;

    Tss->Ist[3] = (ULONG64)NmiStack;
}

CODE_SEG("INIT")
VOID
KiInitializeProcessorBootStructures(
    _In_ ULONG ProcessorNumber,
    _Out_ PKIPCR Pcr,
    _In_ PKGDTENTRY64 GdtBase,
    _In_ PKIDTENTRY64 IdtBase,
    _In_ PKTSS64 TssBase,
    _In_ PKTHREAD IdleThread,
    _In_ PVOID KernelStack,
    _In_ PVOID DpcStack,
    _In_ PVOID DoubleFaultStack,
    _In_ PVOID NmiStack)
{
    KiInitializePcr(Pcr,
                    ProcessorNumber,
                    GdtBase,
                    IdtBase,
                    TssBase,
                    IdleThread,
                    DpcStack);

    KiInitializeTss(Pcr,
                    TssBase,
                    KernelStack,
                    DoubleFaultStack,
                    NmiStack);
}

CODE_SEG("INIT")
static
VOID
KiInitializeP0BootStructures(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    KDESCRIPTOR GdtDescriptor = {{0},0,0}, IdtDescriptor = {{0},0,0};
    PKGDTENTRY64 TssEntry;
    PKTSS64 TssBase;

    LoaderBlock->KernelStack = (ULONG_PTR)KiP0BootStack;
    LoaderBlock->Thread = (ULONG_PTR)&KiInitialThread;
    LoaderBlock->Process = (ULONG_PTR)&KiInitialProcess.Pcb;
    LoaderBlock->Prcb = (ULONG_PTR)&KiInitialPcr.Prcb;

    __sgdt(&GdtDescriptor.Limit);
    __sidt(&IdtDescriptor.Limit);

    TssEntry = KiGetGdtEntry(GdtDescriptor.Base, KGDT64_SYS_TSS);
    TssBase = KiGetGdtDescriptorBase(TssEntry);

    KiInitializeProcessorBootStructures(0,
                                        &KiInitialPcr,
                                        GdtDescriptor.Base,
                                        IdtDescriptor.Base,
                                        TssBase,
                                        &KiInitialThread.Tcb,
                                        KiP0BootStack,
                                        KiP0DoubleFaultStack,
                                        KiP0DoubleFaultStack,
                                        KiP0NmiStack);
}

CODE_SEG("INIT")
static
ULONG
KiQueryHypervisorRecommendations(
    _Out_ PULONG Privileges)
{
    INT CpuInfo[4];

    *Privileges = 0;

    __cpuid(CpuInfo, 1);
    if (!(CpuInfo[2] & 0x80000000))
        return 0;

    __cpuid(CpuInfo, 0x40000000);
    if ((ULONG)CpuInfo[0] < 0x40000004)
        return 0;

    __cpuid(CpuInfo, 0x40000001);
    if ((ULONG)CpuInfo[0] != HV_CPUID_INTERFACE_HV1)
        return 0;

    __cpuid(CpuInfo, 0x40000003);
    *Privileges = (ULONG)CpuInfo[0];

    __cpuid(CpuInfo, 0x40000004);
    return (ULONG)CpuInfo[0];
}

CODE_SEG("INIT")
VOID
NTAPI
KiInitializeKernelMachineDependent(
    _In_ PKPRCB Prcb,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG64 FeatureBits = KeFeatureBits;
    ULONG HvPrivileges, HvRecommendations;
    ULONG NxFeatureBits;

    HvRecommendations = KiQueryHypervisorRecommendations(&HvPrivileges);
    KiHvApicMsrs = (HvPrivileges & HV_ACCESS_INTR_CTRL_REGS) &&
                   (HvRecommendations & (HV_APIC_ACCESS_RECOMMENDED | HV_X2APIC_MSRS_RECOMMENDED));
    KiRelaxedTiming = (HvRecommendations & HV_RELAXED_TIMING_RECOMMENDED) != 0;

    KeI386CpuType = Prcb->CpuType;
    KeI386CpuStep = Prcb->CpuStep;
    KeProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
    KeProcessorLevel = (USHORT)Prcb->CpuType;
    if (Prcb->CpuID)
        KeProcessorRevision = Prcb->CpuStep;

    SharedUserData->ProcessorFeatures[PF_FLOATING_POINT_PRECISION_ERRATA] = FALSE;
    SharedUserData->ProcessorFeatures[PF_FLOATING_POINT_EMULATED] = FALSE;
    SharedUserData->ProcessorFeatures[PF_COMPARE_EXCHANGE_DOUBLE] = TRUE;
    SharedUserData->ProcessorFeatures[PF_MMX_INSTRUCTIONS_AVAILABLE] =
        (FeatureBits & KF_MMX) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_XMMI_INSTRUCTIONS_AVAILABLE] =
        ((FeatureBits & KF_FXSR) && (FeatureBits & KF_XMMI)) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_3DNOW_INSTRUCTIONS_AVAILABLE] =
        (FeatureBits & KF_3DNOW) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_RDTSC_INSTRUCTION_AVAILABLE] = TRUE;
    SharedUserData->ProcessorFeatures[PF_PAE_ENABLED] = TRUE;
    SharedUserData->ProcessorFeatures[PF_XMMI64_INSTRUCTIONS_AVAILABLE] =
        ((FeatureBits & KF_FXSR) && (FeatureBits & KF_XMMI64)) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_SSE_DAZ_MODE_AVAILABLE] = FALSE;
    SharedUserData->ProcessorFeatures[PF_NX_ENABLED] = TRUE;
    SharedUserData->ProcessorFeatures[PF_SSE3_INSTRUCTIONS_AVAILABLE] =
        (FeatureBits & KF_SSE3) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_COMPARE_EXCHANGE128] =
        (FeatureBits & KF_CMPXCHG16B) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_COMPARE64_EXCHANGE128] = FALSE;
    SharedUserData->ProcessorFeatures[PF_CHANNELS_ENABLED] = FALSE;
    SharedUserData->ProcessorFeatures[PF_XSAVE_ENABLED] =
        (FeatureBits & KF_XSTATE) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_SECOND_LEVEL_ADDRESS_TRANSLATION] =
        (FeatureBits & KF_SLAT) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_VIRT_FIRMWARE_ENABLED] =
        (FeatureBits & KF_VIRT_FIRMWARE_ENABLED) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_RDWRFSGSBASE_AVAILABLE] =
        (FeatureBits & KF_RDWRFSGSBASE) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_FASTFAIL_AVAILABLE] = TRUE;
    SharedUserData->ProcessorFeatures[PF_RDRAND_INSTRUCTION_AVAILABLE] =
        (FeatureBits & KF_RDRAND) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_RDTSCP_INSTRUCTION_AVAILABLE] =
        (FeatureBits & KF_RDTSCP) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_RDPID_INSTRUCTION_AVAILABLE] = FALSE;
    SharedUserData->ProcessorFeatures[PF_SSSE3_INSTRUCTIONS_AVAILABLE] =
        (FeatureBits & KF_SSSE3) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_SSE4_1_INSTRUCTIONS_AVAILABLE] =
        (FeatureBits & KF_SSE4_1) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_SSE4_2_INSTRUCTIONS_AVAILABLE] =
        (FeatureBits & KF_SSE4_2) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_AVX_INSTRUCTIONS_AVAILABLE] =
        (FeatureBits & KF_AVX) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_AVX2_INSTRUCTIONS_AVAILABLE] =
        (FeatureBits & KF_AVX2) ? TRUE : FALSE;
    SharedUserData->ProcessorFeatures[PF_AVX512F_INSTRUCTIONS_AVAILABLE] =
        (FeatureBits & KF_AVX512F) ? TRUE : FALSE;

    SharedUserData->NXSupportPolicy = KiNxPolicyFromLoadOptions(KeLoaderBlock->LoadOptions, &NxFeatureBits);
    Prcb->FeatureBits |= NxFeatureBits;

#if DBG

    KiReportCpuFeatures(Prcb);
#endif
}

static LDR_DATA_TABLE_ENTRY LdrCoreEntries[3];

void
KiInitModuleList(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PLIST_ENTRY Entry;
    ULONG i;

    InitializeListHead(&PsLoadedModuleList);

    for (Entry = LoaderBlock->LoadOrderListHead.Flink, i = 0;
         Entry != &LoaderBlock->LoadOrderListHead && i < 3;
         Entry = Entry->Flink, i++)
    {
        LdrEntry = CONTAINING_RECORD(Entry,
                                     LDR_DATA_TABLE_ENTRY,
                                     InLoadOrderLinks);

        LdrCoreEntries[i] = *LdrEntry;

        InsertTailList(&PsLoadedModuleList, &LdrCoreEntries[i].InLoadOrderLinks);
    }
}

#define KI_BOOT_MARK_COLUMNS 8
#define KI_BOOT_MARK_ROWS    4
#define KI_BOOT_MARK_WIDTH   40
#define KI_BOOT_MARK_HEIGHT  20
#define KI_BOOT_MARK_GAP     6
#define KI_BOOT_MARK_BELOW   112
#define KI_BOOT_HEX_SCALE    4
#define KI_BOOT_HEX_PITCH    (4 * KI_BOOT_HEX_SCALE)
#define KI_BOOT_HEX_LINE     (7 * KI_BOOT_HEX_SCALE)
#define KI_BOOT_HEX_LINES    5
#define KI_BOOT_HEX_SLOT     (KI_BOOT_HEX_LINES * KI_BOOT_HEX_LINE + 12)
#define KI_BOOT_HEX_SLOTS    4
#define KI_BOOT_CPUID_MCA    0x00004000

C_ASSERT(BootMarkMaximum <= KI_BOOT_MARK_COLUMNS * KI_BOOT_MARK_ROWS);

static volatile ULONG* KiBootMarkPixels;
static ULONG KiBootMarkPitch, KiBootMarkLeft, KiBootMarkTop;
static ULONG KiBootHexLeft, KiBootHexSlot, KiBootHexSlots;

static
BOOLEAN
KiAmd64BootMarkWanted(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    static const CHAR Option[] = "DEBUGPORT=SCREEN";
    PCHAR Options = LoaderBlock->LoadOptions;

    if (Options == NULL)
        return FALSE;

    for (; *Options != ANSI_NULL; Options++)
    {
        if (_strnicmp(Options, Option, sizeof(Option) - 1) == 0)
            return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
KiAmd64BootMarkMapped(
    _In_ ULONG_PTR First,
    _In_ ULONG_PTR Last)
{
    ULONG_PTR Address;

    for (Address = First & ~(ULONG_PTR)(PAGE_SIZE - 1); Address <= Last; Address += PAGE_SIZE)
    {
        if (!MiAddressToPxe((PVOID)Address)->u.Hard.Valid ||
            !MiAddressToPpe((PVOID)Address)->u.Hard.Valid)
        {
            return FALSE;
        }

        if (MiAddressToPpe((PVOID)Address)->u.Hard.LargePage)
            continue;

        if (!MiAddressToPde((PVOID)Address)->u.Hard.Valid)
            return FALSE;

        if (MiAddressToPde((PVOID)Address)->u.Hard.LargePage)
            continue;

        if (!MiAddressToPte((PVOID)Address)->u.Hard.Valid)
            return FALSE;
    }

    return TRUE;
}

static
VOID
KiAmd64BootHexLine(
    _In_ ULONG Slot,
    _In_ ULONG Line,
    _In_ ULONG64 Value,
    _In_ ULONG Colour)
{
    static const USHORT Glyphs[16] = { 0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF, 0x7BED, 0x6BAE, 0x7927, 0x6B6E, 0x79E7, 0x79E4 };
    volatile ULONG* Pixels = KiBootMarkPixels;
    ULONG Top = 8 + Slot * KI_BOOT_HEX_SLOT + Line * KI_BOOT_HEX_LINE;
    ULONG Digit, Row, Col, Left, Bits;
    BOOLEAN On;

    for (Digit = 0; Digit < 16; Digit++)
    {
        Bits = Glyphs[(Value >> ((15 - Digit) * 4)) & 0xF];
        Left = KiBootHexLeft + Digit * KI_BOOT_HEX_PITCH;
        for (Row = 0; Row < KI_BOOT_HEX_LINE; Row++)
        {
            for (Col = 0; Col < KI_BOOT_HEX_PITCH; Col++)
            {
                On = Row < 5 * KI_BOOT_HEX_SCALE && Col < 3 * KI_BOOT_HEX_SCALE &&
                     ((Bits >> (14 - ((Row / KI_BOOT_HEX_SCALE) * 3 + Col / KI_BOOT_HEX_SCALE))) & 1);
                Pixels[(Top + Row) * KiBootMarkPitch + Left + Col] = On ? Colour : 0;
            }
        }
    }
}

VOID
NTAPI
KiAmd64BootReadout(
    _In_ ULONG Source,
    _In_ ULONG64 Code,
    _In_ ULONG64 A,
    _In_ ULONG64 B,
    _In_ ULONG64 C,
    _In_ ULONG64 D)
{
    ULONG Slot;

    if (KiBootMarkPixels == NULL || KiBootHexSlot >= KiBootHexSlots)
        return;

    Slot = KiBootHexSlot++;
    KiAmd64BootHexLine(Slot, 0, ((ULONG64)Source << 60) | (Code & 0x0FFFFFFFFFFFFFFFULL), 0x00FFFF00);
    KiAmd64BootHexLine(Slot, 1, A, 0x00FFFFFF);
    KiAmd64BootHexLine(Slot, 2, B, 0x00FFFFFF);
    KiAmd64BootHexLine(Slot, 3, C, 0x00FFFFFF);
    KiAmd64BootHexLine(Slot, 4, D, 0x00FFFFFF);
}

VOID
NTAPI
KiAmd64BootMarkStop(VOID)
{
    KiBootMarkPixels = NULL;
}

static
VOID
KiAmd64BootMarkStart(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLOADER_PARAMETER_EXTENSION Extension = LoaderBlock->Extension;
    ULONG GridWidth = KI_BOOT_MARK_COLUMNS * (KI_BOOT_MARK_WIDTH + KI_BOOT_MARK_GAP);
    ULONG GridHeight = KI_BOOT_MARK_ROWS * (KI_BOOT_MARK_HEIGHT + KI_BOOT_MARK_GAP);
    ULONG Width, Height, Pitch;
    ULONG_PTR Base;
    int CpuInfo[4];

    if (Extension == NULL ||
        Extension->GopFramebuffer.FrameBufferBase.QuadPart == 0 ||
        Extension->GopFramebuffer.PixelFormat != 32 ||
        !KiAmd64BootMarkWanted(LoaderBlock))
    {
        return;
    }

    Width = Extension->GopFramebuffer.HorizontalResolution;
    Height = Extension->GopFramebuffer.VerticalResolution;
    Pitch = Extension->GopFramebuffer.PixelsPerScanLine;
    if (Pitch < Width ||
        Width < max(GridWidth, 16 * KI_BOOT_HEX_PITCH + 8) ||
        Height < GridHeight + KI_BOOT_MARK_BELOW + 8 + KI_BOOT_HEX_SLOT ||
        (ULONG64)Pitch * Height * sizeof(ULONG) > Extension->GopFramebuffer.FrameBufferSize)
    {
        return;
    }

    /* The loader maps this view only when it drew its own marks */
    Base = (ULONG_PTR)(KSEG0_BASE + Extension->GopFramebuffer.FrameBufferBase.QuadPart);
    if (!KiAmd64BootMarkMapped(Base, Base + (ULONG_PTR)Pitch * Height * sizeof(ULONG) - 1))
        return;

    KiBootMarkPitch = Pitch;
    KiBootMarkLeft = Width - GridWidth;
    KiBootMarkTop = Height - KI_BOOT_MARK_BELOW - GridHeight;
    KiBootHexLeft = Width - 16 * KI_BOOT_HEX_PITCH - 8;
    KiBootHexSlots = min((KiBootMarkTop - 8) / KI_BOOT_HEX_SLOT, KI_BOOT_HEX_SLOTS);
    KiBootMarkPixels = (volatile ULONG*)Base;

    __cpuid(CpuInfo, 1);
    KiAmd64BootReadout(BootReadoutProcessorState,
                       __readcr4(),
                       __readcr0(),
                       __readmsr(MSR_EFER),
                       (CpuInfo[3] & KI_BOOT_CPUID_MCA) ? __readmsr(MSR_MCG_STATUS) : 0,
                       (CpuInfo[3] & KI_BOOT_CPUID_MCA) ? __readmsr(MSR_MCG_CAP) : 0);
}

VOID
NTAPI
KiAmd64BootMark(
    _In_ ULONG Mark)
{
    static const ULONG Colours[KI_BOOT_MARK_ROWS] =
        { 0x00FFFFFF, 0x0000FFFF, 0x00FF00FF, 0x00FF8000 };
    volatile ULONG* Pixels = KiBootMarkPixels;
    ULONG Row, Col, Left, Top;

    if (Pixels == NULL || Mark >= BootMarkMaximum)
        return;

    Left = KiBootMarkLeft + (Mark % KI_BOOT_MARK_COLUMNS) * (KI_BOOT_MARK_WIDTH + KI_BOOT_MARK_GAP);
    Top = KiBootMarkTop + (Mark / KI_BOOT_MARK_COLUMNS) * (KI_BOOT_MARK_HEIGHT + KI_BOOT_MARK_GAP);

    for (Row = 0; Row < KI_BOOT_MARK_HEIGHT; Row++)
    {
        for (Col = 0; Col < KI_BOOT_MARK_WIDTH; Col++)
            Pixels[(Top + Row) * KiBootMarkPitch + Left + Col] = Colours[Mark / KI_BOOT_MARK_COLUMNS];
    }
}

CODE_SEG("INIT")
DECLSPEC_NORETURN
VOID
NTAPI
KiSystemStartup(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG Cpu;
    PKTHREAD InitialThread;
    ULONG64 InitialStack;
    PKIPCR Pcr;
    CPU_INFO CpuInfo;

    BootCycles = __rdtsc();

    FrLdrDbgPrint = LoaderBlock->u.I386.CommonDataArea;

    if (KeProcessorCount == 0)
    {
        Cpu = 0;

        KeLoaderBlock = LoaderBlock;

        KiInitializeP0BootStructures(LoaderBlock);
        KiAmd64BootMarkStart(LoaderBlock);
        KiAmd64BootMark(BootMarkBootStructures);
    }
    else
    {
        /* An application processor was numbered when its PRCB was built */
        Pcr = CONTAINING_RECORD(LoaderBlock->Prcb, KIPCR, Prcb);
        Cpu = Pcr->Prcb.Number;
    }

    Pcr = CONTAINING_RECORD(LoaderBlock->Prcb, KIPCR, Prcb);

    /* Publish the PRCB before counting it: every index below KeProcessorCount must be valid */
    KiProcessorBlock[Cpu] = &Pcr->Prcb;
    KeMemoryBarrier();
    if (Cpu + 1 > KeProcessorCount)
        KiSetProcessorCount(Cpu + 1);

    InitialThread = (PKTHREAD)LoaderBlock->Thread;

    InitialThread->ApcState.Process = (PVOID)LoaderBlock->Process;

    KiInitializeCpu(Pcr);
    KiAmd64BootMark(BootMarkProcessor);

    if (Cpu == 0)
    {
        KeFeatureBits = (ULONG64)Pcr->Prcb.FeatureBitsHigh << 32 |
                        Pcr->Prcb.FeatureBits;
        KiInitializeMxCsrMask();

        KiInitModuleList(LoaderBlock);
        KiAmd64BootMark(BootMarkModuleList);

        KeInitExceptions();
        KiAmd64BootMark(BootMarkExceptions);

        MiInitializeKernelVaLayout(LoaderBlock);
        KiAmd64BootMark(BootMarkVaLayout);
    }

    KiCpuId(&CpuInfo, 1);
    if (CpuInfo.Edx & X86_FEATURE_MCE)
        __writecr4(__readcr4() | CR4_MCE);

    while (InterlockedBitTestAndSet64((PLONG64)&KiFreezeExecutionLock, 0))
    {
        while ((*(volatile KSPIN_LOCK*)&KiFreezeExecutionLock) & 1);
    }

    KiAmd64BootMark(BootMarkHalProcessorEntry);
    HalInitializeProcessor(Cpu, KeLoaderBlock);
    KiAmd64BootMark(BootMarkHalProcessor);

    /* End interrupts the same way the HAL reaches the local APIC */
    if ((__readmsr(X2APIC_BASE_MSR) & X2APIC_EXTENDED) != 0)
        KiX2ApicMsrs = TRUE;

    if (Cpu == 0)
        KiActivateProcessor(&Pcr->Prcb);

    InterlockedAnd64((PLONG64)&KiFreezeExecutionLock, 0);

    if (Cpu == 0)
    {
        KdInitSystem(0, KeLoaderBlock);
        KiAmd64BootMark(BootMarkDebugger);

        if (KdPollBreakIn()) DbgBreakPointWithStatus(DBG_STATUS_CONTROL_C);
    }

    DPRINT1("Pcr = %p, Gdt = %p, Idt = %p, Tss = %p\n",
           Pcr, Pcr->GdtBase, Pcr->IdtBase, Pcr->TssBase);

    KfRaiseIrql(HIGH_LEVEL);

    if (Cpu == 0) KiInitializeKernelMachineDependent(&Pcr->Prcb, LoaderBlock);
    KiAmd64BootMark(BootMarkMachineDependent);

    KiInitializeXStateConfiguration(Cpu);
    KiAmd64BootMark(BootMarkExtendedState);

    InitialStack = ALIGN_DOWN_BY(LoaderBlock->KernelStack - KeXStateLength, 64);

    KiSwitchToBootStack(InitialStack);
}
