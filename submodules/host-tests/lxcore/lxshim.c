/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NT and lxcore services for lxcore memory code running on the NVS harness
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "lxhost.h"
#include <stdarg.h>

#define LX_HOST_HANDLES 256

TEST_WORLD *LxHostWorld;
_Thread_local PMI_ADDRESS_SPACE LxHostSpace;
_Thread_local PLX_THREAD LxHostThread;
_Thread_local jmp_buf *LxHostSehFrame;
_Thread_local NTSTATUS LxHostSehCode;
_Thread_local ULONG64 LxHostQueries;
_Thread_local ULONG64 LxHostCommittedScan;
ULONG LxHostLogged;
BOOLEAN LxHostQuiet;
static _Thread_local int LxHostThreadId;
static PVOID LxHostHandles[LX_HOST_HANDLES];
static int LxHostFileTypeTag;
static struct _OBJECT_TYPE_HOST *LxHostFileType = (struct _OBJECT_TYPE_HOST *)&LxHostFileTypeTag;
POBJECT_TYPE *IoFileObjectType = &LxHostFileType;

static
void
LxHostObjectInit(PLX_HOST_OBJECT Object, ULONG Kind)
{
    Object->RefCount = 1;
    Object->Kind = Kind;
}

static
HANDLE
LxHostInsertHandle(PVOID Object)
{
    ULONG Index;

    for (Index = 0; Index < LX_HOST_HANDLES; Index++)
    {
        if (LxHostHandles[Index] == NULL)
        {
            LxHostHandles[Index] = Object;
            return (HANDLE)(ULONG_PTR)((Index + 1) * 4);
        }
    }
    abort();
}

static
PVOID
LxHostHandleObject(HANDLE Handle)
{
    ULONG_PTR Index = (ULONG_PTR)Handle / 4;

    if ((Index == 0) || (Index > LX_HOST_HANDLES))
        return NULL;
    return LxHostHandles[Index - 1];
}

HANDLE
LxHostCreateFile(PLX_HOST_FILE File, TEST_FILE *Data)
{
    memset(File, 0, sizeof(*File));
    LxHostObjectInit(&File->Object.Header, LX_HOST_OBJECT_FILE);
    File->Data = Data;
    File->Object.SectionObjectPointer = &File->Pointers;
    return LxHostInsertHandle(File);
}

void
ObReferenceObject(PVOID Object)
{
    __sync_fetch_and_add(&((PLX_HOST_OBJECT)Object)->RefCount, 1);
}

void
ObDereferenceObject(PVOID Object)
{
    PLX_HOST_OBJECT Header = Object;

    if (__sync_fetch_and_add(&Header->RefCount, -1) != 1)
        return;
    if (Header->Kind == LX_HOST_OBJECT_SECTION)
    {
        PLX_HOST_SECTION Section = Object;

        MiSegmentDereference(Section->Segment);
        if (Section->File != NULL)
            ObDereferenceObject(Section->File);
        free(Section);
    }
    else if (Header->Kind == LX_HOST_OBJECT_FILE)
    {
        PLX_HOST_FILE File = Object;

        if (File->DataSegment != NULL)
            MiSegmentDereference(File->DataSegment);
        File->DataSegment = NULL;
    }
}

NTSTATUS
ObOpenObjectByPointer(PVOID Object, ULONG Attributes, PVOID AccessState, ACCESS_MASK Access, POBJECT_TYPE Type,
                      KPROCESSOR_MODE Mode, PHANDLE Handle)
{
    ObReferenceObject(Object);
    *Handle = LxHostInsertHandle(Object);
    return STATUS_SUCCESS;
}

NTSTATUS
ObReferenceObjectByHandle(HANDLE Handle, ACCESS_MASK Access, POBJECT_TYPE Type, KPROCESSOR_MODE Mode,
                          PVOID *Object, PVOID Information)
{
    PLX_HOST_OBJECT Header = LxHostHandleObject(Handle);

    if (Header == NULL)
        return STATUS_INVALID_HANDLE;
    if ((Type == LxHostFileType) && (Header->Kind != LX_HOST_OBJECT_FILE))
        return STATUS_OBJECT_TYPE_MISMATCH;
    ObReferenceObject(Header);
    *Object = Header;
    return STATUS_SUCCESS;
}

NTSTATUS
ZwClose(HANDLE Handle)
{
    ULONG_PTR Index = (ULONG_PTR)Handle / 4;
    PVOID Object = LxHostHandleObject(Handle);

    if (Object == NULL)
        return STATUS_INVALID_HANDLE;
    LxHostHandles[Index - 1] = NULL;
    ObDereferenceObject(Object);
    return STATUS_SUCCESS;
}

