/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux memory system calls on top of NT virtual memory
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxcore.h"

#define NDEBUG
#include <debug.h>

static
VOID
LxMmLock(
    _In_ PLX_PROCESS Process)
{
    KeWaitForSingleObject(&Process->MmLock, Executive, KernelMode, FALSE, NULL);
}

static
VOID
LxMmUnlock(
    _In_ PLX_PROCESS Process)
{
    KeSetEvent(&Process->MmLock, IO_NO_INCREMENT, FALSE);
}

ULONG
LxProtToNt(
    _In_ ULONG Prot)
{
    switch (Prot & (LX_PROT_READ | LX_PROT_WRITE | LX_PROT_EXEC))
    {
        case LX_PROT_NONE:
            return PAGE_NOACCESS;
        case LX_PROT_READ:
            return PAGE_READONLY;
        case LX_PROT_WRITE:
        case LX_PROT_READ | LX_PROT_WRITE:
            return PAGE_READWRITE;
        case LX_PROT_EXEC:
        case LX_PROT_READ | LX_PROT_EXEC:
            return PAGE_EXECUTE_READ;
        default:
            return PAGE_EXECUTE_READWRITE;
    }
}

static
NTSTATUS
LxVmAllocate(
    _Inout_ PULONG64 Base,
    _In_ ULONG64 Size,
    _In_ ULONG Type,
    _In_ ULONG Protect)
{
    PVOID Address = (PVOID)(ULONG_PTR)*Base;
    SIZE_T RegionSize = (SIZE_T)Size;
    NTSTATUS Status;

    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &Address, 0, &RegionSize, Type, Protect);
    if (NT_SUCCESS(Status))
        *Base = (ULONG64)(ULONG_PTR)Address;
    return Status;
}

static
NTSTATUS
LxVmFree(
    _In_ ULONG64 Base,
    _In_ ULONG64 Size,
    _In_ ULONG Type)
{
    PVOID Address = (PVOID)(ULONG_PTR)Base;
    SIZE_T RegionSize = (SIZE_T)Size;

    return ZwFreeVirtualMemory(NtCurrentProcess(), &Address, &RegionSize, Type);
}

static
NTSTATUS
LxVmProtect(
    _In_ ULONG64 Base,
    _In_ ULONG64 Size,
    _In_ ULONG Protect)
{
    PVOID Address = (PVOID)(ULONG_PTR)Base;
    SIZE_T RegionSize = (SIZE_T)Size;
    ULONG OldProtect;

    return ZwProtectVirtualMemory(NtCurrentProcess(), &Address, &RegionSize, Protect, &OldProtect);
}

static
NTSTATUS
LxVmQuery(
    _In_ ULONG64 Address,
    _Out_ PMEMORY_BASIC_INFORMATION Information)
{
    return ZwQueryVirtualMemory(NtCurrentProcess(),
                                (PVOID)(ULONG_PTR)Address,
                                MemoryBasicInformation,
                                Information,
                                sizeof(*Information),
                                NULL);
}

static
VOID
LxVmFlushCode(
    _In_ ULONG64 Base,
    _In_ ULONG64 Size)
{
    ZwFlushInstructionCache(NtCurrentProcess(), (PVOID)(ULONG_PTR)Base, (SIZE_T)Size);
}

static
VOID
LxVmReleaseIfEmpty(
    _In_ ULONG64 AllocationBase)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = AllocationBase;

    for (;;)
    {
        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)) ||
            ((ULONG64)(ULONG_PTR)Information.AllocationBase != AllocationBase) ||
            (Information.State == MEM_FREE))
        {
            break;
        }
        if (Information.State == MEM_COMMIT)
            return;
        Cursor = (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize;
    }

    LxVmFree(AllocationBase, 0, MEM_RELEASE);
}

static
LONG
LxRangeState(
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _Out_ PBOOLEAN AnyFree,
    _Out_ PBOOLEAN AnyUsed)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = Address, End = Address + Length;

    *AnyFree = FALSE;
    *AnyUsed = FALSE;
    while (Cursor < End)
    {
        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
            return -LX_ENOMEM;
        if (Information.State == MEM_FREE)
            *AnyFree = TRUE;
        else if (Information.Type != MEM_PRIVATE)
            return -LX_ENOMEM;
        else
            *AnyUsed = TRUE;
        Cursor = (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize;
    }
    return 0;
}

