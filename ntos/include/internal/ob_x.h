/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object manager inline helpers: headers, locks, names, lookups
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ex.h"

#define OBP_NAME_LOOKASIDE_MAX_SIZE 248

FORCEINLINE
ULONG
ObpValidateAttributes(
    _In_ ULONG Attributes,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    if (PreviousMode == KernelMode)
        return Attributes & OBJ_VALID_KERNEL_ATTRIBUTES;

    return Attributes & OBJ_VALID_ATTRIBUTES & ~OBJ_KERNEL_HANDLE;
}

FORCEINLINE
UCHAR
ObpTypeIndexMask(
    _In_ POBJECT_HEADER ObjectHeader)
{
    return (UCHAR)(ObHeaderCookie ^ (UCHAR)((ULONG_PTR)ObjectHeader >> 8));
}

FORCEINLINE
POBJECT_TYPE
ObpGetObjectTypeFromHeader(
    _In_ POBJECT_HEADER ObjectHeader)
{
    return ObTypeIndexTable[(UCHAR)(ObjectHeader->TypeIndex ^ ObpTypeIndexMask(ObjectHeader))];
}

FORCEINLINE
VOID
ObpEncodeObjectTypeIndex(
    _Inout_ POBJECT_HEADER ObjectHeader,
    _In_ POBJECT_TYPE ObjectType)
{
    ObjectHeader->TypeIndex = (UCHAR)(ObjectType->Index ^ ObpTypeIndexMask(ObjectHeader));
    ASSERT(ObpGetObjectTypeFromHeader(ObjectHeader) == ObjectType);
}

FORCEINLINE
VOID
ObpGetQuotaCharges(
    _In_ POBJECT_HEADER ObjectHeader,
    _In_ POBJECT_TYPE ObjectType,
    _Out_ PULONG PagedPoolCharge,
    _Out_ PULONG NonPagedPoolCharge)
{
    POBJECT_HEADER_QUOTA_INFO QuotaInfo = OBJECT_HEADER_TO_QUOTA_INFO(ObjectHeader);

    *PagedPoolCharge = QuotaInfo ? QuotaInfo->PagedPoolCharge : ObjectType->TypeInfo.DefaultPagedPoolCharge;
    *NonPagedPoolCharge = QuotaInfo ? QuotaInfo->NonPagedPoolCharge : ObjectType->TypeInfo.DefaultNonPagedPoolCharge;
}

FORCEINLINE
VOID
ObpLockPushLock(
    _Inout_ PEX_PUSH_LOCK Lock,
    _In_ BOOLEAN Exclusive)
{
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    KeEnterCriticalRegion();
    if (Exclusive)
        ExAcquirePushLockExclusive(Lock);
    else
        ExAcquirePushLockShared(Lock);
}

FORCEINLINE
VOID
ObpUnlockPushLock(
    _Inout_ PEX_PUSH_LOCK Lock)
{
    ExReleasePushLock(Lock);
    KeLeaveCriticalRegion();

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
}

FORCEINLINE
VOID
ObpAcquireObjectLock(
    _In_ POBJECT_HEADER ObjectHeader)
{
    ObpLockPushLock(&ObjectHeader->Lock, TRUE);
}

FORCEINLINE
VOID
ObpAcquireObjectLockShared(
    _In_ POBJECT_HEADER ObjectHeader)
{
    ObpLockPushLock(&ObjectHeader->Lock, FALSE);
}

FORCEINLINE
VOID
ObpReleaseObjectLock(
    _In_ POBJECT_HEADER ObjectHeader)
{
    ObpUnlockPushLock(&ObjectHeader->Lock);
}

FORCEINLINE
VOID
ObpEnterObjectTypeMutex(
    _In_ POBJECT_TYPE ObjectType)
{
    ObpLockPushLock(&ObjectType->TypeLock, TRUE);
}

FORCEINLINE
VOID
ObpLeaveObjectTypeMutex(
    _In_ POBJECT_TYPE ObjectType)
{
    ObpUnlockPushLock(&ObjectType->TypeLock);
}

FORCEINLINE
VOID
ObpAcquireDirectoryLockShared(
    _In_ POBJECT_DIRECTORY Directory,
    _In_ POBP_LOOKUP_CONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);
    ObpLockPushLock(&Directory->Lock, FALSE);
}

FORCEINLINE
VOID
ObpAcquireDirectoryLockExclusive(
    _In_ POBJECT_DIRECTORY Directory,
    _In_ POBP_LOOKUP_CONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);
    ObpLockPushLock(&Directory->Lock, TRUE);
}

FORCEINLINE
VOID
ObpReleaseDirectoryLock(
    _In_ POBJECT_DIRECTORY Directory,
    _In_ POBP_LOOKUP_CONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);
    ExReleasePushLock(&Directory->Lock);
    KeLeaveCriticalRegion();
}

FORCEINLINE
POBJECT_HEADER_NAME_INFO
ObpReferenceNameInfo(
    _In_ POBJECT_HEADER ObjectHeader)
{
    POBJECT_HEADER_NAME_INFO NameInfo = OBJECT_HEADER_TO_NAME_INFO(ObjectHeader);
    LONG Seen, Count;

    if (NameInfo == NULL)
        return NULL;

    Count = *(volatile LONG *)&NameInfo->QueryReferences;
    while (Count != 0)
    {
        Seen = InterlockedCompareExchange((PLONG)&NameInfo->QueryReferences, Count + 1, Count);
        if (Seen == Count)
            return NameInfo;

        Count = Seen;
    }

    return NULL;
}

