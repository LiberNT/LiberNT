/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 trap dispatch: TLB refill, page faults, NT
 *              exceptions and the non-returning stop path
 *
 * Dispatch is driven by ESTAT alone, because this port runs with ECFG.VS=0
 * ([V1-6.3.1]: with VS=0 all general exceptions and all line interrupts
 * share one entry and software tells them apart by Ecode and IS):
 *
 *   Ecode 0            one of the 13 line interrupts; interrupt.c owns it
 *   Ecode 0x1-0x7      memory faults, dispatched to MmAccessFault
 *   Ecode 0xb          syscall entry (usercall/syscall path)
 *   everything else    an NT exception record for KiDispatchException
 *
 * The TLB has no hardware refill on this architecture ([V1-6.3.4]): a miss
 * raises the TLB refill exception at CSR.TLBRENTRY, which runs in direct
 * address mode. KiLa64TlbRefillHandler fills the pair from the page tables
 * when the mapping exists and installs an invalid pair when it does not, so
 * the retried access reports PIL/PIS/PIF - the only way LoongArch64 tells
 * software what kind of access missed ([V1-7.4.6] Table 21). That keeps
 * every presence decision in one place: this file never invents a fault
 * code, it forwards the architecturally reported one.
 */

#include <ntoskrnl.h>
#include "la64ke.h"
#define NDEBUG
#include <debug.h>
#ifdef KDBG
#include <kdbg/kdb.h>
#endif

#ifndef EXCEPTION_SOFTWARE_ORIGINATE
#define EXCEPTION_SOFTWARE_ORIGINATE 0x80
#endif

/* One TLB entry covers an adjacent even/odd pair of 4 KiB pages
 * ([V1-5.4.2]: VPPN is the virtual page number with the selector bit
 * dropped), so a refill works on the 8 KiB pair, not on the single page. */
#define KI_LA64_TLB_PAIR_MASK   0x1FFFULL
#define KI_LA64_TLB_VPPN_MASK   0x7FFFFFFFFULL   /* VA[47:13], VALEN = 48 */

/* Repeated-fault detector for a leaf that already permits the access. */
typedef struct _KI_LA64_SPURIOUS_FAULT
{
    ULONG_PTR Pc;
    ULONG_PTR Address;
    ULONG Count;
} KI_LA64_SPURIOUS_FAULT;

static KI_LA64_SPURIOUS_FAULT KiLa64SpuriousFault[MAXIMUM_PROCESSORS];

static const CHAR *const KiLa64RegisterNames[32] =
{
    "zero", "ra", "tp", "sp", "a0", "a1", "a2", "a3",
    "a4", "a5", "a6", "a7", "t0", "t1", "t2", "t3",
    "t4", "t5", "t6", "t7", "t8", "u0", "fp", "s0",
    "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8"
};

static
const CHAR *
KiLa64DecodeEcode(
    _In_ ULONG Ecode)
{
    switch (Ecode)
    {
        case LA64_ECODE_INT: return "interrupt";
        case LA64_ECODE_PIL: return "page invalid, load";
        case LA64_ECODE_PIS: return "page invalid, store";
        case LA64_ECODE_PIF: return "page invalid, fetch";
        case LA64_ECODE_PME: return "page modification";
        case LA64_ECODE_PNR: return "page non-readable";
        case LA64_ECODE_PNX: return "page non-executable";
        case LA64_ECODE_PPI: return "page privilege illegal";
        case LA64_ECODE_ADEF: return "address error";
        case LA64_ECODE_ALE: return "alignment fault";
        case LA64_ECODE_BCE: return "bound check";
        case LA64_ECODE_SYS: return "syscall";
        case LA64_ECODE_BRK: return "breakpoint";
        case LA64_ECODE_INE: return "instruction non-defined";
        case LA64_ECODE_IPE: return "instruction privilege error";
        case LA64_ECODE_FPD: return "floating point disabled";
        case LA64_ECODE_SXD: return "LSX disabled";
        case LA64_ECODE_ASXD: return "LASX disabled";
        case LA64_ECODE_FPE: return "floating point error";
        case LA64_ECODE_WPE: return "watchpoint";
        case LA64_ECODE_BTD: return "LBT disabled";
        case LA64_ECODE_BTE: return "binary translation";
        case LA64_ECODE_GSPR: return "guest sensitive privileged resource";
        case LA64_ECODE_HVC: return "hypervisor call";
        default: return "unknown";
    }
}

