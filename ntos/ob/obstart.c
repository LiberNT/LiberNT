/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object manager start-up
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/unaligned.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

GENERIC_MAPPING ObpTypeMapping =
{
    STANDARD_RIGHTS_READ,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE,
    0x000F0001
};

GENERIC_MAPPING ObpDirectoryMapping =
{
    STANDARD_RIGHTS_READ    | DIRECTORY_QUERY               | DIRECTORY_TRAVERSE,
    STANDARD_RIGHTS_WRITE   | DIRECTORY_CREATE_SUBDIRECTORY | DIRECTORY_CREATE_OBJECT,
    STANDARD_RIGHTS_EXECUTE | DIRECTORY_QUERY               | DIRECTORY_TRAVERSE,
    DIRECTORY_ALL_ACCESS
};

GENERIC_MAPPING ObpSymbolicLinkMapping =
{
    STANDARD_RIGHTS_READ    | SYMBOLIC_LINK_QUERY,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE | SYMBOLIC_LINK_QUERY,
    SYMBOLIC_LINK_ALL_ACCESS
};

ULONG ObpTraceLevel = 0;

CODE_SEG("INIT")
VOID
NTAPI
PsInitializeQuotaSystem(VOID);

ULONG ObpInitializationPhase;

ULONG ObpObjectSecurityMode = 1;
ULONG ObpProtectionMode = 0;

/* Optional headers must preserve the low bits used for handle attributes. */
C_ASSERT((sizeof(OBJECT_HEADER_NAME_INFO) & OBJ_HANDLE_ATTRIBUTES) == 0);
C_ASSERT((sizeof(OBJECT_HEADER_CREATOR_INFO) & OBJ_HANDLE_ATTRIBUTES) == 0);
C_ASSERT((sizeof(OBJECT_HEADER_HANDLE_INFO) & OBJ_HANDLE_ATTRIBUTES) == 0);
C_ASSERT((sizeof(OBJECT_HEADER_QUOTA_INFO) & OBJ_HANDLE_ATTRIBUTES) == 0);
C_ASSERT((sizeof(OBJECT_HEADER_PROCESS_INFO) & OBJ_HANDLE_ATTRIBUTES) == 0);
#ifndef _WIN64
C_ASSERT(sizeof(OBJECT_HEADER_NAME_INFO) == 16);
C_ASSERT(sizeof(OBJECT_HEADER_CREATOR_INFO) == 16);
#endif

#ifdef _WIN64
C_ASSERT(sizeof(OBJECT_HEADER) == 56);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, PointerCount) == 0);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, HandleCount) == 8);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, Lock) == 16);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, TypeIndex) == 24);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, TraceFlags) == 25);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, InfoMask) == 26);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, Flags) == 27);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, ObjectCreateInfo) == 32);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, SecurityDescriptor) == 40);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER, Body) == 48);
C_ASSERT(sizeof(OBJECT_HEADER_QUOTA_INFO) == 32);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_QUOTA_INFO, SecurityDescriptorQuotaBlock) == 16);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_QUOTA_INFO, Reserved2) == 24);
C_ASSERT(sizeof(OBJECT_HEADER_PROCESS_INFO) == 16);
C_ASSERT(sizeof(OBJECT_HEADER_HANDLE_INFO) == 16);
C_ASSERT(sizeof(OBJECT_HEADER_NAME_INFO) == 32);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_NAME_INFO, Directory) == 0);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_NAME_INFO, Name) == 8);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_NAME_INFO, ReferenceCount) == 24);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_NAME_INFO, Reserved) == 28);
C_ASSERT(sizeof(OBJECT_HEADER_CREATOR_INFO) == 32);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_CREATOR_INFO, CreatorUniqueProcess) == 16);
C_ASSERT(FIELD_OFFSET(OBJECT_HEADER_CREATOR_INFO, CreatorBackTraceIndex) == 24);
C_ASSERT(sizeof(OBJECT_HEADER_AUDIT_INFO) == 16);
C_ASSERT(sizeof(OBJECT_HEADER_EXTENDED_INFO) == 16);
C_ASSERT(sizeof(OBJECT_HEADER_PADDING_INFO) == 4);
C_ASSERT(sizeof(OB_EXTENDED_PARSE_PARAMETERS) == 16);
C_ASSERT(FIELD_OFFSET(OB_EXTENDED_PARSE_PARAMETERS, Length) == 0);
C_ASSERT(FIELD_OFFSET(OB_EXTENDED_PARSE_PARAMETERS, RestrictedAccessMask) == 4);
C_ASSERT(FIELD_OFFSET(OB_EXTENDED_PARSE_PARAMETERS, Silo) == 8);
C_ASSERT(sizeof(OBJECT_TYPE_INITIALIZER) == 120);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, ObjectTypeFlags) == 2);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, ObjectTypeCode) == 4);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, InvalidAttributes) == 8);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, GenericMapping) == 12);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, ValidAccessMask) == 28);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, RetainAccess) == 32);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, PoolType) == 36);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, DefaultPagedPoolCharge) == 40);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, DefaultNonPagedPoolCharge) == 44);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, DumpProcedure) == 48);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, OkayToCloseProcedure) == 104);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, WaitObjectFlagMask) == 112);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, WaitObjectFlagOffset) == 116);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE_INITIALIZER, WaitObjectPointerOffset) == 118);
C_ASSERT(sizeof(OBJECT_TYPE) == 224);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, TypeList) == 0);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, Name) == 16);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, DefaultObject) == 32);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, Index) == 40);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, TotalNumberOfObjects) == 44);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, TotalNumberOfHandles) == 48);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, HighWaterNumberOfObjects) == 52);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, HighWaterNumberOfHandles) == 56);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, TypeInfo) == 64);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, TypeLock) == 184);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, Key) == 192);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, CallbackList) == 200);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, SeMandatoryLabelMask) == 216);
C_ASSERT(FIELD_OFFSET(OBJECT_TYPE, SeTrustConstraintMask) == 220);
#endif


