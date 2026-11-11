/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Power request objects: what PowerCreateRequest, PowerSetRequest and PowerClearRequest act on
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

typedef struct _POP_POWER_REQUEST
{
    FAST_MUTEX Lock;
    PVOID SystemState;
    ULONG Count[PowerRequestMaximum];
} POP_POWER_REQUEST, *PPOP_POWER_REQUEST;

typedef struct _POP_POWER_REQUEST_ACTION
{
    HANDLE PowerRequestHandle;
    POWER_REQUEST_TYPE RequestType;
    ULONG SetAction;
    PVOID Reserved;
} POP_POWER_REQUEST_ACTION, *PPOP_POWER_REQUEST_ACTION;

static POBJECT_TYPE PopPowerRequestObjectType;

static GENERIC_MAPPING PopPowerRequestMapping =
{
    STANDARD_RIGHTS_READ,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE,
    STANDARD_RIGHTS_REQUIRED
};

static
NTSTATUS
NTAPI
PopOpenPowerRequest(
    _In_ OB_OPEN_REASON Reason,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_opt_ PEPROCESS Process,
    _In_ PVOID ObjectBody,
    _Inout_ PACCESS_MASK GrantedAccess,
    _In_ ULONG HandleCount)
{
    UNREFERENCED_PARAMETER(AccessMode);
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(ObjectBody);
    UNREFERENCED_PARAMETER(GrantedAccess);
    UNREFERENCED_PARAMETER(HandleCount);

    return (Reason == ObDuplicateHandle) ? STATUS_ACCESS_DENIED : STATUS_SUCCESS;
}

static
VOID
NTAPI
PopDeletePowerRequest(
    _In_ PVOID ObjectBody)
{
    PPOP_POWER_REQUEST Request = ObjectBody;

    if (Request->SystemState != NULL) PoUnregisterSystemState(Request->SystemState);
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
PopInitializePowerRequests(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"PowerRequest");

    RtlZeroMemory(&Initializer, sizeof(Initializer));
    Initializer.Length = sizeof(Initializer);
    Initializer.GenericMapping = PopPowerRequestMapping;
    Initializer.ValidAccessMask = STANDARD_RIGHTS_ALL;
    Initializer.PoolType = NonPagedPool;
    Initializer.DefaultNonPagedPoolCharge = sizeof(POP_POWER_REQUEST);
    Initializer.OpenProcedure = PopOpenPowerRequest;
    Initializer.DeleteProcedure = PopDeletePowerRequest;

    return NT_SUCCESS(ObCreateObjectType(&Name, &Initializer, NULL, &PopPowerRequestObjectType));
}

static
NTSTATUS
PopCheckReasonString(
    _In_ PCUNICODE_STRING String,
    _In_ BOOLEAN Required,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    if (String->MaximumLength < String->Length || (String->Length & (sizeof(WCHAR) - 1)))
        return STATUS_INVALID_PARAMETER;

    if (String->Length == 0) return Required ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
    if (String->Buffer == NULL) return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode) ProbeForRead(String->Buffer, String->Length, sizeof(WCHAR));

    return STATUS_SUCCESS;
}