PKTHREAD
KeGetCurrentThread(void)
{
    return &LxHostThreadId;
}

void
KeEnterCriticalRegion(void)
{
}

void
KeLeaveCriticalRegion(void)
{
}

KIRQL
KeGetCurrentIrql(void)
{
    return PASSIVE_LEVEL;
}

BOOLEAN
KeAreAllApcsDisabled(void)
{
    return FALSE;
}

void
KeInitializeEvent(PKEVENT Event, EVENT_TYPE Type, BOOLEAN State)
{
    pthread_mutex_init(&Event->Mutex, NULL);
    pthread_cond_init(&Event->Condition, NULL);
    Event->Signaled = State;
}

NTSTATUS
KeWaitForSingleObject(PVOID Object, KWAIT_REASON Reason, KPROCESSOR_MODE Mode, BOOLEAN Alertable,
                      PLARGE_INTEGER Timeout)
{
    PKEVENT Event = Object;

    pthread_mutex_lock(&Event->Mutex);
    while (!Event->Signaled)
        pthread_cond_wait(&Event->Condition, &Event->Mutex);
    Event->Signaled = 0;
    pthread_mutex_unlock(&Event->Mutex);
    return STATUS_SUCCESS;
}

LONG
KeSetEvent(PKEVENT Event, LONG Increment, BOOLEAN Wait)
{
    pthread_mutex_lock(&Event->Mutex);
    Event->Signaled = 1;
    pthread_cond_signal(&Event->Condition);
    pthread_mutex_unlock(&Event->Mutex);
    return 0;
}

void
KeStackAttachProcess(PKPROCESS Process, PKAPC_STATE ApcState)
{
    ApcState->SavedSpace = LxHostSpace;
    LxHostSpace = Process->Space;
    WorldAttach(LxHostWorld, 0, LxHostSpace);
}

void
KeUnstackDetachProcess(PKAPC_STATE ApcState)
{
    LxHostSpace = ApcState->SavedSpace;
    WorldAttach(LxHostWorld, 0, LxHostSpace);
}

static
NTSTATUS
LxHostAllocate(PVOID *BaseAddress, PSIZE_T RegionSize, ULONG AllocationType, ULONG Protect, ULONG64 Lowest,
               ULONG64 Highest)
{
    ULONG64 Base = (ULONG64)(ULONG_PTR)*BaseAddress, Size = *RegionSize;
    ULONG Protection, Type = 0;
    NTSTATUS Status;

    if (!MiAllocationProtectionFromWin32(Protect, &Protection) ||
        (MI_PROT_IS_COPY(Protection) && (AllocationType & MEM_RESERVE)))
        return STATUS_INVALID_PAGE_PROTECTION;
    if (Size == 0)
        return STATUS_INVALID_PARAMETER;
    if (AllocationType & MEM_COMMIT)
        Type |= MI_MEM_COMMIT;
    if (AllocationType & MEM_RESERVE)
        Type |= MI_MEM_RESERVE;
    if (AllocationType & MEM_TOP_DOWN)
        Type |= MI_MEM_TOP_DOWN;
    if (Base == 0)
        Type |= MI_MEM_RESERVE;

    Status = MiAllocateVirtualMemoryBounded(LxHostSpace, &Base, &Size, Type, Protection, Lowest,
                                            (Base != 0) ? LxHostSpace->HighestVa : Highest, 0, FALSE);
    if (NT_SUCCESS(Status))
    {
        *BaseAddress = (PVOID)(ULONG_PTR)Base;
        *RegionSize = (SIZE_T)Size;
    }
    return Status;
}

NTSTATUS
ZwAllocateVirtualMemory(HANDLE Process, PVOID *BaseAddress, ULONG_PTR ZeroBits, PSIZE_T RegionSize,
                        ULONG AllocationType, ULONG Protect)
{
    return LxHostAllocate(BaseAddress, RegionSize, AllocationType, Protect, 0, LX_HOST_USER_LIMIT - 1);
}