/* PRIVATE FUNCTIONS **********************************************************/

/*
 * \KernelObjects is readable by everyone but may only be changed by the system
 * and by administrators.
 */
static
CODE_SEG("INIT")
NTSTATUS
NTAPI
ObpCreateKernelObjectsSD(
    _Out_ PSECURITY_DESCRIPTOR *SecurityDescriptor)
{
    PSECURITY_DESCRIPTOR Sd;
    NTSTATUS Status;
    ULONG AclSize;
    PACL Dacl;

    AclSize = sizeof(ACL) +
              sizeof(ACE) + RtlLengthSid(SeWorldSid) +
              sizeof(ACE) + RtlLengthSid(SeAliasAdminsSid) +
              sizeof(ACE) + RtlLengthSid(SeLocalSystemSid);

    Sd = ExAllocatePoolWithTag(PagedPool,
                               sizeof(SECURITY_DESCRIPTOR) + AclSize,
                               TAG_SD);
    if (Sd == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = RtlCreateSecurityDescriptor(Sd, SECURITY_DESCRIPTOR_REVISION);
    if (NT_SUCCESS(Status))
    {
        Dacl = (PACL)((ULONG_PTR)Sd + sizeof(SECURITY_DESCRIPTOR));

        RtlCreateAcl(Dacl, AclSize, ACL_REVISION);
        RtlAddAccessAllowedAce(Dacl, ACL_REVISION, GENERIC_READ, SeWorldSid);
        RtlAddAccessAllowedAce(Dacl, ACL_REVISION, GENERIC_ALL, SeAliasAdminsSid);
        RtlAddAccessAllowedAce(Dacl, ACL_REVISION, GENERIC_ALL, SeLocalSystemSid);

        Status = RtlSetDaclSecurityDescriptor(Sd, TRUE, Dacl, FALSE);
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Sd, TAG_SD);
        return Status;
    }

    WriteUnalignedUlongPtr((ULONG_PTR *)SecurityDescriptor, (ULONG_PTR)Sd);
    return STATUS_SUCCESS;
}

/*
 * Builds one of the two per-processor lookaside lists, falling back to the
 * shared one when there is no pool for a private copy.
 */
