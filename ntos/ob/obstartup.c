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

PDEVICE_MAP ObSystemDeviceMap = NULL;
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
 * \KernelObjects holds the system-wide events every process has to find, so it
 * is readable by everyone and writable only by the system and administrators.
 */
CODE_SEG("INIT")
static
NTSTATUS
ObpCreateKernelObjectsSD(
    _Outptr_ PSECURITY_DESCRIPTOR *SecurityDescriptor)
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
        Dacl = (PACL)((PUCHAR)Sd + sizeof(SECURITY_DESCRIPTOR));

        Status = RtlCreateAcl(Dacl, AclSize, ACL_REVISION);
        if (NT_SUCCESS(Status))
            Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, GENERIC_READ,
                                            SeWorldSid);
        if (NT_SUCCESS(Status))
            Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, GENERIC_ALL,
                                            SeAliasAdminsSid);
        if (NT_SUCCESS(Status))
            Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, GENERIC_ALL,
                                            SeLocalSystemSid);
        if (NT_SUCCESS(Status))
            Status = RtlSetDaclSecurityDescriptor(Sd, TRUE, Dacl, FALSE);
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Sd, TAG_SD);
        return Status;
    }

    *SecurityDescriptor = Sd;
    return STATUS_SUCCESS;
}

/*
 * Every directory the object manager owns outlives the handle it was made
 * with, so each one is created permanent and its handle closed at once.
 */
CODE_SEG("INIT")
static
NTSTATUS
ObpCreatePermanentDirectory(
    _In_ PCWSTR Path,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Outptr_opt_ POBJECT_DIRECTORY *Directory)
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

    Status = NtCreateDirectoryObject(&Handle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return Status;

    if (Directory != NULL)
    {
        Status = ObReferenceObjectByHandle(Handle,
                                           0,
                                           ObpDirectoryObjectType,
                                           KernelMode,
                                           (PVOID *)Directory,
                                           NULL);
    }

    NtClose(Handle);
    return Status;
}

/* The information mask names which optional headers precede the object, so the
 * offset each mask adds up to is worked out once. */
CODE_SEG("INIT")
static
VOID
ObpInitializeHeaderMetadata(VOID)
{
    LARGE_INTEGER Counter;
    UCHAR InfoOffset;
    ULONG InfoMask;

    RtlZeroMemory(ObTypeIndexTable, sizeof(ObTypeIndexTable));

    for (InfoMask = 0; InfoMask < RTL_NUMBER_OF(ObpInfoMaskToOffset); InfoMask++)
    {
        InfoOffset = 0;
        if (InfoMask & OBP_CREATOR_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_CREATOR_INFO);
        if (InfoMask & OBP_NAME_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_NAME_INFO);
        if (InfoMask & OBP_HANDLE_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_HANDLE_INFO);
        if (InfoMask & OBP_QUOTA_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_QUOTA_INFO);
        if (InfoMask & OBP_PROCESS_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_PROCESS_INFO);
        if (InfoMask & OBP_AUDIT_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_AUDIT_INFO);
        if (InfoMask & OBP_EXTENDED_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_EXTENDED_INFO);
        if (InfoMask & OBP_PADDING_INFO_MASK) InfoOffset += sizeof(OBJECT_HEADER_PADDING_INFO);
        ObpInfoMaskToOffset[InfoMask] = InfoOffset;
    }

    Counter = KeQueryPerformanceCounter(NULL);
    ObHeaderCookie = (UCHAR)(Counter.LowPart ^ Counter.HighPart ^
                             (ULONG_PTR)KeGetCurrentPrcb());
}

/* Nothing outside the bootstrap reads these, and the rights each one maps are
 * the documented ones for its type. */
static const GENERIC_MAPPING ObpTypeMapping =
{
    STANDARD_RIGHTS_READ, STANDARD_RIGHTS_WRITE, STANDARD_RIGHTS_EXECUTE, 0x000F0001
};

static const GENERIC_MAPPING ObpDirectoryMapping =
{
    STANDARD_RIGHTS_READ | DIRECTORY_QUERY | DIRECTORY_TRAVERSE,
    STANDARD_RIGHTS_WRITE | DIRECTORY_CREATE_SUBDIRECTORY | DIRECTORY_CREATE_OBJECT,
    STANDARD_RIGHTS_EXECUTE | DIRECTORY_QUERY | DIRECTORY_TRAVERSE,
    DIRECTORY_ALL_ACCESS
};

static const GENERIC_MAPPING ObpSymbolicLinkMapping =
{
    STANDARD_RIGHTS_READ | SYMBOLIC_LINK_QUERY,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE | SYMBOLIC_LINK_QUERY,
    SYMBOLIC_LINK_ALL_ACCESS
};

