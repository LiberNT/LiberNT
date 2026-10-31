/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Detection of writes to translated x64 code in writable executable memory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../amd64emu.h"
#include "bridge.h"

#define CODEWATCH_BITS 15
#define CODEWATCH_SIZE (1UL << CODEWATCH_BITS)
#define CODEWATCH_LIMIT (CODEWATCH_SIZE / 4 * 3)
#define CODEWATCH_IMAGES 32
#define CODEWATCH_RECENT 64
#define CODEWATCH_EXCLUDED 64

typedef struct _CODEWATCH_PAGE
{
    ULONG_PTR Page;
    ULONG Protect;
} CODEWATCH_PAGE;

typedef struct _CODEWATCH_RANGE
{
    ULONG_PTR Start;
    ULONG_PTR End;
} CODEWATCH_RANGE;

static CODEWATCH_PAGE CodeWatchPages[CODEWATCH_SIZE];
static ULONG CodeWatchCount;
static CODEWATCH_RANGE CodeWatchImages[CODEWATCH_IMAGES];
static ULONG CodeWatchNextImage;
static ULONG_PTR CodeWatchRecent[CODEWATCH_RECENT];
static CODEWATCH_RANGE CodeWatchExcluded[CODEWATCH_EXCLUDED];
static ULONG CodeWatchExcludedCount;
static ULONG CodeWatchNextRecent;
static RTL_SRWLOCK CodeWatchLock = RTL_SRWLOCK_INIT;
static volatile LONG CodeWatchHandler;

static
BOOLEAN
CodeWatchWritable(
    _In_ ULONG Protect)
{
    switch (Protect & 0xFF)
    {
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return TRUE;
        default:
            return FALSE;
    }
}

static
BOOLEAN
CodeWatchWritableCode(
    _In_ ULONG Protect)
{
    return (Protect & 0xFF) == PAGE_EXECUTE_READWRITE || (Protect & 0xFF) == PAGE_EXECUTE_WRITECOPY;
}

static
ULONG
CodeWatchSlot(
    _In_ ULONG_PTR Page)
{
    return (ULONG)(((ULONG64)(Page >> PAGE_SHIFT) * 0x9E3779B97F4A7C15ULL) >> (64 - CODEWATCH_BITS));
}

static
ULONG
CodeWatchFind(
    _In_ ULONG_PTR Page)
{
    ULONG Slot = CodeWatchSlot(Page);

    while (CodeWatchPages[Slot].Page != 0)
    {
        if (CodeWatchPages[Slot].Page == Page)
            return Slot;
        Slot = (Slot + 1) & (CODEWATCH_SIZE - 1);
    }

    return CODEWATCH_SIZE;
}

static
VOID
CodeWatchInsert(
    _In_ ULONG_PTR Page,
    _In_ ULONG Protect)
{
    ULONG Slot = CodeWatchSlot(Page);

    while (CodeWatchPages[Slot].Page != 0)
        Slot = (Slot + 1) & (CODEWATCH_SIZE - 1);

    CodeWatchPages[Slot].Page = Page;
    CodeWatchPages[Slot].Protect = Protect;
    CodeWatchCount++;
}

static
VOID
CodeWatchRemove(
    _In_ ULONG Slot)
{
    ULONG Next = Slot;

    CodeWatchPages[Slot].Page = 0;
    CodeWatchCount--;

    for (;;)
    {
        ULONG Home;

        Next = (Next + 1) & (CODEWATCH_SIZE - 1);
        if (CodeWatchPages[Next].Page == 0)
            break;

        Home = CodeWatchSlot(CodeWatchPages[Next].Page);
        if (((Next - Home) & (CODEWATCH_SIZE - 1)) >= ((Next - Slot) & (CODEWATCH_SIZE - 1)))
        {
            CodeWatchPages[Slot] = CodeWatchPages[Next];
            CodeWatchPages[Next].Page = 0;
            Slot = Next;
        }
    }
}