NTSTATUS
ZwAllocateVirtualMemoryEx(HANDLE Process, PVOID *BaseAddress, PSIZE_T RegionSize, ULONG AllocationType,
                          ULONG PageProtection, PMEM_EXTENDED_PARAMETER ExtendedParameters,
                          ULONG ExtendedParameterCount)
{
    ULONG64 Lowest = 0, Highest = LX_HOST_USER_LIMIT - 1;
    ULONG Index;

    for (Index = 0; Index < ExtendedParameterCount; Index++)
    {
        PMEM_ADDRESS_REQUIREMENTS Requirements = ExtendedParameters[Index].Pointer;

        if (ExtendedParameters[Index].Type != MemExtendedParameterAddressRequirements || Requirements == NULL)
            return STATUS_INVALID_PARAMETER;
        Lowest = (ULONG64)(ULONG_PTR)Requirements->LowestStartingAddress;
        if (Requirements->HighestEndingAddress != NULL)
            Highest = (ULONG64)(ULONG_PTR)Requirements->HighestEndingAddress;
    }
    return LxHostAllocate(BaseAddress, RegionSize, AllocationType, PageProtection, Lowest, Highest);
}

PLX_TMPFS_INODE
LxTmpfsFileInode(PLX_FILE File)
{
    return NULL;
}

ULONG64
LxTmpfsInodeSize(PLX_TMPFS_INODE Inode)
{
    return 0;
}

LONG
LxTmpfsInodeRemoveRange(PLX_TMPFS_INODE Inode, ULONG64 Start, ULONG64 End)
{
    return 0;
}

