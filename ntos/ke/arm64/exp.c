/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/exp.c
 * PURPOSE:         Exception handling stubs for ARM64
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#ifndef EXCEPTION_SOFTWARE_ORIGINATE
#define EXCEPTION_SOFTWARE_ORIGINATE 0x80
#endif
#ifdef KDBG
#include <kdbg/kdb.h>
#endif


#if defined(_MSC_VER) && !defined(__clang__)
static VOID KiArm64DbgPrintBacktraceImpl(_In_ PCONTEXT Ctx)
#else
static __attribute__((unused)) VOID KiArm64DbgPrintBacktraceImpl(_In_ PCONTEXT Ctx)
#endif
{
    if (!Ctx || !(Ctx->ContextFlags & CONTEXT_ARM64)) return;

    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "[arm64] Backtrace: PC=%p SP=%p CPSR=0x%08lx\n",
               (PVOID)(ULONG_PTR)Ctx->Pc,
               (PVOID)(ULONG_PTR)Ctx->Sp,
               (ULONG)Ctx->Cpsr);
}

static
BOOLEAN
KiUserStackPageIsCommitted(
    _In_ ULONG_PTR Page)
{
    MEMORY_BASIC_INFORMATION MemoryInfo;
    SIZE_T ReturnLength = 0;
    NTSTATUS Status;

    if (Page == 0 || KeGetCurrentIrql() > PASSIVE_LEVEL)
        return FALSE;

    Status = ZwQueryVirtualMemory(NtCurrentProcess(),
                                  (PVOID)Page,
                                  MemoryBasicInformation,
                                  &MemoryInfo,
                                  sizeof(MemoryInfo),
                                  &ReturnLength);
    if (!NT_SUCCESS(Status))
        return FALSE;

    return (MemoryInfo.State == MEM_COMMIT);
}

VOID
NTAPI
KiChpeContinueToEmulation(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PPEB Peb = Process->Peb;
    ULONG_PTR Pc = (ULONG_PTR)TrapFrame->Pc;
    ULONG_PTR UserSp;
    BOOLEAN Emulated = FALSE;
    CONTEXT Context;

    if ((KeUserEmulationDispatcher == NULL) || (Peb == NULL)) return;
    if ((Pc == 0) || (Pc >= (ULONG_PTR)MmUserProbeAddress)) return;
    if (TrapFrame->Sp >= (ULONG_PTR)MmUserProbeAddress) return;

    _SEH2_TRY
    {
        Emulated = (Peb->EcCodeBitMap != NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Emulated = FALSE;
    }
    _SEH2_END;

    if (!Emulated) return;
    if (MiIsEcCodeAddress(Process, (PVOID)Pc)) return;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = CONTEXT_FULL;
    KeTrapFrameToContext(TrapFrame, ExceptionFrame, &Context);

    UserSp = ((ULONG_PTR)TrapFrame->Sp - sizeof(CONTEXT)) & ~(ULONG_PTR)0xF;

    _SEH2_TRY
    {
        ProbeForWrite((PVOID)UserSp, sizeof(CONTEXT), 16);
        RtlCopyMemory((PVOID)UserSp, &Context, sizeof(Context));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return);
    }
    _SEH2_END;

    TrapFrame->Sp = UserSp;
    TrapFrame->X[0] = UserSp;
    TrapFrame->Pc = (ULONG64)KeUserEmulationDispatcher;
    TrapFrame->Lr = (ULONG64)KeUserEmulationDispatcher;
}

