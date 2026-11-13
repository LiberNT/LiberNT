/*
 * PROJECT:     LiberNT Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Baseline LoongArch64 sequenced lists
 */

/*
 * LoongArch64 has no 128-bit compare-and-exchange, so the 16-byte
 * SLIST_HEADER cannot be updated with a single atomic step. The second word
 * (Region) holds the next-entry pointer in its upper bits; its low nibble is
 * free because the header is 16-byte aligned and entries are 16-byte aligned.
 * A lock bit there turns the header update into a 64-bit compare-and-exchange
 * while the first word (Depth and Sequence) is published under the lock.
 */

#include <rtl.h>
#include <intrin.h>

#define LA64_SLIST_HEADER_TYPE 0x1ULL
#define LA64_SLIST_LOCK_BIT    0x4ULL
#define LA64_SLIST_RESERVED    0x8ULL
#define LA64_SLIST_POINTER     (~0xFULL)

C_ASSERT(sizeof(SLIST_HEADER) == 16);
C_ASSERT(__alignof(SLIST_HEADER) == 16);
C_ASSERT(FIELD_OFFSET(SLIST_HEADER, Region) == 8);

BOOLEAN RtlpUse16ByteSLists = TRUE;

static
DECLSPEC_NORETURN
VOID
RtlpLoongArchSListFail(_In_ ULONG Code)
{
    __fastfail(Code);
}

static
ULONGLONG
RtlpLoongArchAcquireSList(_Inout_ PSLIST_HEADER SListHead)
{
    ULONGLONG Region, LockedRegion;

    if ((SListHead == NULL) || ((ULONG_PTR)SListHead & 15))
        RtlpLoongArchSListFail(FAST_FAIL_INVALID_ARG);

    for (;;)
    {
        Region = __atomic_load_n(&SListHead->Region, __ATOMIC_ACQUIRE);
        if (Region & LA64_SLIST_LOCK_BIT)
        {
            YieldProcessor();
            continue;
        }

        if (!(Region & LA64_SLIST_HEADER_TYPE) ||
            (Region & LA64_SLIST_RESERVED))
        {
            RtlpLoongArchSListFail(FAST_FAIL_CORRUPT_LIST_ENTRY);
        }

        LockedRegion = Region | LA64_SLIST_LOCK_BIT;
        if (__atomic_compare_exchange_n(&SListHead->Region,
                                        &Region,
                                        LockedRegion,
                                        FALSE,
                                        __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED))
        {
            return Region;
        }
    }
}

static
VOID
RtlpLoongArchPublishSList(
    _Inout_ PSLIST_HEADER SListHead,
    _In_ const SLIST_HEADER *NewHeader)
{
    ULONGLONG Region = NewHeader->Region & ~LA64_SLIST_LOCK_BIT;

    __atomic_store_n(&SListHead->Alignment,
                     NewHeader->Alignment,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&SListHead->Region, Region, __ATOMIC_RELEASE);
}

static
PSLIST_ENTRY
RtlpLoongArchDecodeSList(_In_ ULONGLONG Region)
{
    return (PSLIST_ENTRY)(ULONG_PTR)(Region & LA64_SLIST_POINTER);
}

VOID
NTAPI
RtlInitializeSListHead(_Out_ PSLIST_HEADER SListHead)
{
    if ((SListHead == NULL) || ((ULONG_PTR)SListHead & 15))
        RtlpLoongArchSListFail(FAST_FAIL_INVALID_ARG);

    __atomic_store_n(&SListHead->Alignment, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&SListHead->Region,
                     LA64_SLIST_HEADER_TYPE,
                     __ATOMIC_RELEASE);
}

PSLIST_ENTRY
NTAPI
RtlFirstEntrySList(_In_ const SLIST_HEADER *SListHead)
{
    PSLIST_HEADER MutableHead = (PSLIST_HEADER)SListHead;
    SLIST_HEADER Header;

    Header.Region = RtlpLoongArchAcquireSList(MutableHead);
    Header.Alignment = __atomic_load_n(&SListHead->Alignment,
                                       __ATOMIC_RELAXED);
    RtlpLoongArchPublishSList(MutableHead, &Header);
    return RtlpLoongArchDecodeSList(Header.Region);
}