static
VOID
KiLa64DumpTrapFrame(
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG Index;
    ULONG Ecode = KiLa64EstatEcode(TrapFrame->Estat);

    DbgPrint("\n*** Unexpected LoongArch64 trap: %s (Ecode %lx, EsubCode %lx)\n",
             KiLa64DecodeEcode(Ecode),
             Ecode,
             KiLa64EstatEsubCode(TrapFrame->Estat));
    DbgPrint("pc   %p  badv %p\nestat %lx  crmd %lx  prmd %lx\n",
             (PVOID)(ULONG_PTR)TrapFrame->Context.Pc,
             (PVOID)(ULONG_PTR)TrapFrame->Badv,
             TrapFrame->Estat, TrapFrame->Crmd, TrapFrame->Prmd);
    DbgPrint("euen %lx  ecfg %lx  vector %lx  previous irql %u\n",
             TrapFrame->Euen, TrapFrame->Ecfg, TrapFrame->Spare,
             TrapFrame->PreviousIrql);

    for (Index = 0; Index < 32; Index += 4)
    {
        DbgPrint("%-4s %p  %-4s %p  %-4s %p  %-4s %p\n",
                 KiLa64RegisterNames[Index], (PVOID)(ULONG_PTR)TrapFrame->Context.R[Index],
                 KiLa64RegisterNames[Index + 1], (PVOID)(ULONG_PTR)TrapFrame->Context.R[Index + 1],
                 KiLa64RegisterNames[Index + 2], (PVOID)(ULONG_PTR)TrapFrame->Context.R[Index + 2],
                 KiLa64RegisterNames[Index + 3], (PVOID)(ULONG_PTR)TrapFrame->Context.R[Index + 3]);
    }
}

DECLSPEC_NORETURN
VOID
NTAPI
KiLa64TrapStop(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();

    /* Describe the trap before the bugcheck path, which may not be able to
     * print register detail. A frame captured by the prologue's panic path
     * (Spare == 2) is already linked to nothing and must not be unlinked. */
    KiLa64DumpTrapFrame(TrapFrame);
    if (TrapFrame->Spare != 2)
    {
        TrapFrame->PreviousTrapFrame = Thread->TrapFrame;
        Thread->TrapFrame = TrapFrame;
    }
    KeBugCheckEx(UNEXPECTED_KERNEL_MODE_TRAP,
                 TrapFrame->Estat,
                 TrapFrame->Context.Pc,
                 TrapFrame->Badv,
                 (ULONG_PTR)TrapFrame);
}

/* ---------------------------------------------------------------------------
 * TLB refill, [V1-6.3.4]
 *
 * Entered from KiLa64TlbRefillEntry with CRMD.DA=1 and CRMD.PG=0, so every
 * address this function forms is a direct-map address whose low PALEN bits
 * are the physical address ([V1-2.1.7]); that is exactly how the page-table
 * walk and the trap frame are addressed, which is why the same C code is
 * correct in both modes.
 *
 * A PTE and a TLBELO share the position of V, D, PLV, MAT, G, the PPN, NR, NX
 * and RPLV ([V1-7.5.3] versus [V1-5.4.5]); the walk-only bits P (7) and W (8)
 * and the software window 11:9 do not exist in a TLB entry, so they are
 * cleared instead of copied.
 * ------------------------------------------------------------------------ */
static
ULONG64
KiLa64PteToTlbLo(
    _In_ ULONG64 Pte)
{
    return Pte & ~(ULONG64)(MI_LA64_PTE_PRESENT | MI_LA64_PTE_WRITE |
                            MI_LA64_PTE_SOFTWARE_MASK);
}