static
CODE_SEG("INIT")
VOID
NTAPI
ObpInitializeProcessorLookaside(
    _Inout_ PGENERAL_LOOKASIDE *Private,
    _Inout_ PGENERAL_LOOKASIDE *Shared,
    _In_ PGENERAL_LOOKASIDE Fallback,
    _In_ POOL_TYPE PoolType,
    _In_ ULONG Size,
    _In_ ULONG Tag,
    _In_ USHORT Depth)
{
    PGENERAL_LOOKASIDE List;

    *Shared = Fallback;

    List = ExAllocatePoolWithTag(NonPagedPool, sizeof(GENERAL_LOOKASIDE), Tag);
    if (List != NULL)
    {
        ExInitializeSystemLookasideList(List,
                                        PoolType,
                                        Size,
                                        Tag,
                                        Depth,
                                        &ExSystemLookasideListHead);
    }
    else
    {
        List = Fallback;
    }

    *Private = List;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ObInit2(VOID)
{
    PKPRCB Prcb;
    CCHAR i;

    for (i = 0; i < KeNumberProcessors; i++)
    {
        Prcb = KiProcessorBlock[(int)i];

        ObpInitializeProcessorLookaside(&Prcb->PPLookasideList[LookasideCreateInfoList].P,
                                        &Prcb->PPLookasideList[LookasideCreateInfoList].L,
                                        &ObpCreateInfoLookasideList,
                                        NonPagedPool,
                                        sizeof(OBJECT_CREATE_INFORMATION),
                                        'ICbO',
                                        32);

        ObpInitializeProcessorLookaside(&Prcb->PPLookasideList[LookasideNameBufferList].P,
                                        &Prcb->PPLookasideList[LookasideNameBufferList].L,
                                        &ObpNameBufferLookasideList,
                                        PagedPool,
                                        248,
                                        'MNbO',
                                        16);
    }

    return TRUE;
}

/*
 * The object header carries its optional parts in front of itself; this table
 * turns the InfoMask into the distance from the header back to its allocation.
 */
static
CODE_SEG("INIT")
VOID
NTAPI
ObpBuildInfoMaskToOffsetTable(VOID)
{
    ULONG InfoMask;
    UCHAR InfoOffset;

    for (InfoMask = 0; InfoMask < RTL_NUMBER_OF(ObpInfoMaskToOffset); InfoMask++)
    {
        InfoOffset = 0;

        if (InfoMask & OBP_CREATOR_INFO_MASK)  InfoOffset += sizeof(OBJECT_HEADER_CREATOR_INFO);
        if (InfoMask & OBP_NAME_INFO_MASK)     InfoOffset += sizeof(OBJECT_HEADER_NAME_INFO);
        if (InfoMask & OBP_HANDLE_INFO_MASK)   InfoOffset += sizeof(OBJECT_HEADER_HANDLE_INFO);
        if (InfoMask & OBP_QUOTA_INFO_MASK)    InfoOffset += sizeof(OBJECT_HEADER_QUOTA_INFO);
        if (InfoMask & OBP_PROCESS_INFO_MASK)  InfoOffset += sizeof(OBJECT_HEADER_PROCESS_INFO);
        if (InfoMask & OBP_AUDIT_INFO_MASK)    InfoOffset += sizeof(OBJECT_HEADER_AUDIT_INFO);
        if (InfoMask & OBP_EXTENDED_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_EXTENDED_INFO);
        if (InfoMask & OBP_PADDING_INFO_MASK)  InfoOffset += sizeof(OBJECT_HEADER_PADDING_INFO);

        ObpInfoMaskToOffset[InfoMask] = InfoOffset;
    }
}

static
CODE_SEG("INIT")
VOID
NTAPI
ObpCreateBuiltinObjectTypes(VOID)
{
    OBJECT_TYPE_INITIALIZER ObjectTypeInitializer;
    UNICODE_STRING Name;

    RtlZeroMemory(&ObjectTypeInitializer, sizeof(ObjectTypeInitializer));
    ObjectTypeInitializer.Length = sizeof(ObjectTypeInitializer);

    /* Type: the type of every other type, and the only one that lists its own */
    RtlInitUnicodeString(&Name, L"Type");
    ObjectTypeInitializer.ValidAccessMask = OBJECT_TYPE_ALL_ACCESS;
    ObjectTypeInitializer.UseDefaultObject = TRUE;
    ObjectTypeInitializer.MaintainTypeList = TRUE;
    ObjectTypeInitializer.PoolType = NonPagedPoolNx;
    ObjectTypeInitializer.GenericMapping = ObpTypeMapping;
    ObjectTypeInitializer.DefaultNonPagedPoolCharge = sizeof(OBJECT_TYPE);
    ObjectTypeInitializer.InvalidAttributes = OBJ_OPENLINK;
    ObjectTypeInitializer.DeleteProcedure = NULL;
    ObCreateObjectType(&Name, &ObjectTypeInitializer, NULL, &ObpTypeObjectType);

    /* Directory */
    RtlInitUnicodeString(&Name, L"Directory");
    ObjectTypeInitializer.PoolType = PagedPool;
    ObjectTypeInitializer.ValidAccessMask = DIRECTORY_ALL_ACCESS;
    ObjectTypeInitializer.CaseInsensitive = TRUE;
    ObjectTypeInitializer.SecurityRequired = TRUE;
    ObjectTypeInitializer.MaintainTypeList = FALSE;
    ObjectTypeInitializer.GenericMapping = ObpDirectoryMapping;
    ObjectTypeInitializer.CloseProcedure = ObpCloseDirectoryObject;
    ObjectTypeInitializer.DeleteProcedure = ObpDeleteDirectoryObject;
    ObjectTypeInitializer.DefaultNonPagedPoolCharge = 344;
    ObCreateObjectType(&Name, &ObjectTypeInitializer, NULL, &ObpDirectoryObjectType);

    ObpInitializePrivateNamespaces();
    ObpDirectoryObjectType->TypeInfo.ValidAccessMask &= ~SYNCHRONIZE;

    /* SymbolicLink */
    RtlInitUnicodeString(&Name, L"SymbolicLink");
    ObjectTypeInitializer.SecurityRequired = FALSE;
    ObjectTypeInitializer.UseExtendedParameters = TRUE;
    ObjectTypeInitializer.DefaultNonPagedPoolCharge = 40;
    ObjectTypeInitializer.GenericMapping = ObpSymbolicLinkMapping;
    ObjectTypeInitializer.ValidAccessMask = 0x000FFFFF;
    ObjectTypeInitializer.CloseProcedure = NULL;
    ObjectTypeInitializer.ParseProcedureEx = ObpParseSymbolicLinkEx;
    ObjectTypeInitializer.DeleteProcedure = ObpDeleteSymbolicLink;
    ObCreateObjectType(&Name, &ObjectTypeInitializer, NULL, &ObpSymbolicLinkObjectType);

    ObpSymbolicLinkObjectType->TypeInfo.ValidAccessMask &= ~SYNCHRONIZE;
}

static
CODE_SEG("INIT")
NTSTATUS
NTAPI
ObpCreateNamespaceDirectory(
    _In_ PCWSTR Path,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Outptr_opt_ POBJECT_DIRECTORY *DirectoryObject)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    NTSTATUS Status;
    HANDLE Handle;

    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                               NULL,
                               SecurityDescriptor);

    Status = NtCreateDirectoryObject(&Handle, DIRECTORY_ALL_ACCESS, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return Status;

    if (DirectoryObject != NULL)
    {
        Status = ObReferenceObjectByHandle(Handle,
                                           0,
                                           ObpDirectoryObjectType,
                                           KernelMode,
                                           (PVOID *)DirectoryObject,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            NtClose(Handle);
            return Status;
        }
    }

    return NtClose(Handle);
}

