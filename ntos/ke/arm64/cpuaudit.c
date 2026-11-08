/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Opt-in ARM64 CPU scheduling audit capture
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/cpuaudit.h>

volatile LONG KiCpuAuditActive;
CPU_AUDIT_IRQ KiCpuAuditIrqs[CPU_AUDIT_CPUS][CPU_AUDIT_IRQS];
static volatile LONG AuditBusy;
static ULONG AuditPid;
static HANDLE AuditOwner;
static ULONGLONG AuditFrequency;
static ULONG EventTypes[6] = {0x11, 0x08, 0x03, 0x17, 0x05, 0x24};
typedef struct _AUDIT_CPU {
    volatile ULONG Producer, Consumer;
    ULONG Dropped, Error, Owned;
    ULONGLONG Pmcr, Selector, IntEnable, Types[6], Counters[6], Last[6], LastTick;
    CPU_AUDIT_EVENT Ring[CPU_AUDIT_RING];
} AUDIT_CPU;
static AUDIT_CPU AuditCpus[CPU_AUDIT_CPUS];

ULONGLONG KiCpuAuditClock(VOID)
{
    ULONGLONG Value;
    ARM64_ISB();
    Value = ARM64_READ_SYSREG(CNTVCT_EL0);
    return Value;
}
static ULONG AuditThreadPid(PKTHREAD Thread)
{
    return HandleToUlong(PsGetProcessId((PEPROCESS)Thread->ApcState.Process));
}
static VOID AuditRecord(CPU_AUDIT_EVENT *Event)
{
    AUDIT_CPU *Cpu = &AuditCpus[Event->Cpu];
    ULONG Producer = Cpu->Producer;
#if defined(_MSC_VER) && !defined(__clang__)
    if (Producer - ReadULongAcquire(&Cpu->Consumer) >= CPU_AUDIT_RING)
#else
    if (Producer - __atomic_load_n(&Cpu->Consumer, __ATOMIC_ACQUIRE) >= CPU_AUDIT_RING)
#endif
    { Cpu->Dropped++; return; }
    Cpu->Ring[Producer % CPU_AUDIT_RING] = *Event;
#if defined(_MSC_VER) && !defined(__clang__)
    WriteULongRelease(&Cpu->Producer, Producer + 1);
#else
    __atomic_store_n(&Cpu->Producer, Producer + 1, __ATOMIC_RELEASE);
#endif
}
static VOID AuditReadCounters(CPU_AUDIT_EVENT *Event)
{
    AUDIT_CPU *Cpu = &AuditCpus[Event->Cpu];
    ULONG i;
    if (!Cpu->Owned) return;
    for (i = 0; i < 6; ++i)
    {
        ULONGLONG Value;
        ARM64_WRITE_SYSREG(PMSELR_EL0, (ULONGLONG)i);
        ARM64_ISB();
        Value = ARM64_READ_SYSREG(PMXEVCNTR_EL0);
        Event->Values[i] = (ULONG)(Value - Cpu->Last[i]);
        Cpu->Last[i] = Value;
    }
}
static VOID AuditCapture(ULONG Kind, PKTHREAD Old, PKTHREAD New, ULONG_PTR Pc)
{
    CPU_AUDIT_EVENT Event = {0};
    ULONGLONG Daif;
    ULONG Cpu;
    Daif = ARM64_READ_SYSREG(DAIF);
    ARM64_DAIFSET(3);
    Cpu = KeGetCurrentProcessorNumber();
    if (!KiCpuAuditActive || Cpu >= CPU_AUDIT_CPUS) goto Done;
    Event.Tick = KiCpuAuditClock();
    Event.Cpu = Cpu; Event.Kind = Kind; Event.Pc = Pc;
    Event.Tid = (ULONG_PTR)PsGetThreadId((PETHREAD)Old);
    Event.Pid = AuditThreadPid(Old); Event.State = Old->State; Event.Reason = Old->WaitReason;
    if (New) { Event.OtherTid = HandleToUlong(PsGetThreadId((PETHREAD)New)); Event.OtherPid = AuditThreadPid(New); }
    AuditReadCounters(&Event);
    Event.Aux = (ULONG)(KiCpuAuditClock() - Event.Tick);
    AuditCpus[Cpu].LastTick = Event.Tick;
    AuditRecord(&Event);
Done:
    ARM64_WRITE_SYSREG(DAIF, Daif);
}
VOID KiCpuAuditTick(ULONG_PTR Pc)
{
    ULONG Cpu;
    if (!KiCpuAuditActive) return;
    Cpu = KeGetCurrentProcessorNumber();
    if (Cpu < CPU_AUDIT_CPUS && KiCpuAuditClock() - AuditCpus[Cpu].LastTick >= AuditFrequency / 100)
        AuditCapture(CPU_AUDIT_SAMPLE, KeGetCurrentThread(), NULL, Pc);
}
VOID KiCpuAuditSwitch(PKTHREAD Old, PKTHREAD New)
{
    if (KiCpuAuditActive) AuditCapture(CPU_AUDIT_SWITCH, Old, New, 0);
}
static VOID AuditReady(PKTHREAD Thread, ULONG Kind, ULONG Processor, KAFFINITY IdleRequest)
{
    CPU_AUDIT_EVENT Event = {0};
    ULONGLONG Daif;
    if (!KiCpuAuditActive || AuditThreadPid(Thread) != AuditPid) return;
    Daif = ARM64_READ_SYSREG(DAIF);
    ARM64_DAIFSET(3);
    Event.Cpu = KeGetCurrentProcessorNumber();
    if (KiCpuAuditActive && Event.Cpu < CPU_AUDIT_CPUS)
    {
        Event.Tick = KiCpuAuditClock(); Event.Kind = Kind;
        Event.Pc = (ULONG_PTR)Thread; Event.State = Thread->NextProcessor;
        Event.Reason = Thread->IdealProcessor; Event.Aux = Processor;
        Event.Values[0] = Thread->Priority; Event.Values[1] = IdleRequest;
        Event.Pid = AuditPid; Event.Tid = (ULONG_PTR)PsGetThreadId((PETHREAD)Thread);
        AuditRecord(&Event);
    }
    ARM64_WRITE_SYSREG(DAIF, Daif);
}
VOID KiCpuAuditReady(PKTHREAD Thread)
{
    AuditReady(Thread, CPU_AUDIT_READY, MAXULONG, 0);
}
VOID KiCpuAuditPlace(PKTHREAD Thread, ULONG Processor, KAFFINITY IdleRequest)
{
    AuditReady(Thread, CPU_AUDIT_PLACE, Processor, IdleRequest);
}
VOID KiCpuAuditEvent(ULONG Kind, ULONG_PTR Pc, ULONGLONG A, ULONGLONG B)
{
    CPU_AUDIT_EVENT Event = {0};
    ULONGLONG Daif;
    if (!KiCpuAuditActive) return;
    Daif = ARM64_READ_SYSREG(DAIF);
    ARM64_DAIFSET(3);
    Event.Cpu = KeGetCurrentProcessorNumber();
    if (KiCpuAuditActive && Event.Cpu < CPU_AUDIT_CPUS)
    {
        Event.Tick = KiCpuAuditClock(); Event.Kind = Kind; Event.Pc = Pc;
        Event.Tid = (ULONG_PTR)PsGetThreadId(PsGetCurrentThread());
        Event.Pid = HandleToUlong(PsGetCurrentProcessId());
        if (Kind != CPU_AUDIT_TIMES || Event.Pid == AuditPid)
        {
            Event.Values[0] = A; Event.Values[1] = B;
            AuditRecord(&Event);
        }
    }
    ARM64_WRITE_SYSREG(DAIF, Daif);
}
VOID KiCpuAuditProcessTimes(VOID)
{
    CPU_AUDIT_EVENT Event = {0};
    PKTRAP_FRAME Frame;
    ULONG_PTR Fp;
    ULONGLONG Daif;
    ULONG i;
    if (!KiCpuAuditActive || HandleToUlong(PsGetCurrentProcessId()) != AuditPid) return;
    Frame = KeGetCurrentThread()->TrapFrame;
    if (!Frame || ExGetPreviousMode() != UserMode) return;
    Event.Tick = KiCpuAuditClock(); Event.Kind = CPU_AUDIT_TIMES;
    Event.Pid = AuditPid; Event.Tid = (ULONG_PTR)PsGetThreadId(PsGetCurrentThread());
    Event.Pc = Frame->Lr; Fp = Frame->Fp;
    _SEH2_TRY
    {
        for (i = 0; i < 6; ++i)
        {
            ULONG_PTR *Pair = (ULONG_PTR *)Fp, Next;
            ProbeForRead(Pair, 16, 16);
            Next = Pair[0]; Event.Values[i] = Pair[1];
            if (Next <= Fp || Next - Fp > 0x100000) break;
            Fp = Next;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { Event.Aux = 1; }
    _SEH2_END;
    Daif = ARM64_READ_SYSREG(DAIF);
    ARM64_DAIFSET(3);
    Event.Cpu = KeGetCurrentProcessorNumber();
    if (KiCpuAuditActive && Event.Cpu < CPU_AUDIT_CPUS) AuditRecord(&Event);
    ARM64_WRITE_SYSREG(DAIF, Daif);
}
static ULONG_PTR NTAPI AuditPmu(ULONG_PTR Operation)
{
    ULONG Number = KeGetCurrentProcessorNumber(), i;
    AUDIT_CPU *Cpu;
    ULONGLONG Enabled, Pmcr, Pmceid, Pmceid1, Dfr;
    CPU_AUDIT_EVENT Event = {0};
    if (Number >= CPU_AUDIT_CPUS) return 0;
    Cpu = &AuditCpus[Number];
    if (Operation == CPU_AUDIT_START)
    {
        Dfr = ARM64_READ_SYSREG(ID_AA64DFR0_EL1);
        if (((Dfr >> 8) & 15) == 0 || ((Dfr >> 8) & 15) == 15) { Cpu->Error = 1; return 0; }
        Pmcr = ARM64_READ_SYSREG(PMCR_EL0);
        Enabled = ARM64_READ_SYSREG(PMCNTENSET_EL0);
        Pmceid = ARM64_READ_SYSREG(PMCEID0_EL0);
        Pmceid1 = ARM64_READ_SYSREG(PMCEID1_EL0);
        Pmceid = (ULONG)Pmceid | ((ULONGLONG)(ULONG)Pmceid1 << 32);
        if (((Pmcr >> 11) & 31) < 6 || (Enabled & 0x7fffffff)) { Cpu->Error = 2; return 0; }
        Cpu->Pmcr = Pmcr;
        Cpu->Selector = ARM64_READ_SYSREG(PMSELR_EL0);
        Cpu->IntEnable = ARM64_READ_SYSREG(PMINTENSET_EL1);
        ARM64_WRITE_SYSREG(PMINTENCLR_EL1, 63ULL);
        for (i = 0; i < 6; ++i)
        {
            ULONGLONG Type = EventTypes[i] | (i ? (1ULL << 31) : 0);
            if (!(Pmceid & (1ULL << EventTypes[i]))) Cpu->Error |= 1UL << (8 + i);
            ARM64_WRITE_SYSREG(PMSELR_EL0, (ULONGLONG)i);
            ARM64_ISB();
            Cpu->Types[i] = ARM64_READ_SYSREG(PMXEVTYPER_EL0);
            Cpu->Counters[i] = ARM64_READ_SYSREG(PMXEVCNTR_EL0);
#if defined(_MSC_VER) && !defined(__clang__)
            ARM64_WRITE_SYSREG(PMXEVTYPER_EL0, Type);
            ARM64_WRITE_SYSREG(PMXEVCNTR_EL0, 0);
#else
            __asm__ __volatile__("msr pmxevtyper_el0, %0; msr pmxevcntr_el0, xzr" :: "r"(Type) : "memory");
#endif
            Cpu->Last[i] = 0;
        }
        ARM64_WRITE_SYSREG(PMOVSCLR_EL0, 63ULL);
        ARM64_WRITE_SYSREG(PMCNTENSET_EL0, 63ULL);
        Pmcr = (Pmcr & ~6ULL) | 1;
        ARM64_WRITE_SYSREG(PMCR_EL0, Pmcr);
        ARM64_ISB();
        Cpu->Owned = 1;
        Cpu->LastTick = KiCpuAuditClock();
        Event.Tick = Cpu->LastTick; Event.Kind = CPU_AUDIT_CONFIG; Event.Cpu = Number;
        Event.Values[0] = Pmcr; Event.Values[1] = Enabled; Event.Values[2] = Pmceid;
        Event.Values[3] = EventTypes[5];
        Event.Aux = Cpu->Error;
        AuditRecord(&Event);
    }
    else if (Cpu->Owned)
    {
        Event.Tick = KiCpuAuditClock(); Event.Kind = CPU_AUDIT_SAMPLE; Event.Cpu = Number;
        Event.Pid = HandleToUlong(PsGetCurrentProcessId()); Event.Tid = (ULONG_PTR)PsGetThreadId(PsGetCurrentThread());
        AuditReadCounters(&Event); AuditRecord(&Event);
        ARM64_WRITE_SYSREG(PMCNTENCLR_EL0, 63ULL);
        ARM64_ISB();
        for (i = 0; i < 6; ++i)
        {
            ARM64_WRITE_SYSREG(PMSELR_EL0, (ULONGLONG)i);
            ARM64_ISB();
            ARM64_WRITE_SYSREG(PMXEVTYPER_EL0, Cpu->Types[i]);
            ARM64_WRITE_SYSREG(PMXEVCNTR_EL0, Cpu->Counters[i]);
        }
        ARM64_WRITE_SYSREG(PMOVSCLR_EL0, 63ULL);
        ARM64_WRITE_SYSREG(PMINTENCLR_EL1, 63ULL);
        ARM64_WRITE_SYSREG(PMINTENSET_EL1, Cpu->IntEnable & 63);
        ARM64_WRITE_SYSREG(PMSELR_EL0, Cpu->Selector);
        ARM64_WRITE_SYSREG(PMCR_EL0, Cpu->Pmcr & ~6ULL);
        ARM64_ISB();
        Cpu->Owned = 0;
    }
    return 0;
}
NTSTATUS KiCpuAuditControl(PVOID Buffer, ULONG Length, KPROCESSOR_MODE Mode)
{
    CPU_AUDIT_PACKET *Request = Buffer, *Packet;
    ULONG Command, Number, Limit, Consumer, Producer;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Held = FALSE;
    if (Length != sizeof(*Request)) return STATUS_INFO_LENGTH_MISMATCH;
    if (!SeSinglePrivilegeCheck(SeDebugPrivilege, Mode)) return STATUS_PRIVILEGE_NOT_HELD;
    Packet = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Packet), 'aUpC');
    if (!Packet) return STATUS_INSUFFICIENT_RESOURCES;
    _SEH2_TRY
    {
        if (Mode != KernelMode) ProbeForWrite(Buffer, Length, sizeof(ULONG));
        RtlCopyMemory(Packet, Request, sizeof(*Packet));
        if (Packet->Version != CPU_AUDIT_VERSION) { Status = STATUS_REVISION_MISMATCH; _SEH2_LEAVE; }
        if (InterlockedCompareExchange(&AuditBusy, 1, 0)) { Status = STATUS_DEVICE_BUSY; _SEH2_LEAVE; }
        Held = TRUE;
        Command = Packet->Command;
        if (Command == CPU_AUDIT_START)
        {
            if (KiCpuAuditActive || (ULONG)KeNumberProcessors != CPU_AUDIT_CPUS)
                Status = STATUS_DEVICE_BUSY;
            else
            {
                RtlZeroMemory(AuditCpus, sizeof(AuditCpus));
                AuditPid = Packet->Pid; AuditOwner = PsGetCurrentProcessId();
                AuditFrequency = ARM64_READ_SYSREG(CNTFRQ_EL0);
                KeIpiGenericCall(AuditPmu, CPU_AUDIT_START);
                InterlockedExchange(&KiCpuAuditActive, 1);
            }
        }
        else if (AuditOwner != PsGetCurrentProcessId()) Status = STATUS_ACCESS_DENIED;
        else if (Command == CPU_AUDIT_STOP)
        {
            InterlockedExchange(&KiCpuAuditActive, 0);
            KeIpiGenericCall(AuditPmu, CPU_AUDIT_STOP);
        }
        else if (Command != CPU_AUDIT_DRAIN && Command != CPU_AUDIT_INTERRUPTS)
            Status = STATUS_INVALID_PARAMETER;
        Packet->Count = Packet->Dropped = Packet->Errors = 0;
        Packet->Active = KiCpuAuditActive; Packet->Frequency = AuditFrequency;
        if (NT_SUCCESS(Status) && Command == CPU_AUDIT_INTERRUPTS)
        {
            if (Packet->Cpu >= CPU_AUDIT_CPUS) Status = STATUS_INVALID_PARAMETER;
            else { RtlCopyMemory(Packet->Data.Irqs, KiCpuAuditIrqs[Packet->Cpu], sizeof(Packet->Data.Irqs)); Packet->Count = CPU_AUDIT_IRQS; }
        }
        else if (NT_SUCCESS(Status))
        {
            for (Number = 0; Number < CPU_AUDIT_CPUS; ++Number)
            {
                AUDIT_CPU *Cpu = &AuditCpus[Number];
                Packet->Dropped += Cpu->Dropped; Packet->Errors |= Cpu->Error;
                Consumer = Cpu->Consumer;
#if defined(_MSC_VER) && !defined(__clang__)
                Producer = ReadULongAcquire(&Cpu->Producer);
#else
                Producer = __atomic_load_n(&Cpu->Producer, __ATOMIC_ACQUIRE);
#endif
                Limit = min(Producer - Consumer, CPU_AUDIT_EVENTS - Packet->Count);
                while (Limit--) Packet->Data.Events[Packet->Count++] = Cpu->Ring[Consumer++ % CPU_AUDIT_RING];
#if defined(_MSC_VER) && !defined(__clang__)
                WriteULongRelease(&Cpu->Consumer, Consumer);
#else
                __atomic_store_n(&Cpu->Consumer, Consumer, __ATOMIC_RELEASE);
#endif
            }
        }
        InterlockedExchange(&AuditBusy, 0); Held = FALSE;
        RtlCopyMemory(Request, Packet, sizeof(*Packet));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) { Status = _SEH2_GetExceptionCode(); }
    _SEH2_END;
    if (Held) InterlockedExchange(&AuditBusy, 0);
    ExFreePoolWithTag(Packet, 'aUpC');
    return Status;
}