NTSTATUS
LxTmpfsMapSection(PLX_TMPFS_INODE Inode, BOOLEAN Shared, PHANDLE Section, PULONG64 Size)
{
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
ZwFreeVirtualMemory(HANDLE Process, PVOID *BaseAddress, PSIZE_T RegionSize, ULONG FreeType)
{
    ULONG64 Base = (ULONG64)(ULONG_PTR)*BaseAddress, Size = *RegionSize;
    NTSTATUS Status;

    Status = MiFreeVirtualMemory(LxHostSpace, &Base, &Size, (FreeType == MEM_DECOMMIT) ? MI_MEM_DECOMMIT : MI_MEM_RELEASE);
    if (NT_SUCCESS(Status))
    {
        *BaseAddress = (PVOID)(ULONG_PTR)Base;
        *RegionSize = (SIZE_T)Size;
    }
    return Status;
}

NTSTATUS
ZwProtectVirtualMemory(HANDLE Process, PVOID *BaseAddress, PSIZE_T RegionSize, ULONG NewProtect, PULONG OldProtect)
{
    ULONG64 Base = (ULONG64)(ULONG_PTR)*BaseAddress, Size = *RegionSize;
    ULONG Protection, Old = 0;
    NTSTATUS Status;

    Status = MiProtectChangeFromWin32(NewProtect, &Protection);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = MiProtectVirtualMemoryEx(LxHostSpace, &Base, &Size, Protection, &Old, FALSE);
    if (NT_SUCCESS(Status))
    {
        *BaseAddress = (PVOID)(ULONG_PTR)Base;
        *RegionSize = (SIZE_T)Size;
    }
    if ((NT_SUCCESS(Status) || (Status == STATUS_SECTION_PROTECTION)) && (OldProtect != NULL))
        *OldProtect = MiProtectionToWin32(Old);
    return Status;
}

NTSTATUS
ZwQueryVirtualMemory(HANDLE Process, PVOID BaseAddress, MEMORY_INFORMATION_CLASS Class, PVOID Information,
                     SIZE_T Length, PSIZE_T ReturnLength)
{
    PMEMORY_BASIC_INFORMATION Basic = Information;
    MI_MEMORY_INFORMATION Info;
    ULONG64 Lowest = LxHostSpace->LowestVa, Query;
    NTSTATUS Status;

    LxHostQueries++;
    if (Class == MemoryWorkingSetExList)
    {
        PULONG_PTR Entries = Information;
        SIZE_T Index;

        for (Index = 0; Index < Length / (2 * sizeof(ULONG_PTR)); Index++)
        {
            MI_WORKING_SET_EX_INFORMATION Ws;

            Entries[2 * Index + 1] = (NT_SUCCESS(MiQueryWorkingSetEx(LxHostSpace, Entries[2 * Index], &Ws)) && Ws.Valid) ? 1 : 0;
        }
        return STATUS_SUCCESS;
    }
    if ((Class != MemoryBasicInformation) || (Length < sizeof(*Basic)))
        return STATUS_INVALID_PARAMETER;
    memset(Basic, 0, sizeof(*Basic));
    Query = ((ULONG64)(ULONG_PTR)BaseAddress < Lowest) ? Lowest : (ULONG64)(ULONG_PTR)BaseAddress;
    Status = MiQueryVirtualMemory(LxHostSpace, Query, &Info);
    if (!NT_SUCCESS(Status))
        return Status;
    if ((Query != (ULONG64)(ULONG_PTR)BaseAddress) && (Info.State != MI_MEM_FREE))
    {
        Info.BaseAddress = Lowest;
        Info.RegionSize = 0;
        Info.State = MI_MEM_FREE;
    }
    if ((Info.State == MI_MEM_FREE) && (Query != (ULONG64)(ULONG_PTR)BaseAddress))
    {
        Info.RegionSize += Info.BaseAddress - ((ULONG64)(ULONG_PTR)BaseAddress & ~(ULONG64)(PAGE_SIZE - 1));
        Info.BaseAddress = (ULONG64)(ULONG_PTR)BaseAddress & ~(ULONG64)(PAGE_SIZE - 1);
    }
    Basic->BaseAddress = (PVOID)(ULONG_PTR)Info.BaseAddress;
    Basic->RegionSize = (SIZE_T)Info.RegionSize;
    if (ReturnLength != NULL)
        *ReturnLength = sizeof(*Basic);
    if (Info.State == MI_MEM_FREE)
    {
        Basic->State = MEM_FREE;
        Basic->Protect = PAGE_NOACCESS;
        return STATUS_SUCCESS;
    }
    Basic->AllocationBase = (PVOID)(ULONG_PTR)Info.AllocationBase;
    Basic->AllocationProtect = MiProtectionToWin32(Info.AllocationProtect);
    Basic->State = (Info.State == MI_MEM_COMMIT) ? MEM_COMMIT : MEM_RESERVE;
    if (Info.State == MI_MEM_COMMIT)
        LxHostCommittedScan += Info.RegionSize / PAGE_SIZE;
    Basic->Protect = (Info.State == MI_MEM_COMMIT) ? MiProtectionToWin32(Info.Protect) : 0;
    Basic->Type = (Info.Type == MI_MEM_PRIVATE) ? MEM_PRIVATE : MEM_MAPPED;
    return STATUS_SUCCESS;
}

NTSTATUS
ZwFlushInstructionCache(HANDLE Process, PVOID BaseAddress, SIZE_T Length)
{
    return STATUS_SUCCESS;
}

NTSTATUS
ZwFlushVirtualMemory(HANDLE Process, PVOID *BaseAddress, PSIZE_T RegionSize, PIO_STATUS_BLOCK Iosb)
{
    Iosb->Status = STATUS_SUCCESS;
    Iosb->Information = 0;
    return STATUS_SUCCESS;
}

SIZE_T LxHostWsMinimum = 50 * PAGE_SIZE;
ULONG LxHostQuotaRaises;

NTSTATUS
ZwLockVirtualMemory(HANDLE Process, PVOID *BaseAddress, PSIZE_T RegionSize, ULONG MapType)
{
    ULONG64 Start = (ULONG64)(ULONG_PTR)*BaseAddress & ~(ULONG64)(PAGE_SIZE - 1);
    ULONG64 End = ((ULONG64)(ULONG_PTR)*BaseAddress + *RegionSize + PAGE_SIZE - 1) & ~(ULONG64)(PAGE_SIZE - 1);
    NTSTATUS Status;

    MI_RW_ACQUIRE_EXCLUSIVE(&LxHostSpace->Lock);
    if (LxHostSpace->LockedPages + ((End - Start) >> PAGE_SHIFT) - MiLockRangePages(LxHostSpace, Start, End) >
        LxHostWsMinimum / PAGE_SIZE)
        Status = STATUS_WORKING_SET_QUOTA;
    else
        Status = MiLockRangeAdd(LxHostSpace, Start, End);
    MI_RW_RELEASE_EXCLUSIVE(&LxHostSpace->Lock);
    return Status;
}

NTSTATUS
ZwUnlockVirtualMemory(HANDLE Process, PVOID *BaseAddress, PSIZE_T RegionSize, ULONG MapType)
{
    ULONG64 Start = (ULONG64)(ULONG_PTR)*BaseAddress & ~(ULONG64)(PAGE_SIZE - 1);
    ULONG64 End = ((ULONG64)(ULONG_PTR)*BaseAddress + *RegionSize + PAGE_SIZE - 1) & ~(ULONG64)(PAGE_SIZE - 1);
    ULONG64 Locked;

    MI_RW_ACQUIRE_EXCLUSIVE(&LxHostSpace->Lock);
    Locked = MiLockRangePages(LxHostSpace, Start, End);
    MI_RW_RELEASE_EXCLUSIVE(&LxHostSpace->Lock);
    if (Locked != ((End - Start) >> PAGE_SHIFT))
        MiTrimRange(LxHostSpace, Start, End);
    MI_RW_ACQUIRE_EXCLUSIVE(&LxHostSpace->Lock);
    MiLockRangeRemove(LxHostSpace, Start, End);
    MI_RW_RELEASE_EXCLUSIVE(&LxHostSpace->Lock);
    return (Locked != ((End - Start) >> PAGE_SHIFT)) ? STATUS_NOT_LOCKED : STATUS_SUCCESS;
}

NTSTATUS
ZwQueryInformationProcess(HANDLE Process, PROCESSINFOCLASS Class, PVOID Information, ULONG Length,
                          PULONG ReturnLength)
{
    PQUOTA_LIMITS Quota = Information;

    if ((Class != ProcessQuotaLimits) || (Length < sizeof(*Quota)))
        return STATUS_INVALID_PARAMETER;
    memset(Quota, 0, sizeof(*Quota));
    Quota->MinimumWorkingSetSize = LxHostWsMinimum;
    Quota->MaximumWorkingSetSize = (LxHostWsMinimum > 345 * PAGE_SIZE) ? LxHostWsMinimum : 345 * PAGE_SIZE;
    if (ReturnLength != NULL)
        *ReturnLength = sizeof(*Quota);
    return STATUS_SUCCESS;
}

NTSTATUS
ZwSetInformationProcess(HANDLE Process, PROCESSINFOCLASS Class, PVOID Information, ULONG Length)
{
    PQUOTA_LIMITS Quota = Information;

    if ((Class != ProcessQuotaLimits) || (Length < sizeof(*Quota)) ||
        (Quota->MinimumWorkingSetSize > Quota->MaximumWorkingSetSize))
        return STATUS_INVALID_PARAMETER;
    LxHostWsMinimum = Quota->MinimumWorkingSetSize;
    LxHostQuotaRaises++;
    return STATUS_SUCCESS;
}

NTSTATUS
ZwCreateSection(PHANDLE SectionHandle, ACCESS_MASK Access, POBJECT_ATTRIBUTES Attributes, PLARGE_INTEGER MaximumSize,
                ULONG PageProtection, ULONG AllocationAttributes, HANDLE FileHandle)
{
    PLX_HOST_SECTION Section;
    PLX_HOST_FILE File = NULL;
    ULONG Protection;
    NTSTATUS Status;

    if (!MiProtectionFromWin32(PageProtection, &Protection))
        return STATUS_INVALID_PAGE_PROTECTION;
    if (FileHandle != NULL)
    {
        File = LxHostHandleObject(FileHandle);
        if ((File == NULL) || (File->Object.Header.Kind != LX_HOST_OBJECT_FILE))
            return STATUS_INVALID_HANDLE;
        if (File->Data->Size == 0)
            return STATUS_MAPPED_FILE_SIZE_ZERO;
    }
    else if ((MaximumSize == NULL) || (MaximumSize->QuadPart <= 0))
    {
        return STATUS_INVALID_PARAMETER_4;
    }

    Section = calloc(1, sizeof(*Section));
    LxHostObjectInit(&Section->Header, LX_HOST_OBJECT_SECTION);
    Section->Protection = Protection;
    if (File != NULL)
    {
        if (File->DataSegment == NULL)
        {
            Status = MiSegmentCreate(&LxHostWorld->System, MiSegmentDataFile, File->Data->Size,
                                     MI_PROT_EXECUTE_READWRITE, &TestFileOps, File->Data, NULL, 0, &File->DataSegment);
            if (!NT_SUCCESS(Status))
            {
                free(Section);
                return Status;
            }
        }
        MiSegmentReference(File->DataSegment);
        ObReferenceObject(File);
        Section->Segment = File->DataSegment;
        Section->File = File;
        Section->Size = File->Data->Size;
    }
    else
    {
        Section->Size = (ULONG64)MaximumSize->QuadPart;
        Status = MiSegmentCreate(&LxHostWorld->System, MiSegmentPageFileBacked,
                                 LX_ALIGN_UP(Section->Size, PAGE_SIZE), MI_PROT_EXECUTE_READWRITE, NULL, NULL, NULL, 0,
                                 &Section->Segment);
        if (!NT_SUCCESS(Status))
        {
            free(Section);
            return Status;
        }
    }
    *SectionHandle = LxHostInsertHandle(Section);
    return STATUS_SUCCESS;
}

NTSTATUS
ZwMapViewOfSection(HANDLE SectionHandle, HANDLE Process, PVOID *BaseAddress, ULONG_PTR ZeroBits, SIZE_T CommitSize,
                   PLARGE_INTEGER SectionOffset, PSIZE_T ViewSize, SECTION_INHERIT Inherit, ULONG AllocationType,
                   ULONG Protect)
{
    PLX_HOST_SECTION Section = LxHostHandleObject(SectionHandle);
    ULONG64 Offset = (SectionOffset != NULL) ? (ULONG64)SectionOffset->QuadPart : 0;
    ULONG64 Base = (ULONG64)(ULONG_PTR)*BaseAddress, Size = *ViewSize, Delta;
    ULONG Protection;
    NTSTATUS Status;

    if ((Section == NULL) || (Section->Header.Kind != LX_HOST_OBJECT_SECTION))
        return STATUS_INVALID_HANDLE;
    if ((Inherit != ViewShare) && (Inherit != ViewUnmap))
        return STATUS_INVALID_PARAMETER;
    if (!MiProtectionFromWin32(Protect, &Protection))
        return STATUS_INVALID_PAGE_PROTECTION;

    Delta = Offset & (LxHostSpace->Granularity - 1);
    if ((Offset >= Section->Size) || (Size > ~0ULL - Offset) ||
        (!(AllocationType & MEM_RESERVE) && (Size > Section->Size - Offset)))
        return STATUS_INVALID_VIEW_SIZE;
    Offset -= Delta;
    if (Size == 0)
        Size = Section->Size - Offset;
    else
        Size += Delta;
    Base &= ~(LxHostSpace->Granularity - 1);

    Status = MiMapViewEx(LxHostSpace, Section->Segment, &Base, Offset, &Size, Protection,
                         (AllocationType & MEM_TOP_DOWN) ? MI_MEM_TOP_DOWN : 0, LX_HOST_USER_LIMIT - 1,
                         Section->Protection, (BOOLEAN)(Inherit == ViewShare));
    if (!NT_SUCCESS(Status))
        return Status;
    *BaseAddress = (PVOID)(ULONG_PTR)Base;
    *ViewSize = (SIZE_T)Size;
    if (SectionOffset != NULL)
        SectionOffset->QuadPart = (LONG64)Offset;
    return STATUS_SUCCESS;
}

NTSTATUS
ZwUnmapViewOfSection(HANDLE Process, PVOID BaseAddress)
{
    return MiUnmapView(LxHostSpace, (ULONG64)(ULONG_PTR)BaseAddress);
}

NTSTATUS
ZwQuerySection(HANDLE SectionHandle, SECTION_INFORMATION_CLASS Class, PVOID Information, SIZE_T Length,
               PSIZE_T ReturnLength)
{
    PLX_HOST_SECTION Section = LxHostHandleObject(SectionHandle);
    PSECTION_BASIC_INFORMATION Basic = Information;

    if ((Section == NULL) || (Section->Header.Kind != LX_HOST_OBJECT_SECTION))
        return STATUS_INVALID_HANDLE;
    if ((Class != SectionBasicInformation) || (Length < sizeof(*Basic)))
        return STATUS_INVALID_PARAMETER;
    Basic->BaseAddress = NULL;
    Basic->Attributes = SEC_COMMIT;
    Basic->Size.QuadPart = (LONG64)Section->Size;
    if (ReturnLength != NULL)
        *ReturnLength = sizeof(*Basic);
    return STATUS_SUCCESS;
}

NTSTATUS
ZwQueryInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK Iosb, PVOID Information, ULONG Length,
                       FILE_INFORMATION_CLASS Class)
{
    PLX_HOST_FILE File = LxHostHandleObject(FileHandle);
    PFILE_STANDARD_INFORMATION Standard = Information;

    if ((File == NULL) || (File->Object.Header.Kind != LX_HOST_OBJECT_FILE))
        return STATUS_INVALID_HANDLE;
    if ((Class != FileStandardInformation) || (Length < sizeof(*Standard)))
        return STATUS_INVALID_PARAMETER;
    memset(Standard, 0, sizeof(*Standard));
    Standard->EndOfFile.QuadPart = (LONG64)File->Data->Size;
    Standard->AllocationSize.QuadPart = (LONG64)LX_ALIGN_UP(File->Data->Size, PAGE_SIZE);
    Standard->NumberOfLinks = 1;
    Iosb->Status = STATUS_SUCCESS;
    Iosb->Information = sizeof(*Standard);
    return STATUS_SUCCESS;
}