/*
 * Every type created before \ObjectTypes existed still has to appear in it.
 */
static
CODE_SEG("INIT")
VOID
NTAPI
ObpPublishObjectTypes(VOID)
{
    POBJECT_HEADER_CREATOR_INFO CreatorInfo;
    POBJECT_HEADER_NAME_INFO NameInfo;
    OBP_LOOKUP_CONTEXT Context;
    PLIST_ENTRY ListHead, NextEntry;
    POBJECT_HEADER Header;

    ObpInitializeLookupContext(&Context);
    ObpAcquireLookupContextLock(&Context, ObpTypeDirectoryObject);

    ListHead = &ObpTypeObjectType->TypeList;
    for (NextEntry = ListHead->Flink; NextEntry != ListHead; NextEntry = NextEntry->Flink)
    {
        CreatorInfo = CONTAINING_RECORD(NextEntry, OBJECT_HEADER_CREATOR_INFO, TypeList);
        Header = (POBJECT_HEADER)(CreatorInfo + 1);
        NameInfo = OBJECT_HEADER_TO_NAME_INFO(Header);

        if (NameInfo == NULL || NameInfo->Directory != NULL) continue;

        if (!ObpLookupEntryDirectory(ObpTypeDirectoryObject,
                                     &NameInfo->Name,
                                     OBJ_CASE_INSENSITIVE,
                                     FALSE,
                                     &Context))
        {
            ObpInsertEntryDirectory(ObpTypeDirectoryObject, &Context, Header);
        }
    }

    ObpReleaseLookupContext(&Context);
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
ObInitSystem(VOID)
{
    PSECURITY_DESCRIPTOR KernelObjectsSD = NULL;
    PKPRCB Prcb = KeGetCurrentPrcb();
    LARGE_INTEGER Counter;
    NTSTATUS Status;

    if (ObpInitializationPhase == 0)
    {
        /* The header metadata has to exist before the first object does */
        RtlZeroMemory(ObTypeIndexTable, sizeof(ObTypeIndexTable));
        ObpBuildInfoMaskToOffsetTable();

        Counter = KeQueryPerformanceCounter(NULL);
        ObHeaderCookie = (UCHAR)(Counter.LowPart ^ Counter.HighPart ^ (ULONG_PTR)Prcb);

        ExInitializeSystemLookasideList(&ObpCreateInfoLookasideList,
                                        NonPagedPool,
                                        sizeof(OBJECT_CREATE_INFORMATION),
                                        'ICbO',
                                        32,
                                        &ExSystemLookasideListHead);

        ExInitializeSystemLookasideList(&ObpNameBufferLookasideList,
                                        PagedPool,
                                        248,
                                        'MNbO',
                                        16,
                                        &ExSystemLookasideListHead);

        /* Until the other processors are up, everyone shares these */
        Prcb->PPLookasideList[LookasideCreateInfoList].L = &ObpCreateInfoLookasideList;
        Prcb->PPLookasideList[LookasideCreateInfoList].P = &ObpCreateInfoLookasideList;
        Prcb->PPLookasideList[LookasideNameBufferList].L = &ObpNameBufferLookasideList;
        Prcb->PPLookasideList[LookasideNameBufferList].P = &ObpNameBufferLookasideList;

        ObpInitSdCache();

        KeInitializeEvent(&ObpDefaultObject, NotificationEvent, TRUE);
        KeInitializeGuardedMutex(&ObpDeviceMapLock);

        /* The system process and thread answer for themselves */
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
        PsGetCurrentProcess()->ImagePathHash = PROCESS_ALL_ACCESS;
        PsGetCurrentThread()->SpareUlong0 = THREAD_ALL_ACCESS;
#else
        PsGetCurrentProcess()->GrantedAccess = PROCESS_ALL_ACCESS;
        PsGetCurrentThread()->GrantedAccess = THREAD_ALL_ACCESS;
#endif

        ExInitializeWorkItem(&ObpReaperWorkItem, ObpReapObject, NULL);
        PsInitializeQuotaSystem();

        PsGetCurrentProcess()->ObjectTable = ExCreateHandleTable(NULL, TRUE);
        ObpKernelHandleTable = PsGetCurrentProcess()->ObjectTable;

        ObpCreateBuiltinObjectTypes();

        ObpInitializationPhase++;
        return TRUE;
    }

    ObInit2();

    Status = ObpCreateNamespaceDirectory(L"\\",
                                         SePublicDefaultUnrestrictedSd,
                                         &ObpRootDirectoryObject);
    if (!NT_SUCCESS(Status)) return FALSE;

    Status = ObpCreateKernelObjectsSD(&KernelObjectsSD);
    if (!NT_SUCCESS(Status)) return FALSE;

    Status = ObpCreateNamespaceDirectory(L"\\KernelObjects", KernelObjectsSD, NULL);
    ExFreePoolWithTag(KernelObjectsSD, TAG_SD);
    if (!NT_SUCCESS(Status)) return FALSE;

    Status = ObpCreateNamespaceDirectory(L"\\ObjectTypes", NULL, &ObpTypeDirectoryObject);
    if (!NT_SUCCESS(Status)) return FALSE;

    ObpPublishObjectTypes();

    return NT_SUCCESS(ObpCreateDosDevicesDirectory());
}

/* EOF */