static
LONG
LxMmFill(
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ PLX_FILE File,
    _In_ ULONG64 Offset)
{
    PUCHAR Bounce = LxAllocPaged(LX_IO_CHUNK);
    ULONG64 Done = 0;
    LONG64 Read;
    LONG64 Error;

    if (Bounce == NULL)
        return -LX_ENOMEM;

    while (Done < Length)
    {
        ULONG Chunk = (ULONG)min(Length - Done, LX_IO_CHUNK);

        Read = LxFileReadKernel(File, Bounce, Chunk, Offset + Done);
        if (Read < 0)
        {
            LxFreePaged(Bounce);
            return (LONG)Read;
        }
        if (Read == 0)
            break;
        Error = LxCopyToUser(Address + Done, Bounce, (SIZE_T)Read);
        if (Error != 0)
        {
            LxFreePaged(Bounce);
            return (LONG)Error;
        }
        Done += (ULONG64)Read;
        if ((ULONG64)Read < Chunk)
            break;
    }

    LxFreePaged(Bounce);
    return 0;
}

LONG64
LxMmMap(
    _In_ PLX_PROCESS Process,
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ ULONG Prot,
    _In_ ULONG Flags,
    _In_opt_ PLX_FILE File,
    _In_ ULONG64 Offset)
{
    BOOLEAN Fixed = (Flags & (LX_MAP_FIXED | LX_MAP_FIXED_NOREPLACE)) != 0;
    ULONG NtProtect = LxProtToNt(Prot);
    BOOLEAN NeedFill = (File != NULL);
    BOOLEAN AnyFree, AnyUsed;
    ULONG64 Base;
    NTSTATUS Status;
    LONG Error;

    if ((Length == 0) || (Offset & (LX_PAGE_SIZE - 1)))
        return -LX_EINVAL;
    if (Fixed && (Address & (LX_PAGE_SIZE - 1)))
        return -LX_EINVAL;

    Length = LX_ALIGN_UP(Length, LX_PAGE_SIZE);
    if (Length == 0)
        return -LX_ENOMEM;

    LxMmLock(Process);

    if (!Fixed)
    {
        Base = 0;
        if ((Prot == LX_PROT_NONE) && !NeedFill)
            Status = LxVmAllocate(&Base, Length, MEM_RESERVE, PAGE_NOACCESS);
        else
            Status = LxVmAllocate(&Base, Length, MEM_RESERVE | MEM_COMMIT, NeedFill ? PAGE_READWRITE : NtProtect);
        if (!NT_SUCCESS(Status))
        {
            LxMmUnlock(Process);
            return -LX_ENOMEM;
        }
    }
    else
    {
        Base = Address;
        Error = LxRangeState(Address, Length, &AnyFree, &AnyUsed);
        if (Error != 0)
        {
            LxMmUnlock(Process);
            return Error;
        }
        if (AnyUsed && (Flags & LX_MAP_FIXED_NOREPLACE))
        {
            LxMmUnlock(Process);
            return -LX_EEXIST;
        }
        if (AnyFree && AnyUsed)
        {
            LxMmUnlock(Process);
            return -LX_ENOMEM;
        }

        if (AnyFree)
        {
            ULONG64 Reserve = Address;

            Status = LxVmAllocate(&Reserve, Length, MEM_RESERVE, PAGE_NOACCESS);
            if (!NT_SUCCESS(Status))
            {
                LxMmUnlock(Process);
                return -LX_ENOMEM;
            }
        }
        else
        {
            LxVmFree(Address, Length, MEM_DECOMMIT);
        }

        if ((Prot != LX_PROT_NONE) || NeedFill)
        {
            ULONG64 Commit = Address;

            Status = LxVmAllocate(&Commit, Length, MEM_COMMIT, NeedFill ? PAGE_READWRITE : NtProtect);
            if (!NT_SUCCESS(Status))
            {
                LxMmUnlock(Process);
                return -LX_ENOMEM;
            }
        }
    }

    if (NeedFill)
    {
        Error = LxMmFill(Base, Length, File, Offset);
        if ((Error == 0) && (NtProtect != PAGE_READWRITE))
            Status = LxVmProtect(Base, Length, NtProtect);
        if (Error != 0)
        {
            LxVmFree(Base, Length, MEM_DECOMMIT);
            LxMmUnlock(Process);
            return Error;
        }
    }

    if (Prot & LX_PROT_EXEC)
        LxVmFlushCode(Base, Length);

    LxMmUnlock(Process);
    return (LONG64)Base;
}

