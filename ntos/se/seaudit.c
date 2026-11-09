/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The records the system offers the authority that audits it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Holds one thing a record is to say. What it is decides whether the record
 * keeps the value itself or only where it is.
 */
NTSTATUS
NTAPI
SeSetAuditParameter(
    _Inout_ PSE_ADT_PARAMETER_ARRAY AuditParameters,
    _In_ SE_ADT_PARAMETER_TYPE Type,
    _In_ ULONG Index,
    _In_ PVOID Data)
{
    PSE_ADT_PARAMETER_ARRAY_ENTRY Entry;

    PAGED_CODE();

    if (AuditParameters == NULL || Index >= SE_MAX_AUDIT_PARAMETERS)
        return STATUS_INVALID_PARAMETER;

    Entry = &AuditParameters->Parameters[Index];
    RtlZeroMemory(Entry, sizeof(*Entry));
    Entry->Type = Type;

    switch (Type)
    {
        case SeAdtParmTypeNone:
            break;

        case SeAdtParmTypeUlong:
        case SeAdtParmTypeHexUlong:
        case SeAdtParmTypeAccessMask:
            if (Data == NULL) return STATUS_INVALID_PARAMETER;
            Entry->Data[0] = *(PULONG)Data;
            Entry->Length = sizeof(ULONG);
            break;

        case SeAdtParmTypeLuid:
        case SeAdtParmTypeLogonId:
            if (Data == NULL) return STATUS_INVALID_PARAMETER;
            Entry->Data[0] = ((PLUID)Data)->LowPart;
            Entry->Data[1] = (ULONG_PTR)((PLUID)Data)->HighPart;
            Entry->Length = sizeof(LUID);
            break;

        case SeAdtParmTypeString:
        case SeAdtParmTypeFileSpec:
            if (Data == NULL) return STATUS_INVALID_PARAMETER;
            Entry->Address = Data;
            Entry->Length = ((PUNICODE_STRING)Data)->Length;
            break;

        case SeAdtParmTypeSid:
            if (Data == NULL || !RtlValidSid(Data))
                return STATUS_INVALID_PARAMETER;
            Entry->Address = Data;
            Entry->Length = RtlLengthSid(Data);
            break;

        default:
            /* TODO: the remaining kinds keep only where the value is */
            Entry->Address = Data;
            break;
    }

    if (AuditParameters->ParameterCount <= Index)
        AuditParameters->ParameterCount = Index + 1;

    return STATUS_SUCCESS;
}

/*
 * Offers a record to the authority that keeps the log. Nothing is audited
 * until one asks to be told, so a record that nobody asked for is let go.
 */
NTSTATUS
NTAPI
SeReportSecurityEvent(
    _In_ ULONG Flags,
    _In_ PUNICODE_STRING SourceName,
    _In_opt_ PSID UserSid,
    _In_ PSE_ADT_PARAMETER_ARRAY AuditParameters)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(UserSid);

    if (SourceName == NULL || AuditParameters == NULL)
        return STATUS_INVALID_PARAMETER;

    if (AuditParameters->ParameterCount > SE_MAX_AUDIT_PARAMETERS)
        return STATUS_INVALID_PARAMETER;

    /* TODO: no record reaches the authority, which is never connected to */
    return STATUS_SUCCESS;
}
