/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stopping the machine for the debugger, and turning it on and off
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdpriv.h"
#ifdef KDBG
#include "../kdbg/kdb.h"
#endif
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Stops the machine so that this processor can talk to the host: every other
 * processor is frozen and the debug link is taken. The value returned has to
 * be handed back to KdExitDebugger(), which is what lets the others run again.
 */
BOOLEAN
NTAPI
KdEnterDebugger(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    BOOLEAN Enable;

    /* How long the machine is held is discounted from what it measures */
    KdTimerStart = KeQueryPerformanceCounter(NULL);

    /* Only one processor may be running while the host is being served */
    Enable = KeFreezeExecution(TrapFrame, ExceptionFrame);

    KdpPortLock();
    KdEnteredDebugger = TRUE;

    return Enable;
}

/*
 * Lets the machine go again.
 */
VOID
NTAPI
KdExitDebugger(
    _In_ BOOLEAN Enable)
{
    KdEnteredDebugger = FALSE;
    KdpPortUnlock();

    /* Add what this stop cost to what every later measurement discounts */
    KdTimerStop = KeQueryPerformanceCounter(NULL);
    if (KdTimerStop.QuadPart > KdTimerStart.QuadPart)
    {
        KdTimerDifference.QuadPart +=
            KdTimerStop.QuadPart - KdTimerStart.QuadPart;
    }

    KeThawExecution(Enable);
}

/*
 * Turns the debugger back on. Enabling is counted, so a debugger disabled
 * twice has to be enabled twice.
 */
NTSTATUS
NTAPI
KdEnableDebuggerWithLock(
    _In_ BOOLEAN NeedLock)
{
    NTSTATUS Status = STATUS_SUCCESS;

    /* A debugger that was never set up has nothing to turn on */
    if (KdPitchDebugger || !KdpDebuggerStructuresInitialized)
        return STATUS_DEBUGGER_INACTIVE;

    /* Once the system has forbidden it, it stays forbidden */
    if (KdBlockEnable)
        return STATUS_ACCESS_DENIED;

    if (NeedLock)
        KdpPortLock();

    if (KdDisableCount == 0)
    {
        /* Nothing had turned it off */
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        KdDisableCount--;

        if (KdDisableCount == 0)
        {
            KdDebuggerEnabled = TRUE;
            KdPreviouslyEnabled = TRUE;
            KiDebugRoutine = KdpTrap;
            SharedUserData->KdDebuggerEnabled = TRUE;
        }
    }

    if (NeedLock)
        KdpPortUnlock();

    return Status;
}

/*
 * Turns the debugger off. Whether that is safe at all is the architecture's
 * to say: a breakpoint still written into memory would fire with nobody to
 * answer for it.
 */
