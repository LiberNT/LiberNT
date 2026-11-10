/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object lifetime and object types
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/unaligned.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

#define OBP_BASE_POOL_TYPE_MASK 1

extern ULONG NtGlobalFlag;

POBJECT_TYPE ObpTypeObjectType = NULL;
KEVENT ObpDefaultObject;
KGUARDED_MUTEX ObpDeviceMapLock;

GENERAL_LOOKASIDE ObpNameBufferLookasideList, ObpCreateInfoLookasideList;

WORK_QUEUE_ITEM ObpReaperWorkItem;
volatile PVOID ObpReaperList;

ULONG ObpObjectsCreated, ObpObjectsWithName, ObpObjectsWithPoolQuota;
ULONG ObpObjectsWithHandleDB, ObpObjectsWithCreatorInfo;
POBJECT_TYPE ObTypeIndexTable[256];
UCHAR ObHeaderCookie;
UCHAR ObpInfoMaskToOffset[256];

/* The reaper takes the whole list at once and leaves this behind, so a pusher
 * can tell a draining reaper from an empty list. */
#define OBP_REAPER_DRAINING ((PVOID)1)

/* NAME BUFFERS ***************************************************************/

/*
 * The buffer a captured name lives in is freed by its length alone, so a pool
 * allocation must never come out exactly the size the lookaside hands out.
 */
PVOID
NTAPI
ObpAllocateObjectNameBuffer(
    _In_ ULONG Length,
    _In_ BOOLEAN UseLookaside,
    _Inout_ PUNICODE_STRING ObjectName)
{
    ULONG MaximumLength = Length + sizeof(UNICODE_NULL);
    PVOID Buffer;

    if (UseLookaside && MaximumLength <= OBP_NAME_LOOKASIDE_MAX_SIZE)
    {
        MaximumLength = OBP_NAME_LOOKASIDE_MAX_SIZE;
        Buffer = ObpAllocateObjectCreateInfoBuffer(LookasideNameBufferList);
    }
    else
    {
        if (MaximumLength <= OBP_NAME_LOOKASIDE_MAX_SIZE)
            MaximumLength = OBP_NAME_LOOKASIDE_MAX_SIZE + sizeof(UNICODE_NULL);

        Buffer = ExAllocatePoolWithTag(PagedPool, MaximumLength, OB_NAME_TAG);
    }

    ObjectName->Buffer = Buffer;
    ObjectName->Length = (USHORT)Length;
    ObjectName->MaximumLength = (USHORT)MaximumLength;

    return Buffer;
}

VOID
NTAPI
ObpFreeObjectNameBuffer(
    _In_ PUNICODE_STRING Name)
{
    /* Only the lookaside hands out exactly its own size, and a parse routine
     * may have replaced the buffer with one of any pool tag */
    if (Name->MaximumLength == OBP_NAME_LOOKASIDE_MAX_SIZE)
        ObpFreeCapturedAttributes(Name->Buffer, LookasideNameBufferList);
    else
        ExFreePool(Name->Buffer);
}

