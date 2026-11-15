/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later),
 *              with portions under the BSD license in COPYING.ARM
 * PURPOSE:     i386 inline helpers for trap entry, exit and debug register checks
 * COPYRIGHT:   Copyright ReactOS Portable Systems Group
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#if defined(_MSC_VER)
#define               __builtin_expect(a,b) (a)
#endif

FORCEINLINE
BOOLEAN
KiUserTrap(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return !!(TrapFrame->SegCs & MODE_MASK);
}

// Debug Macros
FORCEINLINE
VOID
KiDumpTrapFrame(
    _In_ PKTRAP_FRAME TrapFrame)
{
    DbgPrint("DbgEbp: %x\n", TrapFrame->DbgEbp);
    DbgPrint("DbgEip: %x\n", TrapFrame->DbgEip);
    DbgPrint("DbgArgMark: %x\n", TrapFrame->DbgArgMark);
    DbgPrint("DbgArgPointer: %x\n", TrapFrame->DbgArgPointer);
    DbgPrint("TempSegCs: %x\n", TrapFrame->TempSegCs);
    DbgPrint("TempEsp: %x\n", TrapFrame->TempEsp);
    DbgPrint("Dr0: %x\n", TrapFrame->Dr0);
    DbgPrint("Dr1: %x\n", TrapFrame->Dr1);
    DbgPrint("Dr2: %x\n", TrapFrame->Dr2);
    DbgPrint("Dr3: %x\n", TrapFrame->Dr3);
    DbgPrint("Dr6: %x\n", TrapFrame->Dr6);
    DbgPrint("Dr7: %x\n", TrapFrame->Dr7);
    DbgPrint("SegGs: %x\n", TrapFrame->SegGs);
    DbgPrint("SegEs: %x\n", TrapFrame->SegEs);
    DbgPrint("SegDs: %x\n", TrapFrame->SegDs);
    DbgPrint("Edx: %x\n", TrapFrame->Edx);
    DbgPrint("Ecx: %x\n", TrapFrame->Ecx);
    DbgPrint("Eax: %x\n", TrapFrame->Eax);
    DbgPrint("PreviousPreviousMode: %x\n", TrapFrame->PreviousPreviousMode);
    DbgPrint("ExceptionList: %p\n", TrapFrame->ExceptionList);
    DbgPrint("SegFs: %x\n", TrapFrame->SegFs);
    DbgPrint("Edi: %x\n", TrapFrame->Edi);
    DbgPrint("Esi: %x\n", TrapFrame->Esi);
    DbgPrint("Ebx: %x\n", TrapFrame->Ebx);
    DbgPrint("Ebp: %x\n", TrapFrame->Ebp);
    DbgPrint("ErrCode: %x\n", TrapFrame->ErrCode);
    DbgPrint("Eip: %x\n", TrapFrame->Eip);
    DbgPrint("SegCs: %x\n", TrapFrame->SegCs);
    DbgPrint("EFlags: %x\n", TrapFrame->EFlags);
    DbgPrint("HardwareEsp: %x\n", TrapFrame->HardwareEsp);
    DbgPrint("HardwareSegSs: %x\n", TrapFrame->HardwareSegSs);
    DbgPrint("V86Es: %x\n", TrapFrame->V86Es);
    DbgPrint("V86Ds: %x\n", TrapFrame->V86Ds);
    DbgPrint("V86Fs: %x\n", TrapFrame->V86Fs);
    DbgPrint("V86Gs: %x\n", TrapFrame->V86Gs);
}

#define KI_INTERRUPT_TRAP_FRAME ((ULONG)-2)
#define KI_EXCEPTION_ACTIVE_FRAME 0x100

#if DBG
FORCEINLINE
VOID
KiFillTrapFrameDebug(
    _In_ PKTRAP_FRAME TrapFrame)
{
    /* Set the debug information */
    TrapFrame->DbgArgPointer = TrapFrame->Edx;
    TrapFrame->DbgArgMark = 0xBADB0D00;
    TrapFrame->DbgEip = TrapFrame->Eip;
    TrapFrame->DbgEbp = TrapFrame->Ebp;
    TrapFrame->PreviousPreviousMode = (ULONG)-1;
}

#define DR7_RESERVED_READ_AS_1 0x400

#define CheckDr(DrNumner, ExpectedValue) \
    { \
        ULONG DrValue = __readdr(DrNumner); \
        if (DrValue != (ExpectedValue)) \
        { \
            DbgPrint("Dr%ld: expected %.8lx, got %.8lx\n", \
                    DrNumner, ExpectedValue, DrValue); \
            __debugbreak(); \
        } \
    }

extern BOOLEAN StopChecking;

