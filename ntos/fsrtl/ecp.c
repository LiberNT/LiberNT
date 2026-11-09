/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The extra things a caller can say when it opens a file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "ecpp.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
FsRtlpIsEcpList(
    _In_opt_ PECP_LIST EcpList)
{
    return (BOOLEAN)((EcpList != NULL) &&
                     (EcpList->Signature == FSRTL_ECP_LIST_SIGNATURE));
}

PECP_HEADER
NTAPI
FsRtlpEcpHeaderOf(
    _In_opt_ PVOID EcpContext)
{
    PECP_HEADER Header;

    if (EcpContext == NULL) return NULL;

    Header = (PECP_HEADER)((PUCHAR)EcpContext - FSRTL_ECP_HEADER_SIZE);

    if (Header->Signature != FSRTL_ECP_SIGNATURE) return NULL;

    return Header;
}

static
PVOID
FsRtlpEcpContextOf(
    _In_ PECP_HEADER Header)
{
    return (PVOID)((PUCHAR)Header + FSRTL_ECP_HEADER_SIZE);
}

static
ULONG
FsRtlpEcpContextSize(
    _In_ PECP_HEADER Header)
{
    if (Header->TotalSize <= FSRTL_ECP_HEADER_SIZE) return 0;

    return Header->TotalSize - FSRTL_ECP_HEADER_SIZE;
}

static
PECP_HEADER
FsRtlpFindEcp(
    _In_ PECP_LIST EcpList,
    _In_ LPCGUID EcpType)
{
    PLIST_ENTRY Entry;

    for (Entry = EcpList->EcpList.Flink;
         Entry != &EcpList->EcpList;
         Entry = Entry->Flink)
    {
        PECP_HEADER Header = CONTAINING_RECORD(Entry, ECP_HEADER, Links);

        if (RtlCompareMemory(&Header->EcpType,
                             EcpType,
                             sizeof(GUID)) == sizeof(GUID))
        {
            return Header;
        }
    }

    return NULL;
}