CODE_SEG("INIT")
static
VOID
ObpCreateBootstrapTypes(VOID)
{
    OBJECT_TYPE_INITIALIZER TypeInitializer;
    UNICODE_STRING Name;

    RtlZeroMemory(&TypeInitializer, sizeof(TypeInitializer));
    TypeInitializer.Length = sizeof(TypeInitializer);

    /* The type type comes first and is its own type once it exists */
    RtlInitUnicodeString(&Name, L"Type");
    TypeInitializer.ValidAccessMask = OBJECT_TYPE_ALL_ACCESS;
    TypeInitializer.UseDefaultObject = TRUE;
    TypeInitializer.MaintainTypeList = TRUE;
    TypeInitializer.PoolType = NonPagedPoolNx;
    TypeInitializer.GenericMapping = ObpTypeMapping;
    TypeInitializer.DefaultNonPagedPoolCharge = sizeof(OBJECT_TYPE);
    TypeInitializer.InvalidAttributes = OBJ_OPENLINK;
    TypeInitializer.DeleteProcedure = NULL;
    ObCreateObjectType(&Name, &TypeInitializer, NULL, &ObpTypeObjectType);

    RtlInitUnicodeString(&Name, L"Directory");
    TypeInitializer.PoolType = PagedPool;
    TypeInitializer.ValidAccessMask = DIRECTORY_ALL_ACCESS;
    TypeInitializer.CaseInsensitive = TRUE;
    TypeInitializer.SecurityRequired = TRUE;
    TypeInitializer.MaintainTypeList = FALSE;
    TypeInitializer.GenericMapping = ObpDirectoryMapping;
    TypeInitializer.CloseProcedure = ObpCloseDirectoryObject;
    TypeInitializer.DeleteProcedure = ObpDeleteDirectoryObject;
    TypeInitializer.DefaultNonPagedPoolCharge = 344;
    ObCreateObjectType(&Name, &TypeInitializer, NULL, &ObpDirectoryObjectType);
    ObpInitializePrivateNamespaces();
    ObpDirectoryObjectType->TypeInfo.ValidAccessMask &= ~SYNCHRONIZE;

    RtlInitUnicodeString(&Name, L"SymbolicLink");
    TypeInitializer.SecurityRequired = FALSE;
    TypeInitializer.UseExtendedParameters = TRUE;
    TypeInitializer.DefaultNonPagedPoolCharge = 40;
    TypeInitializer.GenericMapping = ObpSymbolicLinkMapping;
    TypeInitializer.ValidAccessMask = 0x000FFFFF;
    TypeInitializer.CloseProcedure = NULL;
    TypeInitializer.ParseProcedureEx = ObpParseSymbolicLinkEx;
    TypeInitializer.DeleteProcedure = ObpDeleteSymbolicLink;
    ObCreateObjectType(&Name, &TypeInitializer, NULL, &ObpSymbolicLinkObjectType);
    ObpSymbolicLinkObjectType->TypeInfo.ValidAccessMask &= ~SYNCHRONIZE;
}

/*
 * The bootstrap types were made before \ObjectTypes existed, so they are the
 * only ones ObCreateObjectType could not name as it went.
 */