VOID
NTAPI
KiLa64TlbRefillHandler(
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG_PTR FaultAddress = (ULONG_PTR)TrapFrame->Badv;
    ULONG_PTR PairBase = FaultAddress & ~KI_LA64_TLB_PAIR_MASK;
    MI_LA64_PAGE_WALK EvenWalk, OddWalk;
    ULONG64 Lo0 = 0;
    ULONG64 Lo1 = 0;
    ULONG64 Vppn;

    if (NT_SUCCESS(MiLa64WalkCurrentPageTables((PVOID)PairBase, &EvenWalk)) &&
        (EvenWalk.Value.u.Long & MI_LA64_PTE_VALID))
    {
        Lo0 = KiLa64PteToTlbLo(EvenWalk.Value.u.Long);
    }

    if (NT_SUCCESS(MiLa64WalkCurrentPageTables((PVOID)(PairBase + PAGE_SIZE), &OddWalk)) &&
        (OddWalk.Value.u.Long & MI_LA64_PTE_VALID))
    {
        Lo1 = KiLa64PteToTlbLo(OddWalk.Value.u.Long);
    }

    /* Install the pair either way. When neither half is present the entry
     * still MATCHES the address, so the retried access raises PIL/PIS/PIF -
     * the architectural statement of "the mapping is absent" ([V1-7.4.6]) -
     * instead of another refill, which is what would loop forever. */
    Vppn = ((ULONG64)FaultAddress >> PAGE_SHIFT) & KI_LA64_TLB_VPPN_MASK;
    KiLa64FillTlbPair(Vppn, Lo0, Lo1);
}

/* ---------------------------------------------------------------------------
 * Memory faults
 * ------------------------------------------------------------------------ */

/* Does a valid leaf grant this access to the trapping mode? Mirrors the
 * permission check the hardware performs, so a fault that arrives on a leaf
 * that already permits the access must be a stale translation. */
static
BOOLEAN
KiLa64LeafPermitsAccess(
    _In_ ULONG64 Leaf,
    _In_ ULONG Ecode)
{
    if (!(Leaf & MI_LA64_PTE_VALID))
        return FALSE;

    /* NR (bit 61) denies reads and writes; NX (bit 62) denies fetches,
     * [V1-7.5.3]. */
    switch (Ecode)
    {
        case LA64_ECODE_PIF:
            return (Leaf & MI_LA64_PTE_NX) == 0;
        case LA64_ECODE_PIS:
        case LA64_ECODE_PME:
            return ((Leaf & MI_LA64_PTE_NR) == 0) &&
                   ((Leaf & MI_LA64_PTE_DIRTY) != 0) &&
                   ((Leaf & MI_LA64_PTE_WRITE) != 0);
        default:
            return (Leaf & MI_LA64_PTE_NR) == 0;
    }
}