FORCEINLINE
VOID
ObpDereferenceNameInfo(
    _In_opt_ POBJECT_HEADER_NAME_INFO NameInfo)
{
    POBJECT_DIRECTORY Directory;

    if (NameInfo == NULL || InterlockedDecrement((PLONG)&NameInfo->QueryReferences) != 0)
        return;

    if (NameInfo->Name.Buffer != NULL)
    {
        ExFreePoolWithTag(NameInfo->Name.Buffer, OB_NAME_TAG);
        RtlInitEmptyUnicodeString(&NameInfo->Name, NULL, 0);
    }

    Directory = NameInfo->Directory;
    if (Directory != NULL)
    {
        NameInfo->Directory = NULL;
        ObDereferenceObjectDeferDelete(Directory);
    }
}

FORCEINLINE
VOID
ObpInitializeLookupContext(
    _Out_ POBP_LOOKUP_CONTEXT Context)
{
    RtlZeroMemory(Context, sizeof(*Context));
}

FORCEINLINE
VOID
ObpAcquireLookupContextLock(
    _Inout_ POBP_LOOKUP_CONTEXT Context,
    _In_ POBJECT_DIRECTORY Directory)
{
    ASSERT(!Context->DirectoryLocked);

    ObpAcquireDirectoryLockExclusive(Directory, Context);
    Context->Directory = Directory;
    Context->DirectoryLocked = TRUE;
}

FORCEINLINE
VOID
ObpReleaseLookupContextObject(
    _Inout_ POBP_LOOKUP_CONTEXT Context)
{
    PVOID Object = Context->Object;

    if (Object == NULL)
        return;

    Context->Object = NULL;
    ObpDereferenceNameInfo(OBJECT_HEADER_TO_NAME_INFO(OBJECT_TO_OBJECT_HEADER(Object)));
    ObDereferenceObject(Object);
}

FORCEINLINE
VOID
ObpReleaseLookupContext(
    _Inout_ POBP_LOOKUP_CONTEXT Context)
{
    if (Context->DirectoryLocked)
    {
        ObpReleaseDirectoryLock(Context->Directory, Context);
        Context->Directory = NULL;
        Context->DirectoryLocked = FALSE;
    }

    ObpReleaseLookupContextObject(Context);
}

FORCEINLINE
PVOID
ObpAllocateObjectCreateInfoBuffer(
    _In_ PP_NPAGED_LOOKASIDE_NUMBER Type)
{
    PGENERAL_LOOKASIDE Lists[2];
    PKPRCB Prcb = KeGetCurrentPrcb();
    PVOID Buffer;
    ULONG Index;

    Lists[0] = Prcb->PPLookasideList[Type].P;
    Lists[1] = Prcb->PPLookasideList[Type].L;

    for (Index = 0; Index < RTL_NUMBER_OF(Lists); Index++)
    {
        Lists[Index]->TotalAllocates++;
        Buffer = InterlockedPopEntrySList(&Lists[Index]->ListHead);
        if (Buffer != NULL)
            return Buffer;

        Lists[Index]->AllocateMisses++;
    }

    return Lists[1]->Allocate(Lists[1]->Type, Lists[1]->Size, Lists[1]->Tag);
}

FORCEINLINE
VOID
ObpFreeCapturedAttributes(
    _In_ PVOID Buffer,
    _In_ PP_NPAGED_LOOKASIDE_NUMBER Type)
{
    PGENERAL_LOOKASIDE Lists[2];
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG Index;

    Lists[0] = Prcb->PPLookasideList[Type].P;
    Lists[1] = Prcb->PPLookasideList[Type].L;

    for (Index = 0; Index < RTL_NUMBER_OF(Lists); Index++)
    {
        Lists[Index]->TotalFrees++;
        if (ExQueryDepthSList(&Lists[Index]->ListHead) < Lists[Index]->Depth)
        {
            InterlockedPushEntrySList(&Lists[Index]->ListHead, (PSLIST_ENTRY)Buffer);
            return;
        }

        Lists[Index]->FreeMisses++;
    }

    Lists[1]->Free(Buffer);
}

FORCEINLINE
VOID
ObpReleaseObjectCreateInformation(
    _Inout_ POBJECT_CREATE_INFORMATION ObjectCreateInfo)
{
    if (ObjectCreateInfo->SecurityDescriptor == NULL)
        return;

    SeReleaseSecurityDescriptor(ObjectCreateInfo->SecurityDescriptor, ObjectCreateInfo->ProbeMode, TRUE);
    ObjectCreateInfo->SecurityDescriptor = NULL;
}

FORCEINLINE
VOID
ObpFreeObjectCreateInformation(
    _Inout_ POBJECT_CREATE_INFORMATION ObjectCreateInfo)
{
    ObpReleaseObjectCreateInformation(ObjectCreateInfo);
    ObpFreeCapturedAttributes(ObjectCreateInfo, LookasideCreateInfoList);
}

FORCEINLINE
VOID
ObpCalloutStart(
    _Out_ PKIRQL CalloutIrql)
{
    *CalloutIrql = KeGetCurrentIrql();
}

FORCEINLINE
VOID
ObpCalloutEnd(
    _In_ KIRQL CalloutIrql,
    _In_ PCHAR Procedure,
    _In_ POBJECT_TYPE ObjectType,
    _In_ PVOID Object)
{
#if DBG
    KIRQL Irql = KeGetCurrentIrql();

    if (Irql != CalloutIrql)
    {
        DbgPrint("The %s procedure of %wZ object %p returned at IRQL %u, entered at %u\n",
                 Procedure, &ObjectType->Name, Object, Irql, CalloutIrql);
        DbgBreakPoint();
    }
#else
    UNREFERENCED_PARAMETER(CalloutIrql);
    UNREFERENCED_PARAMETER(Procedure);
    UNREFERENCED_PARAMETER(ObjectType);
    UNREFERENCED_PARAMETER(Object);
#endif
}