FORCEINLINE
VOID
KiExitTrapDebugChecks(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ BOOLEAN SkipPreviousMode)
{
    if (StopChecking) return;
    StopChecking = TRUE;

    if (__readeflags() & EFLAGS_INTERRUPT_MASK)
    {
        DbgPrint("Exiting with interrupts enabled: %lx\n", __readeflags());
        __debugbreak();
    }

    if (TrapFrame->DbgArgMark != 0xBADB0D00)
    {
        DbgPrint("Exiting with an invalid trap frame? (No MAGIC in trap frame)\n");
        KiDumpTrapFrame(TrapFrame);
        __debugbreak();
    }

    if (Ke386GetFs() != KGDT_R0_PCR)
    {
        DbgPrint("Exiting with an invalid FS: %lx\n", Ke386GetFs());
        __debugbreak();
    }

    if (KeGetPcr()->NtTib.ExceptionList == 0)
    {
        DbgPrint("Exiting with NULL exception chain: %p\n", KeGetPcr()->NtTib.ExceptionList);
        __debugbreak();
    }

    if (TrapFrame->ExceptionList == 0)
    {
        DbgPrint("Entered a trap with a NULL exception chain: %p\n", TrapFrame->ExceptionList);
        __debugbreak();
    }

    if (SkipPreviousMode && (TrapFrame->PreviousPreviousMode != (ULONG)-1) &&
        (TrapFrame->PreviousPreviousMode != KI_INTERRUPT_TRAP_FRAME))
    {
        DbgPrint("Exiting a trap witout restoring previous mode, yet previous mode seems valid: %lx\n", TrapFrame->PreviousPreviousMode);
        __debugbreak();
    }

    /* FIXME: KDBG messes around with these improperly */
#if !defined(KDBG)

    if (KiUserTrap(TrapFrame))
    {
        /* Check for active debugging */
        if (KeGetCurrentThread()->Header.ActiveDR7)
        {
            if ((TrapFrame->Dr7 & ~DR7_RESERVED_MASK) == 0) __debugbreak();

            CheckDr(0, TrapFrame->Dr0);
            CheckDr(1, TrapFrame->Dr1);
            CheckDr(2, TrapFrame->Dr2);
            CheckDr(3, TrapFrame->Dr3);
            CheckDr(7, TrapFrame->Dr7 | DR7_RESERVED_READ_AS_1);
        }
    }
    else
    {
        PKPRCB Prcb = KeGetCurrentPrcb();
        CheckDr(0, Prcb->ProcessorState.SpecialRegisters.KernelDr0);
        CheckDr(1, Prcb->ProcessorState.SpecialRegisters.KernelDr1);
        CheckDr(2, Prcb->ProcessorState.SpecialRegisters.KernelDr2);
        CheckDr(3, Prcb->ProcessorState.SpecialRegisters.KernelDr3);
    }
#endif

    StopChecking = FALSE;
}

#else
#define KiExitTrapDebugChecks(x, y)
#define KiFillTrapFrameDebug(x) ((x)->PreviousPreviousMode = (ULONG)-1)
#endif

FORCEINLINE
VOID
KiExitSystemCallDebugChecks(
    _In_ ULONG SystemCall,
    _In_ PKTRAP_FRAME TrapFrame)
{
    KIRQL OldIrql;

    if (KiUserTrap(TrapFrame))
    {
        OldIrql = KeGetCurrentIrql();
        if (OldIrql != PASSIVE_LEVEL)
        {
            KeGetPcr()->Irql = PASSIVE_LEVEL;
            _disable();

            KeBugCheckEx(IRQL_GT_ZERO_AT_SYSTEM_SERVICE,
                         SystemCall,
                         OldIrql,
                         0,
                         0);
        }

        if ((KeGetCurrentThread()->ApcStateIndex != OriginalApcEnvironment) ||
            (KeGetCurrentThread()->CombinedApcDisable != 0))
        {
            KeBugCheckEx(APC_INDEX_MISMATCH,
                         SystemCall,
                         KeGetCurrentThread()->ApcStateIndex,
                         KeGetCurrentThread()->CombinedApcDisable,
                         0);
        }
    }
}

DECLSPEC_NORETURN VOID FASTCALL KiSystemCallReturn(IN PKTRAP_FRAME TrapFrame);
DECLSPEC_NORETURN VOID FASTCALL KiSystemCallSysExitReturn(IN PKTRAP_FRAME TrapFrame);
DECLSPEC_NORETURN VOID FASTCALL KiSystemCallTrapReturn(IN PKTRAP_FRAME TrapFrame);
DECLSPEC_NORETURN VOID FASTCALL KiEditedTrapReturn(IN PKTRAP_FRAME TrapFrame);
DECLSPEC_NORETURN VOID FASTCALL KiTrapReturn(IN PKTRAP_FRAME TrapFrame);
DECLSPEC_NORETURN VOID FASTCALL KiTrapReturnNoSegments(IN PKTRAP_FRAME TrapFrame);
DECLSPEC_NORETURN VOID FASTCALL KiTrapReturnNoSegmentsRet8(IN PKTRAP_FRAME TrapFrame);

typedef
VOID
(FASTCALL *PFAST_SYSTEM_CALL_EXIT)(
    IN PKTRAP_FRAME TrapFrame
);

extern PFAST_SYSTEM_CALL_EXIT KiFastCallExitHandler;