NTSTATUS
ZwSetInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK Iosb, PVOID Information, ULONG Length,
                     FILE_INFORMATION_CLASS Class)
{
    return STATUS_INVALID_PARAMETER;
}

LARGE_INTEGER
MmGetPhysicalAddress(PVOID BaseAddress)
{
    LARGE_INTEGER Address = { 0 };

    return Address;
}

NTSTATUS
ZwWriteFile(HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK Iosb,
            PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key)
{
    PLX_HOST_FILE File = LxHostHandleObject(FileHandle);
    ULONG64 Offset = (ULONG64)ByteOffset->QuadPart;

    if ((File == NULL) || (File->Object.Header.Kind != LX_HOST_OBJECT_FILE))
        return STATUS_INVALID_HANDLE;
    if (Offset < File->Data->Size)
        memcpy(File->Data->Data + Offset, Buffer, (size_t)min(Length, File->Data->Size - Offset));
    Iosb->Status = STATUS_SUCCESS;
    Iosb->Information = Length;
    return STATUS_SUCCESS;
}

static
__attribute__((noreturn))
void
LxHostRaise(NTSTATUS Status)
{
    if (LxHostSehFrame == NULL)
    {
        fprintf(stderr, "unhandled user access fault 0x%08x\n", Status);
        abort();
    }
    LxHostSehCode = Status;
    longjmp(*LxHostSehFrame, 1);
}