NTSTATUS
NTAPI
ObpCaptureObjectName(
    _Inout_ PUNICODE_STRING CapturedName,
    _In_ PUNICODE_STRING ObjectName,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN AllocateFromLookaside)
{
    UNICODE_STRING Supplied;
    NTSTATUS Status = STATUS_SUCCESS;
    PWCHAR Buffer;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(CapturedName, NULL, 0);

    if (AccessMode != KernelMode)
    {
        _SEH2_TRY
        {
            Supplied = ProbeForReadUnicodeString(ObjectName);
            if (Supplied.Length != 0)
            {
                ProbeForRead(Supplied.Buffer, Supplied.Length, sizeof(WCHAR));
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        Supplied = *ObjectName;
    }

    /* A name made of whole characters is the only shape there is */
    if (Supplied.Length == 0) return STATUS_SUCCESS;
    if ((Supplied.Length % sizeof(WCHAR)) != 0 || Supplied.Buffer == NULL)
        return STATUS_OBJECT_NAME_INVALID;

    Buffer = ObpAllocateObjectNameBuffer(Supplied.Length,
                                         AllocateFromLookaside,
                                         CapturedName);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    _SEH2_TRY
    {
        RtlCopyMemory(Buffer, Supplied.Buffer, Supplied.Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        ObpFreeObjectNameBuffer(CapturedName);
        RtlInitEmptyUnicodeString(CapturedName, NULL, 0);
        return Status;
    }

    Buffer[Supplied.Length / sizeof(WCHAR)] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

/* CREATE INFORMATION *********************************************************/

VOID
NTAPI
ObFreeObjectCreateInfoBuffer(
    _In_ POBJECT_CREATE_INFORMATION ObjectCreateInfo)
{
    ObpFreeObjectCreateInformation(ObjectCreateInfo);
}

NTSTATUS
NTAPI
ObpCaptureObjectCreateInformation(
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ KPROCESSOR_MODE CreatorMode,
    _In_ BOOLEAN AllocateFromLookaside,
    _Inout_ POBJECT_CREATE_INFORMATION ObjectCreateInfo,
    _Out_ PUNICODE_STRING ObjectName)
{
    OBJECT_ATTRIBUTES Supplied;
    PUNICODE_STRING SuppliedName;
    NTSTATUS Status;

    PAGED_CODE();

    RtlZeroMemory(ObjectCreateInfo, sizeof(*ObjectCreateInfo));
    RtlInitEmptyUnicodeString(ObjectName, NULL, 0);
    ObjectCreateInfo->ProbeMode = CreatorMode;

    /* No attributes at all leaves an unnamed object with no security */
    if (ObjectAttributes == NULL) return STATUS_SUCCESS;

    if (AccessMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(ObjectAttributes,
                         sizeof(OBJECT_ATTRIBUTES),
                         sizeof(ULONG));
            Supplied = *ObjectAttributes;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        Supplied = *ObjectAttributes;
    }

    if (Supplied.Length != sizeof(OBJECT_ATTRIBUTES))
        return STATUS_INVALID_PARAMETER;

    ObjectCreateInfo->RootDirectory = Supplied.RootDirectory;
    ObjectCreateInfo->Attributes = ObpValidateAttributes(Supplied.Attributes,
                                                         AccessMode);

    if (Supplied.SecurityQualityOfService != NULL)
    {
        /* The service is copied into the create information itself, so the
         * caller's own copy need not outlive this call */
        if (AccessMode != KernelMode)
        {
            _SEH2_TRY
            {
                ProbeForRead(Supplied.SecurityQualityOfService,
                             sizeof(SECURITY_QUALITY_OF_SERVICE),
                             sizeof(ULONG));
                ObjectCreateInfo->SecurityQualityOfService =
                    *(PSECURITY_QUALITY_OF_SERVICE)
                    Supplied.SecurityQualityOfService;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            ObjectCreateInfo->SecurityQualityOfService =
                *(PSECURITY_QUALITY_OF_SERVICE)
                Supplied.SecurityQualityOfService;
        }

        if (ObjectCreateInfo->SecurityQualityOfService.Length !=
            sizeof(SECURITY_QUALITY_OF_SERVICE))
        {
            return STATUS_INVALID_PARAMETER;
        }

        ObjectCreateInfo->SecurityQos =
            &ObjectCreateInfo->SecurityQualityOfService;
    }

    if (Supplied.SecurityDescriptor != NULL)
    {
        Status = SeCaptureSecurityDescriptor(Supplied.SecurityDescriptor,
                                             AccessMode,
                                             PagedPool,
                                             TRUE,
                                             &ObjectCreateInfo->SecurityDescriptor);
        if (!NT_SUCCESS(Status))
        {
            ObjectCreateInfo->SecurityDescriptor = NULL;
            return Status;
        }

        ObjectCreateInfo->SecurityDescriptorCharge = 2048;
    }

    /* Most objects are created with no name at all, which is not an error */
    SuppliedName = Supplied.ObjectName;
    if (SuppliedName == NULL) return STATUS_SUCCESS;

    Status = ObpCaptureObjectName(ObjectName,
                                  SuppliedName,
                                  AccessMode,
                                  AllocateFromLookaside);

    /* A name that is there but empty names nothing, unless a root directory
     * is what the caller meant */
    if (NT_SUCCESS(Status) && ObjectName->Length == 0 &&
        Supplied.RootDirectory == NULL)
    {
        Status = STATUS_OBJECT_NAME_INVALID;
    }

    if (!NT_SUCCESS(Status)) ObpReleaseObjectCreateInformation(ObjectCreateInfo);

    return Status;
}

/* ALLOCATION *****************************************************************/

/*
 * The optional headers an object needs are decided here and recorded in the
 * information mask, which is how every accessor finds them again.
 */
static
UCHAR
ObpInformationMaskFor(
    _In_opt_ POBJECT_CREATE_INFORMATION ObjectCreateInfo,
    _In_ PUNICODE_STRING ObjectName,
    _In_opt_ POBJECT_TYPE ObjectType)
{
    UCHAR Mask = 0;

    if (ObjectName->Buffer != NULL) Mask |= OBP_NAME_INFO_MASK;

    if (ObjectType == NULL ||
        ObjectType->TypeInfo.MaintainTypeList ||
        (NtGlobalFlag & FLG_MAINTAIN_OBJECT_TYPELIST))
    {
        Mask |= OBP_CREATOR_INFO_MASK;
    }

    if (ObjectType != NULL && ObjectType->TypeInfo.MaintainHandleCount)
        Mask |= OBP_HANDLE_INFO_MASK;

    if (ObjectCreateInfo != NULL &&
        (ObjectCreateInfo->PagedPoolCharge != 0 ||
         ObjectCreateInfo->NonPagedPoolCharge != 0 ||
         ObjectCreateInfo->SecurityDescriptorCharge > 2048))
    {
        Mask |= OBP_QUOTA_INFO_MASK;
    }

    if (ObjectCreateInfo != NULL &&
        (ObjectCreateInfo->Attributes & OBJ_EXCLUSIVE))
    {
        Mask |= OBP_PROCESS_INFO_MASK;
    }

    return Mask;
}

NTSTATUS
NTAPI
ObpAllocateObject(
    _In_opt_ POBJECT_CREATE_INFORMATION ObjectCreateInfo,
    _In_ PUNICODE_STRING ObjectName,
    _In_opt_ POBJECT_TYPE ObjectType,
    _In_ ULONG ObjectSize,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Outptr_ POBJECT_HEADER *ObjectHeader)
{
    POBJECT_HEADER_CREATOR_INFO CreatorInfo;
    POBJECT_HEADER_HANDLE_INFO HandleInfo;
    POBJECT_HEADER_QUOTA_INFO QuotaInfo;
    POBJECT_HEADER_NAME_INFO NameInfo;
    POBJECT_HEADER Header;
    POOL_TYPE PoolType;
    ULONG HeaderSize;
    UCHAR InfoMask;
    PVOID Base;

    InfoMask = ObpInformationMaskFor(ObjectCreateInfo, ObjectName, ObjectType);
    HeaderSize = ObpInfoMaskToOffset[InfoMask];

    PoolType = (ObjectType != NULL) ?
               (POOL_TYPE)ObjectType->TypeInfo.PoolType : NonPagedPool;

    Base = ExAllocatePoolWithTag(PoolType,
                                 HeaderSize + sizeof(OBJECT_HEADER) +
                                 ObjectSize,
                                 (ObjectType != NULL) ?
                                 ObjectType->Key : TAG_OBJECT_TYPE);
    if (Base == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    /* The optional headers sit in front of the header, youngest last */
    RtlZeroMemory(Base, HeaderSize + sizeof(OBJECT_HEADER));
    Header = (POBJECT_HEADER)((PUCHAR)Base + HeaderSize);
    Header->InfoMask = InfoMask;

    if (InfoMask & OBP_CREATOR_INFO_MASK)
    {
        CreatorInfo = OBJECT_HEADER_TO_CREATOR_INFO(Header);
        CreatorInfo->CreatorUniqueProcess = PsGetCurrentProcessId();
        InitializeListHead(&CreatorInfo->TypeList);
        ObpObjectsWithCreatorInfo++;
    }

    if (InfoMask & OBP_NAME_INFO_MASK)
    {
        NameInfo = OBJECT_HEADER_TO_NAME_INFO(Header);
        NameInfo->Name = *ObjectName;
        NameInfo->Directory = NULL;
        NameInfo->QueryReferences = 1;
        ObpObjectsWithName++;
    }

    if (InfoMask & OBP_HANDLE_INFO_MASK)
    {
        HandleInfo = OBJECT_HEADER_TO_HANDLE_INFO(Header);
        HandleInfo->SingleEntry.HandleCount = 0;
        Header->Flags |= OB_FLAG_SINGLE_PROCESS;
        ObpObjectsWithHandleDB++;
    }

    if (InfoMask & OBP_QUOTA_INFO_MASK)
    {
        QuotaInfo = OBJECT_HEADER_TO_QUOTA_INFO(Header);
        QuotaInfo->PagedPoolCharge = ObjectCreateInfo->PagedPoolCharge;
        QuotaInfo->NonPagedPoolCharge = ObjectCreateInfo->NonPagedPoolCharge;
        QuotaInfo->SecurityDescriptorCharge =
            ObjectCreateInfo->SecurityDescriptorCharge;
        ObpObjectsWithPoolQuota++;
    }

    Header->PointerCount = 1;
    Header->HandleCount = 0;
    Header->Flags |= OB_FLAG_CREATE_INFO;
    Header->ObjectCreateInfo = ObjectCreateInfo;
    Header->SecurityDescriptor = NULL;
    ExInitializePushLock(&Header->Lock);

    if (PreviousMode == KernelMode) Header->Flags |= OB_FLAG_KERNEL_MODE;

    if (ObjectCreateInfo != NULL)
    {
        if (ObjectCreateInfo->Attributes & OBJ_PERMANENT)
            Header->Flags |= OB_FLAG_PERMANENT;

        if (ObjectCreateInfo->Attributes & OBJ_EXCLUSIVE)
            Header->Flags |= OB_FLAG_EXCLUSIVE;
    }

    if (ObjectType != NULL)
    {
        ObpEncodeObjectTypeIndex(Header, ObjectType);

        InterlockedIncrement((PLONG)&ObjectType->TotalNumberOfObjects);
        if (ObjectType->TotalNumberOfObjects >
            ObjectType->HighWaterNumberOfObjects)
        {
            ObjectType->HighWaterNumberOfObjects =
                ObjectType->TotalNumberOfObjects;
        }
    }

    ObpObjectsCreated++;
    *ObjectHeader = Header;
    return STATUS_SUCCESS;
}

/* DELETION *******************************************************************/

static
VOID
ObpDeallocateObject(
    _In_ PVOID Object)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE ObjectType = ObpGetObjectTypeFromHeader(Header);
    POBJECT_HEADER_CREATOR_INFO CreatorInfo;
    POBJECT_HEADER_NAME_INFO NameInfo;
    PVOID Descriptor;

    CreatorInfo = OBJECT_HEADER_TO_CREATOR_INFO(Header);
    NameInfo = OBJECT_HEADER_TO_NAME_INFO(Header);

    if (NameInfo != NULL && NameInfo->Name.Buffer != NULL)
    {
        ExFreePoolWithTag(NameInfo->Name.Buffer, OB_NAME_TAG);
        RtlInitEmptyUnicodeString(&NameInfo->Name, NULL, 0);
    }

    if (CreatorInfo != NULL && !IsListEmpty(&CreatorInfo->TypeList))
    {
        ObpEnterObjectTypeMutex(ObjectType);
        RemoveEntryList(&CreatorInfo->TypeList);
        ObpLeaveObjectTypeMutex(ObjectType);
    }

    /* The descriptor is shared, so only the reference goes back */
    Descriptor = ExGetObjectFastReference(
        *(PEX_FAST_REF)&Header->SecurityDescriptor);
    if (Descriptor != NULL)
        ObDeassignSecurity((PSECURITY_DESCRIPTOR *)&Header->SecurityDescriptor);

    if (Header->Flags & OB_FLAG_CREATE_INFO)
    {
        if (Header->ObjectCreateInfo != NULL)
            ObpFreeObjectCreateInformation(Header->ObjectCreateInfo);
    }
    else
    {
        Header->QuotaBlockCharged = NULL;
    }

    InterlockedDecrement((PLONG)&ObjectType->TotalNumberOfObjects);

    ExFreePool((PUCHAR)Header - ObpInfoMaskToOffset[Header->InfoMask]);
}

VOID
NTAPI
ObpDeleteObject(
    _In_ PVOID Object,
    _In_ BOOLEAN CalledFromWorkerThread)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE ObjectType = ObpGetObjectTypeFromHeader(Header);
    KIRQL CalloutIrql;

    UNREFERENCED_PARAMETER(CalledFromWorkerThread);

    if (ObjectType->TypeInfo.DeleteProcedure != NULL)
    {
        ObpCalloutStart(&CalloutIrql);
        ObjectType->TypeInfo.DeleteProcedure(Object);
        ObpCalloutEnd(CalloutIrql, "Delete", ObjectType, Object);
    }

    ObpDeallocateObject(Object);
}

/*
 * Drains everything that reached its last reference above APC level. The list
 * is taken whole, and the sentinel left in its place tells a pusher that the
 * reaper is still running and needs no waking.
 */
VOID
NTAPI
ObpReapObject(
    _In_ PVOID Unused)
{
    POBJECT_HEADER Header;
    PVOID Current;

    UNREFERENCED_PARAMETER(Unused);

    do
    {
        Current = InterlockedExchangePointer(&ObpReaperList,
                                             OBP_REAPER_DRAINING);

        while (Current != NULL && Current != OBP_REAPER_DRAINING)
        {
            Header = Current;
            Current = Header->NextToFree;

            Header->HandleCount = 0;
            ObpDeleteObject(&Header->Body, TRUE);
        }
    }
    while (ObpReaperList != OBP_REAPER_DRAINING ||
           InterlockedCompareExchangePointer(&ObpReaperList,
                                             NULL,
                                             OBP_REAPER_DRAINING) !=
           OBP_REAPER_DRAINING);
}

VOID
FASTCALL
ObpSetPermanentObject(
    _In_ PVOID ObjectBody,
    _In_ BOOLEAN Permanent)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(ObjectBody);

    ObpAcquireObjectLock(Header);

    if (Permanent)
        Header->Flags |= OB_FLAG_PERMANENT;
    else
        Header->Flags &= ~OB_FLAG_PERMANENT;

    ObpReleaseObjectLock(Header);

    /* Losing the flag may be the last thing keeping the name alive */
    if (!Permanent) ObpDeleteNameCheck(ObjectBody);
}

VOID
NTAPI
ObMakeTemporaryObject(
    _In_ PVOID ObjectBody)
{
    PAGED_CODE();
    ObpSetPermanentObject(ObjectBody, FALSE);
}

VOID
NTAPI
ObpDeleteObjectType(
    _In_ PVOID Object)
{
    POBJECT_TYPE ObjectType = Object;

    /* A type is permanent for the life of the system, so this is only ever
     * reached by a failed creation */
    if (ObjectType->Index != 0 &&
        ObTypeIndexTable[ObjectType->Index] == ObjectType)
    {
        ObTypeIndexTable[ObjectType->Index] = NULL;
    }
}

VOID
NTAPI
ObDeleteCapturedInsertInfo(
    _In_ PVOID Object)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);

    PAGED_CODE();

    if (Header->Flags & OB_FLAG_CREATE_INFO)
    {
        if (Header->ObjectCreateInfo != NULL)
        {
            ObpFreeObjectCreateInformation(Header->ObjectCreateInfo);
            Header->ObjectCreateInfo = NULL;
        }

        Header->Flags &= ~OB_FLAG_CREATE_INFO;
    }
}

/* PUBLIC OBJECT CREATION *****************************************************/

NTSTATUS
NTAPI
ObCreateObject(
    _In_ KPROCESSOR_MODE ProbeMode,
    _In_ POBJECT_TYPE Type,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _Inout_opt_ PVOID ParseContext,
    _In_ ULONG ObjectSize,
    _In_opt_ ULONG PagedPoolCharge,
    _In_opt_ ULONG NonPagedPoolCharge,
    _Outptr_ PVOID *Object)
{
    POBJECT_CREATE_INFORMATION ObjectCreateInfo;
    POBJECT_HEADER Header;
    UNICODE_STRING ObjectName;
    NTSTATUS Status;

    PAGED_CODE();

    ObjectCreateInfo = ObpAllocateObjectCreateInfoBuffer(
        LookasideCreateInfoList);
    if (ObjectCreateInfo == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = ObpCaptureObjectCreateInformation(ObjectAttributes,
                                               ProbeMode,
                                               AccessMode,
                                               TRUE,
                                               ObjectCreateInfo,
                                               &ObjectName);
    if (!NT_SUCCESS(Status))
    {
        ObpFreeObjectCreateInformation(ObjectCreateInfo);
        return Status;
    }

    /* A caller's own charges override whatever the type asks for */
    ObjectCreateInfo->PagedPoolCharge = (PagedPoolCharge != 0) ?
        PagedPoolCharge : Type->TypeInfo.DefaultPagedPoolCharge;
    ObjectCreateInfo->NonPagedPoolCharge = (NonPagedPoolCharge != 0) ?
        NonPagedPoolCharge : Type->TypeInfo.DefaultNonPagedPoolCharge;
    ObjectCreateInfo->ParseContext = ParseContext;

    Status = ObpAllocateObject(ObjectCreateInfo,
                               &ObjectName,
                               Type,
                               ObjectSize,
                               AccessMode,
                               &Header);
    if (!NT_SUCCESS(Status))
    {
        if (ObjectName.Buffer != NULL) ObpFreeObjectNameBuffer(&ObjectName);
        ObpFreeObjectCreateInformation(ObjectCreateInfo);
        return Status;
    }

    *Object = &Header->Body;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObCreateObjectType(
    _In_ PUNICODE_STRING TypeName,
    _In_ POBJECT_TYPE_INITIALIZER ObjectTypeInitializer,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Outptr_ POBJECT_TYPE *ObjectType)
{
    return ObCreateObjectTypeEx(TypeName,
                                ObjectTypeInitializer,
                                SecurityDescriptor,
                                0,
                                ObjectType);
}

NTSTATUS
NTAPI
NtMakeTemporaryObject(
    _In_ HANDLE ObjectHandle)
{
    NTSTATUS Status;
    PVOID Object;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ObjectHandle,
                                       DELETE,
                                       NULL,
                                       ExGetPreviousMode(),
                                       &Object,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    ObpSetPermanentObject(Object, FALSE);
    ObDereferenceObject(Object);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtMakePermanentObject(
    _In_ HANDLE ObjectHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NTSTATUS Status;
    PVOID Object;

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeCreatePermanentPrivilege, PreviousMode))
        return STATUS_PRIVILEGE_NOT_HELD;

    Status = ObReferenceObjectByHandle(ObjectHandle,
                                       0,
                                       NULL,
                                       PreviousMode,
                                       &Object,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    ObpSetPermanentObject(Object, TRUE);
    ObDereferenceObject(Object);

    return STATUS_SUCCESS;
}

/* OBJECT TYPES ***************************************************************/


static
NTSTATUS
ObpCreateDefaultObjectTypeSecurityDescriptor(
    _Out_ PSECURITY_DESCRIPTOR *SecurityDescriptor)
{
    ULONG WorldSidBuffer[SECURITY_MAX_SID_SIZE / sizeof(ULONG)];
    ULONG AdminSidBuffer[SECURITY_MAX_SID_SIZE / sizeof(ULONG)];
    ULONG SystemSidBuffer[SECURITY_MAX_SID_SIZE / sizeof(ULONG)];
    SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};
    SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    PSID WorldSid = (PSID)WorldSidBuffer;
    PSID AdminSid = (PSID)AdminSidBuffer;
    PSID SystemSid = (PSID)SystemSidBuffer;
    PISECURITY_DESCRIPTOR_RELATIVE RelativeDescriptor;
    ULONG AclSize;
    ULONG DescriptorSize;
    PACL Dacl;
    NTSTATUS Status;

    Status = RtlInitializeSid(WorldSid, &WorldAuthority, 1);
    if (!NT_SUCCESS(Status)) return Status;
    *RtlSubAuthoritySid(WorldSid, 0) = SECURITY_WORLD_RID;
    Status = RtlInitializeSid(AdminSid, &NtAuthority, 2);
    if (!NT_SUCCESS(Status)) return Status;
    *RtlSubAuthoritySid(AdminSid, 0) = SECURITY_BUILTIN_DOMAIN_RID;
    *RtlSubAuthoritySid(AdminSid, 1) = DOMAIN_ALIAS_RID_ADMINS;
    Status = RtlInitializeSid(SystemSid, &NtAuthority, 1);
    if (!NT_SUCCESS(Status)) return Status;
    *RtlSubAuthoritySid(SystemSid, 0) = SECURITY_LOCAL_SYSTEM_RID;

    AclSize = sizeof(ACL) +
              sizeof(ACCESS_ALLOWED_ACE) + RtlLengthSid(WorldSid) +
              sizeof(ACCESS_ALLOWED_ACE) + RtlLengthSid(AdminSid) +
              sizeof(ACCESS_ALLOWED_ACE) + RtlLengthSid(SystemSid);
    DescriptorSize = sizeof(*RelativeDescriptor) + AclSize;
    RelativeDescriptor = ExAllocatePoolWithTag(PagedPool, DescriptorSize, TAG_DACL);
    if (!RelativeDescriptor) return STATUS_INSUFFICIENT_RESOURCES;

    Status = RtlCreateSecurityDescriptorRelative(RelativeDescriptor, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(Status)) goto Failure;

    Dacl = (PACL)(RelativeDescriptor + 1);
    Status = RtlCreateAcl(Dacl, AclSize, ACL_REVISION);
    if (!NT_SUCCESS(Status)) goto Failure;

    Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, OBJECT_TYPE_ALL_ACCESS, WorldSid);
    if (!NT_SUCCESS(Status)) goto Failure;
    Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, OBJECT_TYPE_ALL_ACCESS, AdminSid);
    if (!NT_SUCCESS(Status)) goto Failure;
    Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, OBJECT_TYPE_ALL_ACCESS, SystemSid);
    if (!NT_SUCCESS(Status)) goto Failure;

    RelativeDescriptor->Control |= SE_DACL_PRESENT;
    RelativeDescriptor->Dacl = sizeof(*RelativeDescriptor);
    *SecurityDescriptor = (PSECURITY_DESCRIPTOR)RelativeDescriptor;
    return STATUS_SUCCESS;