BOOLEAN
KiDispatchExceptionToUser(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ PEXCEPTION_RECORD ExceptionRecord)
{
    EXCEPTION_RECORD LocalExceptRecord;
    ULONG_PTR UserSp;
    PKUSER_EXCEPTION_STACK UserStack;
    PCONTEXT_EX ContextEx;

    ASSERT(TrapFrame != NULL);
    ASSERT(Context != NULL);
    ASSERT(ExceptionRecord != NULL);

    if (Context->Sp < sizeof(KUSER_EXCEPTION_STACK))
    {
        DPRINT1("[arm64][EXC] KiDispatchExceptionToUser: SP too low Sp=%p Need=%Ix Pc=%p Lr=%p Code=0x%08lx\n",
                (PVOID)(ULONG_PTR)Context->Sp,
                sizeof(KUSER_EXCEPTION_STACK),
                (PVOID)(ULONG_PTR)Context->Pc,
                (PVOID)(ULONG_PTR)Context->Lr,
                ExceptionRecord->ExceptionCode);
        return FALSE;
    }

    /*
     * ARM64 FIX (Bug #19): Recursion guard.
     *
     * Detect and break infinite exception dispatch loops:
     *   exception -> dispatch to user -> dispatcher crashes -> exception -> ...
     *
     * Only guard against faults within KiUserExceptionDispatcher itself
     * and its nested helper (about 0x200 bytes). The previous guard used
     * a 2.25MB window that covered ALL of ntdll, preventing legitimate
     * exceptions in ntdll functions (e.g., streamout/printf null pointer)
     * from being dispatched to SEH handlers.
     *
     * Faults in RtlDispatchException/RtlpUnwindInternal are handled by
     * their own SEH frames and KiUserExceptionDispatcherNestedArm64.
     */
    {
        PKTHREAD Thread = KeGetCurrentThread();
        PEPROCESS Process = (PEPROCESS)Thread->ApcState.Process;

        if ((ExceptionRecord->ExceptionCode == STATUS_ACCESS_VIOLATION ||
             ExceptionRecord->ExceptionCode == STATUS_STACK_OVERFLOW) &&
            KeUserExceptionDispatcher != NULL &&
            PspSystemDllBase != NULL)
        {
            PVOID UserDispatcher = KiResolveUserDispatcherAddress(KeUserExceptionDispatcher, Process);

            if (UserDispatcher != NULL)
            {
                ULONG_PTR DispatcherAddr = (ULONG_PTR)UserDispatcher;
                ULONG_PTR FaultPc = (ULONG_PTR)Context->Pc;

                /*
                 * Guard 1: PC within KiUserExceptionDispatcher or its
                 * nested helper. These functions span about 0x200 bytes
                 * before the dispatcher entry point.
                 */
                ULONG_PTR DispLo = (DispatcherAddr > 0x200) ? (DispatcherAddr - 0x200) : 0;
                ULONG_PTR DispHi = DispatcherAddr + 0x100;
                PVOID UserWorker = KiResolveUserDispatcherAddress(KeUserExceptionDispatcherWorker,
                                                                  Process);
                ULONG_PTR WorkerLo = 0;
                ULONG_PTR WorkerHi = 0;

                if (UserWorker != NULL)
                {
                    ULONG_PTR WorkerAddr = (ULONG_PTR)UserWorker;

                    WorkerLo = (WorkerAddr > 0x200) ? (WorkerAddr - 0x200) : 0;
                    WorkerHi = WorkerAddr + 0x100;
                }

                if ((FaultPc >= DispLo && FaultPc < DispHi) ||
                    (WorkerHi != 0 && FaultPc >= WorkerLo && FaultPc < WorkerHi))
                {
                    DPRINT1("[arm64][EXC] KiDispatchExceptionToUser: recursion guard hit (dispatcher range) Pc=%p Lr=%p Disp=[%p,%p)\n",
                            (PVOID)FaultPc,
                            (PVOID)(ULONG_PTR)Context->Lr,
                            (PVOID)DispLo,
                            (PVOID)DispHi);
                    return FALSE;
                }

                /*
                 * Guard 2: NULL function pointer call from the dispatcher.
                 * Instruction fetch at VA near 0 means a branch to NULL.
                 * Check if the return address (LR) points into the dispatcher.
                 */
                if (FaultPc < PAGE_SIZE)
                {
                    ULONG_PTR CallerLr = (ULONG_PTR)Context->Lr;
                    if ((CallerLr >= DispLo && CallerLr < DispHi) ||
                        (WorkerHi != 0 && CallerLr >= WorkerLo && CallerLr < WorkerHi))
                    {
                        DPRINT1("[arm64][EXC] KiDispatchExceptionToUser: recursion guard hit (null-call) Pc=%p Lr=%p Disp=[%p,%p)\n",
                                (PVOID)FaultPc,
                                (PVOID)CallerLr,
                                (PVOID)DispLo,
                                (PVOID)DispHi);
                        return FALSE;
                    }
                }
            }
        }
    }

    /* Allocate a 16-byte aligned exception stack frame on the user stack */
    UserSp = (ULONG_PTR)(Context->Sp - sizeof(KUSER_EXCEPTION_STACK));
    UserSp &= ~0xFULL;
    UserStack = (PKUSER_EXCEPTION_STACK)UserSp;

    if ((UserSp + sizeof(KUSER_EXCEPTION_STACK) - 1) > (ULONG_PTR)MmUserProbeAddress)
    {
        RtlZeroMemory(&LocalExceptRecord, sizeof(LocalExceptRecord));
        LocalExceptRecord.ExceptionCode = STATUS_ACCESS_VIOLATION;
        LocalExceptRecord.ExceptionFlags = 0;
        LocalExceptRecord.ExceptionRecord = NULL;
        LocalExceptRecord.NumberParameters = 0;
        ExceptionRecord = &LocalExceptRecord;
    }

    /*
     * ARM64 FIX: PSEH2 uses label-based pseudo-SEH on ARM64 which CANNOT
     * catch hardware page faults. The _SEH2_TRY below will NOT protect
     * against faults when writing to unmapped user stack pages.
     *
     * Pre-validate that all pages of the user exception frame are present
     * in TTBR0 page tables by walking via KSEG0 direct mapping. If any
     * page is not mapped, return FALSE to force process termination
     * instead of causing an unhandled kernel fault that stalls the system.
     *
     * This handles the case where a thread's stack has been freed (VAD
     * removed, PTEs cleared) but the thread still tries to dispatch an
     * exception — e.g., svchost.exe accessing a freed thread stack.
     */
    _enable();

    {
        ULONG64 Ttbr0Val;
        Ttbr0Val = ARM64_READ_SYSREG(TTBR0_EL1);
        ULONG64 RootPa = Ttbr0Val & 0x0000FFFFFFFFF000ULL;
        ULONG_PTR BadPage = 0;

        if (RootPa == 0)
        {
            DPRINT1("[arm64][EXC] KiDispatchExceptionToUser: TTBR0 root is zero Pc=%p Lr=%p Sp=%p\n",
                    (PVOID)(ULONG_PTR)Context->Pc,
                    (PVOID)(ULONG_PTR)Context->Lr,
                    (PVOID)(ULONG_PTR)Context->Sp);
            return FALSE;
        }

        ULONG_PTR StartPage = UserSp & ~(ULONG_PTR)0xFFF;
        ULONG_PTR EndPage = (UserSp + sizeof(KUSER_EXCEPTION_STACK) - 1) & ~(ULONG_PTR)0xFFF;

        for (ULONG_PTR ChkPage = StartPage; ChkPage <= EndPage; ChkPage += PAGE_SIZE)
        {
            volatile ULONG64 *T;
            ULONG64 E;

            /* L0 */
            T = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(RootPa);
            E = T[(ChkPage >> 39) & 0x1FF];
            if ((E & 0x3ULL) != 0x3ULL) { BadPage = ChkPage; goto StackNotMapped; }

            /* L1 */
            T = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(E & 0x0000FFFFFFFFF000ULL);
            E = T[(ChkPage >> 30) & 0x1FF];
            if ((E & 0x3ULL) != 0x3ULL) { BadPage = ChkPage; goto StackNotMapped; }

            /* L2 */
            T = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(E & 0x0000FFFFFFFFF000ULL);
            E = T[(ChkPage >> 21) & 0x1FF];
            if ((E & 0x3ULL) != 0x3ULL) { BadPage = ChkPage; goto StackNotMapped; }

            /* L3 */
            T = (volatile ULONG64 *)MI_ARM64_PHYS_TO_VA(E & 0x0000FFFFFFFFF000ULL);
            E = T[(ChkPage >> 12) & 0x1FF];
            if ((E & 0x3ULL) != 0x3ULL) { BadPage = ChkPage; goto StackNotMapped; }
            continue;

        StackNotMapped:
            if (KiUserStackPageIsCommitted(BadPage))
            {
                BadPage = 0;
                continue;
            }
            goto StackUnmapped;
        }
        goto StackOk;

    StackUnmapped:
        DPRINT1("[arm64][EXC] KiDispatchExceptionToUser: user exception stack unmapped Start=%p End=%p BadPage=%p Pc=%p Lr=%p\n",
                (PVOID)StartPage,
                (PVOID)EndPage,
                (PVOID)BadPage,
                (PVOID)(ULONG_PTR)Context->Pc,
                (PVOID)(ULONG_PTR)Context->Lr);
        return FALSE;

    StackOk:
        ;
    }

    _SEH2_TRY
    {
        ProbeForWrite(UserStack,
                      sizeof(*UserStack),
                      TYPE_ALIGNMENT(KUSER_EXCEPTION_STACK));
        RtlZeroMemory(UserStack, sizeof(*UserStack));
        UserStack->Context = *Context;
        UserStack->ExceptionRecord = *ExceptionRecord;
        UserStack->MachineFrame.Sp = Context->Sp;
        UserStack->MachineFrame.Pc = Context->Pc;
        ContextEx = (PCONTEXT_EX)UserStack->ContextEx;
        ContextEx->Legacy.Offset = -(LONG)sizeof(CONTEXT);
        ContextEx->Legacy.Length = sizeof(CONTEXT);
        ContextEx->XState.Offset = (LONG)((ULONG_PTR)(UserStack + 1) - (ULONG_PTR)ContextEx);
        ContextEx->XState.Length = 0;
        ContextEx->All.Offset = -(LONG)sizeof(CONTEXT);
        ContextEx->All.Length = sizeof(CONTEXT) + ContextEx->XState.Offset;
    }
    _SEH2_EXCEPT((LocalExceptRecord = *_SEH2_GetExceptionInformation()->ExceptionRecord),
                 EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT1("[arm64][EXC] KiDispatchExceptionToUser: ProbeForWrite failed Code=0x%08lx Addr=%p Pc=%p Lr=%p Sp=%p\n",
                LocalExceptRecord.ExceptionCode,
                LocalExceptRecord.ExceptionAddress,
                (PVOID)(ULONG_PTR)Context->Pc,
                (PVOID)(ULONG_PTR)Context->Lr,
                (PVOID)(ULONG_PTR)Context->Sp);
        _disable();
        return FALSE;
    }
    _SEH2_END;

    /*
     * ARM64 FIX: Convert KeUserExceptionDispatcher to user address.
     */
    {
        PKTHREAD Thread = KeGetCurrentThread();
        PEPROCESS Process = (PEPROCESS)Thread->ApcState.Process;
        PVOID UserExceptionDispatcher = KiResolveUserDispatcherAddress(KeUserExceptionDispatcher, Process);
        if (UserExceptionDispatcher == NULL)
        {
            DPRINT1("[arm64][EXC] unresolved exception dispatcher: proc=%.16s KeUserExceptionDispatcher=%p SystemDllBase=%p PspSystemDllBase=%p TrapPc=%p CtxPc=%p CtxLr=%p\n",
                    PsGetCurrentProcess()->ImageFileName,
                    KeUserExceptionDispatcher,
                    Process ? PspSystemDllBase : NULL,
                    PspSystemDllBase,
                    (PVOID)(ULONG_PTR)TrapFrame->Pc,
                    (PVOID)(ULONG_PTR)Context->Pc,
                    (PVOID)(ULONG_PTR)Context->Lr);
            _disable();
            return FALSE;
        }

        TrapFrame->X0 = (ULONG64)(ULONG_PTR)&UserStack->ExceptionRecord;
        TrapFrame->X1 = (ULONG64)(ULONG_PTR)&UserStack->Context;
        TrapFrame->Sp = UserSp;
        TrapFrame->Pc = (ULONG64)(ULONG_PTR)UserExceptionDispatcher;
        TrapFrame->Lr = (ULONG64)(ULONG_PTR)UserExceptionDispatcher;

    }

    _disable();
    return TRUE;
}

