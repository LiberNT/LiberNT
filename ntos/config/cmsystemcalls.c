/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The registry as the system calls present it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* GLOBALS ********************************************************************/

BOOLEAN CmFirstTime = TRUE;
BOOLEAN CmBootAcceptFirstTime = TRUE;

/* PRIVATE DATA ***************************************************************/

#define CMP_COMPACT_KEY_LIMIT 64

typedef struct _CM_CAPTURED_NAME
{
    UNICODE_STRING Name;
    BOOLEAN Allocated;
} CM_CAPTURED_NAME, *PCM_CAPTURED_NAME;

typedef struct _CM_CAPTURED_ATTRIBUTES
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Name;
    BOOLEAN Allocated;
} CM_CAPTURED_ATTRIBUTES, *PCM_CAPTURED_ATTRIBUTES;

/* PRIVATE FUNCTIONS **********************************************************/

/* Takes a copy of a caller's string so the hive never reads user memory */
static
NTSTATUS
CmpCaptureName(
    _In_ PUNICODE_STRING Source,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ ULONG MaximumLength,
    _Out_ PCM_CAPTURED_NAME Captured)
{
    UNICODE_STRING Local;
    PWCHAR Buffer;

    Captured->Name.Buffer = NULL;
    Captured->Name.Length = 0;
    Captured->Name.MaximumLength = 0;
    Captured->Allocated = FALSE;

    if (Source == NULL) return STATUS_SUCCESS;

    if (PreviousMode != KernelMode)
    {
        Local = ProbeForReadUnicodeString(Source);
    }
    else
    {
        Local = *Source;
    }

    if (Local.Length > MaximumLength) return STATUS_INVALID_PARAMETER;

    if (Local.Length == 0 || Local.Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }

    if (PreviousMode != KernelMode)
    {
        ProbeForRead(Local.Buffer, Local.Length, sizeof(WCHAR));
    }

    Buffer = ExAllocatePoolZero(PagedPool,
                                Local.Length + sizeof(WCHAR),
                                TAG_CM);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Buffer, Local.Buffer, Local.Length);

    Captured->Name.Buffer = Buffer;
    Captured->Name.Length = Local.Length;
    Captured->Name.MaximumLength = Local.Length + sizeof(WCHAR);
    Captured->Allocated = TRUE;
    return STATUS_SUCCESS;
}

static
VOID
CmpReleaseName(
    _Inout_ PCM_CAPTURED_NAME Captured)
{
    if (Captured->Allocated) ExFreePool(Captured->Name.Buffer);

    Captured->Name.Buffer = NULL;
    Captured->Name.Length = 0;
    Captured->Allocated = FALSE;
}

/*
 * The load and unload calls name a key and a file by object attributes rather
 * than by handle, so the whole of both has to be taken from the caller before
 * the registry lock is held.
 */
static
NTSTATUS
CmpCaptureAttributes(
    _In_ POBJECT_ATTRIBUTES Source,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PCM_CAPTURED_ATTRIBUTES Captured)
{
    CM_CAPTURED_NAME Name;
    NTSTATUS Status;

    RtlZeroMemory(Captured, sizeof(*Captured));

    if (Source == NULL) return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        ProbeForRead(Source, sizeof(OBJECT_ATTRIBUTES), sizeof(ULONG));
    }

    Captured->Attributes = *Source;

    if (Captured->Attributes.Length != sizeof(OBJECT_ATTRIBUTES))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = CmpCaptureName(Captured->Attributes.ObjectName,
                            PreviousMode,
                            CMP_HIVE_FILE_NAME_MAX * sizeof(WCHAR),
                            &Name);
    if (!NT_SUCCESS(Status)) return Status;

    if (Name.Name.Buffer == NULL) return STATUS_OBJECT_NAME_INVALID;

    Captured->Name = Name.Name;
    Captured->Allocated = Name.Allocated;
    Captured->Attributes.ObjectName = &Captured->Name;
    Captured->Attributes.SecurityDescriptor = NULL;
    Captured->Attributes.SecurityQualityOfService = NULL;
    return STATUS_SUCCESS;
}

static
VOID
CmpReleaseAttributes(
    _Inout_ PCM_CAPTURED_ATTRIBUTES Captured)
{
    if (Captured->Allocated) ExFreePool(Captured->Name.Buffer);

    Captured->Name.Buffer = NULL;
    Captured->Allocated = FALSE;
}

