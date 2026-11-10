/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What a debugger may ask the kernel to do on its behalf
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

WORK_QUEUE_ITEM ExpDebuggerWorkItem;
WINKD_WORKER_STATE ExpDebuggerWork = WinKdWorkerReady;
PEPROCESS ExpDebuggerProcessAttach = NULL;
PEPROCESS ExpDebuggerProcessKill = NULL;
ULONG_PTR ExpDebuggerPageIn = 0;

/* PRIVATE DATA ***************************************************************/

#define EXP_DBGCTRL_TAG 'gbdE'

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Carries out, at a level where waiting is allowed, what the debugger asked for
 * while it had the machine stopped: attaching to a process, killing one, or
 * bringing a page in so the debugger can read it.
 */
VOID
NTAPI
ExpDebuggerWorker(
    _In_ PVOID Context)
{
    PEPROCESS Attach, Kill;
    ULONG_PTR PageIn;

    UNREFERENCED_PARAMETER(Context);

    if (ExpDebuggerWork != WinKdWorkerStart)
    {
        return;
    }

    Attach = ExpDebuggerProcessAttach;
    Kill = ExpDebuggerProcessKill;
    PageIn = ExpDebuggerPageIn;

    ExpDebuggerProcessAttach = NULL;
    ExpDebuggerProcessKill = NULL;
    ExpDebuggerPageIn = 0;

    ExpDebuggerWork = WinKdWorkerReady;

    if (Kill != NULL)
    {
        /* TODO: the process the debugger asked to end is not ended. */
        DPRINT1("Debugger asked to kill process %p\n", Kill);
    }

    if (Attach != NULL)
    {
        /* TODO: the process the debugger asked to attach to is not attached. */
        DPRINT1("Debugger asked to attach to process %p\n", Attach);
    }

    if (PageIn != 0)
    {
        /* TODO: the address the debugger asked for is not brought in. */
        DPRINT1("Debugger asked to page in %p\n", (PVOID)PageIn);
    }
}