LONG64
LxMmUnmap(
    _In_ ULONG64 Address,
    _In_ ULONG64 Length)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor, End, Bases[16];
    ULONG BaseCount = 0, Index;

    if ((Address & (LX_PAGE_SIZE - 1)) || (Length == 0))
        return -LX_EINVAL;

    Length = LX_ALIGN_UP(Length, LX_PAGE_SIZE);
    End = Address + Length;
    if (End < Address)
        return -LX_EINVAL;

    for (Cursor = Address; Cursor < End;)
    {
        ULONG64 RegionEnd;

        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)))
            break;

        RegionEnd = min((ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize, End);
        if ((Information.State != MEM_FREE) && (Information.Type == MEM_PRIVATE))
        {
            ULONG64 AllocationBase = (ULONG64)(ULONG_PTR)Information.AllocationBase;

            if (Information.State == MEM_COMMIT)
                LxVmFree(Cursor, RegionEnd - Cursor, MEM_DECOMMIT);

            for (Index = 0; Index < BaseCount; Index++)
            {
                if (Bases[Index] == AllocationBase)
                    break;
            }
            if ((Index == BaseCount) && (BaseCount < RTL_NUMBER_OF(Bases)))
                Bases[BaseCount++] = AllocationBase;
        }
        Cursor = RegionEnd;
    }

    for (Index = 0; Index < BaseCount; Index++)
        LxVmReleaseIfEmpty(Bases[Index]);

    return 0;
}

LONG64
LxMmProtect(
    _In_ ULONG64 Address,
    _In_ ULONG64 Length,
    _In_ ULONG Prot)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG NtProtect = LxProtToNt(Prot);
    ULONG64 Cursor, End;

    if (Address & (LX_PAGE_SIZE - 1))
        return -LX_EINVAL;
    if (Length == 0)
        return 0;

    End = Address + LX_ALIGN_UP(Length, LX_PAGE_SIZE);
    for (Cursor = Address; Cursor < End;)
    {
        ULONG64 RegionEnd;
        NTSTATUS Status = STATUS_SUCCESS;

        if (!NT_SUCCESS(LxVmQuery(Cursor, &Information)) || (Information.State == MEM_FREE))
            return -LX_ENOMEM;

        RegionEnd = min((ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize, End);
        if (Information.State == MEM_COMMIT)
        {
            Status = LxVmProtect(Cursor, RegionEnd - Cursor, NtProtect);
        }
        else if (Prot != LX_PROT_NONE)
        {
            ULONG64 Commit = Cursor;

            Status = LxVmAllocate(&Commit, RegionEnd - Cursor, MEM_COMMIT, NtProtect);
        }
        if (!NT_SUCCESS(Status))
            return -LxStatusToErrno(Status);
        Cursor = RegionEnd;
    }

    if (Prot & LX_PROT_EXEC)
        LxVmFlushCode(Address, End - Address);
    return 0;
}

VOID
LxMmReset(
    _In_ PLX_PROCESS Process)
{
    MEMORY_BASIC_INFORMATION Information;
    ULONG64 Cursor = 0;

    while (NT_SUCCESS(LxVmQuery(Cursor, &Information)))
    {
        ULONG64 Next = (ULONG64)(ULONG_PTR)Information.BaseAddress + Information.RegionSize;

        if ((Information.State != MEM_FREE) && (Information.Type == MEM_PRIVATE))
            LxVmFree((ULONG64)(ULONG_PTR)Information.AllocationBase, 0, MEM_RELEASE);
        if (Next <= Cursor)
            break;
        Cursor = Next;
    }

    Process->BrkBase = 0;
    Process->BrkCurrent = 0;
    Process->BrkCommitted = 0;
    Process->BrkLimit = 0;
    Process->BrkReserved = FALSE;
}

LX_SYSCALL(LxSysMmap)
{
    ULONG Flags = (ULONG)Call->Args[3];
    PLX_FILE File = NULL;
    LONG64 Result;

    if (!(Flags & LX_MAP_ANONYMOUS))
    {
        File = LxFdGet(Call->Process, (LONG)Call->Args[4]);
        if (File == NULL)
            return -LX_EBADF;
        if (((File->Handle == NULL) && (File->Data == NULL)) || File->Directory || File->NtPipe)
        {
            BOOLEAN Zero = (File->Type == LX_S_IFCHR) && (File->Rdev == ((1 << 8) | 5));

            LxFileDereference(File);
            if (!Zero)
                return -LX_ENODEV;
            File = NULL;
        }
    }

    Result = LxMmMap(Call->Process, Call->Args[0], Call->Args[1], (ULONG)Call->Args[2], Flags, File, Call->Args[5]);
    if (File != NULL)
        LxFileDereference(File);
    return Result;
}