WORD
NTAPI
RtlQueryDepthSList(_In_ PSLIST_HEADER SListHead)
{
    SLIST_HEADER Header;

    Header.Region = RtlpLoongArchAcquireSList(SListHead);
    Header.Alignment = __atomic_load_n(&SListHead->Alignment,
                                       __ATOMIC_RELAXED);
    RtlpLoongArchPublishSList(SListHead, &Header);
    return Header.Header16.Depth;
}

PSLIST_ENTRY
FASTCALL
RtlInterlockedPushListSList(
    _Inout_ PSLIST_HEADER SListHead,
    _Inout_ __drv_aliasesMem PSLIST_ENTRY List,
    _Inout_ PSLIST_ENTRY ListEnd,
    _In_ ULONG Count)
{
    SLIST_HEADER Header;
    PSLIST_ENTRY FirstEntry;

    if ((List == NULL) || (ListEnd == NULL) || (Count == 0) ||
        (((ULONG_PTR)List | (ULONG_PTR)ListEnd) & 15))
    {
        RtlpLoongArchSListFail(FAST_FAIL_INVALID_ARG);
    }

    Header.Region = RtlpLoongArchAcquireSList(SListHead);
    Header.Alignment = __atomic_load_n(&SListHead->Alignment,
                                       __ATOMIC_RELAXED);
    FirstEntry = RtlpLoongArchDecodeSList(Header.Region);
    ListEnd->Next = FirstEntry;
    Header.Header16.Depth = (USHORT)(Header.Header16.Depth + Count);
    Header.Header16.Sequence++;
    Header.Header16.HeaderType = 1;
    Header.Header16.Init = 1;
    Header.Header16.Reserved = 0;
    Header.Header16.NextEntry = (ULONG_PTR)List >> 4;
    RtlpLoongArchPublishSList(SListHead, &Header);
    return FirstEntry;
}

PSLIST_ENTRY
NTAPI
RtlInterlockedPushEntrySList(
    _Inout_ PSLIST_HEADER SListHead,
    _Inout_ __drv_aliasesMem PSLIST_ENTRY SListEntry)
{
    return RtlInterlockedPushListSList(SListHead,
                                       SListEntry,
                                       SListEntry,
                                       1);
}

PSLIST_ENTRY
NTAPI
RtlInterlockedPopEntrySList(_Inout_ PSLIST_HEADER SListHead)
{
    SLIST_HEADER Header;
    PSLIST_ENTRY FirstEntry, NextEntry;

    Header.Region = RtlpLoongArchAcquireSList(SListHead);
    Header.Alignment = __atomic_load_n(&SListHead->Alignment,
                                       __ATOMIC_RELAXED);
    FirstEntry = RtlpLoongArchDecodeSList(Header.Region);
    if (FirstEntry == NULL)
    {
        RtlpLoongArchPublishSList(SListHead, &Header);
        return NULL;
    }

    NextEntry = FirstEntry->Next;
    if ((ULONG_PTR)NextEntry & 15)
        RtlpLoongArchSListFail(FAST_FAIL_CORRUPT_LIST_ENTRY);

    Header.Header16.Depth--;
    Header.Header16.Sequence++;
    Header.Header16.HeaderType = 1;
    Header.Header16.Init = 1;
    Header.Header16.Reserved = 0;
    Header.Header16.NextEntry = (ULONG_PTR)NextEntry >> 4;
    RtlpLoongArchPublishSList(SListHead, &Header);
    return FirstEntry;
}

PSLIST_ENTRY
NTAPI
RtlInterlockedFlushSList(_Inout_ PSLIST_HEADER SListHead)
{
    SLIST_HEADER Header;
    PSLIST_ENTRY FirstEntry;

    Header.Region = RtlpLoongArchAcquireSList(SListHead);
    Header.Alignment = __atomic_load_n(&SListHead->Alignment,
                                       __ATOMIC_RELAXED);
    FirstEntry = RtlpLoongArchDecodeSList(Header.Region);
    if (FirstEntry != NULL)
    {
        Header.Header16.Depth = 0;
        Header.Header16.Sequence++;
        Header.Header16.HeaderType = 1;
        Header.Header16.Init = 1;
        Header.Header16.Reserved = 0;
        Header.Header16.NextEntry = 0;
    }

    RtlpLoongArchPublishSList(SListHead, &Header);
    return FirstEntry;
}
