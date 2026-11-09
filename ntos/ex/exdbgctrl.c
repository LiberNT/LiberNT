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
    ULONG Length = 0;
    NTSTATUS Status;

    PAGED_CODE();

    /*
     * Some of these read or write any address in the machine, so the whole call
     * is behind the one privilege that stands for being trusted with it.
     */
    switch (Command)
    {
        case SysDbgQueryModuleInformation:
        case SysDbgBreakPoint:
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
        case SysDbgEnableKernelDebugger:
        case SysDbgDisableKernelDebugger:
        case SysDbgGetAutoKdEnable:
        case SysDbgSetAutoKdEnable:
        case SysDbgGetPrintBufferSize:
        case SysDbgSetPrintBufferSize:
        case SysDbgGetKdUmExceptionEnable:
        case SysDbgSetKdUmExceptionEnable:
        case SysDbgGetKdBlockEnable:
        case SysDbgSetKdBlockEnable:
        {
            if (!SeSinglePrivilegeCheck(SeDebugPrivilege, PreviousMode))
            {
                return STATUS_ACCESS_DENIED;
            }

            break;
        }

        default:
            return STATUS_INVALID_INFO_CLASS;
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

    Status = KdSystemDebugControl(Command,
                                  InputBuffer,
                                  InputBufferLength,
                                  OutputBuffer,
                                  OutputBufferLength,
                                  &Length,
                                  PreviousMode);

    if (ReturnLength == NULL)
    {
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
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

    *ReturnLength = Length;
    return Status;
}

/* EOF */
