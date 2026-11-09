/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The records the system offers the authority that audits it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Nothing is written down until somebody asks to be told, and nobody has: the
 * authority that keeps the log is never connected to. Every question about
 * whether an event would be recorded is therefore answered the same way, in
 * one place, so that turning auditing on later is one change and not thirty.
 */
static
BOOLEAN
SepAuditingIsAskedFor(VOID)
{
    /* TODO: no category is ever turned on, the authority setting none */
    return FALSE;
}

/* Only a caller trusted to keep the log may write in it */
static
NTSTATUS
SepCheckAuditCaller(VOID)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();

    /* The log belongs to the subsystems, and the kernel does not ask here */
    if (PreviousMode == KernelMode) return STATUS_INVALID_PARAMETER;

    if (!SeSinglePrivilegeCheck(SeAuditPrivilege, PreviousMode))
        return STATUS_PRIVILEGE_NOT_HELD;

    return STATUS_SUCCESS;
}

/* A name the caller handed in has to be readable before it is used */
static
NTSTATUS
SepProbeAuditName(
    _In_opt_ PUNICODE_STRING Name,
    _In_ BOOLEAN Required)
{
    UNICODE_STRING Local;

    if (Name == NULL) return Required ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;

    ProbeForRead(Name, sizeof(*Name), sizeof(ULONG));
    Local = *Name;

    if (Local.Length != 0)
    {
        if (Local.Buffer == NULL) return STATUS_INVALID_PARAMETER;
        ProbeForRead(Local.Buffer, Local.Length, sizeof(WCHAR));
    }

    return STATUS_SUCCESS;
}

/*
 * What a subsystem asks of the system when it wants an access check made for
 * it and the answer written down. The check is the same one every caller
 * gets; what is asked about and in whose name differ.
 */
