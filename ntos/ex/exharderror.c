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

/* PRIVATE DATA ***************************************************************/

#define EXP_HARDERROR_TAG 'rrEH'

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Brings one of the caller's strings into the kernel, so the error carries what
 * the caller said at the moment it said it and not whatever it says later.
 */
static
NTSTATUS
ExpCaptureHardErrorString(
    _Out_ PUNICODE_STRING Captured,
    _In_ PUNICODE_STRING Source)
{
    NTSTATUS Status = STATUS_SUCCESS;

    Captured->Length = 0;
    Captured->MaximumLength = 0;
    Captured->Buffer = NULL;

    if ((Source->Length == 0) || (Source->Buffer == NULL))
    {
        return STATUS_SUCCESS;
    }

    Captured->Buffer = ExAllocatePoolZero(PagedPool,
                                          Source->Length + sizeof(WCHAR),
                                          EXP_HARDERROR_TAG);
    if (Captured->Buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    _SEH2_TRY
    {
        ProbeForRead(Source->Buffer, Source->Length, sizeof(WCHAR));
        RtlCopyMemory(Captured->Buffer, Source->Buffer, Source->Length);
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Captured->Buffer, EXP_HARDERROR_TAG);
        Captured->Buffer = NULL;
        return Status;
    }

    Captured->Length = Source->Length;
    Captured->MaximumLength = Source->Length + sizeof(WCHAR);

    return STATUS_SUCCESS;
}

static
VOID
ExpFreeHardErrorStrings(
    _Inout_ PHARDERROR_USER_PARAMETERS Captured,
    _In_ ULONG NumberOfParameters,
    _In_ ULONG UnicodeStringParameterMask)
{
    ULONG Index;

    for (Index = 0; Index < NumberOfParameters; Index++)
    {
        if (!(UnicodeStringParameterMask & (1 << Index)))
        {
            continue;
        }

        if (Captured->Strings[Index].Buffer != NULL)
        {
            ExFreePoolWithTag(Captured->Strings[Index].Buffer,
                              EXP_HARDERROR_TAG);
            Captured->Strings[Index].Buffer = NULL;
        }
    }
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

    for (Index = 0; Index < NumberOfParameters; Index++)
    {
        if (UnicodeStringParameterMask & (1 << Index))
        {
            DPRINT1("  Parameter %lu: %wZ\n",
                    Index,
                    (PUNICODE_STRING)Parameters[Index]);
            continue;
        }

        DPRINT1("  Parameter %lu: %p\n", Index, (PVOID)Parameters[Index]);
    }

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
    HARDERROR_MSG Message;
    NTSTATUS Status;

    PAGED_CODE();

    *Response = ResponseNotHandled;

    /*
     * An error that reaches here before anyone can be told about it, or one
     * raised by the system process itself, has nowhere to go but the debugger.
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

    Message.Status = ErrorStatus;
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

    Status = LpcRequestWaitReplyPortEx(ExpDefaultErrorPort,
                                       (PPORT_MESSAGE)&Message,
                                       (PPORT_MESSAGE)&Message);
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
    PAGED_CODE();

    if (NumberOfParameters > MAXIMUM_HARDERROR_PARAMETERS)
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    return ExpRaiseHardError(ErrorStatus,
                             NumberOfParameters,
                             UnicodeStringParameterMask,
                             Parameters,
                             ValidResponseOptions,
                             Response);
}

/*
 * The strings a caller in user mode names in its parameters have to be brought
 * into the kernel before they are passed on, because the caller must not be
 * able to change them once the error is on its way.
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
    PHARDERROR_USER_PARAMETERS Captured = NULL;
    PULONG_PTR SafeParameters = NULL;
    ULONG SafeResponse = ResponseNotHandled;
    ULONG Index;
    NTSTATUS Status;

    PAGED_CODE();

    if (NumberOfParameters > MAXIMUM_HARDERROR_PARAMETERS)
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    if ((Parameters != NULL) && (NumberOfParameters == 0))
    {
        return STATUS_INVALID_PARAMETER_2;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteUlong(Response);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    if ((Parameters != NULL) && (PreviousMode != KernelMode))
    {
        Captured = ExAllocatePoolZero(PagedPool,
                                      sizeof(HARDERROR_USER_PARAMETERS),
                                      EXP_HARDERROR_TAG);
        if (Captured == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        _SEH2_TRY
        {
            ProbeForRead(Parameters,
                         NumberOfParameters * sizeof(ULONG_PTR),
                         sizeof(ULONG_PTR));

            RtlCopyMemory(Captured->Parameters,
                          Parameters,
                          NumberOfParameters * sizeof(ULONG_PTR));
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
            ExFreePoolWithTag(Captured, EXP_HARDERROR_TAG);
            _SEH2_YIELD(return Status);
        }
        _SEH2_END;

        for (Index = 0; Index < NumberOfParameters; Index++)
        {
            UNICODE_STRING Source;

            if (!(UnicodeStringParameterMask & (1 << Index)))
            {
                continue;
            }

            _SEH2_TRY
            {
                ProbeForRead((PVOID)Captured->Parameters[Index],
                             sizeof(UNICODE_STRING),
                             sizeof(ULONG));
                Source = *(PUNICODE_STRING)Captured->Parameters[Index];
            }
            _SEH2_EXCEPT(ExSystemExceptionFilter())
            {
                Status = _SEH2_GetExceptionCode();
                ExFreePoolWithTag(Captured, EXP_HARDERROR_TAG);
                _SEH2_YIELD(return Status);
            }
            _SEH2_END;

            Status = ExpCaptureHardErrorString(&Captured->Strings[Index],
                                               &Source);
            if (!NT_SUCCESS(Status))
            {
                ExpFreeHardErrorStrings(Captured,
                                        NumberOfParameters,
                                        UnicodeStringParameterMask);
                ExFreePoolWithTag(Captured, EXP_HARDERROR_TAG);
                return Status;
            }

            Captured->Parameters[Index] =
                (ULONG_PTR)&Captured->Strings[Index];
        }

        SafeParameters = Captured->Parameters;
    }
    else
    {
        SafeParameters = Parameters;
    }

    Status = ExpRaiseHardError(ErrorStatus,
                               NumberOfParameters,
                               UnicodeStringParameterMask,
                               SafeParameters,
                               ValidResponseOptions,
                               &SafeResponse);

    if (Captured != NULL)
    {
        ExpFreeHardErrorStrings(Captured,
                                NumberOfParameters,
                                UnicodeStringParameterMask);
        ExFreePoolWithTag(Captured, EXP_HARDERROR_TAG);
    }

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
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

    *Response = SafeResponse;
    return STATUS_SUCCESS;
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
