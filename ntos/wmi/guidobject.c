/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The object a caller holds a WMI data block or trace class through
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "wmipriv.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

POBJECT_TYPE WmipGuidObjectType = NULL;

GENERIC_MAPPING WmipGenericMapping =
{
    STANDARD_RIGHTS_READ | WMIGUID_QUERY | WMIGUID_NOTIFICATION |
        WMIGUID_READ_DESCRIPTION | TRACELOG_ACCESS_REALTIME,
    STANDARD_RIGHTS_WRITE | WMIGUID_SET | TRACELOG_CREATE_REALTIME |
        TRACELOG_CREATE_ONDISK | TRACELOG_GUID_ENABLE | TRACELOG_LOG_EVENT |
        TRACELOG_REGISTER_GUIDS | TRACELOG_JOIN_GROUP,
    STANDARD_RIGHTS_EXECUTE | WMIGUID_EXECUTE | TRACELOG_ACCESS_KERNEL_LOGGER,
    WMIGUID_ALL_ACCESS
};

/* PRIVATE DATA ***************************************************************/

#define WMIP_TAG 'imWD'

/* PRIVATE FUNCTIONS **********************************************************/

/* Reads one hexadecimal figure, or says the character was not one */
static
BOOLEAN
WmipHexFigure(
    _In_ WCHAR Character,
    _Out_ PULONG Value)
{
    if (Character >= L'0' && Character <= L'9')
    {
        *Value = (ULONG)(Character - L'0');
        return TRUE;
    }

    if (Character >= L'a' && Character <= L'f')
    {
        *Value = (ULONG)(Character - L'a') + 10;
        return TRUE;
    }

    if (Character >= L'A' && Character <= L'F')
    {
        *Value = (ULONG)(Character - L'A') + 10;
        return TRUE;
    }

    *Value = 0;
    return FALSE;
}

/* Reads a run of hexadecimal figures as one number */
static
BOOLEAN
WmipHexRun(
    _In_reads_(Count) PCWSTR Text,
    _In_ ULONG Count,
    _Out_ PULONGLONG Value)
{
    ULONGLONG Result = 0;
    ULONG Figure;
    ULONG i;

    *Value = 0;

    for (i = 0; i < Count; i++)
    {
        if (!WmipHexFigure(Text[i], &Figure)) return FALSE;
        Result = (Result << 4) | Figure;
    }

    *Value = Result;
    return TRUE;
}

static
BOOLEAN
WmipGuidFromName(
    _In_ PCUNICODE_STRING Name,
    _Out_ LPGUID Guid)
{
    PCWSTR Text = Name->Buffer;
    USHORT Length = Name->Length / sizeof(WCHAR);
    ULONGLONG Part;
    ULONG i;

    RtlZeroMemory(Guid, sizeof(*Guid));

    if (Text == NULL) return FALSE;

    if (Length == GUID_STRING_LENGTH + 2 && Text[0] == L'{' &&
        Text[Length - 1] == L'}')
    {
        Text++;
        Length -= 2;
    }

    if (Length != GUID_STRING_LENGTH) return FALSE;

    if (Text[8] != L'-' || Text[13] != L'-' || Text[18] != L'-' ||
        Text[23] != L'-')
    {
        return FALSE;
    }

    if (!WmipHexRun(&Text[0], 8, &Part)) return FALSE;
    Guid->Data1 = (ULONG)Part;

    if (!WmipHexRun(&Text[9], 4, &Part)) return FALSE;
    Guid->Data2 = (USHORT)Part;

    if (!WmipHexRun(&Text[14], 4, &Part)) return FALSE;
    Guid->Data3 = (USHORT)Part;

    if (!WmipHexRun(&Text[19], 4, &Part)) return FALSE;
    Guid->Data4[0] = (UCHAR)(Part >> 8);
    Guid->Data4[1] = (UCHAR)Part;

    if (!WmipHexRun(&Text[24], 12, &Part)) return FALSE;

    for (i = 0; i < 6; i++)
    {
        Guid->Data4[2 + i] = (UCHAR)(Part >> (8 * (5 - i)));
    }

    return TRUE;
}

static
NTSTATUS
WmipCreateGuidObject(
    _In_ LPCGUID Guid,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PHANDLE OutGuidObjectHandle,
    _Outptr_ PVOID *OutGuidObject)
{
    PWMIP_GUID_OBJECT GuidObject;
    NTSTATUS Status;
    HANDLE Handle;

    *OutGuidObjectHandle = NULL;
    *OutGuidObject = NULL;

    Status = ObCreateObject(AccessMode,
                            WmipGuidObjectType,
                            NULL,
                            AccessMode,
                            NULL,
                            sizeof(WMIP_GUID_OBJECT),
                            0,
                            0,
                            (PVOID *)&GuidObject);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(GuidObject, sizeof(*GuidObject));

    KeInitializeEvent(&GuidObject->Event, NotificationEvent, FALSE);
    GuidObject->Guid = *Guid;
    InitializeListHead(&GuidObject->IrpLink);
    InitializeListHead(&GuidObject->NotificationLink);

    Status = ObInsertObject(GuidObject,
                            NULL,
                            DesiredAccess,
                            0,
                            OutGuidObject,
                            &Handle);
    if (!NT_SUCCESS(Status)) return Status;

    WmipGuidObjectCreated(Guid);

    *OutGuidObjectHandle = Handle;
    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
WmipInitializeGuidObjectType(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"WmiGuid");

    PAGED_CODE();

    RtlZeroMemory(&Initializer, sizeof(Initializer));

    Initializer.Length = sizeof(Initializer);
    Initializer.UnnamedObjectsOnly = TRUE;
    Initializer.SecurityRequired = TRUE;
    Initializer.MaintainHandleCount = TRUE;
    Initializer.InvalidAttributes = OBJ_OPENLINK | OBJ_PERMANENT;
    Initializer.ValidAccessMask = WMIGUID_ALL_ACCESS;
    Initializer.GenericMapping = WmipGenericMapping;
    Initializer.PoolType = NonPagedPool;
    Initializer.DefaultNonPagedPoolCharge = sizeof(WMIP_GUID_OBJECT);
    Initializer.CloseProcedure = WmipCloseMethod;
    Initializer.DeleteProcedure = WmipDeleteMethod;
    Initializer.SecurityProcedure = WmipSecurityMethod;

    return ObCreateObjectType(&Name,
                              &Initializer,
                              NULL,
                              &WmipGuidObjectType);
}