void
LxHostCopy(void *Destination, const void *Source, SIZE_T Length)
{
    NTSTATUS Status;

    if (Length == 0)
        return;
    if ((ULONG64)(ULONG_PTR)Destination < LX_HOST_USER_LIMIT)
    {
        if ((ULONG64)(ULONG_PTR)Source < LX_HOST_USER_LIMIT)
        {
            PUCHAR Bounce = malloc(Length);

            Status = UserRead(LxHostWorld, 0, (ULONG64)(ULONG_PTR)Source, Bounce, Length);
            if (NT_SUCCESS(Status))
                Status = UserWrite(LxHostWorld, 0, (ULONG64)(ULONG_PTR)Destination, Bounce, Length);
            free(Bounce);
        }
        else
        {
            Status = UserWrite(LxHostWorld, 0, (ULONG64)(ULONG_PTR)Destination, Source, Length);
        }
    }
    else if ((ULONG64)(ULONG_PTR)Source < LX_HOST_USER_LIMIT)
    {
        Status = UserRead(LxHostWorld, 0, (ULONG64)(ULONG_PTR)Source, Destination, Length);
    }
    else
    {
        memcpy(Destination, Source, Length);
        return;
    }
    if (!NT_SUCCESS(Status))
        LxHostRaise(Status);
}