static
NTSTATUS
ExpReadDebugOption(
    _In_ PVOID Value,
    _In_ ULONG ValueLength,
    _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _Out_ PULONG Length)
{
    if (OutputBufferLength != ValueLength) return STATUS_INFO_LENGTH_MISMATCH;

    _SEH2_TRY
    {
        RtlCopyMemory(OutputBuffer, Value, ValueLength);
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    *Length = ValueLength;

    return STATUS_SUCCESS;
}

static
NTSTATUS
ExpCaptureDebugOption(
    _Out_writes_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength)
{
    if (InputBufferLength != ValueLength) return STATUS_INFO_LENGTH_MISMATCH;

    _SEH2_TRY
    {
        RtlCopyMemory(Value, InputBuffer, ValueLength);
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtSystemDebugControl(
    _In_ SYSDBG_COMMAND Command,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    UNICODE_STRING FileName;
    BOOLEAN Flag = FALSE;
    ULONG Size = 0;
    ULONG Length = 0;
    NTSTATUS Status;

    PAGED_CODE();

    /*
     * Some of these read or write any address in the machine, so the whole call
     * is behind the one privilege that stands for being trusted with it.
     */
    if (!SeSinglePrivilegeCheck(SeDebugPrivilege, PreviousMode))
    {
        return STATUS_ACCESS_DENIED;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            if ((InputBuffer != NULL) && (InputBufferLength != 0))
            {
                ProbeForRead(InputBuffer, InputBufferLength, sizeof(ULONG));
            }

            if ((OutputBuffer != NULL) && (OutputBufferLength != 0))
            {
                ProbeForWrite(OutputBuffer, OutputBufferLength, sizeof(ULONG));
            }

            if (ReturnLength != NULL)
            {
                ProbeForWriteUlong(ReturnLength);
            }
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /* The two dump requests are served by the system itself; every other one needs a debugger */
    if (Command == SysDbgGetTriageDump)
    {
        if (InputBufferLength != sizeof(SYSDBG_TRIAGE_DUMP))
        {
            return STATUS_INFO_LENGTH_MISMATCH;
        }

        /* TODO: a triage dump of a running system is not written */
        return STATUS_NOT_IMPLEMENTED;
    }

    if (Command == SysDbgGetLiveKernelDump)
    {
        if (InputBufferLength != sizeof(SYSDBG_LIVEDUMP_CONTROL))
        {
            return STATUS_INFO_LENGTH_MISMATCH;
        }

        /* TODO: a live kernel dump is not written */
        return STATUS_NOT_IMPLEMENTED;
    }

    if (!KdDebuggerEnabled || KdPitchDebugger || KdDebuggerNotPresent)
    {
        return STATUS_DEBUGGER_INACTIVE;
    }

    switch ((ULONG)Command)
    {
        case SysDbgQueryModuleInformation:
        case SysDbgQueryTraceInformation:
        case SysDbgSetTracepoint:
        case SysDbgSetSpecialCall:
        case SysDbgClearSpecialCalls:
        case SysDbgQuerySpecialCalls:

            /* TODO: the call tracing requests of the debugger are not served */
            Status = STATUS_NOT_IMPLEMENTED;
            break;

        case SysDbgBreakPoint:

            DbgBreakPointWithStatus(DBG_STATUS_DEBUG_CONTROL);
            Status = STATUS_SUCCESS;
            break;

        case SysDbgQueryVersion:
        case SysDbgReadVirtual:
        case SysDbgWriteVirtual:
        case SysDbgReadPhysical:
        case SysDbgWritePhysical:
        case SysDbgReadControlSpace:
        case SysDbgWriteControlSpace:
        case SysDbgReadIoSpace:
        case SysDbgWriteIoSpace:
        case SysDbgReadMsr:
        case SysDbgWriteMsr:
        case SysDbgReadBusData:
        case SysDbgWriteBusData:
        case SysDbgCheckLowMemory:

            Status = KdSystemDebugControl(Command,
                                          InputBuffer,
                                          InputBufferLength,
                                          OutputBuffer,
                                          OutputBufferLength,
                                          &Length,
                                          PreviousMode);
            break;

        case SysDbgEnableKernelDebugger:

            Status = KdEnableDebugger();
            break;

        case SysDbgDisableKernelDebugger:

            Status = KdDisableDebugger();
            break;

        case SysDbgGetAutoKdEnable:

            Flag = KdAutoEnableOnEvent;
            Status = ExpReadDebugOption(&Flag,
                                        sizeof(Flag),
                                        OutputBuffer,
                                        OutputBufferLength,
                                        &Length);
            break;

        case SysDbgSetAutoKdEnable:

            Status = ExpCaptureDebugOption(&Flag,
                                           sizeof(Flag),
                                           InputBuffer,
                                           InputBufferLength);
            if (NT_SUCCESS(Status)) KdAutoEnableOnEvent = (Flag != FALSE);
            break;

        case SysDbgGetPrintBufferSize:

            Size = KdPrintBufferSize;
            Status = ExpReadDebugOption(&Size,
                                        sizeof(Size),
                                        OutputBuffer,
                                        OutputBufferLength,
                                        &Length);
            break;

        case SysDbgSetPrintBufferSize:

            Status = ExpCaptureDebugOption(&Size,
                                           sizeof(Size),
                                           InputBuffer,
                                           InputBufferLength);

            /* TODO: the print buffer keeps the size it was given at start-up */
            if (NT_SUCCESS(Status)) Status = STATUS_NOT_IMPLEMENTED;
            break;

        case SysDbgGetKdUmExceptionEnable:

            Flag = !KdIgnoreUmExceptions;
            Status = ExpReadDebugOption(&Flag,
                                        sizeof(Flag),
                                        OutputBuffer,
                                        OutputBufferLength,
                                        &Length);
            break;

        case SysDbgSetKdUmExceptionEnable:

            Status = ExpCaptureDebugOption(&Flag,
                                           sizeof(Flag),
                                           InputBuffer,
                                           InputBufferLength);
            if (NT_SUCCESS(Status)) KdIgnoreUmExceptions = (Flag == FALSE);
            break;

        case SysDbgGetKdBlockEnable:

            Flag = KdBlockEnable;
            Status = ExpReadDebugOption(&Flag,
                                        sizeof(Flag),
                                        OutputBuffer,
                                        OutputBufferLength,
                                        &Length);
            break;

        case SysDbgSetKdBlockEnable:

            Status = ExpCaptureDebugOption(&Flag,
                                           sizeof(Flag),
                                           InputBuffer,
                                           InputBufferLength);
            if (NT_SUCCESS(Status))
            {
                Status = KdChangeOption(KD_OPTION_SET_BLOCK_ENABLE,
                                        sizeof(Flag),
                                        &Flag,
                                        0,
                                        NULL,
                                        NULL);
            }
            break;

        case SysDbgRegisterForUmBreakInfo:
        case SysDbgGetUmBreakPid:
        case SysDbgClearUmBreakPid:
        case SysDbgGetUmAttachPid:
        case SysDbgClearUmAttachPid:

            /* TODO: the user-mode break and attach bookkeeping is not kept */
            Status = STATUS_NOT_IMPLEMENTED;
            break;

        case SysDbgKdPullRemoteFile:

            if (InputBufferLength != sizeof(SYSDBG_KD_PULL_REMOTE_FILE))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }

            Status = ProbeAndCaptureUnicodeString(
                &FileName,
                PreviousMode,
                &((PSYSDBG_KD_PULL_REMOTE_FILE)InputBuffer)->ImageFileName);
            if (!NT_SUCCESS(Status)) break;

            Status = KdPullRemoteFile(&FileName);

            ReleaseCapturedUnicodeString(&FileName, PreviousMode);
            break;

        default:

            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }

    if (ReturnLength == NULL)
    {
        return Status;
    }

    _SEH2_TRY
    {
        *ReturnLength = Length;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* EOF */
