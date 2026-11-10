/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hard errors: telling a user about a failure nothing can handle
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PVOID ExpDefaultErrorPort = NULL;
PEPROCESS ExpDefaultErrorPortProcess = NULL;

BOOLEAN ExReadyForErrors = FALSE;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Lays a kernel caller's parameters out in the address space of its own
 * process. Whoever shows the error reads the strings from the process that
 * raised it, so strings held in kernel memory have to be put where it can see
 * them.
 */
static
NTSTATUS
ExpCopyHardErrorParametersToUser(
    _In_ ULONG NumberOfParameters,
    _In_ ULONG UnicodeStringParameterMask,
    _In_ PULONG_PTR Parameters,
    _Out_ PHARDERROR_USER_PARAMETERS *UserParameters,
    _Out_ PSIZE_T UserSize)
{
    UNICODE_STRING Strings[MAXIMUM_HARDERROR_PARAMETERS];
    PHARDERROR_USER_PARAMETERS Block = NULL;
    SIZE_T Size = FIELD_OFFSET(HARDERROR_USER_PARAMETERS, Buffer);
    PWCHAR Buffer;
    ULONG Index;
    NTSTATUS Status = STATUS_SUCCESS;

    *UserParameters = NULL;
    *UserSize = 0;

    _SEH2_TRY
    {
        for (Index = 0; Index < NumberOfParameters; Index++)
        {
            if (!(UnicodeStringParameterMask & (1 << Index)))
            {
                continue;
            }

            Strings[Index] = *(PUNICODE_STRING)Parameters[Index];
            Size += Strings[Index].Length + sizeof(WCHAR);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ZwAllocateVirtualMemory(NtCurrentProcess(),
                                     (PVOID*)&Block,
                                     0,
                                     &Size,
                                     MEM_COMMIT,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    _SEH2_TRY
    {
        Buffer = Block->Buffer;

        for (Index = 0; Index < NumberOfParameters; Index++)
        {
            if (!(UnicodeStringParameterMask & (1 << Index)))
            {
                Block->Parameters[Index] = Parameters[Index];
                continue;
            }

            Block->Strings[Index].Length = Strings[Index].Length;
            Block->Strings[Index].MaximumLength =
                Strings[Index].Length + sizeof(WCHAR);
            Block->Strings[Index].Buffer = Buffer;

            RtlCopyMemory(Buffer,
                          Strings[Index].Buffer,
                          Strings[Index].Length);

            Buffer = (PWCHAR)((PUCHAR)Buffer +
                              Strings[Index].Length + sizeof(WCHAR));

            Block->Parameters[Index] = (ULONG_PTR)&Block->Strings[Index];
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        Size = 0;
        ZwFreeVirtualMemory(NtCurrentProcess(),
                            (PVOID*)&Block,
                            &Size,
                            MEM_RELEASE);
        return Status;
    }

    *UserParameters = Block;
    *UserSize = Size;

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Decides what to do with an exception that reached the top of the kernel. A
 * debugger that is listening gets the exception; otherwise the thread is told
 * to carry on, because killing the system over one thread is worse.
 */
LONG
NTAPI
ExSystemExceptionFilter(VOID)
{
    return (ExGetPreviousMode() != KernelMode) ?
           EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

VOID
NTAPI
ExRaiseAccessViolation(VOID)
{
    ExRaiseStatus(STATUS_ACCESS_VIOLATION);
}

VOID
NTAPI
ExRaiseDatatypeMisalignment(VOID)
{
    ExRaiseStatus(STATUS_DATATYPE_MISALIGNMENT);
}

/*
 * Reached when a pure virtual call is made from C++ code in the kernel, which
 * is a defect in that code and cannot be carried on from.
 */
VOID
__cdecl
_purecall(VOID)
{
    KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED,
                 STATUS_NOT_IMPLEMENTED,
                 (ULONG_PTR)_ReturnAddress(),
                 0,
                 0);
}

/*
 * Writes what is known about a failure to the debugger. This is the whole of
 * what happens to an error raised before anything is listening for one.
 */
VOID
NTAPI
ExpSystemErrorHandler(
    _In_ NTSTATUS ErrorStatus,
    _In_ ULONG NumberOfParameters,
    _In_ ULONG UnicodeStringParameterMask,
    _In_ PULONG_PTR Parameters,
    _In_ BOOLEAN Shutdown)
{
    ULONG Index;

    DPRINT1("Unhandled hard error %lx\n", ErrorStatus);

    _SEH2_TRY
    {
        for (Index = 0;
             (Parameters != NULL) && (Index < NumberOfParameters);
             Index++)
        {
            if (UnicodeStringParameterMask & (1 << Index))
            {
                DPRINT1("  Parameter %lu: %wZ\n",
                        Index,
                        (PUNICODE_STRING)Parameters[Index]);
                continue;
            }

            DPRINT1("  Parameter %lu: %p\n",
                    Index,
                    (PVOID)Parameters[Index]);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    _SEH2_END;

    if (!Shutdown)
    {
        return;
    }

    KeBugCheckEx(FATAL_UNHANDLED_HARD_ERROR,
                 (ULONG_PTR)ErrorStatus,
                 (ULONG_PTR)Parameters,
                 0,
                 0);
}

/*
 * Carries a hard error to whoever is listening for them, and waits for the
 * answer. Nothing listening, or a caller that cannot wait, means the error is
 * only written to the debugger and the default answer is given back.
 */
NTSTATUS
NTAPI
ExpRaiseHardError(
    _In_ NTSTATUS ErrorStatus,
    _In_ ULONG NumberOfParameters,
    _In_ ULONG UnicodeStringParameterMask,
    _In_opt_ PULONG_PTR Parameters,
    _In_ ULONG ValidResponseOptions,
    _Out_ PULONG Response)
{
    PEPROCESS Process = PsGetCurrentProcess();
    PETHREAD Thread = PsGetCurrentThread();
    HARDERROR_MSG Message;
    SIZE_T BufferLength = sizeof(Message);
    NTSTATUS Status;

    PAGED_CODE();

    if (ValidResponseOptions == OptionShutdownSystem)
    {
        if (!SeSinglePrivilegeCheck(SeShutdownPrivilege, ExGetPreviousMode()))
        {
            return STATUS_PRIVILEGE_NOT_HELD;
        }

        ExpSystemErrorHandler(ErrorStatus,
                              NumberOfParameters,
                              UnicodeStringParameterMask,
                              Parameters,
                              TRUE);
    }

    /*
     * A thread or a process that asked not to be shown errors gets them back
     * as its own to deal with, unless the error says it overrides that.
     */
    if (Thread->HardErrorsAreDisabled ||
        (!(Process->DefaultHardErrorProcessing & SEM_FAILCRITICALERRORS) &&
         !(ErrorStatus & HARDERROR_OVERRIDE_ERRORMODE)))
    {
        *Response = ResponseReturnToCaller;
        return STATUS_SUCCESS;
    }

    *Response = ResponseNotHandled;

    /*
     * An error that reaches here before anyone can be told about it, or one
     * raised by the listening process itself, has nowhere to go but the
     * debugger.
     */
    if (!ExReadyForErrors || (ExpDefaultErrorPort == NULL) ||
        (Process == ExpDefaultErrorPortProcess))
    {
        ExpSystemErrorHandler(ErrorStatus,
                              NumberOfParameters,
                              UnicodeStringParameterMask,
                              Parameters,
                              (BOOLEAN)(ExGetPreviousMode() == KernelMode));

        return STATUS_SUCCESS;
    }

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        return STATUS_UNSUCCESSFUL;
    }

    RtlZeroMemory(&Message, sizeof(Message));

    Message.h.u1.s1.DataLength =
        (CSHORT)(sizeof(HARDERROR_MSG) - sizeof(PORT_MESSAGE));
    Message.h.u1.s1.TotalLength = (CSHORT)sizeof(HARDERROR_MSG);
    Message.h.u2.ZeroInit = 0;
    Message.h.u2.s2.Type = LPC_ERROR_EVENT;

    Message.Status = ErrorStatus & ~HARDERROR_OVERRIDE_ERRORMODE;
    Message.ValidResponseOptions = ValidResponseOptions;
    Message.NumberOfParameters = NumberOfParameters;
    Message.UnicodeStringParameterMask = UnicodeStringParameterMask;
    Message.Response = ResponseNotHandled;

    KeQuerySystemTime(&Message.ErrorTime);

    if (Parameters != NULL)
    {
        RtlCopyMemory(Message.Parameters,
                      Parameters,
                      NumberOfParameters * sizeof(ULONG_PTR));
    }

    Status = LpcSendWaitReceivePort(ExpDefaultErrorPort,
                                    ALPC_MSGFLG_SYNC_REQUEST,
                                    &Message.h,
                                    &Message.h,
                                    &BufferLength,
                                    NULL);
    if (!NT_SUCCESS(Status))
    {
        ExpSystemErrorHandler(ErrorStatus,
                              NumberOfParameters,
                              UnicodeStringParameterMask,
                              Parameters,
                              FALSE);

        return Status;
    }

    *Response = Message.Response;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ExRaiseHardError(
    _In_ NTSTATUS ErrorStatus,
    _In_ ULONG NumberOfParameters,
    _In_ ULONG UnicodeStringParameterMask,
    _In_opt_ PULONG_PTR Parameters,
    _In_ ULONG ValidResponseOptions,
    _Out_ PULONG Response)
{
    PHARDERROR_USER_PARAMETERS UserParameters;
    SIZE_T UserSize;
    NTSTATUS Status;

    PAGED_CODE();

    if (NumberOfParameters > MAXIMUM_HARDERROR_PARAMETERS)
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    if ((Parameters == NULL) || (UnicodeStringParameterMask == 0))
    {
        return ExpRaiseHardError(ErrorStatus,
                                 NumberOfParameters,
                                 UnicodeStringParameterMask,
                                 Parameters,
                                 ValidResponseOptions,
                                 Response);
    }

    Status = ExpCopyHardErrorParametersToUser(NumberOfParameters,
                                              UnicodeStringParameterMask,
                                              Parameters,
                                              &UserParameters,
                                              &UserSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ExpRaiseHardError(ErrorStatus,
                               NumberOfParameters,
                               UnicodeStringParameterMask,
                               UserParameters->Parameters,
                               ValidResponseOptions,
                               Response);

    UserSize = 0;
    ZwFreeVirtualMemory(NtCurrentProcess(),
                        (PVOID*)&UserParameters,
                        &UserSize,
                        MEM_RELEASE);

    return Status;
}

/*
 * A caller in user mode names its strings by address, and those addresses are
 * what is passed on: whoever shows the error reads them from the caller's own
 * process. They are only checked here, so that a caller cannot hand in a
 * string it could not have read itself.
 */
NTSTATUS
NTAPI
NtRaiseHardError(
    _In_ NTSTATUS ErrorStatus,
    _In_ ULONG NumberOfParameters,
    _In_ ULONG UnicodeStringParameterMask,
    _In_opt_ PULONG_PTR Parameters,
    _In_ ULONG ValidResponseOptions,
    _Out_ PULONG Response)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ULONG_PTR SafeParameters[MAXIMUM_HARDERROR_PARAMETERS];
    UNICODE_STRING String;
    ULONG SafeResponse = ResponseNotHandled;
    ULONG Index;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (NumberOfParameters > MAXIMUM_HARDERROR_PARAMETERS)
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    if ((Parameters != NULL) && (NumberOfParameters == 0))
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    if (PreviousMode == KernelMode)
    {
        return ExRaiseHardError(ErrorStatus,
                                NumberOfParameters,
                                UnicodeStringParameterMask,
                                Parameters,
                                ValidResponseOptions,
                                Response);
    }

    _SEH2_TRY
    {
        ProbeForWriteUlong(Response);

        if (Parameters != NULL)
        {
            ProbeForRead(Parameters,
                         NumberOfParameters * sizeof(ULONG_PTR),
                         sizeof(ULONG_PTR));

            RtlCopyMemory(SafeParameters,
                          Parameters,
                          NumberOfParameters * sizeof(ULONG_PTR));

            for (Index = 0; Index < NumberOfParameters; Index++)
            {
                if (!(UnicodeStringParameterMask & (1 << Index)))
                {
                    continue;
                }

                String = ProbeForReadUnicodeString(
                             (PUNICODE_STRING)SafeParameters[Index]);

                ProbeForRead(String.Buffer, String.Length, sizeof(UCHAR));
            }
        }
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ExpRaiseHardError(ErrorStatus,
                               NumberOfParameters,
                               UnicodeStringParameterMask,
                               (Parameters != NULL) ? SafeParameters : NULL,
                               ValidResponseOptions,
                               &SafeResponse);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    _SEH2_TRY
    {
        *Response = SafeResponse;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/*
 * Names the port the session manager listens on. Only the session manager may
 * do this, and only once, because the whole system's errors go there.
 */
NTSTATUS
NTAPI
NtSetDefaultHardErrorPort(
    _In_ HANDLE PortHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PVOID Port;
    NTSTATUS Status;

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if (ExpDefaultErrorPort != NULL)
    {
        return STATUS_UNSUCCESSFUL;
    }

    Status = ObReferenceObjectByHandle(PortHandle,
                                       0,
                                       LpcPortObjectType,
                                       PreviousMode,
                                       &Port,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    ExpDefaultErrorPort = Port;
    ExpDefaultErrorPortProcess = PsGetCurrentProcess();
    ExReadyForErrors = TRUE;

    return STATUS_SUCCESS;
}

/* EOF */