PVOID
LxAlloc(SIZE_T Size)
{
    return calloc(1, Size);
}

VOID
LxFree(PVOID Buffer)
{
    free(Buffer);
}

PVOID
LxAllocPaged(SIZE_T Size)
{
    return calloc(1, Size);
}

VOID
LxFreePaged(PVOID Buffer)
{
    free(Buffer);
}

LONG
LxStatusToErrno(NTSTATUS Status)
{
    switch (Status)
    {
        case STATUS_NO_MEMORY:
        case STATUS_COMMITMENT_LIMIT:
        case STATUS_CONFLICTING_ADDRESSES:
            return LX_ENOMEM;
        case STATUS_ACCESS_VIOLATION:
            return LX_EFAULT;
        case STATUS_INVALID_PAGE_PROTECTION:
        case STATUS_SECTION_PROTECTION:
            return LX_EACCES;
        default:
            return LX_EINVAL;
    }
}

static
LONG64
LxHostUserWrite(ULONG64 Destination, const VOID *Source, SIZE_T Length)
{
    PUCHAR Zero = NULL;
    NTSTATUS Status;

    if (Length == 0)
        return 0;
    if (Source == NULL)
        Source = Zero = calloc(1, Length);
    for (;;)
    {
        Status = UserWrite(LxHostWorld, 0, Destination, Source, Length);
        if (NT_SUCCESS(Status) || !LxMmFaultIn(Destination, Length, TRUE))
            break;
    }
    free(Zero);
    return NT_SUCCESS(Status) ? 0 : -LX_EFAULT;
}

LONG64
LxCopyToUser(ULONG64 Destination, const VOID *Source, SIZE_T Length)
{
    return LxHostUserWrite(Destination, Source, Length);
}

LONG64
LxZeroUser(ULONG64 Destination, SIZE_T Length)
{
    return LxHostUserWrite(Destination, NULL, Length);
}

LONG64
LxCopyFromUser(PVOID Destination, ULONG64 Source, SIZE_T Length)
{
    NTSTATUS Status;

    if (Length == 0)
        return 0;
    for (;;)
    {
        Status = UserRead(LxHostWorld, 0, Source, Destination, Length);
        if (NT_SUCCESS(Status) || !LxMmFaultIn(Source, Length, FALSE))
            break;
    }
    return NT_SUCCESS(Status) ? 0 : -LX_EFAULT;
}