static
NTSTATUS
CmpReferenceKey(
    _In_ HANDLE KeyHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PCM_KEY_BODY *KeyBody)
{
    NTSTATUS Status;

    Status = ObReferenceObjectByHandle(KeyHandle,
                                       DesiredAccess,
                                       CmpKeyObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID*)KeyBody,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    if ((*KeyBody)->KeyControlBlock == NULL)
    {
        ObDereferenceObject(*KeyBody);
        return STATUS_KEY_DELETED;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
CmpOpenKeyForLoadUnload(
    _In_ PCM_CAPTURED_ATTRIBUTES Captured,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PHANDLE KeyHandle,
    _Out_ PCM_KEY_BODY *KeyBody)
{
    NTSTATUS Status;

    Captured->Attributes.Attributes |= OBJ_KERNEL_HANDLE;

    Status = ObOpenObjectByName(&Captured->Attributes,
                                CmpKeyObjectType,
                                AccessMode,
                                NULL,
                                DesiredAccess,
                                NULL,
                                KeyHandle);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(*KeyHandle,
                                       DesiredAccess,
                                       CmpKeyObjectType,
                                       KernelMode,
                                       (PVOID*)KeyBody,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(*KeyHandle);
        *KeyHandle = NULL;
    }

    return Status;
}

/*
 * The shape every open and create takes once the caller's words have been
 * taken away from it: a parse context the namespace walk can act on.
 */
static
NTSTATUS
CmpOpenOrCreateKey(
    _Out_ PHANDLE KeyHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG TitleIndex,
    _In_opt_ PUNICODE_STRING Class,
    _In_ ULONG Options,
    _In_opt_ HANDLE TransactionHandle,
    _Out_opt_ PULONG Disposition,
    _In_ BOOLEAN Create)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    CM_CAPTURED_NAME CapturedClass;
    CM_PARSE_CONTEXT ParseContext;
    PKTRANSACTION Transaction = NULL;
    HANDLE Local = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (Create)
    {
        if ((Options & ~REG_LEGAL_OPTION) != 0) return STATUS_INVALID_PARAMETER;
    }
    else if ((Options & ~REG_OPEN_LEGAL_OPTION) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(KeyHandle);
            if (Disposition != NULL) ProbeForWriteUlong(Disposition);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    _SEH2_TRY
    {
        Status = CmpCaptureName(Class,
                                PreviousMode,
                                CMP_CLASS_NAME_MAX,
                                &CapturedClass);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;

    if (TransactionHandle != NULL)
    {
        Status = ObReferenceObjectByHandle(TransactionHandle,
                                           0,
                                           TmTransactionObjectType,
                                           PreviousMode,
                                           (PVOID*)&Transaction,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            CmpReleaseName(&CapturedClass);
            return Status;
        }

        Status = CmpTransCheckActive(Transaction);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(Transaction);
            CmpReleaseName(&CapturedClass);
            return Status;
        }
    }

    RtlZeroMemory(&ParseContext, sizeof(ParseContext));

    ParseContext.TitleIndex = TitleIndex;
    ParseContext.Class = CapturedClass.Name;
    ParseContext.CreateOptions = Options;
    ParseContext.CreateOperation = Create;
    ParseContext.Disposition = REG_OPENED_EXISTING_KEY;
    ParseContext.ChildHive.KeyHive = NULL;
    ParseContext.ChildHive.KeyCell = HCELL_NIL;
    ParseContext.Transaction = Transaction;
    ParseContext.EffectiveTransaction = Transaction;

    Status = ObOpenObjectByName(ObjectAttributes,
                                CmpKeyObjectType,
                                PreviousMode,
                                NULL,
                                DesiredAccess,
                                &ParseContext,
                                &Local);

    if (Status == STATUS_PREDEFINED_HANDLE)
    {
        Local = ParseContext.PredefinedHandle;
    }

    if (Transaction != NULL) ObDereferenceObject(Transaction);
    CmpReleaseName(&CapturedClass);

    if (!NT_SUCCESS(Status)) return Status;

    _SEH2_TRY
    {
        *KeyHandle = Local;
        if (Disposition != NULL) *Disposition = ParseContext.Disposition;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
NtCreateKey(
    _Out_ PHANDLE KeyHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG TitleIndex,
    _In_opt_ PUNICODE_STRING Class,
    _In_ ULONG CreateOptions,
    _Out_opt_ PULONG Disposition)
{
    return CmpOpenOrCreateKey(KeyHandle,
                              DesiredAccess,
                              ObjectAttributes,
                              TitleIndex,
                              Class,
                              CreateOptions,
                              NULL,
                              Disposition,
                              TRUE);
}

NTSTATUS
NTAPI
NtCreateKeyTransacted(
    _Out_ PHANDLE KeyHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG TitleIndex,
    _In_opt_ PUNICODE_STRING Class,
    _In_ ULONG CreateOptions,
    _In_ HANDLE TransactionHandle,
    _Out_opt_ PULONG Disposition)
{
    return CmpOpenOrCreateKey(KeyHandle,
                              DesiredAccess,
                              ObjectAttributes,
                              TitleIndex,
                              Class,
                              CreateOptions,
                              TransactionHandle,
                              Disposition,
                              TRUE);
}

NTSTATUS
NTAPI
NtOpenKey(
    _Out_ PHANDLE KeyHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    return CmpOpenOrCreateKey(KeyHandle,
                              DesiredAccess,
                              ObjectAttributes,
                              0,
                              NULL,
                              0,
                              NULL,
                              NULL,
                              FALSE);
}

NTSTATUS
NTAPI
NtOpenKeyEx(
    _Out_ PHANDLE KeyHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG OpenOptions)
{
    return CmpOpenOrCreateKey(KeyHandle,
                              DesiredAccess,
                              ObjectAttributes,
                              0,
                              NULL,
                              OpenOptions,
                              NULL,
                              NULL,
                              FALSE);
}

NTSTATUS
NTAPI
NtOpenKeyTransacted(
    _Out_ PHANDLE KeyHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ HANDLE TransactionHandle)
{
    return CmpOpenOrCreateKey(KeyHandle,
                              DesiredAccess,
                              ObjectAttributes,
                              0,
                              NULL,
                              0,
                              TransactionHandle,
                              NULL,
                              FALSE);
}

NTSTATUS
NTAPI
NtOpenKeyTransactedEx(
    _Out_ PHANDLE KeyHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG OpenOptions,
    _In_ HANDLE TransactionHandle)
{
    return CmpOpenOrCreateKey(KeyHandle,
                              DesiredAccess,
                              ObjectAttributes,
                              0,
                              NULL,
                              OpenOptions,
                              TransactionHandle,
                              NULL,
                              FALSE);
}

NTSTATUS
NTAPI
NtDeleteKey(
    _In_ HANDLE KeyHandle)
{
    REG_DELETE_KEY_INFORMATION Information;
    CMP_CALLBACK_CALL Call;
    PCM_KEY_BODY KeyBody;
    NTSTATUS Status;

    PAGED_CODE();

    Status = CmpReferenceKey(KeyHandle, DELETE, &KeyBody);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Object = KeyBody;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreDeleteKey,
                         RegNtPostDeleteKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         KeyBody,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);
        ObDereferenceObject(KeyBody);
        return Status;
    }

    CmpLockRegistryExclusive();

    if (KeyBody->Trans.TransPtr != NULL)
    {
        Status = CmpTransDeleteKey(KeyBody);
    }
    else
    {
        Status = CmDeleteKey(KeyBody->KeyControlBlock);
    }

    CmpUnlockRegistry();

    Status = CmpPostCallbacks(&Call, Status);
    ObDereferenceObject(KeyBody);
    return Status;
}

NTSTATUS
NTAPI
NtQueryKey(
    _In_ HANDLE KeyHandle,
    _In_ KEY_INFORMATION_CLASS KeyInformationClass,
    _Out_writes_bytes_opt_(Length) PVOID KeyInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    REG_QUERY_KEY_INFORMATION Information;
    CMP_CALLBACK_CALL Call;
    PCM_KEY_BODY KeyBody;
    ACCESS_MASK Wanted;
    ULONG Local = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (KeyInformationClass >= MaxKeyInfoClass)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteUlong(ResultLength);
            if (Length != 0) ProbeForWrite(KeyInformation, Length, sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Wanted = (KeyInformationClass == KeyNameInformation) ? 0 : KEY_QUERY_VALUE;

    Status = CmpReferenceKey(KeyHandle, Wanted, &KeyBody);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Object = KeyBody;
    Information.KeyInformationClass = KeyInformationClass;
    Information.KeyInformation = KeyInformation;
    Information.Length = Length;
    Information.ResultLength = &Local;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreQueryKey,
                         RegNtPostQueryKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         KeyBody,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);
        ObDereferenceObject(KeyBody);
        return Status;
    }

    CmpLockRegistry();

    _SEH2_TRY
    {
        if (KeyBody->Trans.TransPtr != NULL)
        {
            Status = CmpTransQueryKey(KeyBody,
                                      KeyInformationClass,
                                      KeyInformation,
                                      Length,
                                      &Local);
        }
        else
        {
            Status = CmQueryKey(KeyBody->KeyControlBlock,
                                NULL,
                                KeyInformationClass,
                                KeyInformation,
                                Length,
                                &Local);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    CmpUnlockRegistry();

    Status = CmpPostCallbacks(&Call, Status);

    _SEH2_TRY
    {
        *ResultLength = Local;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ObDereferenceObject(KeyBody);
    return Status;
}

NTSTATUS
NTAPI
NtEnumerateKey(
    _In_ HANDLE KeyHandle,
    _In_ ULONG Index,
    _In_ KEY_INFORMATION_CLASS KeyInformationClass,
    _Out_writes_bytes_opt_(Length) PVOID KeyInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    REG_ENUMERATE_KEY_INFORMATION Information;
    CMP_CALLBACK_CALL Call;
    PCM_KEY_BODY KeyBody;
    ULONG Local = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteUlong(ResultLength);
            if (Length != 0) ProbeForWrite(KeyInformation, Length, sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = CmpReferenceKey(KeyHandle, KEY_ENUMERATE_SUB_KEYS, &KeyBody);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Object = KeyBody;
    Information.Index = Index;
    Information.KeyInformationClass = KeyInformationClass;
    Information.KeyInformation = KeyInformation;
    Information.Length = Length;
    Information.ResultLength = &Local;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreEnumerateKey,
                         RegNtPostEnumerateKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         KeyBody,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);
        ObDereferenceObject(KeyBody);
        return Status;
    }

    CmpLockRegistry();

    _SEH2_TRY
    {
        if (KeyBody->Trans.TransPtr != NULL)
        {
            Status = CmpTransEnumerateKey(KeyBody,
                                          Index,
                                          KeyInformationClass,
                                          KeyInformation,
                                          Length,
                                          &Local);
        }
        else
        {
            Status = CmEnumerateKey(KeyBody->KeyControlBlock,
                                    NULL,
                                    Index,
                                    KeyInformationClass,
                                    KeyInformation,
                                    Length,
                                    &Local);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    CmpUnlockRegistry();

    Status = CmpPostCallbacks(&Call, Status);

    _SEH2_TRY
    {
        *ResultLength = Local;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ObDereferenceObject(KeyBody);
    return Status;
}

NTSTATUS
NTAPI
NtQueryValueKey(
    _In_ HANDLE KeyHandle,
    _In_ PUNICODE_STRING ValueName,
    _In_ KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    _Out_writes_bytes_opt_(Length) PVOID KeyValueInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    REG_QUERY_VALUE_KEY_INFORMATION Information;
    CM_CAPTURED_NAME Captured;
    CMP_CALLBACK_CALL Call;
    PCM_KEY_BODY KeyBody;
    ULONG Local = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteUlong(ResultLength);
            if (Length != 0) ProbeForWrite(KeyValueInformation, Length, sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    _SEH2_TRY
    {
        Status = CmpCaptureName(ValueName,
                                PreviousMode,
                                CMP_VALUE_NAME_MAX,
                                &Captured);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;

    Status = CmpReferenceKey(KeyHandle, KEY_QUERY_VALUE, &KeyBody);
    if (!NT_SUCCESS(Status))
    {
        CmpReleaseName(&Captured);
        return Status;
    }

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Object = KeyBody;
    Information.ValueName = &Captured.Name;
    Information.KeyValueInformationClass = KeyValueInformationClass;
    Information.KeyValueInformation = KeyValueInformation;
    Information.Length = Length;
    Information.ResultLength = &Local;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreQueryValueKey,
                         RegNtPostQueryValueKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         KeyBody,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);
        ObDereferenceObject(KeyBody);
        CmpReleaseName(&Captured);
        return Status;
    }

    CmpLockRegistry();

    _SEH2_TRY
    {
        if (KeyBody->Trans.TransPtr != NULL)
        {
            Status = CmpTransQueryValueKey(KeyBody,
                                           &Captured.Name,
                                           KeyValueInformationClass,
                                           KeyValueInformation,
                                           Length,
                                           &Local);
        }
        else
        {
            Status = CmQueryValueKey(KeyBody->KeyControlBlock,
                                     Captured.Name,
                                     KeyValueInformationClass,
                                     KeyValueInformation,
                                     Length,
                                     &Local);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    CmpUnlockRegistry();

    Status = CmpPostCallbacks(&Call, Status);

    _SEH2_TRY
    {
        *ResultLength = Local;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ObDereferenceObject(KeyBody);
    CmpReleaseName(&Captured);
    return Status;
}

NTSTATUS
NTAPI
NtEnumerateValueKey(
    _In_ HANDLE KeyHandle,
    _In_ ULONG Index,
    _In_ KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    _Out_writes_bytes_opt_(Length) PVOID KeyValueInformation,
    _In_ ULONG Length,
    _Out_ PULONG ResultLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    REG_ENUMERATE_VALUE_KEY_INFORMATION Information;
    CMP_CALLBACK_CALL Call;
    PCM_KEY_BODY KeyBody;
    ULONG Local = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteUlong(ResultLength);
            if (Length != 0) ProbeForWrite(KeyValueInformation, Length, sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = CmpReferenceKey(KeyHandle, KEY_QUERY_VALUE, &KeyBody);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Object = KeyBody;
    Information.Index = Index;
    Information.KeyValueInformationClass = KeyValueInformationClass;
    Information.KeyValueInformation = KeyValueInformation;
    Information.Length = Length;
    Information.ResultLength = &Local;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreEnumerateValueKey,
                         RegNtPostEnumerateValueKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         KeyBody,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);
        ObDereferenceObject(KeyBody);
        return Status;
    }

    CmpLockRegistry();

    _SEH2_TRY
    {
        if (KeyBody->Trans.TransPtr != NULL)
        {
            Status = CmpTransEnumerateValueKey(KeyBody,
                                               Index,
                                               KeyValueInformationClass,
                                               KeyValueInformation,
                                               Length,
                                               &Local);
        }
        else
        {
            Status = CmEnumerateValueKey(KeyBody->KeyControlBlock,
                                         Index,
                                         KeyValueInformationClass,
                                         KeyValueInformation,
                                         Length,
                                         &Local);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    CmpUnlockRegistry();

    Status = CmpPostCallbacks(&Call, Status);

    _SEH2_TRY
    {
        *ResultLength = Local;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ObDereferenceObject(KeyBody);
    return Status;
}

NTSTATUS
NTAPI
NtSetValueKey(
    _In_ HANDLE KeyHandle,
    _In_ PUNICODE_STRING ValueName,
    _In_opt_ ULONG TitleIndex,
    _In_ ULONG Type,
    _In_reads_bytes_opt_(DataSize) PVOID Data,
    _In_ ULONG DataSize)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    REG_SET_VALUE_KEY_INFORMATION Information;
    CM_CAPTURED_NAME Captured;
    CMP_CALLBACK_CALL Call;
    PCM_KEY_BODY KeyBody;
    PVOID Copy = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(TitleIndex);

    _SEH2_TRY
    {
        Status = CmpCaptureName(ValueName,
                                PreviousMode,
                                CMP_VALUE_NAME_MAX,
                                &Captured);

        if (NT_SUCCESS(Status) && DataSize != 0)
        {
            if (Data == NULL)
            {
                Status = STATUS_INVALID_PARAMETER;
            }
            else
            {
                if (PreviousMode != KernelMode)
                {
                    ProbeForRead(Data, DataSize, 1);
                }

                Copy = ExAllocatePoolZero(PagedPool, DataSize, TAG_CM);
                if (Copy == NULL) Status = STATUS_INSUFFICIENT_RESOURCES;
                else RtlCopyMemory(Copy, Data, DataSize);
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        if (Copy != NULL) ExFreePool(Copy);
        CmpReleaseName(&Captured);
        return Status;
    }

    Status = CmpReferenceKey(KeyHandle, KEY_SET_VALUE, &KeyBody);
    if (!NT_SUCCESS(Status))
    {
        if (Copy != NULL) ExFreePool(Copy);
        CmpReleaseName(&Captured);
        return Status;
    }

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Object = KeyBody;
    Information.ValueName = &Captured.Name;
    Information.Type = Type;
    Information.Data = Copy;
    Information.DataSize = DataSize;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreSetValueKey,
                         RegNtPostSetValueKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         KeyBody,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);
        ObDereferenceObject(KeyBody);
        if (Copy != NULL) ExFreePool(Copy);
        CmpReleaseName(&Captured);
        return Status;
    }

    CmpLockRegistryExclusive();

    if (KeyBody->Trans.TransPtr != NULL)
    {
        Status = CmpTransSetValueKey(KeyBody,
                                     &Captured.Name,
                                     Type,
                                     Copy,
                                     DataSize);
    }
    else
    {
        Status = CmSetValueKey(KeyBody->KeyControlBlock,
                               &Captured.Name,
                               Type,
                               Copy,
                               DataSize);
    }

    CmpUnlockRegistry();

    Status = CmpPostCallbacks(&Call, Status);

    ObDereferenceObject(KeyBody);
    if (Copy != NULL) ExFreePool(Copy);
    CmpReleaseName(&Captured);
    return Status;
}

NTSTATUS
NTAPI
NtDeleteValueKey(
    _In_ HANDLE KeyHandle,
    _In_ PUNICODE_STRING ValueName)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    REG_DELETE_VALUE_KEY_INFORMATION Information;
    CM_CAPTURED_NAME Captured;
    CMP_CALLBACK_CALL Call;
    PCM_KEY_BODY KeyBody;
    NTSTATUS Status;

    PAGED_CODE();

    _SEH2_TRY
    {
        Status = CmpCaptureName(ValueName,
                                PreviousMode,
                                CMP_VALUE_NAME_MAX,
                                &Captured);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;

    Status = CmpReferenceKey(KeyHandle, KEY_SET_VALUE, &KeyBody);
    if (!NT_SUCCESS(Status))
    {
        CmpReleaseName(&Captured);
        return Status;
    }

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Object = KeyBody;
    Information.ValueName = &Captured.Name;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreDeleteValueKey,
                         RegNtPostDeleteValueKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         KeyBody,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);
        ObDereferenceObject(KeyBody);
        CmpReleaseName(&Captured);
        return Status;
    }

    CmpLockRegistryExclusive();

    if (KeyBody->Trans.TransPtr != NULL)
    {
        Status = CmpTransDeleteValueKey(KeyBody, &Captured.Name);
    }
    else
    {
        Status = CmDeleteValueKey(KeyBody->KeyControlBlock, Captured.Name);
    }

    CmpUnlockRegistry();

    Status = CmpPostCallbacks(&Call, Status);

    ObDereferenceObject(KeyBody);
    CmpReleaseName(&Captured);
    return Status;
}

NTSTATUS
NTAPI
NtFlushKey(
    _In_ HANDLE KeyHandle)
{
    REG_DELETE_KEY_INFORMATION Information;
    CMP_CALLBACK_CALL Call;
    PCM_KEY_BODY KeyBody;
    NTSTATUS Status;

    PAGED_CODE();

    Status = CmpReferenceKey(KeyHandle, 0, &KeyBody);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Object = KeyBody;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreFlushKey,
                         RegNtPostFlushKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         KeyBody,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);
        ObDereferenceObject(KeyBody);
        return Status;
    }

    CmpLockRegistry();
    Status = CmFlushKey(KeyBody->KeyControlBlock, FALSE);
    CmpUnlockRegistry();

    Status = CmpPostCallbacks(&Call, Status);
    ObDereferenceObject(KeyBody);
    return Status;
}

NTSTATUS
NTAPI
NtRenameKey(
    _In_ HANDLE KeyHandle,
    _In_ PUNICODE_STRING NewName)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    REG_RENAME_KEY_INFORMATION Information;
    CM_CAPTURED_NAME Captured;
    CMP_CALLBACK_CALL Call;
    PCM_KEY_BODY KeyBody;
    NTSTATUS Status;

    PAGED_CODE();

    _SEH2_TRY
    {
        Status = CmpCaptureName(NewName,
                                PreviousMode,
                                CMP_KEY_NAME_MAX,
                                &Captured);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;
    if (Captured.Name.Length == 0)
    {
        CmpReleaseName(&Captured);
        return STATUS_INVALID_PARAMETER;
    }

    Status = CmpReferenceKey(KeyHandle, KEY_WRITE, &KeyBody);
    if (!NT_SUCCESS(Status))
    {
        CmpReleaseName(&Captured);
        return Status;
    }

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Object = KeyBody;
    Information.NewName = &Captured.Name;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreRenameKey,
                         RegNtPostRenameKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         KeyBody,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);
        ObDereferenceObject(KeyBody);
        CmpReleaseName(&Captured);
        return Status;
    }

    CmpLockRegistryExclusive();
    Status = CmRenameKey(KeyBody->KeyControlBlock, &Captured.Name);
    CmpUnlockRegistry();

    Status = CmpPostCallbacks(&Call, Status);
    ObDereferenceObject(KeyBody);
    CmpReleaseName(&Captured);
    return Status;
}

/*
 * Reads several values of one key in one call, which spares a caller that
 * wants a whole set of settings a system call for each of them.
 */
NTSTATUS
NTAPI
NtQueryMultipleValueKey(
    _In_ HANDLE KeyHandle,
    _Inout_updates_(EntryCount) PKEY_VALUE_ENTRY ValueEntries,
    _In_ ULONG EntryCount,
    _Out_writes_bytes_(*BufferLength) PVOID ValueBuffer,
    _Inout_ PULONG BufferLength,
    _Out_opt_ PULONG RequiredBufferLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    REG_QUERY_MULTIPLE_VALUE_KEY_INFORMATION Information;
    HCELL_INDEX CellToRelease = HCELL_NIL;
    HCELL_INDEX DataToRelease = HCELL_NIL;
    PCM_KEY_CONTROL_BLOCK Kcb;
    PCM_CACHED_VALUE *Cached;
    CMP_CALLBACK_CALL Call;
    PCM_KEY_BODY KeyBody;
    BOOLEAN ValueIsCached;
    BOOLEAN Allocated;
    CM_CAPTURED_NAME Captured;
    PCM_KEY_VALUE Value;
    PVOID Data;
    NTSTATUS Status;
    ULONG Available;
    ULONG Required = 0;
    ULONG Offset = 0;
    ULONG Position;
    ULONG Length;
    ULONG Index;

    PAGED_CODE();

    if (EntryCount == 0) return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWriteUlong(BufferLength);
            if (RequiredBufferLength != NULL)
            {
                ProbeForWriteUlong(RequiredBufferLength);
            }

            ProbeForWrite(ValueEntries,
                          EntryCount * sizeof(KEY_VALUE_ENTRY),
                          sizeof(ULONG));
        }

        Available = *BufferLength;

        if (PreviousMode != KernelMode && Available != 0)
        {
            ProbeForWrite(ValueBuffer, Available, sizeof(ULONG));
        }

        Status = STATUS_SUCCESS;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;

    Status = CmpReferenceKey(KeyHandle, KEY_QUERY_VALUE, &KeyBody);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Object = KeyBody;
    Information.ValueEntries = ValueEntries;
    Information.EntryCount = EntryCount;
    Information.ValueBuffer = ValueBuffer;
    Information.BufferLength = BufferLength;
    Information.RequiredBufferLength = RequiredBufferLength;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreQueryMultipleValueKey,
                         RegNtPostQueryMultipleValueKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         KeyBody,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);
        ObDereferenceObject(KeyBody);
        return Status;
    }

    Kcb = KeyBody->KeyControlBlock;

    CmpLockRegistry();

    _SEH2_TRY
    {
        for (Index = 0; Index < EntryCount; Index++)
        {
            Status = CmpCaptureName(ValueEntries[Index].ValueName,
                                    PreviousMode,
                                    CMP_VALUE_NAME_MAX,
                                    &Captured);
            if (!NT_SUCCESS(Status)) break;

            if (CmpFindValueByNameFromCache(Kcb,
                                            &Captured.Name,
                                            &Cached,
                                            &Position,
                                            &Value,
                                            &ValueIsCached,
                                            &CellToRelease) != SearchSuccess)
            {
                CmpReleaseName(&Captured);
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                break;
            }

            CmpReleaseName(&Captured);
            CmpIsKeyValueSmall(&Length, Value->DataLength);

            Offset = ALIGN_UP_BY(Required, sizeof(ULONGLONG));
            Required = Offset + Length;

            ValueEntries[Index].Type = Value->Type;
            ValueEntries[Index].DataLength = Length;
            ValueEntries[Index].DataOffset = Offset;

            if (Required <= Available && Length != 0)
            {
                if (CmpGetValueDataFromCache(Kcb,
                                             Cached,
                                             (PCELL_DATA)Value,
                                             ValueIsCached,
                                             &Data,
                                             &Allocated,
                                             &DataToRelease) == SearchSuccess)
                {
                    RtlCopyMemory((PUCHAR)ValueBuffer + Offset, Data, Length);

                    if (Allocated) ExFreePool(Data);
                    if (DataToRelease != HCELL_NIL)
                    {
                        HvReleaseCell(Kcb->KeyHive, DataToRelease);
                    }
                }
            }

            if (CellToRelease != HCELL_NIL)
            {
                HvReleaseCell(Kcb->KeyHive, CellToRelease);
                CellToRelease = HCELL_NIL;
            }
        }

        if (NT_SUCCESS(Status))
        {
            *BufferLength = (Required <= Available) ? Required : Available;
            if (RequiredBufferLength != NULL) *RequiredBufferLength = Required;
            if (Required > Available) Status = STATUS_BUFFER_OVERFLOW;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    CmpUnlockRegistry();

    Status = CmpPostCallbacks(&Call, Status);
    ObDereferenceObject(KeyBody);
    return Status;
}

/*
 * The one call that changes what a key is rather than what it holds. Only the
 * write time, the user flags and the virtualisation marks can be set.
 */
NTSTATUS
NTAPI
NtSetInformationKey(
    _In_ HANDLE KeyHandle,
    _In_ KEY_SET_INFORMATION_CLASS KeySetInformationClass,
    _In_reads_bytes_(KeySetInformationLength) PVOID KeySetInformation,
    _In_ ULONG KeySetInformationLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    REG_SET_INFORMATION_KEY_INFORMATION Information;
    PCM_KEY_CONTROL_BLOCK Kcb;
    CMP_CALLBACK_CALL Call;
    PCM_KEY_BODY KeyBody;
    PCM_KEY_NODE Node;
    NTSTATUS Status;
    ULONG Required;
    ULONG Value = 0;
    LARGE_INTEGER Time = {{0, 0}};

    PAGED_CODE();

    switch (KeySetInformationClass)
    {
        case KeyWriteTimeInformation:
            Required = sizeof(KEY_WRITE_TIME_INFORMATION);
            break;

        case KeyWow64FlagsInformation:
            Required = sizeof(KEY_WOW64_FLAGS_INFORMATION);
            break;

        case KeyControlFlagsInformation:
            Required = sizeof(KEY_CONTROL_FLAGS_INFORMATION);
            break;

        case KeySetVirtualizationInformation:
            Required = sizeof(KEY_SET_VIRTUALIZATION_INFORMATION);
            break;

        case KeySetDebugInformation:
            Required = sizeof(ULONG);
            break;

        case KeySetHandleTagsInformation:
            Required = sizeof(KEY_HANDLE_TAGS_INFORMATION);
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    if (KeySetInformationLength != Required)
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForRead(KeySetInformation, Required, sizeof(ULONG));
        }

        if (KeySetInformationClass == KeyWriteTimeInformation)
        {
            Time = ((PKEY_WRITE_TIME_INFORMATION)KeySetInformation)->LastWriteTime;
        }
        else
        {
            Value = *(PULONG)KeySetInformation;
        }

        Status = STATUS_SUCCESS;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;

    Status = CmpReferenceKey(KeyHandle, KEY_SET_VALUE, &KeyBody);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Object = KeyBody;
    Information.KeySetInformationClass = KeySetInformationClass;
    Information.KeySetInformation = KeySetInformation;
    Information.KeySetInformationLength = KeySetInformationLength;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreSetInformationKey,
                         RegNtPostSetInformationKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         KeyBody,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);
        ObDereferenceObject(KeyBody);
        return Status;
    }

    CmpLockRegistryExclusive();

    Kcb = KeyBody->KeyControlBlock;

    if (Kcb->Delete)
    {
        Status = STATUS_KEY_DELETED;
    }
    else
    {
        Node = (PCM_KEY_NODE)HvGetCell(Kcb->KeyHive, Kcb->KeyCell);

        if (Node == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            switch (KeySetInformationClass)
            {
                case KeyWriteTimeInformation:
                    if (HvMarkCellDirty(Kcb->KeyHive, Kcb->KeyCell, FALSE))
                    {
                        Node->LastWriteTime = Time;
                        Kcb->KcbLastWriteTime = Time;
                    }
                    break;

                case KeyWow64FlagsInformation:
                case KeyControlFlagsInformation:
                    /* TODO: Windows 11 packs UserFlags, VirtControlFlags and
                       Debug into the ULONG at CM_KEY_NODE offset 52 beside
                       MaxNameLen, which cmdata.h declares as a plain ULONG
                       that cmlib writes whole; until both change, the flags
                       live only as long as the key stays cached */
                    Kcb->KcbUserFlags = Value & 0xF;
                    break;

                case KeySetVirtualizationInformation:
                    if (HvMarkCellDirty(Kcb->KeyHive, Kcb->KeyCell, FALSE))
                    {
                        Node->Flags &= ~(KEY_VIRT_TARGET | KEY_VIRTUAL_STORE |
                                         KEY_VIRT_MIRRORED);
                        if (Value & 1) Node->Flags |= KEY_VIRT_TARGET;
                        if (Value & 2) Node->Flags |= KEY_VIRTUAL_STORE;
                        if (Value & 4) Node->Flags |= KEY_VIRT_MIRRORED;
                        Kcb->Flags = Node->Flags;
                    }
                    break;

                case KeySetDebugInformation:
                    Kcb->KcbDebug = Value & 0xFF;
                    break;

                case KeySetHandleTagsInformation:
                    Kcb->KcbVirtControlFlags = Value & 0xF;
                    break;

                default:
                    Status = STATUS_INVALID_PARAMETER;
                    break;
            }

            HvReleaseCell(Kcb->KeyHive, Kcb->KeyCell);
        }
    }

    CmpUnlockRegistry();

    Status = CmpPostCallbacks(&Call, Status);
    ObDereferenceObject(KeyBody);
    return Status;
}

NTSTATUS
NTAPI
NtNotifyChangeMultipleKeys(
    _In_ HANDLE MasterKeyHandle,
    _In_opt_ ULONG Count,
    _In_reads_opt_(Count) OBJECT_ATTRIBUTES SubordinateObjects[],
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ ULONG CompletionFilter,
    _In_ BOOLEAN WatchTree,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _In_ BOOLEAN Asynchronous)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    CM_CAPTURED_ATTRIBUTES Captured;
    PCM_KEY_BODY SlaveBody = NULL;
    HANDLE SlaveHandle = NULL;
    PCM_KEY_BODY KeyBody;
    PKEVENT EventObject = NULL;
    BOOLEAN IoStatus32;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(BufferSize);

    if (Count > 1) return STATUS_INVALID_PARAMETER;
    if (Count == 1 && SubordinateObjects == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    IoStatus32 = (PreviousMode != KernelMode) &&
                 (PsGetCurrentProcessWow64Process() != NULL);

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            if (IoStatus32)
            {
                ProbeForWrite(IoStatusBlock,
                              sizeof(IO_STATUS_BLOCK32),
                              sizeof(ULONG));
            }
            else
            {
                ProbeForWriteIoStatusBlock(IoStatusBlock);
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = CmpReferenceKey(MasterKeyHandle, KEY_NOTIFY, &KeyBody);
    if (!NT_SUCCESS(Status)) return Status;

    if (Count == 1)
    {
        _SEH2_TRY
        {
            Status = CmpCaptureAttributes(&SubordinateObjects[0],
                                          PreviousMode,
                                          &Captured);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(KeyBody);
            return Status;
        }

        Status = CmpOpenKeyForLoadUnload(&Captured,
                                         KEY_NOTIFY,
                                         PreviousMode,
                                         &SlaveHandle,
                                         &SlaveBody);
        CmpReleaseAttributes(&Captured);

        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(KeyBody);
            return Status;
        }
    }

    if (Event != NULL && Asynchronous)
    {
        Status = ObReferenceObjectByHandle(Event,
                                           EVENT_MODIFY_STATE,
                                           ExEventObjectType,
                                           PreviousMode,
                                           (PVOID*)&EventObject,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            if (SlaveBody != NULL) ObDereferenceObject(SlaveBody);
            if (SlaveHandle != NULL) ZwClose(SlaveHandle);
            ObDereferenceObject(KeyBody);
            return Status;
        }
    }

    CmpLockRegistry();

    Status = CmpNotifyChangeKey(KeyBody,
                                SlaveBody,
                                EventObject,
                                ApcRoutine,
                                ApcContext,
                                IoStatusBlock,
                                IoStatus32,
                                CompletionFilter,
                                WatchTree,
                                Asynchronous,
                                PreviousMode);

    CmpUnlockRegistry();

    if (!NT_SUCCESS(Status) && EventObject != NULL)
    {
        ObDereferenceObject(EventObject);
    }

    if (SlaveBody != NULL) ObDereferenceObject(SlaveBody);
    if (SlaveHandle != NULL) ZwClose(SlaveHandle);
    ObDereferenceObject(KeyBody);
    return Status;
}

NTSTATUS
NTAPI
NtNotifyChangeKey(
    _In_ HANDLE KeyHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ ULONG CompletionFilter,
    _In_ BOOLEAN WatchTree,
    _Out_writes_bytes_opt_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ BOOLEAN Asynchronous)
{
    return NtNotifyChangeMultipleKeys(KeyHandle,
                                      0,
                                      NULL,
                                      Event,
                                      ApcRoutine,
                                      ApcContext,
                                      IoStatusBlock,
                                      CompletionFilter,
                                      WatchTree,
                                      Buffer,
                                      Length,
                                      Asynchronous);
}

/*
 * Reads a hive file in under a name. The trust class, when one is named,
 * is a key whose hive must outlive the hive being loaded, so a reference is
 * held on it for as long as the loaded hive is there.
 */
NTSTATUS
NTAPI
NtLoadKeyEx(
    _In_ POBJECT_ATTRIBUTES TargetKey,
    _In_ POBJECT_ATTRIBUTES SourceFile,
    _In_ ULONG Flags,
    _In_opt_ HANDLE TrustClassKey,
    _In_opt_ HANDLE Event,
    _In_opt_ ACCESS_MASK DesiredAccess,
    _Out_opt_ PHANDLE RootHandle,
    _Out_opt_ PIO_STATUS_BLOCK IoStatus)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    CM_CAPTURED_ATTRIBUTES CapturedTarget;
    CM_CAPTURED_ATTRIBUTES CapturedSource;
    REG_LOAD_KEY_INFORMATION Information;
    PCM_KEY_BODY TrustBody = NULL;
    PKEVENT EventObject = NULL;
    PCMHIVE TrustClass = NULL;
    CMP_CALLBACK_CALL Call;
    IO_STATUS_BLOCK Local;
    HANDLE Root = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DesiredAccess);

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            if (RootHandle != NULL) ProbeForWriteHandle(RootHandle);
            if (IoStatus != NULL) ProbeForWriteIoStatusBlock(IoStatus);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    _SEH2_TRY
    {
        Status = CmpCaptureAttributes(TargetKey, PreviousMode, &CapturedTarget);
        if (NT_SUCCESS(Status))
        {
            Status = CmpCaptureAttributes(SourceFile,
                                          PreviousMode,
                                          &CapturedSource);
            if (!NT_SUCCESS(Status)) CmpReleaseAttributes(&CapturedTarget);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;

    if (TrustClassKey != NULL)
    {
        Status = CmpReferenceKey(TrustClassKey, 0, &TrustBody);
        if (!NT_SUCCESS(Status))
        {
            CmpReleaseAttributes(&CapturedSource);
            CmpReleaseAttributes(&CapturedTarget);
            return Status;
        }

        TrustClass = CmpFindLoadedHive(TrustBody->KeyControlBlock->KeyHive);

        if (TrustClass == NULL)
        {
            ObDereferenceObject(TrustBody);
            CmpReleaseAttributes(&CapturedSource);
            CmpReleaseAttributes(&CapturedTarget);
            return STATUS_INVALID_PARAMETER;
        }
    }

    if (Event != NULL)
    {
        Status = ObReferenceObjectByHandle(Event,
                                           EVENT_MODIFY_STATE,
                                           ExEventObjectType,
                                           PreviousMode,
                                           (PVOID*)&EventObject,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            if (TrustBody != NULL) ObDereferenceObject(TrustBody);
            CmpReleaseAttributes(&CapturedSource);
            CmpReleaseAttributes(&CapturedTarget);
            return Status;
        }
    }

    RtlZeroMemory(&Information, sizeof(Information));
    Information.KeyName = &CapturedTarget.Name;
    Information.SourceFile = &CapturedSource.Name;
    Information.Flags = Flags;
    Information.TrustClassObject = TrustBody;
    Information.UserEvent = EventObject;
    Information.RootHandle = RootHandle;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreLoadKey,
                         RegNtPostLoadKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         NULL,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);
    }
    else
    {
        Status = CmpLoadHiveAtName(&CapturedTarget.Attributes,
                                   &CapturedSource.Attributes,
                                   Flags,
                                   PreviousMode,
                                   TrustClass,
                                   NULL,
                                   (RootHandle != NULL) ? &Root : NULL,
                                   NULL);
        Status = CmpPostCallbacks(&Call, Status);
    }

    if (TrustBody != NULL) ObDereferenceObject(TrustBody);

    CmpReleaseAttributes(&CapturedSource);
    CmpReleaseAttributes(&CapturedTarget);

    if (NT_SUCCESS(Status) && EventObject != NULL)
    {
        KeSetEvent(EventObject, EVENT_INCREMENT, FALSE);
    }

    if (EventObject != NULL) ObDereferenceObject(EventObject);

    Local.Status = Status;
    Local.Information = 0;

    _SEH2_TRY
    {
        if (RootHandle != NULL) *RootHandle = Root;
        if (IoStatus != NULL)
        {
            IopWriteIoStatusBlock(IoStatus,
                                  &Local,
                                  (PreviousMode != KernelMode) &&
                                      (PsGetCurrentProcessWow64Process() !=
                                       NULL));
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtLoadKey2(
    _In_ POBJECT_ATTRIBUTES TargetKey,
    _In_ POBJECT_ATTRIBUTES SourceFile,
    _In_ ULONG Flags)
{
    return NtLoadKeyEx(TargetKey,
                       SourceFile,
                       Flags,
                       NULL,
                       NULL,
                       0,
                       NULL,
                       NULL);
}

NTSTATUS
NTAPI
NtLoadKey(
    _In_ POBJECT_ATTRIBUTES TargetKey,
    _In_ POBJECT_ATTRIBUTES SourceFile)
{
    return NtLoadKeyEx(TargetKey,
                       SourceFile,
                       0,
                       NULL,
                       NULL,
                       0,
                       NULL,
                       NULL);
}

/*
 * Takes a hive back out. A caller that handed in an event is told through it
 * once the hive has really gone, which can be later than this call returns
 * when something still holds the hive open.
 */
static
NTSTATUS
CmpUnloadKeyByName(
    _In_ POBJECT_ATTRIBUTES TargetKey,
    _In_ ULONG Flags,
    _In_opt_ HANDLE Event)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    REG_UNLOAD_KEY_INFORMATION Information;
    CM_CAPTURED_ATTRIBUTES Captured;
    PKEVENT EventObject = NULL;
    CMP_CALLBACK_CALL Call;
    PCM_KEY_BODY KeyBody = NULL;
    HANDLE KeyHandle = NULL;
    PCMHIVE Hive;
    NTSTATUS Status;

    _SEH2_TRY
    {
        Status = CmpCaptureAttributes(TargetKey, PreviousMode, &Captured);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;

    Status = CmpOpenKeyForLoadUnload(&Captured,
                                     KEY_WRITE,
                                     KernelMode,
                                     &KeyHandle,
                                     &KeyBody);
    CmpReleaseAttributes(&Captured);

    if (!NT_SUCCESS(Status)) return Status;

    if (Event != NULL)
    {
        Status = ObReferenceObjectByHandle(Event,
                                           EVENT_MODIFY_STATE,
                                           ExEventObjectType,
                                           PreviousMode,
                                           (PVOID*)&EventObject,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(KeyBody);
            ZwClose(KeyHandle);
            return Status;
        }
    }

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Object = KeyBody;
    Information.UserEvent = EventObject;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreUnLoadKey,
                         RegNtPostUnLoadKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         KeyBody,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);

        if (EventObject != NULL) ObDereferenceObject(EventObject);
        ObDereferenceObject(KeyBody);
        ZwClose(KeyHandle);
        return Status;
    }

    CmpLockRegistryExclusive();

    Hive = CmpFindLoadedHive(KeyBody->KeyControlBlock->KeyHive);

    if (Hive == NULL)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        if (EventObject != NULL)
        {
            Status = CmpAddUnloadEvent(Hive, EventObject);
            if (NT_SUCCESS(Status)) EventObject = NULL;
        }

        if (NT_SUCCESS(Status))
        {
            Status = CmUnloadKey(KeyBody->KeyControlBlock, Flags);
            if (!NT_SUCCESS(Status)) CmpDiscardUnloadEvents(Hive);
        }
    }

    CmpUnlockRegistry();

    Status = CmpPostCallbacks(&Call, Status);

    if (EventObject != NULL) ObDereferenceObject(EventObject);

    ObDereferenceObject(KeyBody);
    ZwClose(KeyHandle);
    return Status;
}

