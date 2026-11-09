/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object lifetime, creation and information
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

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Every object type is readable by everyone and fully controlled by the system
 * and by administrators, which is what Windows gives \ObjectTypes entries.
 */
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
    PISECURITY_DESCRIPTOR_RELATIVE RelativeDescriptor;
    PSID WorldSid = (PSID)WorldSidBuffer;
    PSID AdminSid = (PSID)AdminSidBuffer;
    PSID SystemSid = (PSID)SystemSidBuffer;
    ULONG AclSize, DescriptorSize;
    NTSTATUS Status;
    PACL Dacl;

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
    if (RelativeDescriptor == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = RtlCreateSecurityDescriptorRelative(RelativeDescriptor,
                                                 SECURITY_DESCRIPTOR_REVISION);
    if (NT_SUCCESS(Status))
    {
        Dacl = (PACL)(RelativeDescriptor + 1);
        Status = RtlCreateAcl(Dacl, AclSize, ACL_REVISION);
    }

    if (NT_SUCCESS(Status))
        Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, OBJECT_TYPE_ALL_ACCESS, WorldSid);
    if (NT_SUCCESS(Status))
        Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, OBJECT_TYPE_ALL_ACCESS, AdminSid);
    if (NT_SUCCESS(Status))
        Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, OBJECT_TYPE_ALL_ACCESS, SystemSid);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(RelativeDescriptor, TAG_DACL);
        return Status;
    }

    RelativeDescriptor->Control |= SE_DACL_PRESENT;
    RelativeDescriptor->Dacl = sizeof(*RelativeDescriptor);

    *SecurityDescriptor = RelativeDescriptor;
    return STATUS_SUCCESS;
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

    if (LocalDescriptor == NULL)
    {
        Status = ObpCreateDefaultObjectTypeSecurityDescriptor(&LocalDescriptor);
        if (!NT_SUCCESS(Status)) return Status;
        FreeDescriptor = TRUE;
    }

    Status = ObLogSecurityDescriptor(LocalDescriptor, &CachedDescriptor, MAX_FAST_REFS + 1);
    if (NT_SUCCESS(Status))
    {
        ExInitializeFastReference((PEX_FAST_REF)&Header->SecurityDescriptor,
                                  CachedDescriptor);
    }

    if (FreeDescriptor) ExFreePoolWithTag(LocalDescriptor, TAG_DACL);
    return Status;
}

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

    return (POBJECT_HEADER_NAME_INFO)
           ((PCHAR)Header - ObpInfoMaskToOffset[Header->InfoMask &
                                                (OBP_CREATOR_INFO_MASK | OBP_NAME_INFO_MASK)]);
}

VOID
FASTCALL
ObpDeallocateObject(
    _In_ PVOID Object)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE ObjectType = ObpGetObjectTypeFromHeader(Header);
    POBJECT_HEADER_CREATOR_INFO CreatorInfo;
    POBJECT_HEADER_PROCESS_INFO ProcessInfo;
    POBJECT_HEADER_HANDLE_INFO HandleInfo;
    POBJECT_HEADER_QUOTA_INFO QuotaInfo;
    POBJECT_HEADER_NAME_INFO NameInfo;
    ULONG PagedPoolCharge, NonPagedPoolCharge;
    PVOID HeaderLocation = Header;

    PAGED_CODE();

    /* The optional parts sit in front of the header, lowest address last */
    CreatorInfo = OBJECT_HEADER_TO_CREATOR_INFO(Header);
    if (CreatorInfo != NULL) HeaderLocation = CreatorInfo;

    NameInfo = OBJECT_HEADER_TO_NAME_INFO(Header);
    if (NameInfo != NULL) HeaderLocation = NameInfo;

    HandleInfo = OBJECT_HEADER_TO_HANDLE_INFO(Header);
    if (HandleInfo != NULL) HeaderLocation = HandleInfo;

    QuotaInfo = OBJECT_HEADER_TO_QUOTA_INFO(Header);
    if (QuotaInfo != NULL) HeaderLocation = QuotaInfo;

    ProcessInfo = OBJECT_HEADER_TO_PROCESS_INFO(Header);
    if (ProcessInfo != NULL) HeaderLocation = ProcessInfo;

    InterlockedDecrement((PLONG)&ObjectType->TotalNumberOfObjects);

    if (Header->Flags & OB_FLAG_CREATE_INFO)
    {
        /* The object never got inserted, so it still owns its create info */
        if (Header->ObjectCreateInfo != NULL)
        {
            ObpFreeObjectCreateInformation(Header->ObjectCreateInfo);
            Header->ObjectCreateInfo = NULL;
        }
    }
    else if (Header->QuotaBlockCharged != NULL)
    {
        if (QuotaInfo != NULL)
        {
            PagedPoolCharge = QuotaInfo->PagedPoolCharge +
                              QuotaInfo->SecurityDescriptorCharge;
            NonPagedPoolCharge = QuotaInfo->NonPagedPoolCharge;
        }
        else
        {
            PagedPoolCharge = ObjectType->TypeInfo.DefaultPagedPoolCharge;
            NonPagedPoolCharge = ObjectType->TypeInfo.DefaultNonPagedPoolCharge;
            if (Header->Flags & OB_FLAG_SECURITY) PagedPoolCharge += 2048;
        }

        if (Header->QuotaBlockCharged != OBP_SYSTEM_PROCESS_QUOTA)
        {
            PsReturnSharedPoolQuota(Header->QuotaBlockCharged,
                                    PagedPoolCharge,
                                    NonPagedPoolCharge);
        }
    }

    if (HandleInfo != NULL && !(Header->Flags & OB_FLAG_SINGLE_PROCESS))
    {
        ExFreePool(HandleInfo->HandleCountDatabase);
        HandleInfo->HandleCountDatabase = NULL;
    }

    if (NameInfo != NULL && NameInfo->Name.Buffer != NULL)
    {
        ExFreePool(NameInfo->Name.Buffer);
        NameInfo->Name.Buffer = NULL;
    }

    Header->TypeIndex = 0;
    ExFreePoolWithTag(HeaderLocation, ObjectType->Key);
}