static
VOID
KiLa64MemoryFault(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Ecode,
    _In_ KPROCESSOR_MODE Mode)
{
    KI_LA64_SPURIOUS_FAULT *Spurious = &KiLa64SpuriousFault[KeGetCurrentPrcb()->Number];
    PVOID Address = (PVOID)(ULONG_PTR)TrapFrame->Badv;
    KIRQL Irql = KeGetCurrentIrql();
    MI_LA64_PAGE_WALK Walk;
    EXCEPTION_RECORD Record;
    ULONG FaultCode;
    NTSTATUS Status;

    /* MI_LA64_FAULT_* are this kernel's own codes, never raw ESTAT values
     * (internal/loongarch64/mm.h). */
    switch (Ecode)
    {
        case LA64_ECODE_PIS:
        case LA64_ECODE_PME:
            FaultCode = MI_LA64_FAULT_WRITE;
            break;
        case LA64_ECODE_PIF:
            FaultCode = MI_LA64_FAULT_EXECUTE;
            break;
        case LA64_ECODE_PNR:
        case LA64_ECODE_PNX:
        case LA64_ECODE_PPI:
        case LA64_ECODE_PIL:
        default:
            FaultCode = 0;
            break;
    }

    /* Present means a valid leaf exists: a protection fault, not demand. */
    if (NT_SUCCESS(MiLa64WalkCurrentPageTables(Address, &Walk)) &&
        (Walk.Value.u.Long & MI_LA64_PTE_VALID))
    {
        FaultCode |= MI_LA64_FAULT_PRESENT;

        if (KiLa64LeafPermitsAccess(Walk.Value.u.Long, Ecode))
        {
            /* The leaf permits the access, so this processor used a stale
             * translation. Drop the entry and retry; only a kernel access to
             * system space that keeps repeating cannot be a race. */
            if ((Spurious->Pc != TrapFrame->Context.Pc) ||
                (Spurious->Address != (ULONG_PTR)Address))
            {
                Spurious->Pc = TrapFrame->Context.Pc;
                Spurious->Address = (ULONG_PTR)Address;
                Spurious->Count = 0;
            }
            if ((Mode == UserMode) ||
                ((ULONG_PTR)Address < (ULONG_PTR)MmSystemRangeStart) ||
                (++Spurious->Count < 8))
            {
                KeInvalidateTlbEntry(Address);
                return;
            }
            DbgPrint("\n*** Repeated fault on permitted leaf %I64x at %p (pc %p)\n",
                     Walk.Value.u.Long, Address, (PVOID)(ULONG_PTR)TrapFrame->Context.Pc);
            KeGetCurrentThread()->TrapFrame = TrapFrame->PreviousTrapFrame;
            KiLa64TrapStop(TrapFrame);
        }
    }
    Spurious->Count = 0;

    /* The resident kernel metadata reader never resolves paging faults.
     * Recover before MM's fatal kernel-address path can recurse into SEH. */
    if ((Mode == KernelMode) &&
        ((ULONG_PTR)Address >= (ULONG_PTR)MmSystemRangeStart) &&
        KiLa64FixupUserCopy(TrapFrame, STATUS_ACCESS_VIOLATION))
    {
        return;
    }

    if (KeGetTrapFrameInterruptState(TrapFrame))
        _enable();

    Status = MmAccessFault(FaultCode, Address, Mode, TrapFrame);
    if (NT_SUCCESS(Status))
    {
        /* Drop the translation the refill installed for this pair; the retry
         * either hits the new leaf or refills from the updated tables. */
        KeInvalidateTlbEntry(Address);
        return;
    }
    if (Irql > APC_LEVEL)
    {
        KeBugCheckEx(IRQL_NOT_LESS_OR_EQUAL,
                     (ULONG_PTR)Address,
                     Irql,
                     ((Ecode == LA64_ECODE_PIS) || (Ecode == LA64_ECODE_PME)) ? 1 : 0,
                     TrapFrame->Context.Pc);
    }
    if (KiLa64FixupUserCopy(TrapFrame, Status))
        return;

    RtlZeroMemory(&Record, sizeof(Record));
    Record.ExceptionAddress = (PVOID)(ULONG_PTR)TrapFrame->Context.Pc;
    if ((Status == STATUS_ACCESS_VIOLATION) ||
        (Status == STATUS_GUARD_PAGE_VIOLATION) ||
        (Status == STATUS_STACK_OVERFLOW))
    {
        Record.ExceptionCode = Status;
        Record.NumberParameters = 2;
        Record.ExceptionInformation[0] =
            (Ecode == LA64_ECODE_PIS) || (Ecode == LA64_ECODE_PME) ? 1 :
            (Ecode == LA64_ECODE_PIF) ? 8 : 0;
        Record.ExceptionInformation[1] = (ULONG_PTR)Address;
    }
    else
    {
        Record.ExceptionCode = STATUS_IN_PAGE_ERROR;
        Record.NumberParameters = 3;
        Record.ExceptionInformation[0] =
            (Ecode == LA64_ECODE_PIS) || (Ecode == LA64_ECODE_PME) ? 1 : 0;
        Record.ExceptionInformation[1] = (ULONG_PTR)Address;
        Record.ExceptionInformation[2] = Status;
    }
    KiDispatchException(&Record, NULL, TrapFrame, Mode, TRUE);
}

/* ---------------------------------------------------------------------------
 * Resumable trap dispatch
 * ------------------------------------------------------------------------ */