NTSTATUS
NTAPI
NtUnloadKey(
    _In_ POBJECT_ATTRIBUTES TargetKey)
{
    PAGED_CODE();
    return CmpUnloadKeyByName(TargetKey, 0, NULL);
}

NTSTATUS
NTAPI
NtUnloadKey2(
    _In_ POBJECT_ATTRIBUTES TargetKey,
    _In_ ULONG Flags)
{
    PAGED_CODE();

    if ((Flags & ~REG_FORCE_UNLOAD) != 0) return STATUS_INVALID_PARAMETER;
    return CmpUnloadKeyByName(TargetKey, Flags, NULL);
}

NTSTATUS
NTAPI
NtUnloadKeyEx(
    _In_ POBJECT_ATTRIBUTES TargetKey,
    _In_opt_ HANDLE Event)
{
    PAGED_CODE();
    return CmpUnloadKeyByName(TargetKey, 0, Event);
}

NTSTATUS
NTAPI
NtSaveKeyEx(
    _In_ HANDLE KeyHandle,
    _In_ HANDLE FileHandle,
    _In_ ULONG Flags)
{
    REG_SAVE_KEY_INFORMATION Information;
    CMP_CALLBACK_CALL Call;
    PCM_KEY_BODY KeyBody;
    NTSTATUS Status;

    PAGED_CODE();

    if (Flags != REG_STANDARD_FORMAT &&
        Flags != REG_LATEST_FORMAT &&
        Flags != REG_NO_COMPRESSION)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = CmpReferenceKey(KeyHandle, 0, &KeyBody);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Object = KeyBody;
    Information.FileHandle = FileHandle;
    Information.Format = Flags;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreSaveKey,
                         RegNtPostSaveKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         KeyBody,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);
        ObDereferenceObject(KeyBody);
        return Status;
    }

    CmpLockRegistry();
    Status = CmSaveKey(KeyBody->KeyControlBlock, FileHandle, Flags);
    CmpUnlockRegistry();

    Status = CmpPostCallbacks(&Call, Status);
    ObDereferenceObject(KeyBody);
    return Status;
}

