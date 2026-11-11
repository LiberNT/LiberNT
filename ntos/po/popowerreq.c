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
    Initializer.DeleteProcedure = PopDeletePowerRequest;

    return NT_SUCCESS(ObCreateObjectType(&Name, &Initializer, NULL, &PopPowerRequestObjectType));
}

/* The reason a caller gives is checked for its shape; the strings in it are not kept */
static
NTSTATUS
PopCheckPowerRequestReason(
    _In_ PCOUNTED_REASON_CONTEXT Context)
{
    if (Context->Version != POWER_REQUEST_CONTEXT_VERSION) return STATUS_INVALID_PARAMETER;

    switch (Context->Flags)
    {
        case DIAGNOSTIC_REASON_NOT_SPECIFIED:
        case POWER_REQUEST_CONTEXT_SIMPLE_STRING:
        case POWER_REQUEST_CONTEXT_DETAILED_STRING:
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
    if (OutputBuffer == NULL || OutputBufferLength < sizeof(HANDLE)) return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&Context, InputBuffer, sizeof(Context));

    Status = PopCheckPowerRequestReason(&Context);
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

    *(PHANDLE)OutputBuffer = Handle;
    return STATUS_SUCCESS;
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

    if ((ULONG)Action.RequestType >= PowerRequestMaximum) return STATUS_INVALID_PARAMETER;

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
        Request->Count[Action.RequestType]++;
    }
    else if (Request->Count[Action.RequestType] != 0)
    {
        Request->Count[Action.RequestType]--;
    }
    else
    {
        /* TODO: INFO_NEEDED - the status Windows gives for clearing a request type that is not set */
        Status = STATUS_INVALID_PARAMETER;
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