/* The reason a caller gives is checked for its shape; the strings in it are not kept */
static
NTSTATUS
PopCheckPowerRequestReason(
    _In_ PCOUNTED_REASON_CONTEXT Context,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    UNICODE_STRING String;
    NTSTATUS Status;
    ULONG Index;

    if (Context->Version != POWER_REQUEST_CONTEXT_VERSION) return STATUS_INVALID_PARAMETER;

    switch (Context->Flags)
    {
        case DIAGNOSTIC_REASON_NOT_SPECIFIED:
            return STATUS_SUCCESS;

        case POWER_REQUEST_CONTEXT_SIMPLE_STRING:
            return PopCheckReasonString(&Context->SimpleString, TRUE, PreviousMode);

        case POWER_REQUEST_CONTEXT_DETAILED_STRING:
            Status = PopCheckReasonString(&Context->ResourceFileName, TRUE, PreviousMode);
            if (!NT_SUCCESS(Status) || Context->StringCount == 0) return Status;

            if (Context->ReasonStrings == NULL ||
                Context->StringCount > MAXULONG / sizeof(UNICODE_STRING))
            {
                return STATUS_INVALID_PARAMETER;
            }

            if (PreviousMode != KernelMode)
            {
                ProbeForRead(Context->ReasonStrings,
                             Context->StringCount * sizeof(UNICODE_STRING),
                             TYPE_ALIGNMENT(UNICODE_STRING));
            }

            for (Index = 0; Index < Context->StringCount; Index++)
            {
                String = Context->ReasonStrings[Index];
                Status = PopCheckReasonString(&String, FALSE, PreviousMode);
                if (!NT_SUCCESS(Status)) return Status;
            }

            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS
NTAPI
PopCreatePowerRequest(
    _In_reads_bytes_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    COUNTED_REASON_CONTEXT Context;
    PPOP_POWER_REQUEST Request;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (InputBuffer == NULL || InputBufferLength != sizeof(Context)) return STATUS_INVALID_PARAMETER;
    if (OutputBuffer == NULL || OutputBufferLength != sizeof(HANDLE)) return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&Context, InputBuffer, sizeof(Context));

    Status = PopCheckPowerRequestReason(&Context, PreviousMode);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObCreateObject(PreviousMode,
                            PopPowerRequestObjectType,
                            NULL,
                            PreviousMode,
                            NULL,
                            sizeof(*Request),
                            0,
                            0,
                            (PVOID *)&Request);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(Request, sizeof(*Request));
    ExInitializeFastMutex(&Request->Lock);

    Status = ObInsertObject(Request, NULL, STANDARD_RIGHTS_ALL, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status)) return Status;

    _SEH2_TRY
    {
        *(PHANDLE)OutputBuffer = Handle;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ObCloseHandle(Handle, PreviousMode);
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
PopActOnPowerRequest(
    _In_reads_bytes_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength)
{
    POP_POWER_REQUEST_ACTION Action;
    PPOP_POWER_REQUEST Request;
    EXECUTION_STATE Flags = 0;
    PVOID SystemState;
    NTSTATUS Status;

    PAGED_CODE();

    if (InputBuffer == NULL || InputBufferLength != sizeof(Action)) return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&Action, InputBuffer, sizeof(Action));

    if ((ULONG)Action.RequestType >= PowerRequestMaximum) return STATUS_NOT_SUPPORTED;
    if (Action.Reserved != NULL) return STATUS_INVALID_PARAMETER;

    Status = ObReferenceObjectByHandle(Action.PowerRequestHandle,
                                       0,
                                       PopPowerRequestObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)&Request,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    ExAcquireFastMutex(&Request->Lock);

    if (Action.SetAction)
    {
        if (Request->Count[Action.RequestType] == MAXLONG) Status = STATUS_INTEGER_OVERFLOW;
        else Request->Count[Action.RequestType]++;
    }
    else if (Request->Count[Action.RequestType] != 0)
    {
        Request->Count[Action.RequestType]--;
    }
    else
    {
        Status = STATUS_NOT_SUPPORTED;
    }

    if (NT_SUCCESS(Status))
    {
        if (Request->Count[PowerRequestDisplayRequired] != 0) Flags |= ES_DISPLAY_REQUIRED;
        if (Request->Count[PowerRequestSystemRequired] != 0) Flags |= ES_SYSTEM_REQUIRED;
        /* TODO: away-mode and execution-required requests are counted but change no system state */

        if (Flags != 0 || Request->SystemState != NULL)
        {
            SystemState = PoRegisterSystemState(Request->SystemState, Flags);
            if (SystemState == NULL) Status = STATUS_INSUFFICIENT_RESOURCES;
            else Request->SystemState = SystemState;
        }

        if (!NT_SUCCESS(Status))
        {
            if (Action.SetAction) Request->Count[Action.RequestType]--;
            else Request->Count[Action.RequestType]++;
        }
    }

    ExReleaseFastMutex(&Request->Lock);
    ObDereferenceObject(Request);
    return Status;
}