VOID
NTAPI
KiLa64TrapHandler(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG Ecode = KiLa64EstatEcode(TrapFrame->Estat);
    KPROCESSOR_MODE Mode = KiLa64UserTrap(TrapFrame) ? UserMode : KernelMode;
    EXCEPTION_RECORD Record;

    TrapFrame->PreviousTrapFrame = Thread->TrapFrame;
    Thread->TrapFrame = TrapFrame;

    if (Ecode == LA64_ECODE_INT)
    {
        /* A DPC-level switch may run other threads meanwhile; this thread
         * resumes here on its own stack with the same frame. */
        KiLa64InterruptDispatch(TrapFrame);
        Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
        return;
    }

    if (Ecode == LA64_ECODE_SYS)
    {
        if (Mode == UserMode)
            KiLa64SystemService(TrapFrame);
        /* A syscall instruction executed in kernel mode has no defined
         * service and no user frame to return to. */
        Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
        KiLa64TrapStop(TrapFrame);
    }

    switch (Ecode)
    {
        case LA64_ECODE_PIL:
        case LA64_ECODE_PIS:
        case LA64_ECODE_PIF:
        case LA64_ECODE_PME:
        case LA64_ECODE_PNR:
        case LA64_ECODE_PNX:
        case LA64_ECODE_PPI:
            KiLa64MemoryFault(TrapFrame, Ecode, Mode);
            _disable();
            Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
            return;
        default:
            break;
    }

    /* Everything else becomes an NT exception. */
    RtlZeroMemory(&Record, sizeof(Record));
    Record.ExceptionAddress = (PVOID)(ULONG_PTR)TrapFrame->Context.Pc;
    switch (Ecode)
    {
        case LA64_ECODE_BRK:
            Record.ExceptionCode = STATUS_BREAKPOINT;
            Record.NumberParameters = 1;
            Record.ExceptionInformation[0] = 0;
            break;
        case LA64_ECODE_INE:
        case LA64_ECODE_SXD:
        case LA64_ECODE_ASXD:
        case LA64_ECODE_BTD:
        case LA64_ECODE_BTE:
            Record.ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
            break;
        case LA64_ECODE_IPE:
        case LA64_ECODE_GSPR:
        case LA64_ECODE_HVC:
            Record.ExceptionCode = STATUS_PRIVILEGED_INSTRUCTION;
            break;
        case LA64_ECODE_ALE:
            Record.ExceptionCode = STATUS_DATATYPE_MISALIGNMENT;
            break;
        case LA64_ECODE_BCE:
            Record.ExceptionCode = STATUS_ARRAY_BOUNDS_EXCEEDED;
            break;
        case LA64_ECODE_WPE:
            Record.ExceptionCode = STATUS_SINGLE_STEP;
            break;
        case LA64_ECODE_ADEF:
            Record.ExceptionCode = STATUS_ACCESS_VIOLATION;
            Record.NumberParameters = 2;
            Record.ExceptionInformation[0] = (KiLa64EstatEsubCode(TrapFrame->Estat) == 0) ? 8 : 0;
            Record.ExceptionInformation[1] = (ULONG_PTR)TrapFrame->Badv;
            break;
        case LA64_ECODE_FPE:
            /* TODO: decode fcsr0.Cause into the per-class STATUS_FLOAT_*
             * codes once a user FPU policy exists; the aggregate code is the
             * only one that needs no decoding. */
            Record.ExceptionCode = STATUS_FLOAT_MULTIPLE_TRAPS;
            break;
        case LA64_ECODE_FPD:
            /* The kernel enables EUEN.FPE for the whole system (handoff
             * decision D-8), so a disabled FPU is a kernel bug. */
            Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
            KiLa64TrapStop(TrapFrame);
            break;
        default:
            Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
            KiLa64TrapStop(TrapFrame);
            break;
    }

    /* Dispatch with the interrupt state the faulting code had, as the other
     * 64-bit ports do. */
    if (KeGetTrapFrameInterruptState(TrapFrame))
        _enable();
    KiDispatchException(&Record, NULL, TrapFrame, Mode, TRUE);
    _disable();
    Thread->TrapFrame = TrapFrame->PreviousTrapFrame;

    /* A synchronous exception that reported success but left the
     * instruction in place would loop; the debugger and KD are expected to
     * have advanced ERA for a breakpoint (see KiDispatchException). */
}

