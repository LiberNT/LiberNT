/*
 */

#include <ntoskrnl.h>
#include <x86x64/Cpuid.h>
#include <x86x64/Msr.h>

#define NDEBUG
#include <debug.h>

#define MSR_AMD_VM_CR        0xC0010114
#define AMD_VM_CR_SVMDIS     (1ULL << 4)

KTSS64 KiBootTss;

ULONG KeI386CpuType;
ULONG KeI386CpuStep;
ULONG KeI386MachineType;
ULONG KeI386NpxPresent = 1;
ULONG KeLargestCacheLine = 0x40;
ULONG KiDmaIoCoherency = 0;
BOOLEAN KiSMTProcessorsPresent;


{

ULONG
NTAPI
KiGetCpuVendor(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    CPU_INFO CpuInfo;

    KiCpuId(&CpuInfo, 0);

    Prcb->VendorString[12] = 0;

    {
    }
        DPRINT1("VIA CPUs not fully supported\n");
        DPRINT1("%s CPU support not fully tested!\n", Prcb->VendorString);

    return Prcb->CpuVendor;
}

static struct
{
    ULONG64 Aperf;
    ULONG64 Mperf;
    ULONG Mhz;
} KiAmd64ClockSample[MAXIMUM_PROCESSORS];

static LONG KiAmd64AperfMperfState;

ULONG
NTAPI
{
    CPU_INFO CpuInfo;
    ULONG64 Aperf, Mperf, DeltaAperf, DeltaMperf;
    ULONG BaseMhz, Mhz;

    if (ProcessorNumber >= MAXIMUM_PROCESSORS)
    {
        return 0;
    }

    BaseMhz = KiProcessorBlock[ProcessorNumber]->MHz;

    if (KiAmd64AperfMperfState == 0)
    {
        KiCpuId(&CpuInfo, 0);
        if (CpuInfo.Eax >= 6)
        {
            KiCpuId(&CpuInfo, 6);
            KiAmd64AperfMperfState = (CpuInfo.Ecx & 1) ? 1 : -1;
        }
        else
        {
            KiAmd64AperfMperfState = -1;
        }
    }

    if ((KiAmd64AperfMperfState < 0) || (BaseMhz == 0))
    {
        return BaseMhz;
    }

    Mperf = __readmsr(MSR_IA32_MPERF);
    Aperf = __readmsr(MSR_IA32_APERF);

    DeltaAperf = Aperf - KiAmd64ClockSample[ProcessorNumber].Aperf;
    DeltaMperf = Mperf - KiAmd64ClockSample[ProcessorNumber].Mperf;

    if (DeltaMperf < 100000ULL)
    {
        Mhz = KiAmd64ClockSample[ProcessorNumber].Mhz;
        return Mhz ? Mhz : BaseMhz;
    }

    KiAmd64ClockSample[ProcessorNumber].Aperf = Aperf;
    KiAmd64ClockSample[ProcessorNumber].Mperf = Mperf;

    Mhz = (ULONG)(((ULONG64)BaseMhz * DeltaAperf + (DeltaMperf / 2)) / DeltaMperf);
    KiAmd64ClockSample[ProcessorNumber].Mhz = Mhz;
    return Mhz;
}

VOID
NTAPI
KiSetProcessorType(VOID)
{
    CPU_INFO CpuInfo;

    KiCpuId(&CpuInfo, 1);

    {
    }
    {
    }
    {
    }

}




ULONG64
NTAPI
KiGetFeatureBits(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    CPUID_VERSION_INFO_REGS VersionInfo;


    __cpuid(VersionInfo.AsInt32, CPUID_VERSION_INFO);

    Prcb->InitialApicId = (UCHAR)VersionInfo.Ebx.Bits.InitialLocalApicId;

    if (VersionInfo.Edx.Bits.HTT)
    {
        if (Prcb->LogicalProcessorsPerCore > 1)
            KiSMTProcessorsPresent = TRUE;
    }
    else
    {
        Prcb->LogicalProcessorsPerCore = 1;
    }








    {
        {
        }
    }
}

VOID
{
    ULONG64 FeatureBits = Prcb->FeatureBits | ((ULONG64)Prcb->FeatureBitsHigh << 32);
    CPU_INFO CpuInfo;

    if (Prcb->CpuVendor)
    {
        KiCpuId(&CpuInfo, 1);
    }

    {
    }
}

VOID
NTAPI
KiGetCacheInformation(VOID)
{
    PKIPCR Pcr = (PKIPCR)KeGetPcr();
    CPU_INFO CpuInfo;

    {
        case CPU_INTEL:
            KiCpuId(&CpuInfo, 0);
            break;

        case CPU_AMD:

            break;
    }
}

VOID
NTAPI
{
    __writecr0(ProcessorState->SpecialRegisters.Cr0);
    __writecr3(ProcessorState->SpecialRegisters.Cr3);
    __writecr4(ProcessorState->SpecialRegisters.Cr4);
    __writecr8(ProcessorState->SpecialRegisters.Cr8);

    __writedr(0, ProcessorState->SpecialRegisters.KernelDr0);
    __writedr(1, ProcessorState->SpecialRegisters.KernelDr1);
    __writedr(2, ProcessorState->SpecialRegisters.KernelDr2);
    __writedr(3, ProcessorState->SpecialRegisters.KernelDr3);
    __writedr(6, ProcessorState->SpecialRegisters.KernelDr6);
    __writedr(7, ProcessorState->SpecialRegisters.KernelDr7);

    __lgdt(&ProcessorState->SpecialRegisters.Gdtr.Limit);
    __lidt(&ProcessorState->SpecialRegisters.Idtr.Limit);

    _mm_setcsr(ProcessorState->SpecialRegisters.MxCsr);

    __writemsr(X86_MSR_GSBASE, ProcessorState->SpecialRegisters.MsrGsBase);
    __writemsr(X86_MSR_KERNEL_GSBASE, ProcessorState->SpecialRegisters.MsrGsSwap);
    __writemsr(X86_MSR_STAR, ProcessorState->SpecialRegisters.MsrStar);
    __writemsr(X86_MSR_LSTAR, ProcessorState->SpecialRegisters.MsrLStar);
    __writemsr(X86_MSR_CSTAR, ProcessorState->SpecialRegisters.MsrCStar);
    __writemsr(X86_MSR_SFMASK, ProcessorState->SpecialRegisters.MsrSyscallMask);
}

VOID
NTAPI
{
    ProcessorState->SpecialRegisters.Cr0 = __readcr0();
    ProcessorState->SpecialRegisters.Cr2 = __readcr2();
    ProcessorState->SpecialRegisters.Cr3 = __readcr3();
    ProcessorState->SpecialRegisters.Cr4 = __readcr4();
    ProcessorState->SpecialRegisters.Cr8 = __readcr8();

    ProcessorState->SpecialRegisters.KernelDr0 = __readdr(0);
    ProcessorState->SpecialRegisters.KernelDr1 = __readdr(1);
    ProcessorState->SpecialRegisters.KernelDr2 = __readdr(2);
    ProcessorState->SpecialRegisters.KernelDr3 = __readdr(3);
    ProcessorState->SpecialRegisters.KernelDr6 = __readdr(6);
    ProcessorState->SpecialRegisters.KernelDr7 = __readdr(7);

    __sgdt(&ProcessorState->SpecialRegisters.Gdtr.Limit);
    __sldt(&ProcessorState->SpecialRegisters.Ldtr);
    __str(&ProcessorState->SpecialRegisters.Tr);
    __sidt(&ProcessorState->SpecialRegisters.Idtr.Limit);

    ProcessorState->SpecialRegisters.MxCsr = _mm_getcsr();

    ProcessorState->SpecialRegisters.MsrGsBase = __readmsr(X86_MSR_GSBASE);
    ProcessorState->SpecialRegisters.MsrGsSwap = __readmsr(X86_MSR_KERNEL_GSBASE);
    ProcessorState->SpecialRegisters.MsrStar = __readmsr(X86_MSR_STAR);
    ProcessorState->SpecialRegisters.MsrLStar = __readmsr(X86_MSR_LSTAR);
    ProcessorState->SpecialRegisters.MsrCStar = __readmsr(X86_MSR_CSTAR);
    ProcessorState->SpecialRegisters.MsrSyscallMask = __readmsr(X86_MSR_SFMASK);
}

VOID
NTAPI
KiSaveProcessorState(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    Prcb->ProcessorState.ContextFrame.ContextFlags = CONTEXT_ALL;
    KeTrapFrameToContext(TrapFrame, ExceptionFrame, &Prcb->ProcessorState.ContextFrame);

    KiSaveProcessorControlState(&Prcb->ProcessorState);

    if (TrapFrame->PreviousMode == KernelMode)
    {
        Prcb->ProcessorState.ContextFrame.Dr0 = Prcb->ProcessorState.SpecialRegisters.KernelDr0;
        Prcb->ProcessorState.ContextFrame.Dr1 = Prcb->ProcessorState.SpecialRegisters.KernelDr1;
        Prcb->ProcessorState.ContextFrame.Dr2 = Prcb->ProcessorState.SpecialRegisters.KernelDr2;
        Prcb->ProcessorState.ContextFrame.Dr3 = Prcb->ProcessorState.SpecialRegisters.KernelDr3;
        Prcb->ProcessorState.ContextFrame.Dr6 = Prcb->ProcessorState.SpecialRegisters.KernelDr6;
        Prcb->ProcessorState.ContextFrame.Dr7 = Prcb->ProcessorState.SpecialRegisters.KernelDr7;
    }
}

VOID
NTAPI
KiRestoreProcessorState(
    _Out_ PKTRAP_FRAME TrapFrame,
    _Out_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    KeContextToTrapFrame(&Prcb->ProcessorState.ContextFrame,
                         ExceptionFrame,
                         TrapFrame,
                         CONTEXT_ALL,
                         TrapFrame->PreviousMode);

    if (TrapFrame->PreviousMode == KernelMode)
    {
        Prcb->ProcessorState.SpecialRegisters.KernelDr0 = Prcb->ProcessorState.ContextFrame.Dr0;
        Prcb->ProcessorState.SpecialRegisters.KernelDr1 = Prcb->ProcessorState.ContextFrame.Dr1;
        Prcb->ProcessorState.SpecialRegisters.KernelDr2 = Prcb->ProcessorState.ContextFrame.Dr2;
        Prcb->ProcessorState.SpecialRegisters.KernelDr3 = Prcb->ProcessorState.ContextFrame.Dr3;
        Prcb->ProcessorState.SpecialRegisters.KernelDr6 = Prcb->ProcessorState.ContextFrame.Dr6;
        Prcb->ProcessorState.SpecialRegisters.KernelDr7 = Prcb->ProcessorState.ContextFrame.Dr7;
    }

    KiRestoreProcessorControlState(&Prcb->ProcessorState);
}

NTSTATUS
NTAPI
{
    UNREFERENCED_PARAMETER(FloatingState);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
{
    UNREFERENCED_PARAMETER(FloatingState);
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
KeInvalidateAllCaches(VOID)
{
    __wbinvd();
    return TRUE;
}

VOID
FASTCALL
KeInvalidateRangeAllCaches(
    _In_ PVOID BaseAddress,
    _In_ ULONG Length)
{
    CPU_INFO CpuInfo;
    ULONG_PTR Address, EndAddress;
    ULONG CacheLineSize;

    if (Length == 0)
        return;

    CacheLineSize = KeLargestCacheLine ? KeLargestCacheLine : 64;
    Address = (ULONG_PTR)BaseAddress & ~((ULONG_PTR)CacheLineSize - 1);
    EndAddress = (ULONG_PTR)BaseAddress + Length;

    KiCpuId(&CpuInfo, 1);
    if (!(CpuInfo.Edx & (1UL << 19)))
    {
        __wbinvd();
        return;
    }

    _mm_mfence();
    do
    {
        _mm_clflush((PVOID)Address);
        Address += CacheLineSize;
    } while ((Address < EndAddress) && (Address != 0));
    _mm_mfence();
}

ULONG
NTAPI
KeGetRecommendedSharedDataAlignment(VOID)
{
    return KeLargestCacheLine;
}

VOID
__cdecl
{
    RtlCaptureContext(&State->ContextFrame);

    KiSaveProcessorControlState(State);
}

VOID
NTAPI
{
    KiDmaIoCoherency = Coherency;
}

#undef KeFlushIoBuffers
VOID
NTAPI
KeFlushIoBuffers(
    _Inout_ PMDL Mdl,
    _In_ BOOLEAN ReadOperation,
    _In_ BOOLEAN DmaOperation)
{
    UNREFERENCED_PARAMETER(Mdl);
    UNREFERENCED_PARAMETER(ReadOperation);
    UNREFERENCED_PARAMETER(DmaOperation);
}

VOID
KiDisplayBugCheckRegisterState(
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context)
{
    CHAR Line[128];

    if (TrapFrame != NULL)
    {
        RtlStringCbPrintfA(Line, sizeof(Line), "RIP=%016I64x RSP=%016I64x RBP=%016I64x EFL=%08lx\r\n", TrapFrame->Rip, TrapFrame->Rsp, TrapFrame->Rbp, TrapFrame->EFlags);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "RAX=%016I64x RBX=%016I64x RCX=%016I64x\r\n", TrapFrame->Rax, TrapFrame->Rbx, TrapFrame->Rcx);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "RDX=%016I64x RSI=%016I64x RDI=%016I64x\r\n", TrapFrame->Rdx, TrapFrame->Rsi, TrapFrame->Rdi);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "R8 =%016I64x R9 =%016I64x R10=%016I64x\r\n", TrapFrame->R8, TrapFrame->R9, TrapFrame->R10);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "R11=%016I64x FAR=%016I64x ERR=%016I64x\r\n", TrapFrame->R11, TrapFrame->FaultAddress, TrapFrame->ErrorCode);
        KiDisplayAndLogBugCheckString(Line);
    }
    else
    {
        RtlStringCbPrintfA(Line, sizeof(Line), "RIP=%016I64x RSP=%016I64x RBP=%016I64x EFL=%08lx\r\n", Context->Rip, Context->Rsp, Context->Rbp, Context->EFlags);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "RAX=%016I64x RBX=%016I64x RCX=%016I64x\r\n", Context->Rax, Context->Rbx, Context->Rcx);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "RDX=%016I64x RSI=%016I64x RDI=%016I64x\r\n", Context->Rdx, Context->Rsi, Context->Rdi);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "R8 =%016I64x R9 =%016I64x R10=%016I64x\r\n", Context->R8, Context->R9, Context->R10);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "R11=%016I64x R12=%016I64x R13=%016I64x\r\n", Context->R11, Context->R12, Context->R13);
        KiDisplayAndLogBugCheckString(Line);
        RtlStringCbPrintfA(Line, sizeof(Line), "R14=%016I64x R15=%016I64x\r\n", Context->R14, Context->R15);
        KiDisplayAndLogBugCheckString(Line);
    }
}

CODE_SEG("INIT")
VOID
NTAPI
KiQueryProcessorEfficiencyClasses(
    _Out_writes_all_(MAXIMUM_PROCESSORS) PUCHAR EfficiencyClasses)
{
    /* TODO: report the core types of hybrid processors as efficiency classes */
    RtlZeroMemory(EfficiencyClasses, MAXIMUM_PROCESSORS);
}