Failure:
    ExFreePoolWithTag(RelativeDescriptor, TAG_DACL);
    return Status;
}


static
NTSTATUS
ObpInitializeObjectTypeSecurity(
    _In_ PVOID Object,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    PSECURITY_DESCRIPTOR LocalDescriptor = SecurityDescriptor;
    PSECURITY_DESCRIPTOR CachedDescriptor;
    BOOLEAN FreeDescriptor = FALSE;
    NTSTATUS Status;

    if (!LocalDescriptor)
    {
        Status = ObpCreateDefaultObjectTypeSecurityDescriptor(&LocalDescriptor);
        if (!NT_SUCCESS(Status)) return Status;
        FreeDescriptor = TRUE;
    }

    Status = ObLogSecurityDescriptor(LocalDescriptor, &CachedDescriptor, MAX_FAST_REFS + 1);
    if (NT_SUCCESS(Status)) ExInitializeFastReference((PEX_FAST_REF)&Header->SecurityDescriptor, CachedDescriptor);
    if (FreeDescriptor) ExFreePoolWithTag(LocalDescriptor, TAG_DACL);
    return Status;
}


NTSTATUS
NTAPI
ObCreateObjectTypeEx(IN PUNICODE_STRING TypeName,
                     IN POBJECT_TYPE_INITIALIZER ObjectTypeInitializer,
                     IN PSECURITY_DESCRIPTOR SecurityDescriptor OPTIONAL,
                     IN LONG_PTR WaitObjectInfo OPTIONAL,
                     OUT POBJECT_TYPE *ObjectType)
{
    POBJECT_HEADER Header;
    POBJECT_TYPE LocalObjectType;
    ULONG HeaderSize;
    NTSTATUS Status;
    OBP_LOOKUP_CONTEXT Context;
    PWCHAR p;
    ULONG i;
    UNICODE_STRING ObjectName;
    ANSI_STRING AnsiName;
    POBJECT_HEADER_CREATOR_INFO CreatorInfo;

    /* Verify parameters */
    if (!(TypeName) ||
        !(TypeName->Length) ||
        (TypeName->Length % sizeof(WCHAR)) ||
        !(ObjectTypeInitializer) ||
        (ObjectTypeInitializer->Length < sizeof(*ObjectTypeInitializer)) ||
        (ObjectTypeInitializer->ObjectTypeFlags & 0xFC00) ||
        (ObjectTypeInitializer->InvalidAttributes & ~OBJ_VALID_KERNEL_ATTRIBUTES) ||
        (ObjectTypeInitializer->MaintainHandleCount &&
         (!(ObjectTypeInitializer->OpenProcedure) &&
          !ObjectTypeInitializer->CloseProcedure)) ||
        ((!ObjectTypeInitializer->UseDefaultObject) &&
         ((ObjectTypeInitializer->PoolType != NonPagedPool) &&
          (ObjectTypeInitializer->PoolType != NonPagedPoolNx)) &&
         !(WaitObjectInfo & 1)))
    {
        /* Fail */
        return STATUS_INVALID_PARAMETER;
    }

    /* Make sure the name doesn't have a separator */
    p = TypeName->Buffer;
    i = TypeName->Length / sizeof(WCHAR);
    while (i--)
    {
        /* Check for one and fail */
        if (*p++ == OBJ_NAME_PATH_SEPARATOR) return STATUS_OBJECT_NAME_INVALID;
    }

    /* Setup a lookup context */
    ObpInitializeLookupContext(&Context);

    /* Check if we've already created the directory of types */
    if (ObpTypeDirectoryObject)
    {
        /* Lock the lookup context */
        ObpAcquireLookupContextLock(&Context, ObpTypeDirectoryObject);

        /* Do the lookup */
        if (ObpLookupEntryDirectory(ObpTypeDirectoryObject,
                                    TypeName,
                                    OBJ_CASE_INSENSITIVE,
                                    FALSE,
                                    &Context))
        {
            /* We have already created it, so fail */
            ObpReleaseLookupContext(&Context);
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }

    /* Now make a copy of the object name */
    ObjectName.Buffer = ExAllocatePoolWithTag(PagedPool,
                                              TypeName->MaximumLength,
                                              OB_NAME_TAG);
    if (!ObjectName.Buffer)
    {
        /* Out of memory, fail */
        ObpReleaseLookupContext(&Context);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Set the length and copy the name */
    ObjectName.MaximumLength = TypeName->MaximumLength;
    RtlCopyUnicodeString(&ObjectName, TypeName);

    /* Allocate the Object */
    Status = ObpAllocateObject(NULL,
                               &ObjectName,
                               ObpTypeObjectType,
                               sizeof(OBJECT_TYPE),
                               KernelMode,
                               &Header);
    if (!NT_SUCCESS(Status))
    {
        /* Free the name and fail */
        ObpReleaseLookupContext(&Context);
        ExFreePool(ObjectName.Buffer);
        return Status;
    }

    /* Setup the flags and name */
    LocalObjectType = (POBJECT_TYPE)&Header->Body;
    LocalObjectType->Name = ObjectName;
    Header->Flags |= OB_FLAG_KERNEL_MODE | OB_FLAG_PERMANENT;

    /* Clear accounting data */
    LocalObjectType->TotalNumberOfObjects =
    LocalObjectType->TotalNumberOfHandles =
    LocalObjectType->HighWaterNumberOfObjects =
    LocalObjectType->HighWaterNumberOfHandles = 0;

    /* Check if this is the first Object Type */
    if (!ObpTypeObjectType)
    {
        /* It is, so set this as the type object */
        ObpTypeObjectType = LocalObjectType;

        /* Set the hard-coded key and object count */
        LocalObjectType->TotalNumberOfObjects = 1;
        LocalObjectType->Key = TAG_OBJECT_TYPE;
    }
    else
    {
        /* Convert the tag to ASCII */
        Status = RtlUnicodeStringToAnsiString(&AnsiName, TypeName, TRUE);
        if (NT_SUCCESS(Status))
        {
            /* For every missing character, use a space */
            for (i = 3; i >= AnsiName.Length; i--) AnsiName.Buffer[i] = ' ';

            /* Set the key and free the converted name */
            LocalObjectType->Key = *(PULONG)AnsiName.Buffer;
            RtlFreeAnsiString(&AnsiName);
        }
        else
        {
            /* Just copy the characters */
            LocalObjectType->Key = *(PULONG)TypeName->Buffer;
        }
    }

    /* Set up the type information */
    LocalObjectType->TypeInfo = *ObjectTypeInitializer;
    LocalObjectType->TypeInfo.PoolType = ObjectTypeInitializer->PoolType;

    /* Check if we have to maintain a type list */
    if (NtGlobalFlag & FLG_MAINTAIN_OBJECT_TYPELIST)
    {
        /* Enable support */
        LocalObjectType->TypeInfo.MaintainTypeList = TRUE;
    }

    /* Calculate how much space our header'll take up */
    HeaderSize = sizeof(OBJECT_HEADER) +
                 sizeof(OBJECT_HEADER_NAME_INFO) +
                 (ObjectTypeInitializer->MaintainHandleCount ?
                  sizeof(OBJECT_HEADER_HANDLE_INFO) : 0);

    /* Check the pool type */
    if ((ObjectTypeInitializer->PoolType & OBP_BASE_POOL_TYPE_MASK) == NonPagedPool)
    {
        /* Update the NonPaged Pool charge */
        LocalObjectType->TypeInfo.DefaultNonPagedPoolCharge += HeaderSize;
    }
    else
    {
        /* Update the Paged Pool charge */
        LocalObjectType->TypeInfo.DefaultPagedPoolCharge += HeaderSize;
    }

    /* All objects types need a security procedure */
    if (!ObjectTypeInitializer->SecurityProcedure)
    {
        LocalObjectType->TypeInfo.SecurityProcedure = SeDefaultObjectMethod;
    }

    /* Select the Wait Object */
    if (LocalObjectType->TypeInfo.UseDefaultObject)
    {
        /* Add the SYNCHRONIZE access mask since it's waitable */
        LocalObjectType->TypeInfo.ValidAccessMask |= SYNCHRONIZE;

        /* Use the "Default Object", a simple event */
        LocalObjectType->DefaultObject = &ObpDefaultObject;
    }
    else
    {
        /* Use the caller-supplied wait object encoding, if present */
        LocalObjectType->DefaultObject = (PVOID)WaitObjectInfo;
    }

    /* Initialize Object Type components */
    ExInitializePushLock(&LocalObjectType->TypeLock);
    InitializeListHead(&LocalObjectType->TypeList);
    InitializeListHead(&LocalObjectType->CallbackList);
    LocalObjectType->SeMandatoryLabelMask = 0;
    LocalObjectType->SeTrustConstraintMask = 0;
    if (ObjectTypeInitializer->Length >= sizeof(*ObjectTypeInitializer) + (2 * sizeof(ULONG)))
    {
        LocalObjectType->SeMandatoryLabelMask = ReadUnalignedU32((const unsigned long*)((PUCHAR)ObjectTypeInitializer + sizeof(*ObjectTypeInitializer)));
        LocalObjectType->SeTrustConstraintMask = ReadUnalignedU32((const unsigned long*)((PUCHAR)ObjectTypeInitializer + sizeof(*ObjectTypeInitializer) + sizeof(ULONG)));
    }

    /* Initialize the type object's security descriptor when policy enables it */
    if (ObpObjectSecurityMode)
    {
        Status = ObpInitializeObjectTypeSecurity(LocalObjectType, SecurityDescriptor);
        if (!NT_SUCCESS(Status))
        {
            ObpReleaseLookupContext(&Context);
            ObDereferenceObject(LocalObjectType);
            return Status;
        }
    }

    /* Lock the object type */
    ObpEnterObjectTypeMutex(ObpTypeObjectType);

    /* Get creator info and insert it into the type list */
    CreatorInfo = OBJECT_HEADER_TO_CREATOR_INFO(Header);
    if (CreatorInfo)
    {
        InsertTailList(&ObpTypeObjectType->TypeList,
                       &CreatorInfo->TypeList);

        /* CORE-8423: Avoid inserting this a second time if someone creates a
         * handle to the object type (bug in Windows 2003) */
        Header->Flags &= ~OB_FLAG_CREATE_INFO;
    }

    /* Set the index and the entry into the object type array */
    ASSERT(ObpTypeObjectType->TotalNumberOfObjects < RTL_NUMBER_OF(ObTypeIndexTable));
    LocalObjectType->Index = (UCHAR)ObpTypeObjectType->TotalNumberOfObjects;

    ASSERT(LocalObjectType->Index != 0);

    /* Publish this type at its native one-based index */
    ObTypeIndexTable[LocalObjectType->Index] = LocalObjectType;

    /* The first type header could only be encoded after index publication */
    if (LocalObjectType == ObpTypeObjectType) ObpEncodeObjectTypeIndex(Header, ObpTypeObjectType);

    /* Release the object type */
    ObpLeaveObjectTypeMutex(ObpTypeObjectType);

    /* Check if we're actually creating the directory object itself */
    if (!(ObpTypeDirectoryObject) ||
        (ObpInsertEntryDirectory(ObpTypeDirectoryObject, &Context, Header)))
    {
        /* Check if the type directory exists */
        if (ObpTypeDirectoryObject)
        {
            /* Reference it */
            ObReferenceObject(ObpTypeDirectoryObject);
        }

        /* Cleanup the lookup context */
        ObpReleaseLookupContext(&Context);

        /* Return the object type and success */
        *ObjectType = LocalObjectType;
        return STATUS_SUCCESS;
    }

    /* If we got here, then we failed */
    ObpReleaseLookupContext(&Context);
    return STATUS_INSUFFICIENT_RESOURCES;
}

/* QUERYING *******************************************************************/

POBJECT_TYPE
NTAPI
ObGetObjectType(
    _In_ PVOID Object)
{
    return ObpGetObjectTypeFromHeader(OBJECT_TO_OBJECT_HEADER(Object));
}

POBJECT_HEADER_NAME_INFO
NTAPI
ObQueryNameInfo(
    _In_ PVOID Object)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);

    if (!(Header->InfoMask & OBP_NAME_INFO_MASK)) return NULL;

    return (POBJECT_HEADER_NAME_INFO)((PCHAR)Header -
        ObpInfoMaskToOffset[Header->InfoMask &
                            (OBP_CREATOR_INFO_MASK | OBP_NAME_INFO_MASK)]);
}