/* ---------------------------------------------------------------------------
 * Trap frame accessors
 * ------------------------------------------------------------------------ */
PKTRAP_FRAME
NTAPI
KeGetTrapFrame(
    _In_ PKTHREAD Thread)
{
    return Thread->TrapFrame;
}

PKEXCEPTION_FRAME
NTAPI
KeGetExceptionFrame(
    _In_ PKTHREAD Thread)
{
    UNREFERENCED_PARAMETER(Thread);
    /* Every base register lives in KTRAP_FRAME.Context; there is no second
     * nonvolatile bank to locate by subtracting a foreign frame length
     * (KI_NO_EXCEPTION_FRAME). */
    return NULL;
}

/* Hand a user-mode exception to the dispatcher ntdll installs in
 * KeUserExceptionDispatcher. The record is built below the interrupted $sp,
 * so the handler owns it and the kernel holds no pointer into user memory.
 * A frame that cannot be built is not an error here: the caller falls
 * through to the second-chance path. */
static
BOOLEAN
KiLa64DeliverUserException(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ PEXCEPTION_RECORD Record)
{
    KUSER_EXCEPTION_STACK UserFrame;
    ULONG_PTR Stack, Entry = (ULONG_PTR)KeUserExceptionDispatcher;
    NTSTATUS Status;

    if (!Entry || (Entry & 3) || Entry >= (ULONG_PTR)MmUserProbeAddress ||
        Context->Sp < MM_ALLOCATION_GRANULARITY + sizeof(UserFrame) ||
        Context->Sp >= (ULONG_PTR)MmUserProbeAddress || (Context->Sp & 15) ||
        Record->NumberParameters > EXCEPTION_MAXIMUM_PARAMETERS)
    {
        return FALSE;
    }

    Stack = Context->Sp - sizeof(UserFrame);
    RtlZeroMemory(&UserFrame, sizeof(UserFrame));
    UserFrame.Context = *Context;
    UserFrame.ExceptionRecord = *Record;
    Status = KiLa64CopyToUser((PVOID)Stack, &UserFrame, sizeof(UserFrame));
    if (!NT_SUCCESS(Status))
        return FALSE;

    /* ntdll's entry takes &ExceptionRecord in $a0 and &Context in $a1; $ra
     * is zero because the dispatcher never returns to the kernel. */
    TrapFrame->Context.Pc = Entry;
    TrapFrame->Context.Sp = Stack;
    TrapFrame->Context.A0 = Stack + FIELD_OFFSET(KUSER_EXCEPTION_STACK, ExceptionRecord);
    TrapFrame->Context.A1 = Stack;
    TrapFrame->Context.Ra = 0;
    TrapFrame->Prmd = LA64_PRMD_PPLV_USER | LA64_PRMD_PIE;
    return TRUE;
}