static
NTSTATUS
SepAuditedAccessCheck(
    _In_ PUNICODE_STRING SubsystemName,
    _In_opt_ PVOID HandleId,
    _In_opt_ HANDLE ClientToken,
    _In_ PUNICODE_STRING ObjectTypeName,
    _In_ PUNICODE_STRING ObjectName,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_opt_ PSID PrincipalSelfSid,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_TYPE_LIST ObjectTypeList,
    _In_ ULONG ObjectTypeListLength,
    _In_ PGENERIC_MAPPING GenericMapping,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PNTSTATUS AccessStatus,
    _Out_ PBOOLEAN GenerateOnClose,
    _In_ BOOLEAN ResultList)
{
    SECURITY_SUBJECT_CONTEXT SubjectContext;
    PRIVILEGE_SET PrivilegeSet;
    ULONG PrivilegeSetLength = sizeof(PrivilegeSet);
    NTSTATUS Status;
    PTOKEN Effective = NULL;

    PAGED_CODE();


    Status = SepCheckAuditCaller();
    if (!NT_SUCCESS(Status)) return Status;

    _SEH2_TRY
    {
        Status = SepProbeAuditName(SubsystemName, TRUE);
        if (NT_SUCCESS(Status)) Status = SepProbeAuditName(ObjectTypeName, TRUE);
        if (NT_SUCCESS(Status)) Status = SepProbeAuditName(ObjectName, TRUE);

        if (NT_SUCCESS(Status))
        {
            ProbeForWrite(GenerateOnClose, sizeof(*GenerateOnClose), sizeof(BOOLEAN));
            *GenerateOnClose = FALSE;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;

    /*
     * A caller that named no token of its own is asking in its own name, so
     * the check is made against the token it is acting with.
     */
    if (ClientToken == NULL)
    {
        SeCaptureSubjectContext(&SubjectContext);
        Effective = (PTOKEN)SeQuerySubjectContextToken(&SubjectContext);
    }

    Status = SepAccessCheckByType(SecurityDescriptor,
                                  PrincipalSelfSid,
                                  ClientToken,
                                  Effective,
                                  DesiredAccess,
                                  ObjectTypeList,
                                  ObjectTypeListLength,
                                  GenericMapping,
                                  &PrivilegeSet,
                                  &PrivilegeSetLength,
                                  GrantedAccess,
                                  AccessStatus,
                                  ResultList);

    if (ClientToken == NULL) SeReleaseSubjectContext(&SubjectContext);

    /* TODO: the answer is not written down, no record reaching the authority */
    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
SeAuditingFileEvents(
    _In_ BOOLEAN AccessGranted,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    return SepAuditingIsAskedFor();
}

BOOLEAN
NTAPI
SeAuditingFileEventsWithContext(
    _In_ BOOLEAN AccessGranted,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_opt_ PSECURITY_SUBJECT_CONTEXT SubjectSecurityContext)
{
    return SepAuditingIsAskedFor();
}

BOOLEAN
NTAPI
SeAuditingFileOrGlobalEvents(
    _In_ BOOLEAN AccessGranted,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectSecurityContext)
{
    return SepAuditingIsAskedFor();
}

BOOLEAN
NTAPI
SeAuditingHardLinkEvents(
    _In_ BOOLEAN AccessGranted,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    return SepAuditingIsAskedFor();
}

BOOLEAN
NTAPI
SeAuditingHardLinkEventsWithContext(
    _In_ BOOLEAN AccessGranted,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_opt_ PSECURITY_SUBJECT_CONTEXT SubjectSecurityContext)
{
    return SepAuditingIsAskedFor();
}

BOOLEAN
NTAPI
SeDetailedAuditingWithToken(
    _In_opt_ PTOKEN Token)
{
    return SepAuditingIsAskedFor();
}

VOID
NTAPI
SeOpenObjectAuditAlarm(
    _In_ PUNICODE_STRING ObjectTypeName,
    _In_opt_ PVOID Object,
    _In_opt_ PUNICODE_STRING AbsoluteObjectName,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ PACCESS_STATE AccessState,
    _In_ BOOLEAN ObjectCreated,
    _In_ BOOLEAN AccessGranted,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PBOOLEAN GenerateOnClose)
{
    /* Nothing was written down, so there is nothing to finish on close */
    *GenerateOnClose = FALSE;
}

VOID
NTAPI
SeOpenObjectForDeleteAuditAlarm(
    _In_ PUNICODE_STRING ObjectTypeName,
    _In_opt_ PVOID Object,
    _In_opt_ PUNICODE_STRING AbsoluteObjectName,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ PACCESS_STATE AccessState,
    _In_ BOOLEAN ObjectCreated,
    _In_ BOOLEAN AccessGranted,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PBOOLEAN GenerateOnClose)
{
    SeOpenObjectAuditAlarm(ObjectTypeName,
                           Object,
                           AbsoluteObjectName,
                           SecurityDescriptor,
                           AccessState,
                           ObjectCreated,
                           AccessGranted,
                           AccessMode,
                           GenerateOnClose);
}

VOID
NTAPI
SeCloseObjectAuditAlarm(
    _In_ PVOID Object,
    _In_ HANDLE Handle,
    _In_ BOOLEAN GenerateOnClose)
{
    /* TODO: nothing is written down, the authority keeping no log */
}

VOID
NTAPI
SeDeleteObjectAuditAlarm(
    _In_ PVOID Object,
    _In_ HANDLE Handle)
{
    /* TODO: nothing is written down, the authority keeping no log */
}

VOID
NTAPI
SePrivilegeObjectAuditAlarm(
    _In_ HANDLE Handle,
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ PPRIVILEGE_SET Privileges,
    _In_ BOOLEAN AccessGranted,
    _In_ KPROCESSOR_MODE CurrentMode)
{
    /* TODO: nothing is written down, the authority keeping no log */
}

VOID
NTAPI
SePrivilegedServiceAuditAlarm(
    _In_opt_ PUNICODE_STRING ServiceName,
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_ PPRIVILEGE_SET PrivilegeSet,
    _In_ BOOLEAN AccessGranted)
{
    /* TODO: nothing is written down, the authority keeping no log */
}

VOID
NTAPI
SeAuditHardLinkCreation(
    _In_ PUNICODE_STRING FileName,
    _In_ PUNICODE_STRING LinkName,
    _In_ BOOLEAN bSuccess)
{
    /* TODO: nothing is written down, the authority keeping no log */
}

VOID
NTAPI
SeAuditProcessCreation(
    _In_ PEPROCESS Process)
{
    /* TODO: nothing is written down, the authority keeping no log */
}

VOID
NTAPI
SeAuditProcessExit(
    _In_ PEPROCESS Process)
{
    /* TODO: nothing is written down, the authority keeping no log */
}

/*
 * Settles the name a process will be known by in a record, read from the file
 * it is running. The name is the caller's to keep and to let go of.
 */
NTSTATUS
NTAPI
SeInitializeProcessAuditName(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN DoAudit,
    _Out_ POBJECT_NAME_INFORMATION *AuditInfo)
{
    POBJECT_NAME_INFORMATION Name;
    ULONG Length = 0;
    NTSTATUS Status;

    PAGED_CODE();


    *AuditInfo = NULL;

    if (FileObject == NULL) return STATUS_INVALID_PARAMETER;

    Status = ObQueryNameString(FileObject, NULL, 0, &Length);
    if (Status != STATUS_INFO_LENGTH_MISMATCH && !NT_SUCCESS(Status))
        return Status;

    if (Length < sizeof(OBJECT_NAME_INFORMATION))
        Length = sizeof(OBJECT_NAME_INFORMATION);

    Name = ExAllocatePoolWithTag(PagedPool, Length, TAG_SEPA);
    if (Name == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = ObQueryNameString(FileObject, Name, Length, &Length);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Name, TAG_SEPA);
        return Status;
    }

    /* TODO: a name settled with auditing asked for is not written down */
    *AuditInfo = Name;
    return STATUS_SUCCESS;
}

/*
 * Hands back the name of the file a process is running, in one block the
 * caller frees. A process whose name was never settled is read from the
 * image it was made from instead.
 */
NTSTATUS
NTAPI
SeLocateProcessImageName(
    _Inout_ PEPROCESS Process,
    _Outptr_ PUNICODE_STRING *pImageFileName)
{
    POBJECT_NAME_INFORMATION Settled;
    POBJECT_NAME_INFORMATION Copy;
    PFILE_OBJECT FileObject;
    ULONG Length;

    PAGED_CODE();

    *pImageFileName = NULL;

    Settled = Process->SeAuditProcessCreationInfo.ImageFileName;

    if (Settled == NULL)
    {
        if (Process->SectionObject == NULL) return STATUS_UNSUCCESSFUL;

        FileObject = MmGetFileObjectForSection(Process->SectionObject);
        if (FileObject == NULL) return STATUS_UNSUCCESSFUL;

        return SeInitializeProcessAuditName(FileObject,
                                            FALSE,
                                            (POBJECT_NAME_INFORMATION *)pImageFileName);
    }

    Length = sizeof(OBJECT_NAME_INFORMATION) + Settled->Name.MaximumLength;

    Copy = ExAllocatePoolWithTag(PagedPool, Length, TAG_SEPA);
    if (Copy == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Copy->Name.Length = Settled->Name.Length;
    Copy->Name.MaximumLength = Settled->Name.MaximumLength;
    Copy->Name.Buffer = (PWSTR)(Copy + 1);

    if (Settled->Name.Length != 0)
    {
        RtlCopyMemory(Copy->Name.Buffer,
                      Settled->Name.Buffer,
                      Settled->Name.Length);
    }

    *pImageFileName = &Copy->Name;
    return STATUS_SUCCESS;
}

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
 * until one asks to be told, so a record nobody asked for is let go.
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


    if (SourceName == NULL || AuditParameters == NULL)
        return STATUS_INVALID_PARAMETER;

    if (AuditParameters->ParameterCount > SE_MAX_AUDIT_PARAMETERS)
        return STATUS_INVALID_PARAMETER;

    /* TODO: no record reaches the authority, which is never connected to */
    return STATUS_SUCCESS;
}

/* SYSTEM CALLS ***************************************************************/

__kernel_entry
NTSTATUS
NTAPI
NtAccessCheckAndAuditAlarm(
    _In_ PUNICODE_STRING SubsystemName,
    _In_opt_ PVOID HandleId,
    _In_ PUNICODE_STRING ObjectTypeName,
    _In_ PUNICODE_STRING ObjectName,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ PGENERIC_MAPPING GenericMapping,
    _In_ BOOLEAN ObjectCreation,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PNTSTATUS AccessStatus,
    _Out_ PBOOLEAN GenerateOnClose)
{
    return SepAuditedAccessCheck(SubsystemName,
                                 HandleId,
                                 NULL,
                                 ObjectTypeName,
                                 ObjectName,
                                 SecurityDescriptor,
                                 NULL,
                                 DesiredAccess,
                                 NULL,
                                 0,
                                 GenericMapping,
                                 GrantedAccess,
                                 AccessStatus,
                                 GenerateOnClose,
                                 FALSE);
}

__kernel_entry
NTSTATUS
NTAPI
NtAccessCheckByTypeAndAuditAlarm(
    _In_ PUNICODE_STRING SubsystemName,
    _In_opt_ PVOID HandleId,
    _In_ PUNICODE_STRING ObjectTypeName,
    _In_ PUNICODE_STRING ObjectName,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_opt_ PSID PrincipalSelfSid,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ AUDIT_EVENT_TYPE AuditType,
    _In_ ULONG Flags,
    _In_opt_ POBJECT_TYPE_LIST ObjectTypeList,
    _In_ ULONG ObjectTypeLength,
    _In_ PGENERIC_MAPPING GenericMapping,
    _In_ BOOLEAN ObjectCreation,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PNTSTATUS AccessStatus,
    _Out_ PBOOLEAN GenerateOnClose)
{
    return SepAuditedAccessCheck(SubsystemName,
                                 HandleId,
                                 NULL,
                                 ObjectTypeName,
                                 ObjectName,
                                 SecurityDescriptor,
                                 PrincipalSelfSid,
                                 DesiredAccess,
                                 ObjectTypeList,
                                 ObjectTypeLength,
                                 GenericMapping,
                                 GrantedAccess,
                                 AccessStatus,
                                 GenerateOnClose,
                                 FALSE);
}

__kernel_entry
NTSTATUS
NTAPI
NtAccessCheckByTypeResultListAndAuditAlarm(
    _In_ PUNICODE_STRING SubsystemName,
    _In_opt_ PVOID HandleId,
    _In_ PUNICODE_STRING ObjectTypeName,
    _In_ PUNICODE_STRING ObjectName,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_opt_ PSID PrincipalSelfSid,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ AUDIT_EVENT_TYPE AuditType,
    _In_ ULONG Flags,
    _In_opt_ POBJECT_TYPE_LIST ObjectTypeList,
    _In_ ULONG ObjectTypeListLength,
    _In_ PGENERIC_MAPPING GenericMapping,
    _In_ BOOLEAN ObjectCreation,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PNTSTATUS AccessStatus,
    _Out_ PBOOLEAN GenerateOnClose)
{
    return SepAuditedAccessCheck(SubsystemName,
                                 HandleId,
                                 NULL,
                                 ObjectTypeName,
                                 ObjectName,
                                 SecurityDescriptor,
                                 PrincipalSelfSid,
                                 DesiredAccess,
                                 ObjectTypeList,
                                 ObjectTypeListLength,
                                 GenericMapping,
                                 GrantedAccess,
                                 AccessStatus,
                                 GenerateOnClose,
                                 TRUE);
}

__kernel_entry
NTSTATUS
NTAPI
NtAccessCheckByTypeResultListAndAuditAlarmByHandle(
    _In_ PUNICODE_STRING SubsystemName,
    _In_opt_ PVOID HandleId,
    _In_ HANDLE ClientToken,
    _In_ PUNICODE_STRING ObjectTypeName,
    _In_ PUNICODE_STRING ObjectName,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_opt_ PSID PrincipalSelfSid,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ AUDIT_EVENT_TYPE AuditType,
    _In_ ULONG Flags,
    _In_opt_ POBJECT_TYPE_LIST ObjectTypeList,
    _In_ ULONG ObjectTypeListLength,
    _In_ PGENERIC_MAPPING GenericMapping,
    _In_ BOOLEAN ObjectCreation,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PNTSTATUS AccessStatus,
    _Out_ PBOOLEAN GenerateOnClose)
{
    /* A caller that named a token is asking in somebody else's name */
    if (ClientToken == NULL) return STATUS_INVALID_HANDLE;

    return SepAuditedAccessCheck(SubsystemName,
                                 HandleId,
                                 ClientToken,
                                 ObjectTypeName,
                                 ObjectName,
                                 SecurityDescriptor,
                                 PrincipalSelfSid,
                                 DesiredAccess,
                                 ObjectTypeList,
                                 ObjectTypeListLength,
                                 GenericMapping,
                                 GrantedAccess,
                                 AccessStatus,
                                 GenerateOnClose,
                                 TRUE);
}

/*
 * The alarms a subsystem raises by itself, for what it decided without
 * asking. Each one is refused unless the caller is trusted with the log.
 */
__kernel_entry
NTSTATUS
NTAPI
NtOpenObjectAuditAlarm(
    _In_ PUNICODE_STRING SubsystemName,
    _In_opt_ PVOID HandleId,
    _In_ PUNICODE_STRING ObjectTypeName,
    _In_ PUNICODE_STRING ObjectName,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ HANDLE ClientToken,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ACCESS_MASK GrantedAccess,
    _In_opt_ PPRIVILEGE_SET Privileges,
    _In_ BOOLEAN ObjectCreation,
    _In_ BOOLEAN AccessGranted,
    _Out_ PBOOLEAN GenerateOnClose)
{
    NTSTATUS Status;

    PAGED_CODE();


    Status = SepCheckAuditCaller();
    if (!NT_SUCCESS(Status)) return Status;

    _SEH2_TRY
    {
        Status = SepProbeAuditName(SubsystemName, TRUE);
        if (NT_SUCCESS(Status)) Status = SepProbeAuditName(ObjectTypeName, TRUE);
        if (NT_SUCCESS(Status)) Status = SepProbeAuditName(ObjectName, TRUE);

        if (NT_SUCCESS(Status))
        {
            ProbeForWrite(GenerateOnClose, sizeof(*GenerateOnClose), sizeof(BOOLEAN));
            *GenerateOnClose = FALSE;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    /* TODO: the alarm is accepted and not written down */
    return Status;
}

__kernel_entry
NTSTATUS
NTAPI
NtCloseObjectAuditAlarm(
    _In_ PUNICODE_STRING SubsystemName,
    _In_opt_ PVOID HandleId,
    _In_ BOOLEAN GenerateOnClose)
{
    NTSTATUS Status;

    PAGED_CODE();


    Status = SepCheckAuditCaller();
    if (!NT_SUCCESS(Status)) return Status;

    _SEH2_TRY
    {
        Status = SepProbeAuditName(SubsystemName, TRUE);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    /* TODO: the alarm is accepted and not written down */
    return Status;
}

__kernel_entry
NTSTATUS
NTAPI
NtDeleteObjectAuditAlarm(
    _In_ PUNICODE_STRING SubsystemName,
    _In_opt_ PVOID HandleId,
    _In_ BOOLEAN GenerateOnClose)
{
    return NtCloseObjectAuditAlarm(SubsystemName, HandleId, GenerateOnClose);
}

__kernel_entry
NTSTATUS
NTAPI
NtPrivilegeObjectAuditAlarm(
    _In_ PUNICODE_STRING SubsystemName,
    _In_opt_ PVOID HandleId,
    _In_ HANDLE ClientToken,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ PPRIVILEGE_SET Privileges,
    _In_ BOOLEAN AccessGranted)
{
    NTSTATUS Status;

    PAGED_CODE();


    Status = SepCheckAuditCaller();
    if (!NT_SUCCESS(Status)) return Status;

    _SEH2_TRY
    {
        Status = SepProbeAuditName(SubsystemName, TRUE);

        if (NT_SUCCESS(Status))
        {
            if (Privileges == NULL) Status = STATUS_INVALID_PARAMETER;
            else ProbeForRead(Privileges, sizeof(PRIVILEGE_SET), sizeof(ULONG));
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    /* TODO: the alarm is accepted and not written down */
    return Status;
}

NTSTATUS
NTAPI
NtPrivilegedServiceAuditAlarm(
    _In_ PUNICODE_STRING SubsystemName,
    _In_ PUNICODE_STRING ServiceName,
    _In_ HANDLE ClientToken,
    _In_ PPRIVILEGE_SET Privileges,
    _In_ BOOLEAN AccessGranted)
{
    NTSTATUS Status;

    PAGED_CODE();


    Status = SepCheckAuditCaller();
    if (!NT_SUCCESS(Status)) return Status;

    _SEH2_TRY
    {
        Status = SepProbeAuditName(SubsystemName, TRUE);
        if (NT_SUCCESS(Status)) Status = SepProbeAuditName(ServiceName, FALSE);

        if (NT_SUCCESS(Status) && Privileges != NULL)
            ProbeForRead(Privileges, sizeof(PRIVILEGE_SET), sizeof(ULONG));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    /* TODO: the alarm is accepted and not written down */
    return Status;
}