static
VOID
FsRtlpDestroyEcp(
    _Inout_ PECP_HEADER Header)
{
    PVOID Context = FsRtlpEcpContextOf(Header);
    PVOID Lookaside = Header->ListAllocatedFrom;
    ULONG Flags = Header->Flags;

    if (Header->CleanupCallback != NULL)
    {
        Header->CleanupCallback(Context, &Header->EcpType);
    }

    Header->Signature = 0;

    if (FlagOn(Flags, FSRTL_ECP_FLAG_STATIC)) return;

    if (FlagOn(Flags, FSRTL_ECP_FLAG_FROM_LOOKASIDE) && (Lookaside != NULL))
    {
        ExFreeToLookasideListEx((PLOOKASIDE_LIST_EX)Lookaside, Header);
        return;
    }

    ExFreePoolWithTag(Header, Header->PoolTag);
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FsRtlInitializeExtraCreateParameterList(
    _Inout_ PECP_LIST EcpList)
{
    PAGED_CODE();

    if (EcpList == NULL) return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(EcpList, sizeof(*EcpList));
    EcpList->Signature = FSRTL_ECP_LIST_SIGNATURE;
    EcpList->Flags = FSRTL_ECP_LIST_FLAG_STATIC;
    InitializeListHead(&EcpList->EcpList);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FsRtlAllocateExtraCreateParameterList(
    _In_ FSRTL_ALLOCATE_ECPLIST_FLAGS Flags,
    _Outptr_ PECP_LIST *EcpList)
{
    PECP_LIST List;

    PAGED_CODE();

    if (EcpList == NULL) return STATUS_INVALID_PARAMETER;

    *EcpList = NULL;

    if (FlagOn(Flags, ~FSRTL_ALLOCATE_ECPLIST_FLAG_CHARGE_QUOTA))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (FlagOn(Flags, FSRTL_ALLOCATE_ECPLIST_FLAG_CHARGE_QUOTA))
    {
        List = ExAllocatePoolWithQuotaTag(PagedPool,
                                          sizeof(ECP_LIST),
                                          FSRTL_ECP_TAG);
    }
    else
    {
        List = ExAllocatePoolWithTag(PagedPool,
                                     sizeof(ECP_LIST),
                                     FSRTL_ECP_TAG);
    }

    if (List == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(List, sizeof(*List));
    List->Signature = FSRTL_ECP_LIST_SIGNATURE;
    InitializeListHead(&List->EcpList);

    if (FlagOn(Flags, FSRTL_ALLOCATE_ECPLIST_FLAG_CHARGE_QUOTA))
    {
        List->Flags |= FSRTL_ECP_LIST_FLAG_QUOTA;
    }

    *EcpList = List;

    return STATUS_SUCCESS;
}

VOID
NTAPI
FsRtlFreeExtraCreateParameterList(
    _In_ PECP_LIST EcpList)
{
    PAGED_CODE();

    if (!FsRtlpIsEcpList(EcpList)) return;

    while (!IsListEmpty(&EcpList->EcpList))
    {
        PECP_HEADER Header = CONTAINING_RECORD(RemoveHeadList(
                                                   &EcpList->EcpList),
                                               ECP_HEADER,
                                               Links);

        ClearFlag(Header->Flags, FSRTL_ECP_FLAG_IN_LIST);
        FsRtlpDestroyEcp(Header);
    }

    EcpList->Signature = 0;

    if (FlagOn(EcpList->Flags, FSRTL_ECP_LIST_FLAG_STATIC)) return;

    ExFreePoolWithTag(EcpList, FSRTL_ECP_TAG);
}

VOID
NTAPI
FsRtlInitializeExtraCreateParameter(
    _Out_ PECP_HEADER Ecp,
    _In_ ULONG EcpFlags,
    _In_opt_ PFSRTL_EXTRA_CREATE_PARAMETER_CLEANUP_CALLBACK CleanupCallback,
    _In_ ULONG TotalSize,
    _In_ LPCGUID EcpType,
    _In_opt_ PVOID ListAllocatedFrom)
{
    PAGED_CODE();

    if ((Ecp == NULL) || (EcpType == NULL)) return;

    RtlZeroMemory(Ecp, sizeof(*Ecp));
    Ecp->Signature = FSRTL_ECP_SIGNATURE;
    Ecp->CallerFlags = EcpFlags;
    Ecp->EcpType = *EcpType;
    Ecp->TotalSize = TotalSize;
    Ecp->CleanupCallback = CleanupCallback;
    Ecp->ListAllocatedFrom = ListAllocatedFrom;
    Ecp->PoolTag = FSRTL_ECP_TAG;
    InitializeListHead(&Ecp->Links);

    if (ListAllocatedFrom != NULL)
    {
        Ecp->Flags |= FSRTL_ECP_FLAG_FROM_LOOKASIDE;
    }
    else
    {
        Ecp->Flags |= FSRTL_ECP_FLAG_STATIC;
    }
}

NTSTATUS
NTAPI
FsRtlAllocateExtraCreateParameter(
    _In_ LPCGUID EcpType,
    _In_ ULONG SizeOfContext,
    _In_ FSRTL_ALLOCATE_ECP_FLAGS Flags,
    _In_opt_ PFSRTL_EXTRA_CREATE_PARAMETER_CLEANUP_CALLBACK CleanupCallback,
    _In_ ULONG PoolTag,
    _Outptr_ PVOID *EcpContext)
{
    PECP_HEADER Header;
    POOL_TYPE PoolType;
    ULONG TotalSize;

    PAGED_CODE();

    if ((EcpType == NULL) || (EcpContext == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    *EcpContext = NULL;

    if (FlagOn(Flags,
               ~(FSRTL_ALLOCATE_ECP_FLAG_CHARGE_QUOTA |
                 FSRTL_ALLOCATE_ECP_FLAG_NONPAGED_POOL)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (SizeOfContext > MAXULONG - FSRTL_ECP_HEADER_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    TotalSize = FSRTL_ECP_HEADER_SIZE + SizeOfContext;

    PoolType = FlagOn(Flags, FSRTL_ALLOCATE_ECP_FLAG_NONPAGED_POOL) ?
        NonPagedPool : PagedPool;

    if (FlagOn(Flags, FSRTL_ALLOCATE_ECP_FLAG_CHARGE_QUOTA))
    {
        Header = ExAllocatePoolWithQuotaTag(PoolType, TotalSize, PoolTag);
    }
    else
    {
        Header = ExAllocatePoolWithTag(PoolType, TotalSize, PoolTag);
    }

    if (Header == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Header, TotalSize);
    Header->Signature = FSRTL_ECP_SIGNATURE;
    Header->EcpType = *EcpType;
    Header->TotalSize = TotalSize;
    Header->CleanupCallback = CleanupCallback;
    Header->PoolTag = PoolTag;
    InitializeListHead(&Header->Links);

    if (FlagOn(Flags, FSRTL_ALLOCATE_ECP_FLAG_CHARGE_QUOTA))
    {
        Header->Flags |= FSRTL_ECP_FLAG_QUOTA;
    }

    if (PoolType == NonPagedPool) Header->Flags |= FSRTL_ECP_FLAG_NONPAGED;

    /* TODO: no page says who marks an ECP as coming from user mode, so the mode of whoever allocated it is what the file system is told */
    if (ExGetPreviousMode() == UserMode)
    {
        Header->Flags |= FSRTL_ECP_FLAG_USER_MODE;
    }

    *EcpContext = FsRtlpEcpContextOf(Header);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FsRtlAllocateExtraCreateParameterFromLookasideList(
    _In_ LPCGUID EcpType,
    _In_ ULONG SizeOfContext,
    _In_ FSRTL_ALLOCATE_ECP_FLAGS Flags,
    _In_opt_ PFSRTL_EXTRA_CREATE_PARAMETER_CLEANUP_CALLBACK CleanupCallback,
    _Inout_ PVOID LookasideList,
    _Outptr_ PVOID *EcpContext)
{
    PLOOKASIDE_LIST_EX Lookaside = (PLOOKASIDE_LIST_EX)LookasideList;
    PECP_HEADER Header;
    ULONG TotalSize;

    PAGED_CODE();

    if ((EcpType == NULL) || (EcpContext == NULL) || (Lookaside == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    *EcpContext = NULL;

    if (SizeOfContext > MAXULONG - FSRTL_ECP_HEADER_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    TotalSize = FSRTL_ECP_HEADER_SIZE + SizeOfContext;

    Header = ExAllocateFromLookasideListEx(Lookaside);
    if (Header == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Header, TotalSize);
    Header->Signature = FSRTL_ECP_SIGNATURE;
    Header->EcpType = *EcpType;
    Header->TotalSize = TotalSize;
    Header->CleanupCallback = CleanupCallback;
    Header->ListAllocatedFrom = Lookaside;
    Header->PoolTag = FSRTL_ECP_TAG;
    Header->Flags = FSRTL_ECP_FLAG_FROM_LOOKASIDE;
    InitializeListHead(&Header->Links);

    if (FlagOn(Flags, FSRTL_ALLOCATE_ECP_FLAG_NONPAGED_POOL))
    {
        Header->Flags |= FSRTL_ECP_FLAG_NONPAGED;
    }

    if (ExGetPreviousMode() == UserMode)
    {
        Header->Flags |= FSRTL_ECP_FLAG_USER_MODE;
    }

    *EcpContext = FsRtlpEcpContextOf(Header);

    return STATUS_SUCCESS;
}

VOID
NTAPI
FsRtlFreeExtraCreateParameter(
    _In_ PVOID EcpContext)
{
    PECP_HEADER Header = FsRtlpEcpHeaderOf(EcpContext);

    PAGED_CODE();

    if (Header == NULL) return;

    if (FlagOn(Header->Flags, FSRTL_ECP_FLAG_IN_LIST))
    {
        RemoveEntryList(&Header->Links);
        ClearFlag(Header->Flags, FSRTL_ECP_FLAG_IN_LIST);
    }

    FsRtlpDestroyEcp(Header);
}

VOID
NTAPI
FsRtlInitExtraCreateParameterLookasideList(
    _Inout_ PVOID Lookaside,
    _In_ FSRTL_ECP_LOOKASIDE_FLAGS Flags,
    _In_ SIZE_T Size,
    _In_ ULONG Tag)
{
    POOL_TYPE PoolType;

    /* TODO: the published prototype does not say how much room the caller gives, so the lookaside list header the kernel keeps is the extended one */
    if (Lookaside == NULL) return;

    PoolType = FlagOn(Flags, FSRTL_ECP_LOOKASIDE_FLAG_NONPAGED_POOL) ?
        NonPagedPool : PagedPool;

    ExInitializeLookasideListEx((PLOOKASIDE_LIST_EX)Lookaside,
                                NULL,
                                NULL,
                                PoolType,
                                0,
                                FSRTL_ECP_HEADER_SIZE + Size,
                                Tag,
                                0);
}

VOID
NTAPI
FsRtlDeleteExtraCreateParameterLookasideList(
    _Inout_ PVOID Lookaside,
    _In_ FSRTL_ECP_LOOKASIDE_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    if (Lookaside == NULL) return;

    ExDeleteLookasideListEx((PLOOKASIDE_LIST_EX)Lookaside);
}

NTSTATUS
NTAPI
FsRtlInsertExtraCreateParameter(
    _Inout_ PECP_LIST EcpList,
    _Inout_ PVOID EcpContext)
{
    PECP_HEADER Header = FsRtlpEcpHeaderOf(EcpContext);

    PAGED_CODE();

    if (!FsRtlpIsEcpList(EcpList) || (Header == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (FlagOn(Header->Flags, FSRTL_ECP_FLAG_IN_LIST))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (FsRtlpFindEcp(EcpList, &Header->EcpType) != NULL)
    {
        return STATUS_OBJECT_NAME_COLLISION;
    }

    InsertTailList(&EcpList->EcpList, &Header->Links);
    SetFlag(Header->Flags, FSRTL_ECP_FLAG_IN_LIST);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FsRtlFindExtraCreateParameter(
    _In_ PECP_LIST EcpList,
    _In_ LPCGUID EcpType,
    _Outptr_opt_ PVOID *EcpContext,
    _Out_opt_ ULONG *EcpContextSize)
{
    PECP_HEADER Header;

    PAGED_CODE();

    if (EcpContext != NULL) *EcpContext = NULL;
    if (EcpContextSize != NULL) *EcpContextSize = 0;

    if (!FsRtlpIsEcpList(EcpList) || (EcpType == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Header = FsRtlpFindEcp(EcpList, EcpType);
    if (Header == NULL) return STATUS_NOT_FOUND;

    if (EcpContext != NULL) *EcpContext = FsRtlpEcpContextOf(Header);
    if (EcpContextSize != NULL)
    {
        *EcpContextSize = FsRtlpEcpContextSize(Header);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FsRtlRemoveExtraCreateParameter(
    _Inout_ PECP_LIST EcpList,
    _In_ LPCGUID EcpType,
    _Outptr_ PVOID *EcpContext,
    _Out_opt_ ULONG *EcpContextSize)
{
    PECP_HEADER Header;

    PAGED_CODE();

    if (EcpContext != NULL) *EcpContext = NULL;
    if (EcpContextSize != NULL) *EcpContextSize = 0;

    if (!FsRtlpIsEcpList(EcpList) || (EcpType == NULL) ||
        (EcpContext == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Header = FsRtlpFindEcp(EcpList, EcpType);
    if (Header == NULL) return STATUS_NOT_FOUND;

    RemoveEntryList(&Header->Links);
    ClearFlag(Header->Flags, FSRTL_ECP_FLAG_IN_LIST);
    InitializeListHead(&Header->Links);

    *EcpContext = FsRtlpEcpContextOf(Header);
    if (EcpContextSize != NULL)
    {
        *EcpContextSize = FsRtlpEcpContextSize(Header);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FsRtlGetNextExtraCreateParameter(
    _In_ PECP_LIST EcpList,
    _In_opt_ PVOID CurrentEcpContext,
    _Out_opt_ LPGUID NextEcpType,
    _Outptr_opt_ PVOID *NextEcpContext,
    _Out_opt_ ULONG *NextEcpContextSize)
{
    PLIST_ENTRY Entry;
    PECP_HEADER Header;

    PAGED_CODE();

    if (NextEcpContext != NULL) *NextEcpContext = NULL;
    if (NextEcpContextSize != NULL) *NextEcpContextSize = 0;

    if (!FsRtlpIsEcpList(EcpList)) return STATUS_INVALID_PARAMETER;

    if (CurrentEcpContext == NULL)
    {
        Entry = EcpList->EcpList.Flink;
    }
    else
    {
        Header = FsRtlpEcpHeaderOf(CurrentEcpContext);
        if (Header == NULL) return STATUS_INVALID_PARAMETER;

        Entry = Header->Links.Flink;
    }

    if (Entry == &EcpList->EcpList) return STATUS_NOT_FOUND;

    Header = CONTAINING_RECORD(Entry, ECP_HEADER, Links);

    if (NextEcpType != NULL) *NextEcpType = Header->EcpType;
    if (NextEcpContext != NULL)
    {
        *NextEcpContext = FsRtlpEcpContextOf(Header);
    }
    if (NextEcpContextSize != NULL)
    {
        *NextEcpContextSize = FsRtlpEcpContextSize(Header);
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
FsRtlAcknowledgeEcp(
    _In_ PVOID EcpContext)
{
    PECP_HEADER Header = FsRtlpEcpHeaderOf(EcpContext);

    PAGED_CODE();

    if (Header == NULL) return;

    SetFlag(Header->Flags, FSRTL_ECP_FLAG_ACKNOWLEDGED);
}

BOOLEAN
NTAPI
FsRtlIsEcpAcknowledged(
    _In_ PVOID EcpContext)
{
    PECP_HEADER Header = FsRtlpEcpHeaderOf(EcpContext);

    PAGED_CODE();

    if (Header == NULL) return FALSE;

    return FlagOn(Header->Flags, FSRTL_ECP_FLAG_ACKNOWLEDGED) ? TRUE : FALSE;
}

BOOLEAN
NTAPI
FsRtlIsEcpFromUserMode(
    _In_ PVOID EcpContext)
{
    PECP_HEADER Header = FsRtlpEcpHeaderOf(EcpContext);

    PAGED_CODE();

    if (Header == NULL) return FALSE;

    return FlagOn(Header->Flags, FSRTL_ECP_FLAG_USER_MODE) ? TRUE : FALSE;
}

VOID
NTAPI
FsRtlPrepareToReuseEcp(
    _In_ PVOID EcpContext)
{
    PECP_HEADER Header = FsRtlpEcpHeaderOf(EcpContext);

    PAGED_CODE();

    if (Header == NULL) return;

    ClearFlag(Header->Flags, FSRTL_ECP_FLAG_ACKNOWLEDGED);
}

VOID
NTAPI
FsRtlpMarkCallerEcps(
    _In_ PECP_LIST EcpList)
{
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (!FsRtlpIsEcpList(EcpList)) return;

    for (Entry = EcpList->EcpList.Flink;
         Entry != &EcpList->EcpList;
         Entry = Entry->Flink)
    {
        PECP_HEADER Header = CONTAINING_RECORD(Entry, ECP_HEADER, Links);

        SetFlag(Header->Flags, FSRTL_ECP_FLAG_CALLER_OWNED);
    }
}

VOID
NTAPI
FsRtlpFreeAddedEcps(
    _In_ PECP_LIST EcpList)
{
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (!FsRtlpIsEcpList(EcpList)) return;

    Entry = EcpList->EcpList.Flink;

    while (Entry != &EcpList->EcpList)
    {
        PECP_HEADER Header = CONTAINING_RECORD(Entry, ECP_HEADER, Links);

        Entry = Entry->Flink;

        if (FlagOn(Header->Flags, FSRTL_ECP_FLAG_CALLER_OWNED)) continue;

        RemoveEntryList(&Header->Links);
        ClearFlag(Header->Flags, FSRTL_ECP_FLAG_IN_LIST);
        FsRtlpDestroyEcp(Header);
    }
}

/* EOF */