/* What ObQueryNameString would need for this object, nothing if it is unnamed. */
static
ULONG
ObpQueryNameInfoSize(
    _In_opt_ POBJECT_HEADER_NAME_INFO NameInfo)
{
    POBJECT_DIRECTORY Directory;
    ULONG Bytes = 0;

    if (NameInfo == NULL || NameInfo->Name.Length == 0) return 0;

    while (NameInfo != NULL && NameInfo->Directory != NULL)
    {
        Bytes += sizeof(OBJ_NAME_PATH_SEPARATOR) + NameInfo->Name.Length;

        Directory = NameInfo->Directory;
        if (Directory == ObpRootDirectoryObject) break;

        NameInfo = OBJECT_HEADER_TO_NAME_INFO(
            OBJECT_TO_OBJECT_HEADER(Directory));
    }

    if (Bytes == 0) return 0;

    return Bytes + sizeof(OBJECT_NAME_INFORMATION) + sizeof(UNICODE_NULL);
}

NTSTATUS
NTAPI
ObQueryTypeInfo(
    _In_ POBJECT_TYPE ObjectType,
    _Out_ POBJECT_TYPE_INFORMATION ObjectTypeInfo,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    ULONG Needed;

    Needed = sizeof(OBJECT_TYPE_INFORMATION) +
             ObjectType->Name.Length + sizeof(UNICODE_NULL);

    *ReturnLength = Needed;
    if (Length < Needed) return STATUS_INFO_LENGTH_MISMATCH;

    RtlZeroMemory(ObjectTypeInfo, sizeof(*ObjectTypeInfo));

    /* The name follows the fixed part, so one buffer carries both */
    ObjectTypeInfo->TypeName.Buffer = (PWCHAR)(ObjectTypeInfo + 1);
    ObjectTypeInfo->TypeName.Length = ObjectType->Name.Length;
    ObjectTypeInfo->TypeName.MaximumLength = ObjectType->Name.Length +
                                             sizeof(UNICODE_NULL);
    RtlCopyMemory(ObjectTypeInfo->TypeName.Buffer,
                  ObjectType->Name.Buffer,
                  ObjectType->Name.Length);
    ObjectTypeInfo->TypeName.Buffer[ObjectType->Name.Length /
                                    sizeof(WCHAR)] = UNICODE_NULL;

    ObjectTypeInfo->TotalNumberOfObjects = ObjectType->TotalNumberOfObjects;
    ObjectTypeInfo->TotalNumberOfHandles = ObjectType->TotalNumberOfHandles;
    ObjectTypeInfo->HighWaterNumberOfObjects =
        ObjectType->HighWaterNumberOfObjects;
    ObjectTypeInfo->HighWaterNumberOfHandles =
        ObjectType->HighWaterNumberOfHandles;
    ObjectTypeInfo->InvalidAttributes = ObjectType->TypeInfo.InvalidAttributes;
    ObjectTypeInfo->GenericMapping = ObjectType->TypeInfo.GenericMapping;
    ObjectTypeInfo->ValidAccessMask = ObjectType->TypeInfo.ValidAccessMask;
    ObjectTypeInfo->SecurityRequired = ObjectType->TypeInfo.SecurityRequired;
    ObjectTypeInfo->MaintainHandleCount =
        ObjectType->TypeInfo.MaintainHandleCount;
    ObjectTypeInfo->PoolType = ObjectType->TypeInfo.PoolType;
    ObjectTypeInfo->DefaultPagedPoolCharge =
        ObjectType->TypeInfo.DefaultPagedPoolCharge;
    ObjectTypeInfo->DefaultNonPagedPoolCharge =
        ObjectType->TypeInfo.DefaultNonPagedPoolCharge;

    return STATUS_SUCCESS;
}

