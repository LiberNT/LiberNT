/*
 * PROJECT:     LiberNT NT User-Mode DLL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Secure memory cache callbacks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

typedef struct _RTLP_SECURE_MEMORY_CACHE_ENTRY
{
    LIST_ENTRY Links;
    PRTL_SECURE_MEMORY_CACHE_CALLBACK Callback;
} RTLP_SECURE_MEMORY_CACHE_ENTRY, *PRTLP_SECURE_MEMORY_CACHE_ENTRY;

static RTL_SRWLOCK RtlpSecureMemoryCacheLock = RTL_SRWLOCK_INIT;
static LIST_ENTRY RtlpSecureMemoryCacheList = { &RtlpSecureMemoryCacheList, &RtlpSecureMemoryCacheList };

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlRegisterSecureMemoryCacheCallback(
    _In_ PRTL_SECURE_MEMORY_CACHE_CALLBACK Callback)
{
    PRTLP_SECURE_MEMORY_CACHE_ENTRY Entry;

    if (Callback == NULL)
        return STATUS_INVALID_PARAMETER;

    Entry = RtlAllocateHeap(RtlGetProcessHeap(), 0, sizeof(*Entry));
    if (Entry == NULL)
        return STATUS_NO_MEMORY;

    Entry->Callback = Callback;

    RtlAcquireSRWLockExclusive(&RtlpSecureMemoryCacheLock);
    InsertTailList(&RtlpSecureMemoryCacheList, &Entry->Links);
    RtlReleaseSRWLockExclusive(&RtlpSecureMemoryCacheLock);

    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlDeregisterSecureMemoryCacheCallback(
    _In_ PRTL_SECURE_MEMORY_CACHE_CALLBACK Callback)
{
    PRTLP_SECURE_MEMORY_CACHE_ENTRY Found = NULL;
    PRTLP_SECURE_MEMORY_CACHE_ENTRY Entry;
    PLIST_ENTRY Link;

    RtlAcquireSRWLockExclusive(&RtlpSecureMemoryCacheLock);
    for (Link = RtlpSecureMemoryCacheList.Flink; Link != &RtlpSecureMemoryCacheList; Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, RTLP_SECURE_MEMORY_CACHE_ENTRY, Links);
        if (Entry->Callback == Callback)
        {
            RemoveEntryList(&Entry->Links);
            Found = Entry;
            break;
        }
    }
    RtlReleaseSRWLockExclusive(&RtlpSecureMemoryCacheLock);

    if (Found == NULL)
        return STATUS_NOT_FOUND;

    RtlFreeHeap(RtlGetProcessHeap(), 0, Found);
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
RtlFlushSecureMemoryCache(
    _In_ PVOID MemoryCache,
    _In_opt_ SIZE_T MemoryLength)
{
    PRTLP_SECURE_MEMORY_CACHE_ENTRY Entry;
    BOOLEAN Flushed = FALSE;
    PLIST_ENTRY Link;

    RtlAcquireSRWLockShared(&RtlpSecureMemoryCacheLock);
    for (Link = RtlpSecureMemoryCacheList.Flink; Link != &RtlpSecureMemoryCacheList; Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, RTLP_SECURE_MEMORY_CACHE_ENTRY, Links);
        if (Entry->Callback(MemoryCache, MemoryLength))
            Flushed = TRUE;
    }
    RtlReleaseSRWLockShared(&RtlpSecureMemoryCacheLock);

    return Flushed;
}