VOID
KiArm64FinishExceptionContext(
    _In_ PKTRAP_FRAME TrapFrame,
    _Inout_ PCONTEXT Context)
{
    if (TrapFrame->ContextFromKFramesUnwound)
        Context->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
}

BOOLEAN
KiArm64LastChanceKernelDebugger(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ PCONTEXT Context)
{
#ifdef KDBG
    EXCEPTION_RECORD64 Record;
    ULONG Index;

    if (KdDebuggerEnabled && !KdDebuggerNotPresent)
        return FALSE;

    RtlZeroMemory(&Record, sizeof(Record));
    Record.ExceptionCode = ExceptionRecord->ExceptionCode;
    Record.ExceptionFlags = ExceptionRecord->ExceptionFlags;
    Record.ExceptionAddress = (ULONG64)(ULONG_PTR)ExceptionRecord->ExceptionAddress;
    Record.NumberParameters = min(ExceptionRecord->NumberParameters, EXCEPTION_MAXIMUM_PARAMETERS);
    for (Index = 0; Index < Record.NumberParameters; Index++)
        Record.ExceptionInformation[Index] = (ULONG64)ExceptionRecord->ExceptionInformation[Index];

    return (BOOLEAN)(KdbEnterDebuggerException(&Record, KernelMode, Context, FALSE) == kdContinue);
#else
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(Context);
    return FALSE;
#endif
}

VOID
KiArm64PrepareUserBreakpoint(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    /* A user debugger resumes after a BRK it was shown, as it does after an x86 int3 */
    if ((ExceptionRecord->NumberParameters == 1) &&
        !(ExceptionRecord->ExceptionFlags & EXCEPTION_SOFTWARE_ORIGINATE) &&
        ((ULONG_PTR)ExceptionRecord->ExceptionAddress == (ULONG_PTR)TrapFrame->Pc))
    {
        TrapFrame->Pc += 4;
    }
}