/* Opens the identifier the caller names */
NTSTATUS
NTAPI
WmipOpenGuidObject(
    _In_ LPCGUID Guid,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PHANDLE OutGuidObjectHandle,
    _Outptr_ PVOID *OutGuidObject)
{
    PAGED_CODE();

    if (Guid == NULL || OutGuidObjectHandle == NULL || OutGuidObject == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (WmipGuidObjectType == NULL) return STATUS_NOT_SUPPORTED;

    return WmipCreateGuidObject(Guid,
                                DesiredAccess,
                                AccessMode,
                                OutGuidObjectHandle,
                                OutGuidObject);
}

NTSTATUS
NTAPI
WmipOpenGuidObjectByName(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PHANDLE OutGuidObjectHandle,
    _Outptr_ PVOID *OutGuidObject)
{
    UNICODE_STRING Name;
    PWSTR Buffer = NULL;
    NTSTATUS Status;
    GUID Guid;

    PAGED_CODE();

    if (ObjectAttributes == NULL || OutGuidObjectHandle == NULL ||
        OutGuidObject == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (WmipGuidObjectType == NULL) return STATUS_NOT_SUPPORTED;

    if (AccessMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(ObjectAttributes,
                         sizeof(*ObjectAttributes),
                         sizeof(ULONG));

            if (ObjectAttributes->ObjectName == NULL)
            {
                _SEH2_YIELD(return STATUS_OBJECT_NAME_INVALID);
            }

            ProbeForRead(ObjectAttributes->ObjectName,
                         sizeof(UNICODE_STRING),
                         sizeof(ULONG));
            Name = *ObjectAttributes->ObjectName;
            ProbeForRead(Name.Buffer, Name.Length, sizeof(WCHAR));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;

        if (Name.Length == 0 || Name.Length > (GUID_STRING_LENGTH + 2) * sizeof(WCHAR))
        {
            return STATUS_OBJECT_NAME_INVALID;
        }

        Buffer = ExAllocatePoolZero(PagedPool, Name.Length, WMIP_TAG);
        if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        _SEH2_TRY
        {
            RtlCopyMemory(Buffer, Name.Buffer, Name.Length);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ExFreePoolWithTag(Buffer, WMIP_TAG);
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;

        Name.Buffer = Buffer;
        Name.MaximumLength = Name.Length;
    }
    else
    {
        if (ObjectAttributes->ObjectName == NULL)
        {
            return STATUS_OBJECT_NAME_INVALID;
        }

        Name = *ObjectAttributes->ObjectName;
    }

    if (!WmipGuidFromName(&Name, &Guid))
    {
        Status = STATUS_OBJECT_NAME_INVALID;
    }
    else
    {
        Status = WmipCreateGuidObject(&Guid,
                                      DesiredAccess,
                                      AccessMode,
                                      OutGuidObjectHandle,
                                      OutGuidObject);
    }

    if (Buffer != NULL) ExFreePoolWithTag(Buffer, WMIP_TAG);

    return Status;
}

VOID
NTAPI
WmipCloseMethod(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount)
{
    PWMIP_GUID_OBJECT GuidObject = Object;

    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(ProcessHandleCount);

    if (SystemHandleCount > 1) return;

    WmipClearIrpObjectList(GuidObject);
}

/* Runs when the object itself goes, once nothing points at it any more */
VOID
NTAPI
WmipDeleteMethod(
    _In_ PVOID Object)
{
    PWMIP_GUID_OBJECT GuidObject = Object;

    WmipClearIrpObjectList(GuidObject);

    if (!IsListEmpty(&GuidObject->NotificationLink))
    {
        ExAcquirePushLockExclusive(&WmipNotificationLock);
        RemoveEntryList(&GuidObject->NotificationLink);
        InitializeListHead(&GuidObject->NotificationLink);
        ExReleasePushLockExclusive(&WmipNotificationLock);
    }

    GuidObject->NotificationCallback = NULL;
    GuidObject->NotificationContext = NULL;

    WmipGuidObjectDeleted(GuidObject);
}

NTSTATUS
NTAPI
WmipSecurityMethod(
    _In_ PVOID Object,
    _In_ SECURITY_OPERATION_CODE OperationType,
    _In_ PSECURITY_INFORMATION SecurityInformation,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PULONG CapturedLength,
    _Inout_ PSECURITY_DESCRIPTOR *ObjectSecurityDescriptor,
    _In_ POOL_TYPE PoolType,
    _In_ PGENERIC_MAPPING GenericMapping,
    _In_ KPROCESSOR_MODE AccessMode)
{
    UNREFERENCED_PARAMETER(GenericMapping);

    /* TODO: read the per-identifier descriptors the WMI security key holds */
    return SeDefaultObjectMethod(Object,
                                 OperationType,
                                 SecurityInformation,
                                 SecurityDescriptor,
                                 CapturedLength,
                                 ObjectSecurityDescriptor,
                                 PoolType,
                                 &WmipGenericMapping,
                                 AccessMode);
}

/* EOF */