VOID
KiDispatchException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN FirstChance)
{
    CONTEXT Context;

    KeGetCurrentPrcb()->KeExceptionDispatchCount++;
    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = CONTEXT_FULL |
        (TrapFrame->Context.ContextFlags & CONTEXT_UNWOUND_TO_CALL);
    KeTrapFrameToContext(TrapFrame, ExceptionFrame, &Context);

    if (PreviousMode != KernelMode)
    {
        if (PspPicoDispatchException(ExceptionRecord, ExceptionFrame, TrapFrame))
            return;

        if (FirstChance)
        {
            if ((!PsGetCurrentProcess()->DebugPort && !KdIgnoreUmExceptions) ||
                KdIsThisAKdTrap(ExceptionRecord, &Context, PreviousMode))
            {
                if (KiDebugRoutine(TrapFrame, ExceptionFrame, ExceptionRecord,
                                   &Context, PreviousMode, FALSE))
                {
                    goto Handled;
                }
            }
            /* A software breakpoint leaves ERA on the break instruction;
             * LoongArch64 instructions are always four bytes wide, so the
             * resume address is the next word. */
            if ((ExceptionRecord->ExceptionCode == STATUS_BREAKPOINT) &&
                (ExceptionRecord->NumberParameters == 1) &&
                !(ExceptionRecord->ExceptionFlags & EXCEPTION_SOFTWARE_ORIGINATE) &&
                ((ULONG_PTR)ExceptionRecord->ExceptionAddress == TrapFrame->Context.Pc))
            {
                TrapFrame->Context.Pc += 4;
            }
            if (DbgkForwardException(ExceptionRecord, TRUE, FALSE))
                return;
            /* First chance in user mode belongs to the user dispatcher
             * before the process is terminated. */
            if (KiLa64DeliverUserException(TrapFrame, &Context, ExceptionRecord))
                return;
        }
        if (DbgkForwardException(ExceptionRecord, TRUE, TRUE) ||
            DbgkForwardException(ExceptionRecord, FALSE, TRUE))
        {
            return;
        }
        DPRINT1("LoongArch64 terminating %.16s: exception %lx at %p\n",
                PsGetCurrentProcess()->ImageFileName, ExceptionRecord->ExceptionCode,
                ExceptionRecord->ExceptionAddress);
        ZwTerminateProcess(NtCurrentProcess(), ExceptionRecord->ExceptionCode);
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED,
                     ExceptionRecord->ExceptionCode,
                     (ULONG_PTR)ExceptionRecord->ExceptionAddress,
                     (ULONG_PTR)TrapFrame,
                     PreviousMode);
    }

    if (FirstChance)
    {
        if (KiDebugRoutine(TrapFrame, ExceptionFrame, ExceptionRecord, &Context, PreviousMode, FALSE))
            goto Handled;
        if (RtlDispatchException(ExceptionRecord, &Context))
            goto Handled;
    }
    if (KiDebugRoutine(TrapFrame, ExceptionFrame, ExceptionRecord, &Context, PreviousMode, TRUE))
        goto Handled;

#ifdef KDBG
    if (!KdDebuggerEnabled || KdDebuggerNotPresent)
    {
        EXCEPTION_RECORD64 Record64;
        ULONG Index;

        RtlZeroMemory(&Record64, sizeof(Record64));
        Record64.ExceptionCode = ExceptionRecord->ExceptionCode;
        Record64.ExceptionFlags = ExceptionRecord->ExceptionFlags;
        Record64.ExceptionAddress = (ULONG64)(ULONG_PTR)ExceptionRecord->ExceptionAddress;
        Record64.NumberParameters = min(ExceptionRecord->NumberParameters, EXCEPTION_MAXIMUM_PARAMETERS);
        for (Index = 0; Index < Record64.NumberParameters; Index++)
            Record64.ExceptionInformation[Index] = ExceptionRecord->ExceptionInformation[Index];
        if (KdbEnterDebuggerException(&Record64, PreviousMode, &Context, FALSE) == kdContinue)
            goto Handled;
    }
#endif

    KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED,
                 ExceptionRecord->ExceptionCode,
                 (ULONG_PTR)ExceptionRecord->ExceptionAddress,
                 (ULONG_PTR)TrapFrame,
                 0);

Handled:
    KeContextToTrapFrame(&Context, ExceptionFrame, TrapFrame, Context.ContextFlags, PreviousMode);
}

NTSTATUS
NTAPI
KiContinueExtendedState(
    _In_ PCONTEXT SourceContext,
    _In_ ULONG ContextFlags,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    UNREFERENCED_PARAMETER(SourceContext);
    UNREFERENCED_PARAMETER(ContextFlags);
    UNREFERENCED_PARAMETER(PreviousMode);

    /* TODO: restore CONTEXT_XSTATE. The port has no extended state yet: the
     * loader leaves EUEN at FPE only (handoff decision D-8), so LSX/LASX/LBT
     * state cannot exist for a context to carry. */
    return STATUS_SUCCESS;
}