static
NTSTATUS
ObpQueryAllTypes(
    _Out_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    POBJECT_ALL_TYPES_INFORMATION AllTypes = Buffer;
    POBJECT_TYPE_INFORMATION Entry;
    POBJECT_TYPE ObjectType;
    ULONG Index, Count = 0;
    ULONG Used, Needed;
    NTSTATUS Status;

    /* Every type is published at its own index, and index zero is unused */
    Needed = ALIGN_UP(sizeof(OBJECT_ALL_TYPES_INFORMATION), ULONG_PTR);
    for (Index = 1; Index < RTL_NUMBER_OF(ObTypeIndexTable); Index++)
    {
        ObjectType = ObTypeIndexTable[Index];
        if (ObjectType == NULL) continue;

        Count++;
        Needed += ALIGN_UP(sizeof(OBJECT_TYPE_INFORMATION) +
                           ObjectType->Name.Length + sizeof(UNICODE_NULL),
                           ULONG_PTR);
    }

    *ReturnLength = Needed;
    if (Length < Needed) return STATUS_INFO_LENGTH_MISMATCH;

    AllTypes->NumberOfTypes = Count;
    Used = ALIGN_UP(sizeof(OBJECT_ALL_TYPES_INFORMATION), ULONG_PTR);

    for (Index = 1; Index < RTL_NUMBER_OF(ObTypeIndexTable); Index++)
    {
        ObjectType = ObTypeIndexTable[Index];
        if (ObjectType == NULL) continue;

        Entry = (POBJECT_TYPE_INFORMATION)((PUCHAR)Buffer + Used);
        Status = ObQueryTypeInfo(ObjectType, Entry, Length - Used, &Needed);
        if (!NT_SUCCESS(Status)) return Status;

        Used += ALIGN_UP(Needed, ULONG_PTR);
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
ObpQueryBasicInfo(
    _In_ PVOID Object,
    _In_ ACCESS_MASK GrantedAccess,
    _In_ ULONG HandleAttributes,
    _Out_ POBJECT_BASIC_INFORMATION BasicInfo)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE ObjectType = ObpGetObjectTypeFromHeader(Header);
    POBJECT_HEADER_QUOTA_INFO QuotaInfo;
    PSECURITY_DESCRIPTOR Descriptor;

    RtlZeroMemory(BasicInfo, sizeof(*BasicInfo));

    BasicInfo->Attributes = HandleAttributes;
    if (Header->Flags & OB_FLAG_PERMANENT) BasicInfo->Attributes |= OBJ_PERMANENT;
    if (Header->Flags & OB_FLAG_EXCLUSIVE) BasicInfo->Attributes |= OBJ_EXCLUSIVE;

    BasicInfo->GrantedAccess = GrantedAccess;
    BasicInfo->HandleCount = (ULONG)Header->HandleCount;

    /* The reference the query itself holds is none of the caller's */
    BasicInfo->PointerCount = (ULONG)Header->PointerCount - 1;

    QuotaInfo = OBJECT_HEADER_TO_QUOTA_INFO(Header);
    if (QuotaInfo != NULL)
    {
        BasicInfo->PagedPoolCharge = QuotaInfo->PagedPoolCharge;
        BasicInfo->NonPagedPoolCharge = QuotaInfo->NonPagedPoolCharge;
    }
    else
    {
        BasicInfo->PagedPoolCharge = ObjectType->TypeInfo.DefaultPagedPoolCharge;
        BasicInfo->NonPagedPoolCharge =
            ObjectType->TypeInfo.DefaultNonPagedPoolCharge;
    }

    BasicInfo->NameInfoSize = ObpQueryNameInfoSize(ObQueryNameInfo(Object));
    BasicInfo->TypeInfoSize = sizeof(OBJECT_TYPE_INFORMATION) +
                              ObjectType->Name.Length + sizeof(UNICODE_NULL);

    Descriptor = ObpReferenceSecurityDescriptor(Header);
    if (Descriptor != NULL)
    {
        BasicInfo->SecurityDescriptorSize =
            RtlLengthSecurityDescriptor(Descriptor);
        ObDereferenceSecurityDescriptor(Descriptor, 1);
    }

    /* Only a type object records when it came into being */
    if (ObjectType == ObpTypeObjectType)
        BasicInfo->CreationTime.QuadPart = 0;

    return STATUS_SUCCESS;
}

/*
 * Protection from close lives in a reserved bit of the granted access, not in
 * the handle attributes, so the entry itself has to be read.
 */
static
NTSTATUS
ObpQueryHandleFlags(
    _In_ HANDLE Handle,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ POBJECT_HANDLE_ATTRIBUTE_INFORMATION Flags)
{
    PHANDLE_TABLE_ENTRY Entry;
    PHANDLE_TABLE Table;

    if (ObpIsKernelHandle(Handle, AccessMode))
    {
        Table = ObpKernelHandleTable;
        Handle = ObKernelHandleToHandle(Handle);
    }
    else
    {
        Table = PsGetCurrentProcess()->ObjectTable;
    }

    if (Table == NULL) return STATUS_INVALID_HANDLE;

    KeEnterCriticalRegion();

    Entry = ExMapHandleToPointer(Table, Handle);
    if (Entry == NULL)
    {
        KeLeaveCriticalRegion();
        return STATUS_INVALID_HANDLE;
    }

    Flags->Inherit = ((Entry->ObAttributes & OBJ_INHERIT) != 0);
    Flags->ProtectFromClose =
        ((Entry->GrantedAccess & ObpAccessProtectCloseBit) != 0);

    ExUnlockHandleTableEntry(Table, Entry);
    KeLeaveCriticalRegion();

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtQueryObject(
    _In_opt_ HANDLE ObjectHandle,
    _In_ OBJECT_INFORMATION_CLASS ObjectInformationClass,
    _Out_opt_ PVOID ObjectInformation,
    _In_ ULONG Length,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    OBJECT_HANDLE_INFORMATION HandleInfo;
    OBJECT_BASIC_INFORMATION BasicInfo;
    OBJECT_HANDLE_ATTRIBUTE_INFORMATION Flags;
    POBJECT_HEADER Header;
    NTSTATUS Status;
    ULONG Needed = 0;
    PVOID Object;

    PAGED_CODE();

    if (ObjectInformationClass >= MaxObjectInfoClass)
        return STATUS_INVALID_INFO_CLASS;

    if (PreviousMode != KernelMode)
    {
        /* The handle flags are two booleans, so demanding the alignment the
         * larger classes need would reject a correctly placed buffer */
        ULONG Alignment = (ObjectInformationClass == ObjectHandleFlagInformation) ?
                          sizeof(BOOLEAN) : sizeof(ULONG);

        _SEH2_TRY
        {
            if (Length != 0)
                ProbeForWrite(ObjectInformation, Length, Alignment);

            if (ReturnLength != NULL) ProbeForWriteUlong(ReturnLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /* The whole type list belongs to no object in particular */
    if (ObjectInformationClass == ObjectTypesInformation)
    {
        _SEH2_TRY
        {
            Status = ObpQueryAllTypes(ObjectInformation, Length, &Needed);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (ReturnLength != NULL)
        {
            _SEH2_TRY
            {
                *ReturnLength = Needed;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }

        return Status;
    }

    Status = ObReferenceObjectByHandle(ObjectHandle,
                                       0,
                                       NULL,
                                       PreviousMode,
                                       &Object,
                                       &HandleInfo);
    if (!NT_SUCCESS(Status)) return Status;

    Header = OBJECT_TO_OBJECT_HEADER(Object);

    _SEH2_TRY
    {
        switch (ObjectInformationClass)
        {
            case ObjectBasicInformation:

                Needed = sizeof(OBJECT_BASIC_INFORMATION);
                if (Length < Needed)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                Status = ObpQueryBasicInfo(Object,
                                           HandleInfo.GrantedAccess,
                                           HandleInfo.HandleAttributes,
                                           &BasicInfo);
                if (NT_SUCCESS(Status))
                    RtlCopyMemory(ObjectInformation, &BasicInfo, Needed);

                break;

            case ObjectNameInformation:

                Status = ObQueryNameString(Object,
                                           ObjectInformation,
                                           Length,
                                           &Needed);
                break;

            case ObjectTypeInformation:

                Status = ObQueryTypeInfo(ObpGetObjectTypeFromHeader(Header),
                                         ObjectInformation,
                                         Length,
                                         &Needed);
                break;

            case ObjectHandleFlagInformation:

                Needed = sizeof(OBJECT_HANDLE_ATTRIBUTE_INFORMATION);
                if (Length < Needed)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                Status = ObpQueryHandleFlags(ObjectHandle, PreviousMode, &Flags);
                if (NT_SUCCESS(Status))
                    RtlCopyMemory(ObjectInformation, &Flags, Needed);

                break;

            default:

                /* TODO: ObjectSessionInformation cannot be read back */
                Status = STATUS_INVALID_INFO_CLASS;
                break;
        }

        if (ReturnLength != NULL) *ReturnLength = Needed;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ObDereferenceObject(Object);
    return Status;
}

NTSTATUS
NTAPI
NtSetInformationObject(
    _In_ HANDLE ObjectHandle,
    _In_ OBJECT_INFORMATION_CLASS ObjectInformationClass,
    _In_ PVOID ObjectInformation,
    _In_ ULONG Length)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    OBJECT_HANDLE_ATTRIBUTE_INFORMATION HandleFlags;
    POBJECT_DIRECTORY Directory;
    OBP_LOOKUP_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();

    /* A directory is claimed for the caller's session, which carries no data
     * of its own and is reserved to the trusted computing base */
    if (ObjectInformationClass == ObjectSessionInformation)
    {
        if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
            return STATUS_PRIVILEGE_NOT_HELD;

        Status = ObReferenceObjectByHandle(ObjectHandle,
                                           0,
                                           ObpDirectoryObjectType,
                                           PreviousMode,
                                           (PVOID *)&Directory,
                                           NULL);
        if (!NT_SUCCESS(Status)) return Status;

        ObpInitializeLookupContext(&Context);
        ObpAcquireDirectoryLockExclusive(Directory, &Context);
        Directory->SessionId = PsGetCurrentProcessSessionId();
        ObpReleaseDirectoryLock(Directory, &Context);

        ObDereferenceObject(Directory);
        return STATUS_SUCCESS;
    }

    if (ObjectInformationClass != ObjectHandleFlagInformation)
        return STATUS_INVALID_INFO_CLASS;

    if (Length != sizeof(OBJECT_HANDLE_ATTRIBUTE_INFORMATION))
        return STATUS_INFO_LENGTH_MISMATCH;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(ObjectInformation, Length, sizeof(BOOLEAN));
            HandleFlags =
                *(POBJECT_HANDLE_ATTRIBUTE_INFORMATION)ObjectInformation;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        HandleFlags = *(POBJECT_HANDLE_ATTRIBUTE_INFORMATION)ObjectInformation;
    }

    Status = ObSetHandleAttributes(ObjectHandle, &HandleFlags, PreviousMode);

    return Status;
}

/* EOF */