// Save user mode debug registers and restore kernel values
FORCEINLINE
VOID
KiHandleDebugRegistersOnTrapEntry(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    /* Save all debug registers in the trap frame */
    TrapFrame->Dr0 = __readdr(0);
    TrapFrame->Dr1 = __readdr(1);
    TrapFrame->Dr2 = __readdr(2);
    TrapFrame->Dr3 = __readdr(3);
    TrapFrame->Dr6 = __readdr(6);
    TrapFrame->Dr7 = __readdr(7);

    /* Disable all active debugging */
    __writedr(7, 0);

    __writedr(0, Prcb->ProcessorState.SpecialRegisters.KernelDr0);
    __writedr(1, Prcb->ProcessorState.SpecialRegisters.KernelDr1);
    __writedr(2, Prcb->ProcessorState.SpecialRegisters.KernelDr2);
    __writedr(3, Prcb->ProcessorState.SpecialRegisters.KernelDr3);
    __writedr(6, Prcb->ProcessorState.SpecialRegisters.KernelDr6);
    __writedr(7, Prcb->ProcessorState.SpecialRegisters.KernelDr7);
}

FORCEINLINE
VOID
KiHandleDebugRegistersOnTrapExit(
    PKTRAP_FRAME TrapFrame)
{
    /* Disable all active debugging */
    __writedr(7, 0);

    /* Load all debug registers from the trap frame */
    __writedr(0, TrapFrame->Dr0);
    __writedr(1, TrapFrame->Dr1);
    __writedr(2, TrapFrame->Dr2);
    __writedr(3, TrapFrame->Dr3);
    __writedr(6, TrapFrame->Dr6);
    __writedr(7, TrapFrame->Dr7);
}

FORCEINLINE
DECLSPEC_NORETURN
VOID
KiExitV86Trap(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread;
    KIRQL OldIrql;

    Thread = KeGetCurrentThread();
    while (TRUE)
    {
        if (!(TrapFrame->EFlags & EFLAGS_V86_MASK)) KiEoiHelper(TrapFrame);

        Thread->Alerted[KernelMode] = FALSE;

        if (__builtin_expect(!Thread->ApcState.UserApcPending, 1)) break;

        OldIrql = KfRaiseIrql(APC_LEVEL);
        _enable();

        KiDeliverApc(UserMode, NULL, TrapFrame);

        KfLowerIrql(OldIrql);
        _disable();
    }

    if (__builtin_expect(TrapFrame->Dr7 & ~DR7_RESERVED_MASK, 0))
    {
        /* Restore debug registers from the trap frame */
        KiHandleDebugRegistersOnTrapExit(TrapFrame);
    }

    KiTrapReturnNoSegments(TrapFrame);
}

FORCEINLINE
VOID
KiEnterV86Trap(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PVOID ExceptionList;

    ExceptionList = KeGetPcr()->NtTib.ExceptionList;
    ASSERTMSG("V86 trap handler must not register an SEH frame\n",
              ExceptionList == TrapFrame->ExceptionList);

    /* Save DR7 and check for debugging */
    TrapFrame->Dr7 = __readdr(7);
    if (__builtin_expect(TrapFrame->Dr7 & ~DR7_RESERVED_MASK, 0))
    {
        /* Handle debug registers */
        KiHandleDebugRegistersOnTrapEntry(TrapFrame);
    }
}

FORCEINLINE
VOID
KiEnterInterruptTrap(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PVOID ExceptionList;

    ExceptionList = KeGetPcr()->NtTib.ExceptionList;
    ASSERTMSG("Interrupt handler must not register an SEH frame\n",
              ExceptionList == TrapFrame->ExceptionList);
    KeGetPcr()->NtTib.ExceptionList = EXCEPTION_CHAIN_END;

    /* Default to debugging disabled */
    TrapFrame->Dr7 = 0;

    if (KiUserTrap(TrapFrame) ||
        (TrapFrame->EFlags & EFLAGS_V86_MASK))
    {
        /* Check for active debugging */
        if (KeGetCurrentThread()->Header.ActiveDR7)
        {
            /* Handle debug registers */
            KiHandleDebugRegistersOnTrapEntry(TrapFrame);
        }
    }

    /* Set debug header */
    KiFillTrapFrameDebug(TrapFrame);
    TrapFrame->PreviousPreviousMode = KI_INTERRUPT_TRAP_FRAME;
}

FORCEINLINE
VOID
KiEnterTrap(
    _In_ PKTRAP_FRAME TrapFrame)
{
    PVOID ExceptionList;

    ExceptionList = KeGetPcr()->NtTib.ExceptionList;
    ASSERTMSG("Trap handler must not register an SEH frame\n",
              ExceptionList == TrapFrame->ExceptionList);

    /* Default to debugging disabled */
    TrapFrame->Dr7 = 0;

    if (KiUserTrap(TrapFrame) ||
        (TrapFrame->EFlags & EFLAGS_V86_MASK))
    {
        /* Check for active debugging */
        if (KeGetCurrentThread()->Header.ActiveDR7)
        {
            /* Handle debug registers */
            KiHandleDebugRegistersOnTrapEntry(TrapFrame);
        }
    }

    /* Set debug header */
    KiFillTrapFrameDebug(TrapFrame);
}