VOID
NTAPI
ObpDeleteObject(
    _In_ PVOID Object,
    _In_ BOOLEAN CalledFromWorkerThread)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE ObjectType = ObpGetObjectTypeFromHeader(Header);
    POBJECT_HEADER_CREATOR_INFO CreatorInfo;
    POBJECT_HEADER_NAME_INFO NameInfo;
    KIRQL CalloutIrql;

    PAGED_CODE();

    NameInfo = OBJECT_HEADER_TO_NAME_INFO(Header);
    CreatorInfo = OBJECT_HEADER_TO_CREATOR_INFO(Header);

    if (CreatorInfo != NULL && !IsListEmpty(&CreatorInfo->TypeList))
    {
        ObpEnterObjectTypeMutex(ObjectType);
        RemoveEntryList(&CreatorInfo->TypeList);
        ObpLeaveObjectTypeMutex(ObjectType);
    }

    if (NameInfo != NULL && NameInfo->Name.Buffer != NULL)
    {
        ExFreePool(NameInfo->Name.Buffer);
        RtlInitEmptyUnicodeString(&NameInfo->Name, NULL, 0);
    }

    if (Header->SecurityDescriptor != NULL)
    {
        ObpCalloutStart(&CalloutIrql);
        ObjectType->TypeInfo.SecurityProcedure(Object,
                                               DeleteSecurityDescriptor,
                                               0,
                                               NULL,
                                               NULL,
                                               &Header->SecurityDescriptor,
                                               0,
                                               NULL,
                                               KernelMode);
        ObpCalloutEnd(CalloutIrql, "Security", ObjectType, Object);
    }

    if (ObjectType->TypeInfo.DeleteProcedure != NULL)
    {
        /* Tell the procedure it may not block when it runs inline */
        if (!CalledFromWorkerThread) Header->Flags |= OB_FLAG_DEFER_DELETE;

        ObpCalloutStart(&CalloutIrql);
        ObjectType->TypeInfo.DeleteProcedure(Object);
        ObpCalloutEnd(CalloutIrql, "Delete", ObjectType, Object);
    }

    ObpDeallocateObject(Object);
}

VOID
NTAPI
ObpReapObject(
    _In_ PVOID Parameter)
{
    POBJECT_HEADER ReapObject, NextObject;

    UNREFERENCED_PARAMETER(Parameter);

    /* Take the whole list at once and leave a marker behind, so a pusher can
     * tell an empty list from one being drained */
    do
    {
        ReapObject = InterlockedExchangePointer(&ObpReaperList, (PVOID)1);

        while (ReapObject != NULL && ReapObject != (PVOID)1)
        {
            NextObject = ReapObject->NextToFree;
            ObpDeleteObject(&ReapObject->Body, TRUE);
            ReapObject = NextObject;
        }
    } while (ObpReaperList != (PVOID)1 ||
             InterlockedCompareExchange((PLONG)&ObpReaperList, 0, 1) != 1);
}

VOID
FASTCALL
ObpSetPermanentObject(
    _In_ PVOID ObjectBody,
    _In_ BOOLEAN Permanent)
{
    POBJECT_HEADER ObjectHeader = OBJECT_TO_OBJECT_HEADER(ObjectBody);

    ObpAcquireObjectLock(ObjectHeader);

    if (Permanent)
    {
        ObjectHeader->Flags |= OB_FLAG_PERMANENT;
        ObpReleaseObjectLock(ObjectHeader);
        return;
    }

    ObjectHeader->Flags &= ~OB_FLAG_PERMANENT;
    ObpReleaseObjectLock(ObjectHeader);

    /* The name may now be due for removal */
    ObpDeleteNameCheck(ObjectBody);
}

PWCHAR
NTAPI
ObpAllocateObjectNameBuffer(
    _In_ ULONG Length,
    _In_ BOOLEAN UseLookaside,
    _Inout_ PUNICODE_STRING ObjectName)
{
    ULONG MaximumLength = Length + sizeof(UNICODE_NULL);
    PVOID Buffer;

    if (!UseLookaside || MaximumLength > OBP_NAME_LOOKASIDE_MAX_SIZE)
    {
        /* Ask for at least the lookaside size, so the buffer can be told
         * apart from a lookaside one when it is freed */
        if (MaximumLength <= OBP_NAME_LOOKASIDE_MAX_SIZE)
            MaximumLength = OBP_NAME_LOOKASIDE_MAX_SIZE + sizeof(UNICODE_NULL);

        Buffer = ExAllocatePoolWithTag(PagedPool, MaximumLength, OB_NAME_TAG);
    }
    else
    {
        MaximumLength = OBP_NAME_LOOKASIDE_MAX_SIZE;
        Buffer = ObpAllocateObjectCreateInfoBuffer(LookasideNameBufferList);
    }

    ObjectName->MaximumLength = (USHORT)MaximumLength;
    ObjectName->Length = (USHORT)Length;
    ObjectName->Buffer = Buffer;

    return Buffer;
}

VOID
NTAPI
ObpFreeObjectNameBuffer(
    _In_ PUNICODE_STRING Name)
{
    if (Name->MaximumLength != OBP_NAME_LOOKASIDE_MAX_SIZE)
        ExFreePool(Name->Buffer);
    else
        ObpFreeCapturedAttributes(Name->Buffer, LookasideNameBufferList);
}