static
LONG
NTAPI
CodeWatchFault(
    _In_ PEXCEPTION_POINTERS Pointers)
{
    PEXCEPTION_RECORD Record = Pointers->ExceptionRecord;
    MEMORY_BASIC_INFORMATION Information;
    ULONG_PTR Page;
    ULONG Protect = 0, Old, Slot, Index;
    BOOLEAN Recent = FALSE;
    PVOID Base;
    SIZE_T Size;

    if (Record->ExceptionCode != STATUS_ACCESS_VIOLATION || Record->NumberParameters < 2 ||
        Record->ExceptionInformation[0] != 1)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    Page = Record->ExceptionInformation[1] & ~(ULONG_PTR)(PAGE_SIZE - 1);

    RtlAcquireSRWLockExclusive(&CodeWatchLock);
    Slot = CodeWatchFind(Page);
    if (Slot != CODEWATCH_SIZE)
    {
        Base = (PVOID)Page;
        Size = PAGE_SIZE;
        if (NT_SUCCESS(ZwProtectVirtualMemory(NtCurrentProcess(), &Base, &Size, CodeWatchPages[Slot].Protect, &Old)))
        {
            Protect = CodeWatchPages[Slot].Protect;
            CodeWatchRemove(Slot);
            CodeWatchRecent[CodeWatchNextRecent] = Page;
            CodeWatchNextRecent = (CodeWatchNextRecent + 1) % CODEWATCH_RECENT;
        }
    }
    else
    {
        for (Index = 0; Index < CODEWATCH_RECENT; Index++)
        {
            if (CodeWatchRecent[Index] == Page)
            {
                Recent = TRUE;
                break;
            }
        }
    }
    RtlReleaseSRWLockExclusive(&CodeWatchLock);

    if (Protect == 0)
    {
        if (Recent &&
            NT_SUCCESS(ZwQueryVirtualMemory(NtCurrentProcess(), (PVOID)Page, MemoryBasicInformation, &Information,
                                            sizeof(Information), NULL)) &&
            Information.State == MEM_COMMIT && CodeWatchWritable(Information.Protect))
        {
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    Felix86NtInvalidate(Page, Page + PAGE_SIZE);
    return EXCEPTION_CONTINUE_EXECUTION;
}

static
VOID
CodeWatchProtect(
    _In_ ULONG_PTR Start,
    _In_ ULONG_PTR End,
    _In_ ULONG Protect)
{
    ULONG_PTR Page;
    PVOID Base = (PVOID)Start;
    SIZE_T Size = End - Start;
    ULONG Old;

    /* TODO: kernel-mode writes into watched pages fail instead of invalidating the translations */
    /* TODO: NtQueryVirtualMemory reports the watched protection instead of the guest protection */
    if (CodeWatchCount + (Size >> PAGE_SHIFT) > CODEWATCH_LIMIT)
        return;

    if (!NT_SUCCESS(ZwProtectVirtualMemory(NtCurrentProcess(), &Base, &Size, (Protect & ~0xFFUL) | PAGE_EXECUTE_READ,
                                           &Old)))
    {
        return;
    }

    for (Page = Start; Page < End; Page += PAGE_SIZE)
        CodeWatchInsert(Page, Protect);
}

void
Felix86NtProtectCode(
    uint64_t Start,
    uint64_t End)
{
    ULONG_PTR Page = (ULONG_PTR)Start & ~(ULONG_PTR)(PAGE_SIZE - 1);
    ULONG_PTR Last = ((ULONG_PTR)End + PAGE_SIZE - 1) & ~(ULONG_PTR)(PAGE_SIZE - 1);

    if (!CodeWatchHandler)
    {
        PVOID Handler = RtlAddVectoredExceptionHandler(1, CodeWatchFault);

        if (Handler == NULL)
            return;
        if (InterlockedCompareExchange(&CodeWatchHandler, 1, 0) != 0)
            RtlRemoveVectoredExceptionHandler(Handler);
    }

    while (Page < Last)
    {
        MEMORY_BASIC_INFORMATION Information;
        ULONG_PTR Next = 0;
        ULONG Index;

        RtlAcquireSRWLockShared(&CodeWatchLock);
        for (Index = 0; Index < CODEWATCH_IMAGES; Index++)
        {
            if (Page >= CodeWatchImages[Index].Start && Page < CodeWatchImages[Index].End)
            {
                Next = CodeWatchImages[Index].End;
                break;
            }
        }
        for (Index = 0; Next == 0 && Index < CodeWatchExcludedCount; Index++)
        {
            if (Page >= CodeWatchExcluded[Index].Start && Page < CodeWatchExcluded[Index].End)
                Next = CodeWatchExcluded[Index].End;
        }
        if (Next == 0 && CodeWatchFind(Page) != CODEWATCH_SIZE)
            Next = Page + PAGE_SIZE;
        RtlReleaseSRWLockShared(&CodeWatchLock);

        if (Next != 0)
        {
            Page = Next;
            continue;
        }

        RtlAcquireSRWLockExclusive(&CodeWatchLock);
        if (!NT_SUCCESS(ZwQueryVirtualMemory(NtCurrentProcess(), (PVOID)Page, MemoryBasicInformation, &Information,
                                             sizeof(Information), NULL)))
        {
            RtlReleaseSRWLockExclusive(&CodeWatchLock);
            return;
        }

        Next = (ULONG_PTR)Information.BaseAddress + Information.RegionSize;
        if (Information.State == MEM_COMMIT && CodeWatchWritableCode(Information.Protect))
        {
            CodeWatchProtect(Page, min(Next, Last), Information.Protect);
        }
        else if (Information.Type == MEM_IMAGE && !CodeWatchWritable(Information.Protect))
        {
            CodeWatchImages[CodeWatchNextImage].Start = (ULONG_PTR)Information.BaseAddress;
            CodeWatchImages[CodeWatchNextImage].End = Next;
            CodeWatchNextImage = (CodeWatchNextImage + 1) % CODEWATCH_IMAGES;
        }
        RtlReleaseSRWLockExclusive(&CodeWatchLock);

        Page = Next;
    }
}

uint32_t
Felix86NtForgetCode(
    uint64_t Base,
    uint64_t Size)
{
    ULONG_PTR Start = (ULONG_PTR)Base & ~(ULONG_PTR)(PAGE_SIZE - 1);
    ULONG_PTR End = ((ULONG_PTR)Base + (ULONG_PTR)Size + PAGE_SIZE - 1) & ~(ULONG_PTR)(PAGE_SIZE - 1);
    ULONG_PTR Page;
    ULONG First = 0, Slot;

    RtlAcquireSRWLockExclusive(&CodeWatchLock);
    if (((End - Start) >> PAGE_SHIFT) > CODEWATCH_SIZE)
    {
        for (Slot = 0; Slot < CODEWATCH_SIZE; Slot++)
        {
            while (CodeWatchPages[Slot].Page != 0 && CodeWatchPages[Slot].Page >= Start &&
                   CodeWatchPages[Slot].Page < End)
            {
                if (CodeWatchPages[Slot].Page == Start)
                    First = CodeWatchPages[Slot].Protect;
                CodeWatchRemove(Slot);
            }
        }
    }
    else
    {
        for (Page = Start; Page < End && CodeWatchCount != 0; Page += PAGE_SIZE)
        {
            Slot = CodeWatchFind(Page);
            if (Slot == CODEWATCH_SIZE)
                continue;
            if (Page == Start)
                First = CodeWatchPages[Slot].Protect;
            CodeWatchRemove(Slot);
        }
    }
    for (Slot = 0; Slot < CODEWATCH_IMAGES; Slot++)
    {
        if (CodeWatchImages[Slot].Start < End && CodeWatchImages[Slot].End > Start)
            CodeWatchImages[Slot].Start = CodeWatchImages[Slot].End = 0;
    }
    RtlReleaseSRWLockExclusive(&CodeWatchLock);

    return First;
}

void
Felix86NtExcludeCode(
    uint64_t Base,
    uint64_t Size)
{
    RtlAcquireSRWLockExclusive(&CodeWatchLock);
    if (CodeWatchExcludedCount < CODEWATCH_EXCLUDED)
    {
        CodeWatchExcluded[CodeWatchExcludedCount].Start = (ULONG_PTR)Base & ~(ULONG_PTR)(PAGE_SIZE - 1);
        CodeWatchExcluded[CodeWatchExcludedCount].End = ((ULONG_PTR)Base + (ULONG_PTR)Size + PAGE_SIZE - 1) &
                                                        ~(ULONG_PTR)(PAGE_SIZE - 1);
        CodeWatchExcludedCount++;
    }
    RtlReleaseSRWLockExclusive(&CodeWatchLock);
}

int
Felix86NtCodeWatchFault(
    void *Pointers)
{
    return CodeWatchFault(Pointers) == EXCEPTION_CONTINUE_EXECUTION;
}