LIST_ENTRY LxProcessList = { &LxProcessList, &LxProcessList };
FAST_MUTEX LxProcessListLock;

VOID
LxProcessReference(PLX_PROCESS Process)
{
}

VOID
LxProcessDereference(PLX_PROCESS Process)
{
}

HANDLE
PsGetCurrentProcessId(VOID)
{
    return NULL;
}

NTSTATUS
PsLookupProcessByProcessId(HANDLE ProcessId, PEPROCESS *Process)
{
    return STATUS_INVALID_PARAMETER;
}

BOOLEAN
LxCapable(PLX_PROCESS Process, ULONG Capability)
{
    return (Process->CapEffective & (1ULL << Capability)) != 0;
}

NTSTATUS
LxFillRandom(PUCHAR Buffer, ULONG Length)
{
    memset(Buffer, 0, Length);
    return STATUS_SUCCESS;
}

VOID
LxArchMmapRndBits(PULONG Minimum, PULONG Maximum)
{
    *Minimum = 18;
    *Maximum = 30;
}

ULONG64
LxArchStackRndMask(VOID)
{
    return 0x3FFFF;
}

PLX_THREAD
LxCurrentThread(VOID)
{
    return LxHostThread;
}

VOID
LxFileReference(PLX_FILE File)
{
    __sync_fetch_and_add(&File->RefCount, 1);
}

VOID
LxFileDereference(PLX_FILE File)
{
    __sync_fetch_and_add(&File->RefCount, -1);
}

PLX_FILE
LxFdGet(PLX_PROCESS Process, LONG Fd)
{
    return NULL;
}

LONG64
LxFileReadKernel(PLX_FILE File, PVOID Buffer, ULONG Length, ULONG64 Offset)
{
    PLX_HOST_FILE Host = LxHostHandleObject(File->Handle);
    ULONG64 Size;

    if ((Host == NULL) || (Host->Object.Header.Kind != LX_HOST_OBJECT_FILE))
        return -LX_EBADF;
    Size = Host->Data->Size;
    if (Offset >= Size)
        return 0;
    if (Length > Size - Offset)
        Length = (ULONG)(Size - Offset);
    memcpy(Buffer, Host->Data->Data + Offset, Length);
    return Length;
}

VOID
LxIpcShmVmaOpen(PVOID Section)
{
}

VOID
LxIpcShmVmaClose(PVOID Section)
{
}

void
LxHostLog(const char *Format, ...)
{
    char Converted[512];
    const char *In = Format;
    char *Out = Converted;
    va_list Args;

    LxHostLogged++;
    if (LxHostQuiet)
        return;
    while ((*In != '\0') && (Out < Converted + sizeof(Converted) - 4))
    {
        if ((In[0] == '%') && (In[1] == 'I') && (In[2] == '6') && (In[3] == '4'))
        {
            *Out++ = '%';
            *Out++ = 'l';
            *Out++ = 'l';
            In += 4;
        }
        else if ((In[0] == '%') && (In[1] == 'I'))
        {
            *Out++ = '%';
            *Out++ = 'z';
            In += 2;
        }
        else
        {
            *Out++ = *In++;
        }
    }
    *Out = '\0';
    fprintf(stderr, "lxcore: ");
    va_start(Args, Format);
    vfprintf(stderr, Converted, Args);
    va_end(Args);
}

ULONG64
LxHugePageSize(VOID)
{
    return 0;
}

LONG
LxHugeStoreReserve(PLX_HUGE_STORE Store, ULONG64 First, ULONG64 Count, BOOLEAN Surplus)
{
    return -LX_ENOMEM;
}

PVOID
LxHugeStorePage(PLX_HUGE_STORE Store, ULONG64 Index)
{
    return NULL;
}

VOID
LxHugeStoreFault(PLX_HUGE_STORE Store, ULONG64 Index)
{
}

NTSTATUS
LxHugeMapPage(PVOID Section, HANDLE ProcessHandle, PVOID *Base, ULONG Protect)
{
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
LxHugeCopyPage(PVOID From, PVOID To)
{
    return STATUS_NOT_SUPPORTED;
}

PLX_FILE
LxHugeFileCreate(PCSTR Path)
{
    return NULL;
}

BOOLEAN
LxDrmIsFile(PLX_FILE File)
{
    return FALSE;
}

LONG64
LxDrmMmap(PLX_PROCESS Process, PLX_FILE File, ULONG64 Address, ULONG64 Length, ULONG Prot, ULONG Flags, ULONG64 Offset)
{
    return -LX_ENODEV;
}
