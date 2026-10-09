/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ps/arm/psctx.c
 * PURPOSE:         Process Manager: Set/Get Context for ARM
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

VOID
NTAPI
PspGetContext(IN PKTRAP_FRAME TrapFrame,
              IN PVOID NonVolatileContext,
              IN OUT PCONTEXT Context)
{
    PAGED_CODE();
}

VOID
NTAPI
PspSetContext(OUT PKTRAP_FRAME TrapFrame,
              OUT PVOID NonVolatileContext,
              IN PCONTEXT Context,
              IN KPROCESSOR_MODE Mode)
{
    PAGED_CODE();
}

VOID
NTAPI
PspGetOrSetContextKernelRoutine(IN PKAPC Apc,
                                IN OUT PKNORMAL_ROUTINE* NormalRoutine,
                                IN OUT PVOID* NormalContext,
                                IN OUT PVOID* SystemArgument1,
                                IN OUT PVOID* SystemArgument2)
{
    UNIMPLEMENTED;
}

/* EOF */

VOID
NTAPI
PsArchInitializeUserThreadContext(
    _Out_ PCONTEXT Context,
    _In_ PVOID ThreadStart,
    _In_ PVOID StartRoutine,
    _In_opt_ PVOID Argument,
    _In_ PVOID StackBase)
{
    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = CONTEXT_FULL;
    Context->Pc = (ULONG)ThreadStart;
    Context->R0 = (ULONG)StartRoutine;
    Context->R1 = (ULONG)Argument;
    Context->Sp = (ULONG)StackBase;
    Context->Cpsr = 0x10;
}

NTSTATUS
NTAPI
PspArchCaptureXStateContext(
    _In_ PCONTEXT Context,
    _In_ ULONG ContextFlags,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN SetContext,
    _Out_ PVOID *XState)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(ContextFlags);
    UNREFERENCED_PARAMETER(PreviousMode);
    UNREFERENCED_PARAMETER(SetContext);

    *XState = NULL;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PspArchCompleteXStateContext(
    _In_opt_ PVOID XState,
    _In_ BOOLEAN CopyOut)
{
    UNREFERENCED_PARAMETER(XState);
    UNREFERENCED_PARAMETER(CopyOut);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PspArchSetInstrumentationCallback(
    _In_ PEPROCESS Process,
    _In_reads_bytes_(Length) PVOID Information,
    _In_ ULONG Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(Information);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(PreviousMode);

    return STATUS_INVALID_INFO_CLASS;
}

VOID
NTAPI
PspArchInitializeUserThreadStartup(
    _In_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    KiInitializeUserApc(ExceptionFrame,
                        TrapFrame,
                        PspSystemDllEntryPoint,
                        NULL,
                        PspSystemDllBase,
                        NULL);
}