LX_SYSCALL(LxSysMunmap)
{
    LONG64 Result;

    LxMmLock(Call->Process);
    Result = LxMmUnmap(Call->Args[0], Call->Args[1]);
    LxMmUnlock(Call->Process);
    return Result;
}

LX_SYSCALL(LxSysMprotect)
{
    LONG64 Result;

    LxMmLock(Call->Process);
    Result = LxMmProtect(Call->Args[0], Call->Args[1], (ULONG)Call->Args[2]);
    LxMmUnlock(Call->Process);
    return Result;
}

LX_SYSCALL(LxSysMremap)
{
    ULONG64 Old = Call->Args[0];
    ULONG64 OldLength = LX_ALIGN_UP(Call->Args[1], LX_PAGE_SIZE);
    ULONG64 NewLength = LX_ALIGN_UP(Call->Args[2], LX_PAGE_SIZE);
    ULONG Flags = (ULONG)Call->Args[3];
    MEMORY_BASIC_INFORMATION Information;
    LONG64 New, Result;

    if ((Old & (LX_PAGE_SIZE - 1)) || (NewLength == 0) || (Flags & ~LX_MREMAP_MAYMOVE))
        return -LX_EINVAL;

    if (NewLength <= OldLength)
    {
        if (NewLength < OldLength)
        {
            LxMmLock(Call->Process);
            LxMmUnmap(Old + NewLength, OldLength - NewLength);
            LxMmUnlock(Call->Process);
        }
        return (LONG64)Old;
    }

    if (!(Flags & LX_MREMAP_MAYMOVE))
        return -LX_ENOMEM;

    if (!NT_SUCCESS(LxVmQuery(Old, &Information)) || (Information.State != MEM_COMMIT))
        return -LX_EFAULT;

    New = LxMmMap(Call->Process, 0, NewLength, LX_PROT_READ | LX_PROT_WRITE, LX_MAP_PRIVATE | LX_MAP_ANONYMOUS, NULL, 0);
    if (New < 0)
        return New;

    Result = 0;
    _SEH2_TRY
    {
        RtlCopyMemory((PVOID)(ULONG_PTR)New, (PVOID)(ULONG_PTR)Old, (SIZE_T)OldLength);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Result = -LX_EFAULT;
    }
    _SEH2_END;

    LxMmLock(Call->Process);
    if (Result != 0)
    {
        LxMmUnmap((ULONG64)New, NewLength);
        LxMmUnlock(Call->Process);
        return Result;
    }
    if (Information.Protect != PAGE_READWRITE)
        LxVmProtect((ULONG64)New, NewLength, Information.Protect);
    LxMmUnmap(Old, OldLength);
    LxMmUnlock(Call->Process);
    return New;
}

LX_SYSCALL(LxSysBrk)
{
    PLX_PROCESS Process = Call->Process;
    ULONG64 Requested = Call->Args[0];
    ULONG64 NewCommitted, Result;
    NTSTATUS Status;

    LxMmLock(Process);

    if (!Process->BrkReserved && (Process->BrkBase != 0))
    {
        ULONG64 Base = Process->BrkBase;

        Process->BrkReserved = TRUE;
        Status = LxVmAllocate(&Base, LX_BRK_RESERVE, MEM_RESERVE, PAGE_NOACCESS);
        if (NT_SUCCESS(Status) && (Base == Process->BrkBase))
            Process->BrkLimit = Process->BrkBase + LX_BRK_RESERVE;
        else
            Process->BrkLimit = Process->BrkBase;
    }

    Result = Process->BrkCurrent;
    if ((Requested < Process->BrkBase) || (Requested > Process->BrkLimit))
    {
        LxMmUnlock(Process);
        return (LONG64)Result;
    }

    NewCommitted = LX_ALIGN_UP(Requested, LX_PAGE_SIZE);
    if (NewCommitted > Process->BrkCommitted)
    {
        ULONG64 Commit = Process->BrkCommitted;

        Status = LxVmAllocate(&Commit, NewCommitted - Process->BrkCommitted, MEM_COMMIT, PAGE_READWRITE);
        if (!NT_SUCCESS(Status))
        {
            LxMmUnlock(Process);
            return (LONG64)Result;
        }
    }
    else if (NewCommitted < Process->BrkCommitted)
    {
        LxVmFree(NewCommitted, Process->BrkCommitted - NewCommitted, MEM_DECOMMIT);
    }

    Process->BrkCommitted = NewCommitted;
    Process->BrkCurrent = Requested;
    LxMmUnlock(Process);
    return (LONG64)Requested;
}