NTSTATUS
NTAPI
NtSaveKey(
    _In_ HANDLE KeyHandle,
    _In_ HANDLE FileHandle)
{
    return NtSaveKeyEx(KeyHandle, FileHandle, REG_STANDARD_FORMAT);
}

NTSTATUS
NTAPI
NtSaveMergedKeys(
    _In_ HANDLE HighPrecedenceKeyHandle,
    _In_ HANDLE LowPrecedenceKeyHandle,
    _In_ HANDLE FileHandle)
{
    PCM_KEY_BODY HighBody;
    PCM_KEY_BODY LowBody;
    NTSTATUS Status;

    PAGED_CODE();

    Status = CmpReferenceKey(HighPrecedenceKeyHandle, 0, &HighBody);
    if (!NT_SUCCESS(Status)) return Status;

    Status = CmpReferenceKey(LowPrecedenceKeyHandle, 0, &LowBody);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(HighBody);
        return Status;
    }

    CmpLockRegistry();
    Status = CmSaveMergedKeys(HighBody->KeyControlBlock,
                              LowBody->KeyControlBlock,
                              FileHandle);
    CmpUnlockRegistry();

    ObDereferenceObject(LowBody);
    ObDereferenceObject(HighBody);
    return Status;
}

NTSTATUS
NTAPI
NtRestoreKey(
    _In_ HANDLE KeyHandle,
    _In_ HANDLE FileHandle,
    _In_ ULONG RestoreFlags)
{
    REG_RESTORE_KEY_INFORMATION Information;
    CMP_CALLBACK_CALL Call;
    PCM_KEY_BODY KeyBody;
    NTSTATUS Status;

    PAGED_CODE();

    Status = CmpReferenceKey(KeyHandle, KEY_WRITE, &KeyBody);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Object = KeyBody;
    Information.FileHandle = FileHandle;
    Information.Flags = RestoreFlags;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreRestoreKey,
                         RegNtPostRestoreKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         KeyBody,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);
        ObDereferenceObject(KeyBody);
        return Status;
    }

    CmpLockRegistryExclusive();
    Status = CmpRestoreKeyFromFile(KeyBody, FileHandle, RestoreFlags);
    CmpUnlockRegistry();

    Status = CmpPostCallbacks(&Call, Status);
    ObDereferenceObject(KeyBody);
    return Status;
}