CODE_SEG("INIT")
static
VOID
ObpPublishUnnamedTypes(VOID)
{
    POBJECT_HEADER_NAME_INFO NameInfo;
    OBP_LOOKUP_CONTEXT Context;
    POBJECT_HEADER Header;
    POBJECT_TYPE Type;
    ULONG Index;

    ObpInitializeLookupContext(&Context);
    ObpAcquireLookupContextLock(&Context, ObpTypeDirectoryObject);

    for (Index = 0; Index < RTL_NUMBER_OF(ObTypeIndexTable); Index++)
    {
        Type = ObTypeIndexTable[Index];
        if (Type == NULL) continue;

        Header = OBJECT_TO_OBJECT_HEADER(Type);
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

/*
 * ObCreateObject and the name capture take their buffers through the per
 * processor lookaside pointers, so each processor has to be pointed at the
 * lists before any object can be created.
 */
CODE_SEG("INIT")
static
VOID
ObpBindLookasideLists(
    _Inout_ PKPRCB Prcb,
    _In_opt_ PGENERAL_LOOKASIDE PrivateCreateInfo,
    _In_opt_ PGENERAL_LOOKASIDE PrivateNameBuffer)
{
    Prcb->PPLookasideList[LookasideCreateInfoList].L = &ObpCreateInfoLookasideList;
    Prcb->PPLookasideList[LookasideCreateInfoList].P =
        (PrivateCreateInfo != NULL) ? PrivateCreateInfo : &ObpCreateInfoLookasideList;

    Prcb->PPLookasideList[LookasideNameBufferList].L = &ObpNameBufferLookasideList;
    Prcb->PPLookasideList[LookasideNameBufferList].P =
        (PrivateNameBuffer != NULL) ? PrivateNameBuffer : &ObpNameBufferLookasideList;
}

/* Gives every processor a list of its own so the shared ones stay cold. */
CODE_SEG("INIT")
static
VOID
ObpBindPerProcessorLookasideLists(VOID)
{
    PGENERAL_LOOKASIDE Private;
    ULONG Index;

    Private = ExAllocatePoolWithTag(NonPagedPool,
                                    2 * KeNumberProcessors *
                                    sizeof(GENERAL_LOOKASIDE),
                                    'LLbO');

    for (Index = 0; Index < (ULONG)KeNumberProcessors; Index++)
    {
        if (Private == NULL)
        {
            ObpBindLookasideLists(KiProcessorBlock[Index], NULL, NULL);
            continue;
        }

        ExInitializeSystemLookasideList(&Private[0],
                                        NonPagedPool,
                                        sizeof(OBJECT_CREATE_INFORMATION),
                                        'ICbO',
                                        32,
                                        &ExSystemLookasideListHead);

        ExInitializeSystemLookasideList(&Private[1],
                                        PagedPool,
                                        OBP_NAME_LOOKASIDE_MAX_SIZE,
                                        'MNbO',
                                        16,
                                        &ExSystemLookasideListHead);

        ObpBindLookasideLists(KiProcessorBlock[Index], &Private[0], &Private[1]);
        Private += 2;
    }
}

CODE_SEG("INIT")
static
BOOLEAN
ObpInitPhase0(VOID)
{
    ObpInitializeHeaderMetadata();

    ExInitializeSystemLookasideList(&ObpCreateInfoLookasideList,
                                    NonPagedPool,
                                    sizeof(OBJECT_CREATE_INFORMATION),
                                    'ICbO',
                                    32,
                                    &ExSystemLookasideListHead);

    ExInitializeSystemLookasideList(&ObpNameBufferLookasideList,
                                    PagedPool,
                                    OBP_NAME_LOOKASIDE_MAX_SIZE,
                                    'MNbO',
                                    16,
                                    &ExSystemLookasideListHead);

    ObpBindLookasideLists(KeGetCurrentPrcb(), NULL, NULL);

    /* A type that names no wait object waits on this one instead */
    KeInitializeEvent(&ObpDefaultObject, NotificationEvent, TRUE);

    KeInitializeGuardedMutex(&ObpDeviceMapLock);
    ExInitializeWorkItem(&ObpReaperWorkItem, ObpReapObject, NULL);

    if (!NT_SUCCESS(ObpInitSdCache())) return FALSE;

    PsInitializeQuotaSystem();

    /* The system process owns the table every kernel handle comes from */
    PsGetCurrentProcess()->ObjectTable = ExCreateHandleTable(NULL, TRUE);
    if (PsGetCurrentProcess()->ObjectTable == NULL) return FALSE;
    ObpKernelHandleTable = PsGetCurrentProcess()->ObjectTable;

    ObpCreateBootstrapTypes();

    ObpInitializationPhase++;
    return TRUE;
}

CODE_SEG("INIT")
static
BOOLEAN
ObpInitPhase1(VOID)
{
    PSECURITY_DESCRIPTOR KernelObjectsSd;
    NTSTATUS Status;

    ObpBindPerProcessorLookasideLists();

    /* The root is the one directory with no parent to inherit from, so it
     * carries the public unrestricted descriptor every child starts from */
    Status = ObpCreatePermanentDirectory(L"\\",
                                         SePublicDefaultUnrestrictedSd,
                                         &ObpRootDirectoryObject);
    if (!NT_SUCCESS(Status)) return FALSE;

    Status = ObpCreateKernelObjectsSD(&KernelObjectsSd);
    if (!NT_SUCCESS(Status)) return FALSE;

    Status = ObpCreatePermanentDirectory(L"\\KernelObjects",
                                         KernelObjectsSd,
                                         NULL);
    ExFreePoolWithTag(KernelObjectsSd, TAG_SD);
    if (!NT_SUCCESS(Status)) return FALSE;

    Status = ObpCreatePermanentDirectory(L"\\ObjectTypes",
                                         NULL,
                                         &ObpTypeDirectoryObject);
    if (!NT_SUCCESS(Status)) return FALSE;

    ObpPublishUnnamedTypes();

    if (!NT_SUCCESS(ObpCreateDosDevicesDirectory())) return FALSE;

    return TRUE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
ObInitSystem(VOID)
{
    if (ObpInitializationPhase == 0) return ObpInitPhase0();

    return ObpInitPhase1();
}

/* EOF */