NTSTATUS
NTAPI
KdDisableDebuggerWithLock(
    _In_ BOOLEAN NeedLock)
{
    NTSTATUS Status;

    if (KdPitchDebugger || !KdpDebuggerStructuresInitialized)
        return STATUS_DEBUGGER_INACTIVE;

    Status = KdpAllowDisable();
    if (!NT_SUCCESS(Status))
        return Status;

    if (NeedLock)
        KdpPortLock();

    if (KdDisableCount == 0)
    {
        KdDebuggerEnabled = FALSE;
        KiDebugRoutine = KdpStub;
        SharedUserData->KdDebuggerEnabled = FALSE;
    }

    KdDisableCount++;

    if (NeedLock)
        KdpPortUnlock();

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdEnableDebugger(VOID)
{
    return KdEnableDebuggerWithLock(TRUE);
}

NTSTATUS
NTAPI
KdDisableDebugger(VOID)
{
    return KdDisableDebuggerWithLock(TRUE);
}

/*
 * Re-reads whether a host is on the other end. The answer is kept up to date
 * by every exchange, so this only looks for news: a host that answers a poll
 * is certainly there, while silence is not proof that it has gone.
 */
BOOLEAN
NTAPI
KdRefreshDebuggerNotPresent(VOID)
{
    BOOLEAN Enable;

    if (!KdDebuggerEnabled || !KdpDebuggerStructuresInitialized)
    {
        KdDebuggerNotPresent = TRUE;
        return TRUE;
    }

    Enable = KdEnterDebugger(NULL, NULL);

    if (KdpPollBreakInWithPortLock())
        KdDebuggerNotPresent = FALSE;

    KdExitDebugger(Enable);

    return KdDebuggerNotPresent;
}

/*
 * The only option the published interface has. Blocking is one way: a system
 * that has forbidden the debugger from being enabled cannot take it back,
 * since whatever it was protecting is still worth protecting.
 */
NTSTATUS
NTAPI
KdChangeOption(
    _In_ KD_OPTION Option,
    _In_opt_ ULONG InBufferBytes,
    _In_ PVOID InBuffer,
    _In_opt_ ULONG OutBufferBytes,
    _Out_ PVOID OutBuffer,
    _Out_opt_ PULONG OutBufferNeeded)
{
    UNREFERENCED_PARAMETER(OutBuffer);

    if (KdPitchDebugger)
        return STATUS_DEBUGGER_INACTIVE;

    switch (Option)
    {
        case KD_OPTION_SET_BLOCK_ENABLE:

            if (InBufferBytes != sizeof(BOOLEAN))
                return STATUS_INVALID_PARAMETER_2;

            if (InBuffer == NULL)
                return STATUS_INVALID_PARAMETER_3;

            if (OutBufferBytes != 0)
                return STATUS_INVALID_PARAMETER_4;

            if (OutBufferNeeded != NULL)
                *OutBufferNeeded = 0;

            if (KdBlockEnable && !(*(PBOOLEAN)InBuffer))
                return STATUS_ACCESS_DENIED;

            KdBlockEnable = *(PBOOLEAN)InBuffer;
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER_1;
    }
}

/*
 * The debug hardware is going to sleep or has just woken up.
 */
NTSTATUS
NTAPI
KdPowerTransition(
    _In_ DEVICE_POWER_STATE NewState)
{
    if (NewState == PowerDeviceD0)
        return KdD0Transition();

    if (NewState == PowerDeviceD3)
        return KdD3Transition();

    return STATUS_INVALID_PARAMETER;
}

/*
 * The host asked the machine to crash. It is told nothing further, because
 * there is nothing further to tell.
 */
VOID
NTAPI
KdpCauseBugCheck(
    _In_ PDBGKD_MANIPULATE_STATE64 State)
{
    UNREFERENCED_PARAMETER(State);

    KeBugCheckEx(MANUALLY_INITIATED_CRASH, 0, 0, 0, 0);
}

/* SYSTEM DEBUG CONTROL *******************************************************/

static
NTSTATUS
KdpSysDebugCopyVirtual(
    _In_ PSYSDBG_VIRTUAL Request,
    _In_ BOOLEAN Write,
    _Out_ PULONG Length)
{
    /* The request names the machine's address and the caller's own buffer */
    return KdpCopyMemoryChunks((ULONG64)(ULONG_PTR)Request->Address,
                               Request->Buffer,
                               Request->Request,
                               0,
                               Write ? MMDBG_COPY_WRITE : 0,
                               Length);
}

static
NTSTATUS
KdpSysDebugCopyPhysical(
    _In_ PSYSDBG_PHYSICAL Request,
    _In_ BOOLEAN Write,
    _Out_ PULONG Length)
{
    ULONG Flags = MMDBG_COPY_PHYSICAL | MMDBG_COPY_CACHED;

    if (Write)
        Flags |= MMDBG_COPY_WRITE;

    return KdpCopyMemoryChunks((ULONG64)Request->Address.QuadPart,
                               Request->Buffer,
                               Request->Request,
                               0,
                               Flags,
                               Length);
}

/*
 * The parts of the system debug interface that only the debugger can answer.
 * Everything here reads or writes the machine itself, which is why the caller
 * must already be in the kernel: a user-mode caller is refused, as it is by
 * the system call that leads here.
 */
NTSTATUS
NTAPI
KdSystemDebugControl(
    _In_ SYSDBG_COMMAND Command,
    _In_reads_bytes_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _Out_opt_ PULONG ReturnLength,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status;
    ULONG Length = 0;

    switch ((ULONG)Command)
    {
        case SysDbgQueryVersion:

            if (OutputBufferLength < sizeof(DBGKD_GET_VERSION64))
            {
                if (ReturnLength != NULL)
                    *ReturnLength = sizeof(DBGKD_GET_VERSION64);

                return STATUS_BUFFER_TOO_SMALL;
            }

            KdpSysGetVersion((PDBGKD_GET_VERSION64)OutputBuffer);
            Length = sizeof(DBGKD_GET_VERSION64);
            Status = STATUS_SUCCESS;
            break;

        case SysDbgReadVirtual:
        case SysDbgWriteVirtual:

            if (PreviousMode != KernelMode)
                return STATUS_NOT_IMPLEMENTED;

            if (InputBufferLength != sizeof(SYSDBG_VIRTUAL))
                return STATUS_INFO_LENGTH_MISMATCH;

            Status = KdpSysDebugCopyVirtual((PSYSDBG_VIRTUAL)InputBuffer,
                                            (Command == SysDbgWriteVirtual),
                                            &Length);
            break;

        case SysDbgReadPhysical:
        case SysDbgWritePhysical:

            if (PreviousMode != KernelMode)
                return STATUS_NOT_IMPLEMENTED;

            if (InputBufferLength != sizeof(SYSDBG_PHYSICAL))
                return STATUS_INFO_LENGTH_MISMATCH;

            Status = KdpSysDebugCopyPhysical((PSYSDBG_PHYSICAL)InputBuffer,
                                             (Command == SysDbgWritePhysical),
                                             &Length);
            break;

        case SysDbgReadControlSpace:
        case SysDbgWriteControlSpace:
        {
            PSYSDBG_CONTROL_SPACE Request = (PSYSDBG_CONTROL_SPACE)InputBuffer;

            if (PreviousMode != KernelMode)
                return STATUS_NOT_IMPLEMENTED;

            if (InputBufferLength != sizeof(SYSDBG_CONTROL_SPACE))
                return STATUS_INFO_LENGTH_MISMATCH;

            if (Command == SysDbgReadControlSpace)
            {
                Status = KdpSysReadControlSpace(Request->Processor,
                                                Request->Address,
                                                Request->Buffer,
                                                Request->Request,
                                                &Length);
            }
            else
            {
                Status = KdpSysWriteControlSpace(Request->Processor,
                                                 Request->Address,
                                                 Request->Buffer,
                                                 Request->Request,
                                                 &Length);
            }
            break;
        }

        case SysDbgReadIoSpace:
        case SysDbgWriteIoSpace:
        {
            PSYSDBG_IO_SPACE Request = (PSYSDBG_IO_SPACE)InputBuffer;

            if (PreviousMode != KernelMode)
                return STATUS_NOT_IMPLEMENTED;

            if (InputBufferLength != sizeof(SYSDBG_IO_SPACE))
                return STATUS_INFO_LENGTH_MISMATCH;

            if (Command == SysDbgReadIoSpace)
            {
                Status = KdpSysReadIoSpace(Request->InterfaceType,
                                           Request->BusNumber,
                                           Request->AddressSpace,
                                           Request->Address,
                                           Request->Buffer,
                                           Request->Request,
                                           &Length);
            }
            else
            {
                Status = KdpSysWriteIoSpace(Request->InterfaceType,
                                            Request->BusNumber,
                                            Request->AddressSpace,
                                            Request->Address,
                                            Request->Buffer,
                                            Request->Request,
                                            &Length);
            }
            break;
        }

        case SysDbgReadMsr:
        case SysDbgWriteMsr:
        {
            PSYSDBG_MSR Request = (PSYSDBG_MSR)InputBuffer;

            if (PreviousMode != KernelMode)
                return STATUS_NOT_IMPLEMENTED;

            if (InputBufferLength != sizeof(SYSDBG_MSR))
                return STATUS_INFO_LENGTH_MISMATCH;

            if (Command == SysDbgReadMsr)
                Status = KdpSysReadMsr(Request->Address, &Request->Data);
            else
                Status = KdpSysWriteMsr(Request->Address, &Request->Data);

            break;
        }

        case SysDbgReadBusData:
        case SysDbgWriteBusData:
        {
            PSYSDBG_BUS_DATA Request = (PSYSDBG_BUS_DATA)InputBuffer;

            if (PreviousMode != KernelMode)
                return STATUS_NOT_IMPLEMENTED;

            if (InputBufferLength != sizeof(SYSDBG_BUS_DATA))
                return STATUS_INFO_LENGTH_MISMATCH;

            if (Command == SysDbgReadBusData)
            {
                Status = KdpSysReadBusData(Request->BusDataType,
                                           Request->BusNumber,
                                           Request->SlotNumber,
                                           Request->Address,
                                           Request->Buffer,
                                           Request->Request,
                                           &Length);
            }
            else
            {
                Status = KdpSysWriteBusData(Request->BusDataType,
                                            Request->BusNumber,
                                            Request->SlotNumber,
                                            Request->Address,
                                            Request->Buffer,
                                            Request->Request,
                                            &Length);
            }
            break;
        }

        case SysDbgCheckLowMemory:

            if (PreviousMode != KernelMode)
                return STATUS_NOT_IMPLEMENTED;

            Status = KdpSysCheckLowMemory(0);
            break;

#ifdef KDBG
        case 'RbdK':

            /*
             * A component adds a command of its own to the interactive
             * debugger. The length carries whether it is being removed.
             */
            Status = KdbRegisterCliCallback(InputBuffer,
                                            (BOOLEAN)InputBufferLength)
                     ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
            break;

        case 'DsoR':
        {
            /* A component hands over a stack it has already walked */
            PULONG_PTR Frames = (PULONG_PTR)InputBuffer;
            ULONG Count = InputBufferLength;
            ULONG Index;

            for (Index = 0; Index < Count; Index++)
            {
                if (Frames[Index] == 0)
                    break;

                if (!KdbSymPrintAddress((PVOID)Frames[Index], NULL))
                    KdbPrintf("<%p>\n", (PVOID)Frames[Index]);
                else
                    KdbPuts("\n");
            }

            Status = STATUS_SUCCESS;
            break;
        }
#endif

        default:
            /* TODO: 'CsoR', the system-call hook registration, has no
             * architecture-neutral home and is not served */
            Status = STATUS_NOT_IMPLEMENTED;
            break;
    }

    if (ReturnLength != NULL)
        *ReturnLength = Length;

    return Status;
}

/* EOF */