NTSTATUS
NTAPI
NtReplaceKey(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ HANDLE Key,
    _In_ POBJECT_ATTRIBUTES ReplacedObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    CM_CAPTURED_ATTRIBUTES CapturedNew;
    CM_CAPTURED_ATTRIBUTES CapturedOld;
    REG_REPLACE_KEY_INFORMATION Information;
    CMP_CALLBACK_CALL Call;
    PCM_KEY_BODY KeyBody;
    PCMHIVE Hive;
    NTSTATUS Status;

    PAGED_CODE();

    _SEH2_TRY
    {
        Status = CmpCaptureAttributes(ObjectAttributes,
                                      PreviousMode,
                                      &CapturedNew);
        if (NT_SUCCESS(Status))
        {
            Status = CmpCaptureAttributes(ReplacedObjectAttributes,
                                          PreviousMode,
                                          &CapturedOld);
            if (!NT_SUCCESS(Status)) CmpReleaseAttributes(&CapturedNew);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;

    Status = CmpReferenceKey(Key, KEY_WRITE, &KeyBody);
    if (!NT_SUCCESS(Status))
    {
        CmpReleaseAttributes(&CapturedOld);
        CmpReleaseAttributes(&CapturedNew);
        return Status;
    }

    RtlZeroMemory(&Information, sizeof(Information));
    Information.Object = KeyBody;
    Information.NewFileName = &CapturedNew.Name;
    Information.OldFileName = &CapturedOld.Name;

    if (!CmpPreCallbacks(&Call,
                         RegNtPreReplaceKey,
                         RegNtPostReplaceKey,
                         &Information,
                         &Information.CallContext,
                         &Information.ObjectContext,
                         KeyBody,
                         NULL,
                         &Status))
    {
        Status = CmpPostCallbacks(&Call, Status);
        ObDereferenceObject(KeyBody);
        CmpReleaseAttributes(&CapturedOld);
        CmpReleaseAttributes(&CapturedNew);
        return Status;
    }

    CmpLockRegistryExclusive();

    Hive = CONTAINING_RECORD(KeyBody->KeyControlBlock->KeyHive, CMHIVE, Hive);
    Status = CmpReplaceHiveFile(Hive, &CapturedNew.Name, &CapturedOld.Name);

    CmpUnlockRegistry();

    Status = CmpPostCallbacks(&Call, Status);

    ObDereferenceObject(KeyBody);
    CmpReleaseAttributes(&CapturedOld);
    CmpReleaseAttributes(&CapturedNew);
    return Status;
}

/*
 * Said once by the session manager when the system is far enough along to
 * trust what it has read. Until then the control set the system booted from
 * is not accepted and lazy flushing is held back.
 */
NTSTATUS
NTAPI
NtInitializeRegistry(
    _In_ USHORT Flag)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (Flag > CM_BOOT_FLAG_MAX) return STATUS_INVALID_PARAMETER;
    if (!CmFirstTime) return STATUS_ACCESS_DENIED;

    CmFirstTime = FALSE;

    if (Flag == CM_BOOT_FLAG_SETUP) CmpWasSetupBoot = TRUE;

    CmpInitializeHiveList();

    CmpLockRegistryExclusive();
    CmpSetGlobalQuotaAllowed();
    CmpNoVolatileCreates = FALSE;
    CmpUnlockRegistry();

    if (Flag >= CM_BOOT_FLAG_ACCEPTED && CmBootAcceptFirstTime)
    {
        CmBootAcceptFirstTime = FALSE;
        Status = CmpSaveBootControlSet((USHORT)(Flag - CM_BOOT_FLAG_ACCEPTED));
    }

    CmSetLazyFlushState(TRUE);
    CmpDoFlushAll(TRUE);
    return Status;
}

NTSTATUS
NTAPI
NtLockRegistryKey(
    _In_ HANDLE KeyHandle)
{
    PCM_KEY_BODY KeyBody;
    NTSTATUS Status;

    PAGED_CODE();

    Status = CmpReferenceKey(KeyHandle, KEY_WRITE, &KeyBody);
    if (!NT_SUCCESS(Status)) return Status;

    CmpLockRegistryExclusive();
    KeyBody->KeyControlBlock->ExtFlags |= CM_KCB_READ_ONLY_KEY;
    CmpUnlockRegistry();

    ObDereferenceObject(KeyBody);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtLockProductActivationKeys(
    _Inout_opt_ PULONG pPrivateVer,
    _Out_opt_ PULONG pSafeMode)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    _SEH2_TRY
    {
        if (pPrivateVer != NULL)
        {
            if (PreviousMode != KernelMode) ProbeForWriteUlong(pPrivateVer);

            /* TODO: no private product version is kept by the registry */
            *pPrivateVer = 0;
        }

        if (pSafeMode != NULL)
        {
            if (PreviousMode != KernelMode) ProbeForWriteUlong(pSafeMode);
            *pSafeMode = InitSafeBootMode;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtQueryOpenSubKeys(
    _In_ POBJECT_ATTRIBUTES TargetKey,
    _Out_ PULONG HandleCount)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    CM_CAPTURED_ATTRIBUTES Captured;
    PCM_KEY_BODY KeyBody = NULL;
    HANDLE KeyHandle = NULL;
    NTSTATUS Status;
    ULONG Count;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteUlong(HandleCount);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    _SEH2_TRY
    {
        Status = CmpCaptureAttributes(TargetKey, PreviousMode, &Captured);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;

    Status = CmpOpenKeyForLoadUnload(&Captured,
                                     KEY_READ,
                                     PreviousMode,
                                     &KeyHandle,
                                     &KeyBody);
    CmpReleaseAttributes(&Captured);

    if (!NT_SUCCESS(Status)) return Status;

    CmpLockRegistryExclusive();
    Count = CmpEnumerateOpenSubKeys(KeyBody->KeyControlBlock, FALSE, FALSE);
    CmpUnlockRegistry();

    ObDereferenceObject(KeyBody);
    ZwClose(KeyHandle);

    _SEH2_TRY
    {
        *HandleCount = Count;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtQueryOpenSubKeysEx(
    _In_ POBJECT_ATTRIBUTES TargetKey,
    _In_ ULONG BufferLength,
    _Out_writes_bytes_opt_(BufferLength) PVOID Buffer,
    _Out_ PULONG RequiredSize)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(TargetKey);
    UNREFERENCED_PARAMETER(BufferLength);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(RequiredSize);

    /* TODO: INFO_NEEDED - neither the SDK nor the Windows 11 public symbols
       publish this call's output layout, so only the count that
       NtQueryOpenSubKeys reports is available */
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * Rewrites the hives behind a set of keys without the gaps their history left
 * in them. What the hives hold does not change; the packed form is what the
 * next boot reads.
 */
NTSTATUS
NTAPI
NtCompactKeys(
    _In_ ULONG Count,
    _In_reads_(Count) PHANDLE KeyArray)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PCM_KEY_BODY *Bodies;
    PHANDLE Handles;
    NTSTATUS Status;
    ULONG i;

    PAGED_CODE();

    if (Count == 0 || Count > CMP_COMPACT_KEY_LIMIT)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!SeSinglePrivilegeCheck(SeRestorePrivilege, PreviousMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    Handles = ExAllocatePoolZero(PagedPool,
                                 Count * sizeof(HANDLE),
                                 TAG_CM);
    if (Handles == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Bodies = ExAllocatePoolZero(PagedPool,
                                Count * sizeof(PCM_KEY_BODY),
                                TAG_CM);
    if (Bodies == NULL)
    {
        ExFreePool(Handles);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForRead(KeyArray, Count * sizeof(HANDLE), sizeof(HANDLE));
        }

        RtlCopyMemory(Handles, KeyArray, Count * sizeof(HANDLE));
        Status = STATUS_SUCCESS;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    for (i = 0; NT_SUCCESS(Status) && i < Count; i++)
    {
        Status = CmpReferenceKey(Handles[i], KEY_WRITE, &Bodies[i]);
    }

    if (NT_SUCCESS(Status))
    {
        CmpLockRegistryExclusive();
        Status = CmpCompactHivesOfKeys(Count, Bodies);
        CmpUnlockRegistry();
    }

    for (i = 0; i < Count; i++)
    {
        if (Bodies[i] != NULL) ObDereferenceObject(Bodies[i]);
    }

    ExFreePool(Bodies);
    ExFreePool(Handles);
    return Status;
}

NTSTATUS
NTAPI
NtCompressKey(
    _In_ HANDLE Key)
{
    PCM_KEY_BODY KeyBody;
    NTSTATUS Status;
    PCMHIVE Hive;

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeRestorePrivilege, ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    Status = CmpReferenceKey(Key, KEY_WRITE, &KeyBody);
    if (!NT_SUCCESS(Status)) return Status;

    CmpLockRegistryExclusive();

    Hive = CmpFindLoadedHive(KeyBody->KeyControlBlock->KeyHive);
    Status = (Hive != NULL) ? CmpCompactHive(Hive) : STATUS_INVALID_PARAMETER;

    CmpUnlockRegistry();

    ObDereferenceObject(KeyBody);
    return Status;
}
