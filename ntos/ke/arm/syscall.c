/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 system service dispatch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kiarm.h"
#define NDEBUG
#include <debug.h>

/*
 * SVC #1 with the service number in r12. The first four argument words are in
 * r0-r3 and the rest on the caller's stack, as the AAPCS placed them; the
 * kernel stubs keep r4 and lr above them (8 bytes).
 */
#define KI_ARM_MAX_SERVICE_WORDS 32
#define KI_ARM_KERNEL_STUB_FRAME 8

ULONG NTAPI KiArmInvokeSystemService(_In_ PVOID Routine, _In_ PULONG Arguments, _In_ ULONG StackWords);
NTSTATUS NTAPI PsConvertToGuiThread(VOID);

static
BOOLEAN
KiArmNeedsGuiConversion(
    _In_ PKTHREAD Thread,
    _In_ PKTRAP_FRAME TrapFrame)
{
    return !Thread->Header.Minimal && ((TrapFrame->R12 >> TABLE_OFFSET_BITS) == WIN32K_SERVICE_INDEX) &&
           !Thread->GuiThread && KeServiceDescriptorTableShadow[WIN32K_SERVICE_INDEX].Base &&
           PspW32ProcessCallout && PspW32ThreadCallout;
}

static
ULONG
KiArmDispatchService(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    ULONG Service = TrapFrame->R12;
    ULONG Table = Service >> TABLE_OFFSET_BITS;
    ULONG Index = Service & SERVICE_NUMBER_MASK;
    ULONG Arguments[KI_ARM_MAX_SERVICE_WORDS + 4];
    PKTHREAD Thread = KeGetCurrentThread();
    PKSERVICE_TABLE_DESCRIPTOR Descriptor;
    ULONG Words, StackWords;
    PULONG Stack;
    NTSTATUS Status = STATUS_SUCCESS;

    if ((Table >= SSDT_MAX_ENTRIES) || (Table && !Thread->GuiThread))
        return (ULONG)STATUS_INVALID_SYSTEM_SERVICE;

    Descriptor = &(Thread->GuiThread ? KeServiceDescriptorTableShadow : KeServiceDescriptorTable)[Table];
    if (!Descriptor->Base || !Descriptor->Number || (Index >= Descriptor->Limit))
        return (ULONG)STATUS_INVALID_SYSTEM_SERVICE;

    Words = Descriptor->Number[Index] / sizeof(ULONG);
    if (Words > KI_ARM_MAX_SERVICE_WORDS)
        return (ULONG)STATUS_INVALID_SYSTEM_SERVICE;

    Arguments[0] = TrapFrame->R0;
    Arguments[1] = TrapFrame->R1;
    Arguments[2] = TrapFrame->R2;
    Arguments[3] = TrapFrame->R3;

    /* One spare word covers the AAPCS padding before an 8-byte stack argument. */
    StackWords = (Words > 4) ? (Words - 4 + 1) : 0;
    if (StackWords)
    {
        Stack = (PULONG)(ULONG_PTR)TrapFrame->Sp;
        if (!KiIsUserModeTrap(TrapFrame))
        {
            Stack = (PULONG)((PUCHAR)Stack + KI_ARM_KERNEL_STUB_FRAME);
            RtlCopyMemory(&Arguments[4], Stack, StackWords * sizeof(ULONG));
        }
        else
        {
            _SEH2_TRY
            {
                ProbeForRead(Stack, StackWords * sizeof(ULONG), sizeof(ULONG));
                RtlCopyMemory(&Arguments[4], Stack, StackWords * sizeof(ULONG));
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
            if (!NT_SUCCESS(Status))
                return (ULONG)Status;
        }
    }

    return KiArmInvokeSystemService((PVOID)Descriptor->Base[Index], Arguments, StackWords);
}

static
VOID
KiArmFlushGdiBatch(
    _In_ PKTHREAD Thread,
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG BatchCount = 0;

    if (((TrapFrame->R12 >> TABLE_OFFSET_BITS) != WIN32K_SERVICE_INDEX) ||
        !Thread->GuiThread || !Thread->Teb || !KeGdiFlushUserBatch)
    {
        return;
    }

    _SEH2_TRY
    {
        BatchCount = ((volatile TEB *)Thread->Teb)->GdiBatchCount;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        BatchCount = 0;
    }
    _SEH2_END;
    if (BatchCount)
        KeGdiFlushUserBatch();
}

static
VOID
KiArmCompleteService(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Result)
{
    PKTHREAD Thread = KeGetCurrentThread();

    TrapFrame->R0 = Result;
    if (KiIsUserModeTrap(TrapFrame))
    {
        if (KeGetCurrentIrql() != PASSIVE_LEVEL)
            KeBugCheckEx(IRQL_GT_ZERO_AT_SYSTEM_SERVICE, TrapFrame->R12, KeGetCurrentIrql(), 0, 0);
        if ((Thread->ApcStateIndex != OriginalApcEnvironment) || Thread->CombinedApcDisable)
            KeBugCheckEx(APC_INDEX_MISMATCH, TrapFrame->R12, Thread->ApcStateIndex, Thread->CombinedApcDisable, 0);
    }
}

/*
 * Second half of a first win32k call: the thread now runs on its large stack,
 * with the service record copied to the top. The old stack is gone, so this
 * leaves through the record instead of returning.
 */
static
DECLSPEC_NORETURN
VOID
NTAPI
KiArmContinueOnLargeStack(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ PVOID OldStack)
{
    PKTHREAD Thread = KeGetCurrentThread();
    NTSTATUS Status;
    ULONG Result;

    MmDeleteKernelStack(OldStack, FALSE);
    KeLeaveGuardedRegion();

    Status = PsConvertToGuiThread();
    if (!NT_SUCCESS(Status) && (Status != STATUS_ALREADY_WIN32))
    {
        Result = (ULONG)KiGetGuiServiceFailureStatus(TrapFrame->R12 & SERVICE_NUMBER_MASK);
    }
    else
    {
        KiArmFlushGdiBatch(Thread, TrapFrame);
        Result = KiArmDispatchService(TrapFrame);
    }
    KiArmCompleteService(TrapFrame, Result);
    KiExceptionExit(TrapFrame, &KI_ARM_TRAP_RECORD(TrapFrame)->ExceptionFrame);
}

static
VOID
KiArmMoveToLargeStack(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKI_ARM_TRAP_RECORD NewRecord;
    PVOID NewStack, OldStack;
    PKTRAP_FRAME NewTrapFrame;

    ASSERT(KiIsUserModeTrap(TrapFrame) && !Thread->LargeStack);
    ASSERT((PUCHAR)KI_ARM_TRAP_RECORD(TrapFrame) + sizeof(KI_ARM_TRAP_RECORD) == (PUCHAR)Thread->InitialStack);

    KeEnterGuardedRegion();
    NewStack = MmCreateKernelStack(TRUE, 0);
    if (!NewStack)
    {
        KeLeaveGuardedRegion();
        return;
    }

    NewRecord = (PKI_ARM_TRAP_RECORD)NewStack - 1;
    RtlCopyMemory(NewRecord, KI_ARM_TRAP_RECORD(TrapFrame), sizeof(*NewRecord));
    NewTrapFrame = &NewRecord->TrapFrame;
    NewTrapFrame->VfpState = &NewRecord->VfpState;

    _disable();
    OldStack = Thread->StackBase;
    Thread->InitialStack = NewStack;
    Thread->StackBase = NewStack;
    Thread->StackLimit = (ULONG_PTR)NewStack - KERNEL_LARGE_STACK_COMMIT;
    Thread->LargeStack = TRUE;
    Thread->TrapFrame = NewTrapFrame;
    _enable();

    KiArmCallOnStack(NewTrapFrame, OldStack, (PVOID)KiArmContinueOnLargeStack, NewRecord);
}

VOID
NTAPI
KiArmSystemServiceDispatch(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG Result;

    KeGetCurrentPrcb()->KeSystemCalls++;
    _enable();

    if (Thread->Header.Minimal && KiIsUserModeTrap(TrapFrame))
    {
        PspPicoSystemCallDispatch(TrapFrame, NULL);
        return;
    }

    if (KiIsUserModeTrap(TrapFrame) && KiArmNeedsGuiConversion(Thread, TrapFrame))
    {
        /* Moving to the large stack continues on it and does not come back here. */
        if (!Thread->LargeStack)
            KiArmMoveToLargeStack(TrapFrame);

        if (!Thread->LargeStack || (!NT_SUCCESS(PsConvertToGuiThread()) && !Thread->GuiThread))
        {
            KiArmCompleteService(TrapFrame, (ULONG)KiGetGuiServiceFailureStatus(TrapFrame->R12 & SERVICE_NUMBER_MASK));
            return;
        }
    }

    KiArmFlushGdiBatch(Thread, TrapFrame);
    Result = KiArmDispatchService(TrapFrame);
    KiArmCompleteService(TrapFrame, Result);
}

PVOID
NTAPI
KeSwitchKernelStack(
    _In_ PVOID StackBase,
    _In_ PVOID StackLimit)
{
    /* The service dispatcher moves a thread to its large stack before the first
     * win32k call, so PsConvertToGuiThread never relocates a live stack here. */
    UNREFERENCED_PARAMETER(StackBase);
    UNREFERENCED_PARAMETER(StackLimit);
    KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_NOT_SUPPORTED, (ULONG_PTR)KeGetCurrentThread(), (ULONG_PTR)StackBase, 0);
}