NTSTATUS
NTAPI
ObpCaptureObjectName(
    _Inout_ PUNICODE_STRING CapturedName,
    _In_ PUNICODE_STRING ObjectName,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN UseLookaside)
{
    PWCHAR _SEH2_VOLATILE StringBuffer = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    UNICODE_STRING LocalName;
    ULONG StringLength;
    PUCHAR RawName;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(CapturedName, NULL, 0);

    _SEH2_TRY
    {
        if (AccessMode != KernelMode)
        {
            LocalName = ProbeForReadUnicodeString(ObjectName);
            ProbeForRead(LocalName.Buffer, LocalName.Length, sizeof(WCHAR));
        }
        else
        {
            /* A driver may hand in a string that is not naturally aligned */
            RawName = (PUCHAR)ObjectName;
            LocalName.Length = ReadUnalignedU16((const unsigned short *)RawName);
            LocalName.MaximumLength =
                ReadUnalignedU16((const unsigned short *)(RawName + sizeof(USHORT)));
            LocalName.Buffer = (PWCHAR)(ULONG_PTR)ReadUnalignedUlongPtr(
                (const ULONG_PTR *)(RawName + FIELD_OFFSET(UNICODE_STRING, Buffer)));
        }

        StringLength = LocalName.Length;
        if (StringLength != 0)
        {
            if ((StringLength & (sizeof(WCHAR) - 1)) ||
                (StringLength == (MAXUSHORT - sizeof(UNICODE_NULL) + 1)))
            {
                Status = STATUS_OBJECT_NAME_INVALID;
            }
            else
            {
                StringBuffer = ObpAllocateObjectNameBuffer(StringLength,
                                                           UseLookaside,
                                                           CapturedName);
                if (StringBuffer == NULL)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                }
                else
                {
                    RtlCopyMemory(StringBuffer, LocalName.Buffer, StringLength);
                    StringBuffer[StringLength / sizeof(WCHAR)] = UNICODE_NULL;
                }
            }
        }
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
        if (StringBuffer != NULL) ObpFreeObjectNameBuffer(CapturedName);
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
ObpCaptureObjectCreateInformation(
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ KPROCESSOR_MODE CreatorMode,
    _In_ BOOLEAN AllocateFromLookaside,
    _In_ POBJECT_CREATE_INFORMATION ObjectCreateInfo,
    _Out_ PUNICODE_STRING ObjectName)
{
    PSECURITY_QUALITY_OF_SERVICE SecurityQos;
    PSECURITY_DESCRIPTOR SecurityDescriptor;
    PUNICODE_STRING LocalObjectName = NULL;
    ULONG LocalLength, LocalAttributes;
    ULONG SdCharge, QuotaInfoSize;
    NTSTATUS Status = STATUS_SUCCESS;
    PUCHAR RawAttributes, RawQos;

    PAGED_CODE();

    RtlZeroMemory(ObjectCreateInfo, sizeof(OBJECT_CREATE_INFORMATION));

    _SEH2_TRY
    {
        if (ObjectAttributes != NULL)
        {
            if (AccessMode != KernelMode)
            {
                ProbeForRead(ObjectAttributes, sizeof(OBJECT_ATTRIBUTES), sizeof(ULONG));
            }

            /* The structure a driver passes need not be naturally aligned */
            RawAttributes = (PUCHAR)ObjectAttributes;
            LocalLength = ReadUnalignedU32((const unsigned long *)
                (RawAttributes + FIELD_OFFSET(OBJECT_ATTRIBUTES, Length)));
            LocalAttributes = ReadUnalignedU32((const unsigned long *)
                (RawAttributes + FIELD_OFFSET(OBJECT_ATTRIBUTES, Attributes)));

            if (LocalLength != sizeof(OBJECT_ATTRIBUTES) ||
                (LocalAttributes & ~OBJ_VALID_KERNEL_ATTRIBUTES))
            {
                _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
            }

            ObjectCreateInfo->RootDirectory = (HANDLE)(ULONG_PTR)ReadUnalignedUlongPtr(
                (const ULONG_PTR *)(RawAttributes + FIELD_OFFSET(OBJECT_ATTRIBUTES, RootDirectory)));
            ObjectCreateInfo->Attributes = LocalAttributes & OBJ_VALID_KERNEL_ATTRIBUTES;

            /* Only a kernel creator may ask for a kernel handle */
            if (CreatorMode != KernelMode)
                ObjectCreateInfo->Attributes &= ~OBJ_KERNEL_HANDLE;

            LocalObjectName = (PUNICODE_STRING)(ULONG_PTR)ReadUnalignedUlongPtr(
                (const ULONG_PTR *)(RawAttributes + FIELD_OFFSET(OBJECT_ATTRIBUTES, ObjectName)));
            SecurityDescriptor = (PSECURITY_DESCRIPTOR)(ULONG_PTR)ReadUnalignedUlongPtr(
                (const ULONG_PTR *)(RawAttributes + FIELD_OFFSET(OBJECT_ATTRIBUTES, SecurityDescriptor)));
            SecurityQos = (PSECURITY_QUALITY_OF_SERVICE)(ULONG_PTR)ReadUnalignedUlongPtr(
                (const ULONG_PTR *)(RawAttributes + FIELD_OFFSET(OBJECT_ATTRIBUTES, SecurityQualityOfService)));

            if (SecurityDescriptor != NULL)
            {
                Status = SeCaptureSecurityDescriptor(SecurityDescriptor,
                                                     AccessMode,
                                                     NonPagedPool,
                                                     TRUE,
                                                     &ObjectCreateInfo->SecurityDescriptor);
                if (!NT_SUCCESS(Status))
                {
                    ObjectCreateInfo->SecurityDescriptor = NULL;
                    _SEH2_YIELD(return Status);
                }

                /* The descriptor is charged for twice its quota footprint, and
                 * never for less than the fixed minimum */
                SdCharge = 2048;
                SeComputeQuotaInformationSize(ObjectCreateInfo->SecurityDescriptor,
                                              &QuotaInfoSize);
                if ((2 * QuotaInfoSize) > 2048) SdCharge = 2 * QuotaInfoSize;

                ObjectCreateInfo->SecurityDescriptorCharge = SdCharge;
                ObjectCreateInfo->ProbeMode = AccessMode;
            }

            if (SecurityQos != NULL)
            {
                if (AccessMode != KernelMode)
                {
                    ProbeForRead(SecurityQos,
                                 sizeof(SECURITY_QUALITY_OF_SERVICE),
                                 sizeof(ULONG));
                }

                RawQos = (PUCHAR)SecurityQos;
                ObjectCreateInfo->SecurityQualityOfService.Length =
                    ReadUnalignedU32((const unsigned long *)
                        (RawQos + FIELD_OFFSET(SECURITY_QUALITY_OF_SERVICE, Length)));
                ObjectCreateInfo->SecurityQualityOfService.ImpersonationLevel =
                    (SECURITY_IMPERSONATION_LEVEL)ReadUnalignedU32((const unsigned long *)
                        (RawQos + FIELD_OFFSET(SECURITY_QUALITY_OF_SERVICE, ImpersonationLevel)));
                ObjectCreateInfo->SecurityQualityOfService.ContextTrackingMode =
                    *(RawQos + FIELD_OFFSET(SECURITY_QUALITY_OF_SERVICE, ContextTrackingMode));
                ObjectCreateInfo->SecurityQualityOfService.EffectiveOnly =
                    *(RawQos + FIELD_OFFSET(SECURITY_QUALITY_OF_SERVICE, EffectiveOnly));
                ObjectCreateInfo->SecurityQos =
                    &ObjectCreateInfo->SecurityQualityOfService;
            }
        }
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        ObpReleaseObjectCreateInformation(ObjectCreateInfo);
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (LocalObjectName != NULL)
    {
        Status = ObpCaptureObjectName(ObjectName,
                                      LocalObjectName,
                                      AccessMode,
                                      AllocateFromLookaside);
    }
    else
    {
        RtlInitEmptyUnicodeString(ObjectName, NULL, 0);

        /* A root directory without a name names nothing */
        if (ObjectCreateInfo->RootDirectory != NULL)
            Status = STATUS_OBJECT_NAME_INVALID;
    }

    if (!NT_SUCCESS(Status)) ObpReleaseObjectCreateInformation(ObjectCreateInfo);

    return Status;
}

VOID
NTAPI
ObFreeObjectCreateInfoBuffer(
    _In_ POBJECT_CREATE_INFORMATION ObjectCreateInfo)
{
    ObpFreeCapturedAttributes(ObjectCreateInfo, LookasideCreateInfoList);
}

/*
 * Works out which optional headers the object needs and lays them out in front
 * of the object header, in the order the InfoMask offsets expect.
 */
NTSTATUS
NTAPI
ObpAllocateObject(
    _In_opt_ POBJECT_CREATE_INFORMATION ObjectCreateInfo,
    _In_ PUNICODE_STRING ObjectName,
    _In_opt_ POBJECT_TYPE ObjectType,
    _In_ ULONG ObjectSize,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ POBJECT_HEADER *ObjectHeader)
{
    ULONG ProcessSize, QuotaSize, HandleSize, NameSize, CreatorSize;
    POBJECT_HEADER_CREATOR_INFO CreatorInfo;
    POBJECT_HEADER_PROCESS_INFO ProcessInfo;
    POBJECT_HEADER_HANDLE_INFO HandleInfo;
    POBJECT_HEADER_QUOTA_INFO QuotaInfo;
    POBJECT_HEADER_NAME_INFO NameInfo;
    POBJECT_HEADER Header;
    POOL_TYPE PoolType;
    ULONG FinalSize;
    ULONG Tag;

    PAGED_CODE();

    ObpObjectsCreated++;

    if (ObjectType == NULL)
    {
        PoolType = NonPagedPool;
        Tag = TAG_OBJECT_TYPE;
    }
    else
    {
        PoolType = ObjectType->TypeInfo.PoolType;
        Tag = ObjectType->Key;
    }

    if (ObjectCreateInfo == NULL)
    {
        /* The very first type object is built before anything can charge it */
        ProcessSize = QuotaSize = HandleSize = 0;
        NameSize = sizeof(OBJECT_HEADER_NAME_INFO);
        CreatorSize = sizeof(OBJECT_HEADER_CREATOR_INFO);
    }
    else
    {
        if ((ObjectCreateInfo->PagedPoolCharge != ObjectType->TypeInfo.DefaultPagedPoolCharge ||
             ObjectCreateInfo->NonPagedPoolCharge != ObjectType->TypeInfo.DefaultNonPagedPoolCharge ||
             ObjectCreateInfo->SecurityDescriptorCharge > 2048) &&
            PsGetCurrentProcess() != PsInitialSystemProcess)
        {
            QuotaSize = sizeof(OBJECT_HEADER_QUOTA_INFO);
            ObpObjectsWithPoolQuota++;
        }
        else
        {
            QuotaSize = 0;
        }

        ProcessSize = (ObjectCreateInfo->Attributes & OBJ_EXCLUSIVE) ?
                      sizeof(OBJECT_HEADER_PROCESS_INFO) : 0;

        if (ObjectType->TypeInfo.MaintainHandleCount)
        {
            HandleSize = sizeof(OBJECT_HEADER_HANDLE_INFO);
            ObpObjectsWithHandleDB++;
        }
        else
        {
            HandleSize = 0;
        }

        if (ObjectName->Buffer != NULL)
        {
            NameSize = sizeof(OBJECT_HEADER_NAME_INFO);
            ObpObjectsWithName++;
        }
        else
        {
            NameSize = 0;
        }

        if (ObjectType->TypeInfo.MaintainTypeList)
        {
            CreatorSize = sizeof(OBJECT_HEADER_CREATOR_INFO);
            ObpObjectsWithCreatorInfo++;
        }
        else
        {
            CreatorSize = 0;
        }
    }

    FinalSize = ProcessSize + QuotaSize + HandleSize + NameSize + CreatorSize +
                FIELD_OFFSET(OBJECT_HEADER, Body);

    Header = ExAllocatePoolWithTag(PoolType, FinalSize + ObjectSize, Tag);
    if (Header == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (ProcessSize != 0)
    {
        ProcessInfo = (POBJECT_HEADER_PROCESS_INFO)Header;
        ProcessInfo->ExclusiveProcess = NULL;
        ProcessInfo->Reserved = 0;
        Header = (POBJECT_HEADER)(ProcessInfo + 1);
    }

    if (QuotaSize != 0)
    {
        QuotaInfo = (POBJECT_HEADER_QUOTA_INFO)Header;
        QuotaInfo->PagedPoolCharge = ObjectCreateInfo->PagedPoolCharge;
        QuotaInfo->NonPagedPoolCharge = ObjectCreateInfo->NonPagedPoolCharge;
        QuotaInfo->SecurityDescriptorCharge = ObjectCreateInfo->SecurityDescriptorCharge;
        QuotaInfo->Reserved1 = 0;
        QuotaInfo->SecurityDescriptorQuotaBlock = NULL;
        QuotaInfo->Reserved2 = 0;
        Header = (POBJECT_HEADER)(QuotaInfo + 1);
    }

    if (HandleSize != 0)
    {
        HandleInfo = (POBJECT_HEADER_HANDLE_INFO)Header;
        HandleInfo->SingleEntry.HandleCount = 0;
        Header = (POBJECT_HEADER)(HandleInfo + 1);
    }

    if (NameSize != 0)
    {
        NameInfo = (POBJECT_HEADER_NAME_INFO)Header;
        NameInfo->Name = *ObjectName;
        NameInfo->Directory = NULL;
        NameInfo->QueryReferences = 1;
#ifdef _WIN64
        NameInfo->Reserved = 0;
#endif
        Header = (POBJECT_HEADER)(NameInfo + 1);
    }

    if (CreatorSize != 0)
    {
        CreatorInfo = (POBJECT_HEADER_CREATOR_INFO)Header;
        CreatorInfo->CreatorBackTraceIndex = 0;
        CreatorInfo->CreatorUniqueProcess = PsGetCurrentProcessId();
        CreatorInfo->Reserved1 = 0;
#ifdef _WIN64
        CreatorInfo->Reserved2 = 0;
#endif
        InitializeListHead(&CreatorInfo->TypeList);
        Header = (POBJECT_HEADER)(CreatorInfo + 1);
    }

    /* The low bits of the header address carry handle attributes */
    ASSERT(((ULONG_PTR)Header & OBJ_HANDLE_ATTRIBUTES) == 0);

    Header->InfoMask = 0;
    if (CreatorSize != 0) Header->InfoMask |= OBP_CREATOR_INFO_MASK;
    if (NameSize != 0) Header->InfoMask |= OBP_NAME_INFO_MASK;
    if (HandleSize != 0) Header->InfoMask |= OBP_HANDLE_INFO_MASK;
    if (QuotaSize != 0) Header->InfoMask |= OBP_QUOTA_INFO_MASK;
    if (ProcessSize != 0) Header->InfoMask |= OBP_PROCESS_INFO_MASK;

    Header->Flags = OB_FLAG_CREATE_INFO;
    if (HandleSize != 0) Header->Flags |= OB_FLAG_SINGLE_PROCESS;

    Header->PointerCount = 1;
    Header->HandleCount = 0;
    ExInitializePushLock(&Header->Lock);
    Header->TypeIndex = 0;
    Header->TraceFlags = 0;
#ifdef _WIN64
    Header->Reserved = 0;
#endif
    Header->ObjectCreateInfo = ObjectCreateInfo;
    Header->SecurityDescriptor = NULL;

    if (ObjectType != NULL) ObpEncodeObjectTypeIndex(Header, ObjectType);

    if (ObjectCreateInfo != NULL)
    {
        if (ObjectCreateInfo->Attributes & OBJ_PERMANENT)
            Header->Flags |= OB_FLAG_PERMANENT;
        if (ObjectCreateInfo->Attributes & OBJ_EXCLUSIVE)
            Header->Flags |= OB_FLAG_EXCLUSIVE;
    }

    if (PreviousMode == KernelMode)
    {
        Header->Flags |= OB_FLAG_KERNEL_MODE;

        if (ObjectCreateInfo != NULL &&
            (ObjectCreateInfo->Attributes & OBJ_KERNEL_EXCLUSIVE))
        {
            Header->Flags |= OB_FLAG_KERNEL_ONLY_ACCESS;
        }
    }

    if (ObjectType != NULL)
    {
        InterlockedIncrement((PLONG)&ObjectType->TotalNumberOfObjects);
        ObjectType->HighWaterNumberOfObjects = max(ObjectType->TotalNumberOfObjects,
                                                   ObjectType->HighWaterNumberOfObjects);
    }

    *ObjectHeader = Header;
    return STATUS_SUCCESS;
}

static
ULONG
ObpQueryNameInfoSize(
    _In_ POBJECT_HEADER ObjectHeader)
{
    POBJECT_HEADER_NAME_INFO NameInfo;
    POBJECT_DIRECTORY ParentDirectory;
    ULONG NameSize;

    PAGED_CODE();

    NameInfo = OBJECT_HEADER_TO_NAME_INFO(ObjectHeader);
    if (NameInfo == NULL) return 0;

    ParentDirectory = NameInfo->Directory;
    if (ParentDirectory == NULL) return 0;

    NameSize = sizeof(OBJ_NAME_PATH_SEPARATOR) + NameInfo->Name.Length;

    for (;;)
    {
        NameInfo = OBJECT_HEADER_TO_NAME_INFO(OBJECT_TO_OBJECT_HEADER(ParentDirectory));
        if (NameInfo == NULL) break;

        ParentDirectory = NameInfo->Directory;
        if (ParentDirectory == NULL) break;

        NameSize += sizeof(OBJ_NAME_PATH_SEPARATOR) + NameInfo->Name.Length;
    }

    return NameSize + sizeof(OBJECT_NAME_INFORMATION) + sizeof(UNICODE_NULL);
}

NTSTATUS
NTAPI
ObQueryTypeInfo(
    _In_ POBJECT_TYPE ObjectType,
    _Out_writes_bytes_to_(Length, *ReturnLength) POBJECT_TYPE_INFORMATION ObjectTypeInfo,
    _In_ ULONG Length,
    _Inout_ PULONG ReturnLength)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PWSTR InfoBuffer;

    /* The type name is stored NUL terminated, which the copy below relies on */
    ASSERT(ObjectType->Name.MaximumLength >= ObjectType->Name.Length + sizeof(UNICODE_NULL));

    _SEH2_TRY
    {
        /* The length accumulates, so one buffer can hold a run of types */
        *ReturnLength += sizeof(*ObjectTypeInfo) +
                         ALIGN_UP(ObjectType->Name.Length + sizeof(UNICODE_NULL), ULONG_PTR);

        if (Length < *ReturnLength) _SEH2_YIELD(return STATUS_INFO_LENGTH_MISMATCH);

        ObjectTypeInfo->TotalNumberOfHandles = ObjectType->TotalNumberOfHandles;
        ObjectTypeInfo->TotalNumberOfObjects = ObjectType->TotalNumberOfObjects;
        ObjectTypeInfo->HighWaterNumberOfHandles = ObjectType->HighWaterNumberOfHandles;
        ObjectTypeInfo->HighWaterNumberOfObjects = ObjectType->HighWaterNumberOfObjects;
        ObjectTypeInfo->PoolType = ObjectType->TypeInfo.PoolType;
        ObjectTypeInfo->DefaultNonPagedPoolCharge = ObjectType->TypeInfo.DefaultNonPagedPoolCharge;
        ObjectTypeInfo->DefaultPagedPoolCharge = ObjectType->TypeInfo.DefaultPagedPoolCharge;
        ObjectTypeInfo->ValidAccessMask = ObjectType->TypeInfo.ValidAccessMask;
        ObjectTypeInfo->SecurityRequired = ObjectType->TypeInfo.SecurityRequired;
        ObjectTypeInfo->InvalidAttributes = ObjectType->TypeInfo.InvalidAttributes;
        ObjectTypeInfo->GenericMapping = ObjectType->TypeInfo.GenericMapping;
        ObjectTypeInfo->MaintainHandleCount = ObjectType->TypeInfo.MaintainHandleCount;

        InfoBuffer = (PWSTR)(ObjectTypeInfo + 1);
        ObjectTypeInfo->TypeName.Buffer = InfoBuffer;
        ObjectTypeInfo->TypeName.MaximumLength = ObjectType->Name.MaximumLength;
        ObjectTypeInfo->TypeName.Length = ObjectType->Name.Length;

        RtlCopyMemory(InfoBuffer, ObjectType->Name.Buffer, ObjectType->Name.Length);
        InfoBuffer[ObjectType->Name.Length / sizeof(WCHAR)] = UNICODE_NULL;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
ObCreateObject(
    _In_opt_ KPROCESSOR_MODE ProbeMode,
    _In_ POBJECT_TYPE Type,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _Inout_opt_ PVOID ParseContext,
    _In_ ULONG ObjectSize,
    _In_opt_ ULONG PagedPoolCharge,
    _In_opt_ ULONG NonPagedPoolCharge,
    _Out_ PVOID *Object)
{
    POBJECT_CREATE_INFORMATION ObjectCreateInfo;
    UNICODE_STRING ObjectName;
    POBJECT_HEADER Header;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ParseContext);

    ObjectCreateInfo = ObpAllocateObjectCreateInfoBuffer(LookasideCreateInfoList);
    if (ObjectCreateInfo == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = ObpCaptureObjectCreateInformation(ObjectAttributes,
                                               ProbeMode,
                                               AccessMode,
                                               FALSE,
                                               ObjectCreateInfo,
                                               &ObjectName);
    if (!NT_SUCCESS(Status))
    {
        ObpFreeCapturedAttributes(ObjectCreateInfo, LookasideCreateInfoList);
        return Status;
    }

    if (Type->TypeInfo.InvalidAttributes & ObjectCreateInfo->Attributes)
    {
        ObpFreeObjectCreateInformation(ObjectCreateInfo);
        if (ObjectName.Buffer) ObpFreeObjectNameBuffer(&ObjectName);
        return STATUS_INVALID_PARAMETER;
    }

    if (PagedPoolCharge == 0) PagedPoolCharge = Type->TypeInfo.DefaultPagedPoolCharge;
    if (NonPagedPoolCharge == 0) NonPagedPoolCharge = Type->TypeInfo.DefaultNonPagedPoolCharge;

    ObjectCreateInfo->PagedPoolCharge = PagedPoolCharge;
    ObjectCreateInfo->NonPagedPoolCharge = NonPagedPoolCharge;

    Status = ObpAllocateObject(ObjectCreateInfo,
                               &ObjectName,
                               Type,
                               ObjectSize,
                               AccessMode,
                               &Header);
    if (!NT_SUCCESS(Status))
    {
        ObpFreeObjectCreateInformation(ObjectCreateInfo);
        if (ObjectName.Buffer) ObpFreeObjectNameBuffer(&ObjectName);
        return Status;
    }

    WriteUnalignedUlongPtr((ULONG_PTR *)Object, (ULONG_PTR)&Header->Body);

    /* A permanent object outlives its creator, which takes a privilege */
    if ((Header->Flags & OB_FLAG_PERMANENT) &&
        !SeSinglePrivilegeCheck(SeCreatePermanentPrivilege, ProbeMode))
    {
        ObpDeallocateObject(*Object);
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObCreateObjectType(
    _In_ PUNICODE_STRING TypeName,
    _In_ POBJECT_TYPE_INITIALIZER ObjectTypeInitializer,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Out_ POBJECT_TYPE *ObjectType)
{
    return ObCreateObjectTypeEx(TypeName,
                                ObjectTypeInitializer,
                                SecurityDescriptor,
                                0,
                                ObjectType);
}

static
BOOLEAN
ObpIsObjectTypeInitializerValid(
    _In_opt_ PUNICODE_STRING TypeName,
    _In_opt_ POBJECT_TYPE_INITIALIZER Initializer,
    _In_ LONG_PTR WaitObjectInfo)
{
    if (TypeName == NULL || TypeName->Length == 0 ||
        (TypeName->Length % sizeof(WCHAR)) != 0)
    {
        return FALSE;
    }

    if (Initializer == NULL || Initializer->Length < sizeof(*Initializer)) return FALSE;
    if (Initializer->ObjectTypeFlags & 0xFC00) return FALSE;
    if (Initializer->InvalidAttributes & ~OBJ_VALID_KERNEL_ATTRIBUTES) return FALSE;

    /* A type that counts handles must be told when they come and go */
    if (Initializer->MaintainHandleCount &&
        Initializer->OpenProcedure == NULL &&
        Initializer->CloseProcedure == NULL)
    {
        return FALSE;
    }

    /* A waitable object has to live where the dispatcher can reach it */
    if (!Initializer->UseDefaultObject &&
        Initializer->PoolType != NonPagedPool &&
        Initializer->PoolType != NonPagedPoolNx &&
        !(WaitObjectInfo & 1))
    {
        return FALSE;
    }

    return TRUE;
}

static
ULONG
ObpObjectTypeKey(
    _In_ PUNICODE_STRING TypeName)
{
    ANSI_STRING AnsiName;
    NTSTATUS Status;
    ULONG Key;
    ULONG i;

    /* The pool tag is the first four characters of the type name, padded */
    Status = RtlUnicodeStringToAnsiString(&AnsiName, TypeName, TRUE);
    if (!NT_SUCCESS(Status)) return *(PULONG)TypeName->Buffer;

    for (i = 3; i >= AnsiName.Length; i--) AnsiName.Buffer[i] = ' ';
    Key = *(PULONG)AnsiName.Buffer;
    RtlFreeAnsiString(&AnsiName);

    return Key;
}

NTSTATUS
NTAPI
ObCreateObjectTypeEx(
    _In_ PUNICODE_STRING TypeName,
    _In_ POBJECT_TYPE_INITIALIZER ObjectTypeInitializer,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_opt_ LONG_PTR WaitObjectInfo,
    _Out_ POBJECT_TYPE *ObjectType)
{
    POBJECT_HEADER_CREATOR_INFO CreatorInfo;
    POBJECT_TYPE LocalObjectType;
    OBP_LOOKUP_CONTEXT Context;
    UNICODE_STRING ObjectName;
    POBJECT_HEADER Header;
    ULONG HeaderSize;
    NTSTATUS Status;
    PWCHAR p;
    ULONG i;

    if (!ObpIsObjectTypeInitializerValid(TypeName, ObjectTypeInitializer, WaitObjectInfo))
        return STATUS_INVALID_PARAMETER;

    /* A type name is one component, never a path */
    p = TypeName->Buffer;
    i = TypeName->Length / sizeof(WCHAR);
    while (i-- != 0)
    {
        if (*p++ == OBJ_NAME_PATH_SEPARATOR) return STATUS_OBJECT_NAME_INVALID;
    }

    ObpInitializeLookupContext(&Context);

    if (ObpTypeDirectoryObject != NULL)
    {
        ObpAcquireLookupContextLock(&Context, ObpTypeDirectoryObject);
        if (ObpLookupEntryDirectory(ObpTypeDirectoryObject, TypeName,
                                    OBJ_CASE_INSENSITIVE, FALSE, &Context))
        {
            ObpReleaseLookupContext(&Context);
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }

    ObjectName.Buffer = ExAllocatePoolWithTag(PagedPool,
                                              TypeName->MaximumLength,
                                              OB_NAME_TAG);
    if (ObjectName.Buffer == NULL)
    {
        ObpReleaseLookupContext(&Context);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ObjectName.MaximumLength = TypeName->MaximumLength;
    RtlCopyUnicodeString(&ObjectName, TypeName);

    Status = ObpAllocateObject(NULL,
                               &ObjectName,
                               ObpTypeObjectType,
                               sizeof(OBJECT_TYPE),
                               KernelMode,
                               &Header);
    if (!NT_SUCCESS(Status))
    {
        ObpReleaseLookupContext(&Context);
        ExFreePool(ObjectName.Buffer);
        return Status;
    }

    LocalObjectType = (POBJECT_TYPE)&Header->Body;
    LocalObjectType->Name = ObjectName;
    Header->Flags |= OB_FLAG_KERNEL_MODE | OB_FLAG_PERMANENT;

    LocalObjectType->TotalNumberOfObjects = 0;
    LocalObjectType->TotalNumberOfHandles = 0;
    LocalObjectType->HighWaterNumberOfObjects = 0;
    LocalObjectType->HighWaterNumberOfHandles = 0;

    if (ObpTypeObjectType == NULL)
    {
        /* This is the type of all types, and it counts itself */
        ObpTypeObjectType = LocalObjectType;
        LocalObjectType->TotalNumberOfObjects = 1;
        LocalObjectType->Key = TAG_OBJECT_TYPE;
    }
    else
    {
        LocalObjectType->Key = ObpObjectTypeKey(TypeName);
    }

    LocalObjectType->TypeInfo = *ObjectTypeInitializer;
    LocalObjectType->TypeInfo.PoolType = ObjectTypeInitializer->PoolType;

    if (NtGlobalFlag & FLG_MAINTAIN_OBJECT_TYPELIST)
        LocalObjectType->TypeInfo.MaintainTypeList = TRUE;

    /* Every object of this type also pays for its own headers */
    HeaderSize = sizeof(OBJECT_HEADER) +
                 sizeof(OBJECT_HEADER_NAME_INFO) +
                 (ObjectTypeInitializer->MaintainHandleCount ?
                  sizeof(OBJECT_HEADER_HANDLE_INFO) : 0);

    if ((ObjectTypeInitializer->PoolType & OBP_BASE_POOL_TYPE_MASK) == NonPagedPool)
        LocalObjectType->TypeInfo.DefaultNonPagedPoolCharge += HeaderSize;
    else
        LocalObjectType->TypeInfo.DefaultPagedPoolCharge += HeaderSize;

    if (ObjectTypeInitializer->SecurityProcedure == NULL)
        LocalObjectType->TypeInfo.SecurityProcedure = SeDefaultObjectMethod;

    if (LocalObjectType->TypeInfo.UseDefaultObject)
    {
        LocalObjectType->TypeInfo.ValidAccessMask |= SYNCHRONIZE;
        LocalObjectType->DefaultObject = &ObpDefaultObject;
    }
    else
    {
        LocalObjectType->DefaultObject = (PVOID)WaitObjectInfo;
    }

    ExInitializePushLock(&LocalObjectType->TypeLock);
    InitializeListHead(&LocalObjectType->TypeList);
    InitializeListHead(&LocalObjectType->CallbackList);

    LocalObjectType->SeMandatoryLabelMask = 0;
    LocalObjectType->SeTrustConstraintMask = 0;

    /* A longer initializer carries the mandatory label and trust masks */
    if (ObjectTypeInitializer->Length >= sizeof(*ObjectTypeInitializer) + (2 * sizeof(ULONG)))
    {
        LocalObjectType->SeMandatoryLabelMask = ReadUnalignedU32((const unsigned long *)
            ((PUCHAR)ObjectTypeInitializer + sizeof(*ObjectTypeInitializer)));
        LocalObjectType->SeTrustConstraintMask = ReadUnalignedU32((const unsigned long *)
            ((PUCHAR)ObjectTypeInitializer + sizeof(*ObjectTypeInitializer) + sizeof(ULONG)));
    }

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

    ObpEnterObjectTypeMutex(ObpTypeObjectType);

    CreatorInfo = OBJECT_HEADER_TO_CREATOR_INFO(Header);
    if (CreatorInfo != NULL)
    {
        InsertTailList(&ObpTypeObjectType->TypeList, &CreatorInfo->TypeList);
        Header->Flags &= ~OB_FLAG_CREATE_INFO;
    }

    ASSERT(ObpTypeObjectType->TotalNumberOfObjects < RTL_NUMBER_OF(ObTypeIndexTable));
    LocalObjectType->Index = (UCHAR)ObpTypeObjectType->TotalNumberOfObjects;
    ASSERT(LocalObjectType->Index != 0);
    ObTypeIndexTable[LocalObjectType->Index] = LocalObjectType;

    /* The type of types could not encode its own index until it had one */
    if (LocalObjectType == ObpTypeObjectType)
        ObpEncodeObjectTypeIndex(Header, ObpTypeObjectType);

    ObpLeaveObjectTypeMutex(ObpTypeObjectType);

    if (ObpTypeDirectoryObject != NULL &&
        !ObpInsertEntryDirectory(ObpTypeDirectoryObject, &Context, Header))
    {
        ObpReleaseLookupContext(&Context);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (ObpTypeDirectoryObject != NULL) ObReferenceObject(ObpTypeDirectoryObject);

    ObpReleaseLookupContext(&Context);

    *ObjectType = LocalObjectType;
    return STATUS_SUCCESS;
}

VOID
NTAPI
ObDeleteCapturedInsertInfo(
    _In_ PVOID Object)
{
    POBJECT_HEADER ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);

    PAGED_CODE();

    if ((ObjectHeader->Flags & OB_FLAG_CREATE_INFO) &&
        ObjectHeader->ObjectCreateInfo != NULL)
    {
        ObpFreeObjectCreateInformation(ObjectHeader->ObjectCreateInfo);
        ObjectHeader->ObjectCreateInfo = NULL;
    }
}

VOID
NTAPI
ObpDeleteObjectType(
    _In_ PVOID Object)
{
    UNREFERENCED_PARAMETER(Object);
}

VOID
NTAPI
ObMakeTemporaryObject(
    _In_ PVOID ObjectBody)
{
    PAGED_CODE();
    ObpSetPermanentObject(ObjectBody, FALSE);
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtMakeTemporaryObject(
    _In_ HANDLE ObjectHandle)
{
    PVOID ObjectBody;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ObjectHandle,
                                       DELETE,
                                       NULL,
                                       KeGetPreviousMode(),
                                       &ObjectBody,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    ObpSetPermanentObject(ObjectBody, FALSE);
    ObDereferenceObject(ObjectBody);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtMakePermanentObject(
    _In_ HANDLE ObjectHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PVOID ObjectBody;
    NTSTATUS Status;

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeCreatePermanentPrivilege, PreviousMode))
        return STATUS_PRIVILEGE_NOT_HELD;

    Status = ObReferenceObjectByHandle(ObjectHandle,
                                       0,
                                       NULL,
                                       PreviousMode,
                                       &ObjectBody,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    ObpSetPermanentObject(ObjectBody, TRUE);
    ObDereferenceObject(ObjectBody);

    return STATUS_SUCCESS;
}

static
NTSTATUS
ObpQueryBasicInformation(
    _In_ PVOID Object,
    _In_ POBJECT_HEADER ObjectHeader,
    _In_ POBJECT_TYPE ObjectType,
    _In_ POBJECT_HANDLE_INFORMATION HandleInfo,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ POBJECT_BASIC_INFORMATION BasicInfo)
{
    POBJECT_HEADER_QUOTA_INFO ObjectQuota;
    SECURITY_INFORMATION SecurityInformation;

    BasicInfo->Attributes = HandleInfo->HandleAttributes;
    BasicInfo->GrantedAccess = HandleInfo->GrantedAccess;
    BasicInfo->HandleCount = ObjectHeader->HandleCount;
    BasicInfo->PointerCount = ObjectHeader->PointerCount;

    if (ObjectHeader->Flags & OB_FLAG_EXCLUSIVE) BasicInfo->Attributes |= OBJ_EXCLUSIVE;
    if (ObjectHeader->Flags & OB_FLAG_PERMANENT) BasicInfo->Attributes |= OBJ_PERMANENT;

    ObjectQuota = OBJECT_HEADER_TO_QUOTA_INFO(ObjectHeader);
    if (ObjectQuota != NULL)
    {
        BasicInfo->PagedPoolCharge = ObjectQuota->PagedPoolCharge;
        BasicInfo->NonPagedPoolCharge = ObjectQuota->NonPagedPoolCharge;
    }
    else
    {
        BasicInfo->PagedPoolCharge = 0;
        BasicInfo->NonPagedPoolCharge = 0;
    }

    BasicInfo->NameInfoSize = ObpQueryNameInfoSize(ObjectHeader);
    BasicInfo->TypeInfoSize = sizeof(OBJECT_TYPE_INFORMATION) +
                              ObjectType->Name.Length + sizeof(UNICODE_NULL);

    /* Only a symbolic link records when it was made */
    if (ObjectType == ObpSymbolicLinkObjectType)
        BasicInfo->CreationTime.QuadPart = ((POBJECT_SYMBOLIC_LINK)Object)->CreationTime.QuadPart;
    else
        BasicInfo->CreationTime.QuadPart = 0;

    BasicInfo->SecurityDescriptorSize = 0;
    if (BooleanFlagOn(HandleInfo->GrantedAccess, READ_CONTROL) &&
        ObjectHeader->SecurityDescriptor != NULL)
    {
        SecurityInformation = OWNER_SECURITY_INFORMATION |
                              GROUP_SECURITY_INFORMATION |
                              DACL_SECURITY_INFORMATION |
                              SACL_SECURITY_INFORMATION;

        ObjectType->TypeInfo.SecurityProcedure(Object,
                                               QuerySecurityDescriptor,
                                               &SecurityInformation,
                                               NULL,
                                               &BasicInfo->SecurityDescriptorSize,
                                               &ObjectHeader->SecurityDescriptor,
                                               ObjectType->TypeInfo.PoolType,
                                               &ObjectType->TypeInfo.GenericMapping,
                                               PreviousMode);
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
ObpQueryAllTypesInformation(
    _Out_ PVOID ObjectInformation,
    _In_ ULONG Length,
    _Inout_ PULONG InfoLength)
{
    POBJECT_ALL_TYPES_INFORMATION TypesInfo = ObjectInformation;
    POBJECT_TYPE_INFORMATION TypeInfo;
    ULONG Index, Count = 0, Used;
    NTSTATUS Status;

    *InfoLength = ALIGN_UP(sizeof(OBJECT_ALL_TYPES_INFORMATION), ULONG_PTR);

    for (Index = 0; Index < RTL_NUMBER_OF(ObTypeIndexTable); Index++)
    {
        if (ObTypeIndexTable[Index] == NULL) continue;

        *InfoLength += sizeof(OBJECT_TYPE_INFORMATION) +
                       ALIGN_UP(ObTypeIndexTable[Index]->Name.Length + sizeof(UNICODE_NULL),
                                ULONG_PTR);
        Count++;
    }

    if (Length < *InfoLength) return STATUS_INFO_LENGTH_MISMATCH;

    TypesInfo->NumberOfTypes = Count;
    Used = ALIGN_UP(sizeof(OBJECT_ALL_TYPES_INFORMATION), ULONG_PTR);
    Status = STATUS_SUCCESS;

    for (Index = 0; Index < RTL_NUMBER_OF(ObTypeIndexTable); Index++)
    {
        if (ObTypeIndexTable[Index] == NULL) continue;

        TypeInfo = (POBJECT_TYPE_INFORMATION)((PUCHAR)ObjectInformation + Used);
        Status = ObQueryTypeInfo(ObTypeIndexTable[Index], TypeInfo, Length, &Used);
        if (!NT_SUCCESS(Status)) break;

        TypeInfo->TypeName.MaximumLength = TypeInfo->TypeName.Length + sizeof(UNICODE_NULL);
    }

    return Status;
}

NTSTATUS
NTAPI
NtQueryObject(
    _In_opt_ HANDLE ObjectHandle,
    _In_ OBJECT_INFORMATION_CLASS ObjectInformationClass,
    _Out_writes_bytes_opt_(Length) PVOID ObjectInformation,
    _In_ ULONG Length,
    _Out_opt_ PULONG ResultLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    POBJECT_HANDLE_ATTRIBUTE_INFORMATION HandleFlags;
    OBJECT_HANDLE_INFORMATION HandleInfo;
    POBJECT_HEADER ObjectHeader = NULL;
    POBJECT_TYPE ObjectType = NULL;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    ULONG InfoLength = 0;
    PVOID Object = NULL;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(ObjectInformation, Length, sizeof(UCHAR));
            if (ResultLength) ProbeForWriteUlong(ResultLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    /* The list of every type is not about one object, so it takes no handle */
    if (ObjectInformationClass != ObjectTypesInformation)
    {
        Status = ObReferenceObjectByHandle(ObjectHandle,
                                           0,
                                           NULL,
                                           KeGetPreviousMode(),
                                           &Object,
                                           &HandleInfo);
        if (!NT_SUCCESS(Status)) return Status;

        ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);
        ObjectType = ObpGetObjectTypeFromHeader(ObjectHeader);
    }

    _SEH2_TRY
    {
        switch (ObjectInformationClass)
        {
            case ObjectBasicInformation:
                InfoLength = sizeof(OBJECT_BASIC_INFORMATION);
                if (Length != sizeof(OBJECT_BASIC_INFORMATION))
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                Status = ObpQueryBasicInformation(Object, ObjectHeader, ObjectType,
                                                  &HandleInfo, PreviousMode,
                                                  ObjectInformation);
                break;

            case ObjectNameInformation:
                Status = ObQueryNameString(Object, ObjectInformation, Length, &InfoLength);
                break;

            case ObjectTypeInformation:
                Status = ObQueryTypeInfo(ObjectType, ObjectInformation, Length, &InfoLength);
                break;

            case ObjectTypesInformation:
                Status = ObpQueryAllTypesInformation(ObjectInformation, Length, &InfoLength);
                break;

            case ObjectHandleFlagInformation:
                InfoLength = sizeof(OBJECT_HANDLE_ATTRIBUTE_INFORMATION);
                if (Length != sizeof(OBJECT_HANDLE_ATTRIBUTE_INFORMATION))
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                HandleFlags = ObjectInformation;
                HandleFlags->Inherit = (HandleInfo.HandleAttributes & OBJ_INHERIT) != 0;
                HandleFlags->ProtectFromClose =
                    (HandleInfo.HandleAttributes & OBJ_PROTECT_CLOSE) != 0;
                Status = STATUS_SUCCESS;
                break;

            default:
                InfoLength = Length;
                Status = STATUS_INVALID_INFO_CLASS;
                break;
        }

        if (ResultLength) *ResultLength = InfoLength;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (Object != NULL) ObDereferenceObject(Object);

    return Status;
}

static
NTSTATUS
ObpSetSessionInformation(
    _In_ HANDLE ObjectHandle,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    OBP_LOOKUP_CONTEXT LookupContext;
    POBJECT_DIRECTORY Directory;
    NTSTATUS Status;

    /* Binding a directory to a session is a trusted computing base operation */
    if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
    {
        DPRINT1("Privilege not held\n");
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    Status = ObReferenceObjectByHandle(ObjectHandle,
                                       0,
                                       ObpDirectoryObjectType,
                                       PreviousMode,
                                       (PVOID *)&Directory,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    ObpInitializeLookupContext(&LookupContext);
    ObpAcquireDirectoryLockExclusive(Directory, &LookupContext);

    Directory->SessionId = PsGetCurrentProcessSessionId();

    ObpReleaseDirectoryLock(Directory, &LookupContext);
    ObpReleaseLookupContext(&LookupContext);
    ObDereferenceObject(Directory);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtSetInformationObject(
    _In_ HANDLE ObjectHandle,
    _In_ OBJECT_INFORMATION_CLASS ObjectInformationClass,
    _In_reads_bytes_(Length) PVOID ObjectInformation,
    _In_ ULONG Length)
{
    OBJECT_HANDLE_ATTRIBUTE_INFORMATION HandleFlags;
    KPROCESSOR_MODE PreviousMode;

    PAGED_CODE();

    switch (ObjectInformationClass)
    {
        case ObjectHandleFlagInformation:
            if (Length != sizeof(HandleFlags)) return STATUS_INFO_LENGTH_MISMATCH;

            PreviousMode = ExGetPreviousMode();

            if (PreviousMode != KernelMode)
            {
                _SEH2_TRY
                {
                    ProbeForRead(ObjectInformation, sizeof(HandleFlags), sizeof(BOOLEAN));
                    HandleFlags = *(POBJECT_HANDLE_ATTRIBUTE_INFORMATION)ObjectInformation;
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

            return ObSetHandleAttributes(ObjectHandle, &HandleFlags, PreviousMode);

        case ObjectSessionInformation:
            UNREFERENCED_PARAMETER(ObjectInformation);
            UNREFERENCED_PARAMETER(Length);
            return ObpSetSessionInformation(ObjectHandle, ExGetPreviousMode());

        default:
            return STATUS_INVALID_INFO_CLASS;
    }
}

/* EOF */
