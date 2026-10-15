/*
 * PROJECT:     LiberNT Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Low fragmentation front end of the RTL heap
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <rtl.h>
#include <heap.h>

#define NDEBUG
#include <debug.h>

#define RTLP_LFH_SIGNATURE 0x4846434C
#define RTLP_LFH_BLOCK_SIGNATURE 0x4B42434C
#define RTLP_LFH_RESERVE 0x1A000
#define RTLP_LFH_USER_BLOCK_BYTES 0x1000
#define RTLP_LFH_MIN_SLOTS 16
#define RTLP_LFH_USAGE_ALLOC 0x21
#define RTLP_LFH_USAGE_MASK 0x1F
#define RTLP_LFH_USAGE_LIMIT 0x10
#define RTLP_LFH_USAGE_MAX 0xFFDE

typedef struct _RTLP_LFH_USER_BLOCK
{
    ULONG Signature;
    USHORT Bucket;
    USHORT SlotSize;
    ULONG SlotCount;
    ULONG FreeCount;
    PHEAP Heap;
    LIST_ENTRY Link;
    PVOID FreeList;
    PUCHAR FirstSlot;
} RTLP_LFH_USER_BLOCK, *PRTLP_LFH_USER_BLOCK;

typedef struct _RTLP_LFH_BUCKET
{
    LIST_ENTRY UserBlocks;
    ULONG BlockCount;
} RTLP_LFH_BUCKET, *PRTLP_LFH_BUCKET;

typedef struct _RTLP_LFH_HEAP
{
    ULONG Signature;
    PHEAP Heap;
    SIZE_T ReservedSize;
    SIZE_T CommittedSize;
    PHEAP_LOCK Lock;
    HEAP_LOCK LockStorage;
    RTLP_LFH_BUCKET Buckets[HEAP_LFH_BUCKETS];
} RTLP_LFH_HEAP, *PRTLP_LFH_HEAP;

static
SIZE_T
RtlpLfhSlotSize(SIZE_T Size)
{
    if (!Size)
        Size = 1;
    return ROUND_UP(Size + HEAP_LFH_OVERHEAD, HEAP_ENTRY_SIZE);
}

static
ULONG
RtlpLfhBucketOfSize(SIZE_T Size)
{
    if (Size > HEAP_LFH_MAX_BLOCK - HEAP_LFH_OVERHEAD)
        return 0;
    return (ULONG)(RtlpLfhSlotSize(Size) >> HEAP_ENTRY_SHIFT);
}

static
BOOLEAN
RtlpLfhFlagsEligible(PHEAP Heap, ULONG Flags)
{
    if (!Heap->FrontEndHeapUsageData)
        return FALSE;
    if (Flags & (HEAP_NO_SERIALIZE | HEAP_EXTRA_FLAGS_MASK))
        return FALSE;
    if (Heap->PseudoTagEntries)
        return FALSE;
    return TRUE;
}

static
BOOLEAN
RtlpLfhBucketEnabled(PHEAP Heap, ULONG Bucket)
{
    return (Heap->FrontEndHeapStatusBitmap[Bucket >> 3] & (1 << (Bucket & 7))) != 0;
}

static
SIZE_T
RtlpLfhUsableSize(PRTLP_LFH_USER_BLOCK Block)
{
    return Block->SlotSize - HEAP_LFH_OVERHEAD;
}

static
PHEAP_ENTRY
RtlpLfhSlotEntry(PRTLP_LFH_USER_BLOCK Block, ULONG Index)
{
    return (PHEAP_ENTRY)(Block->FirstSlot + (SIZE_T)Index * Block->SlotSize);
}

static
VOID
RtlpLfhSetSlotOffset(PRTLP_LFH_USER_BLOCK Block, PHEAP_ENTRY Entry)
{
    ULONG Offset = (ULONG)(((PUCHAR)Entry - (PUCHAR)Block) >> HEAP_ENTRY_SHIFT);

    Entry->Size = (USHORT)Offset;
    Entry->PreviousSize = (USHORT)(Offset >> 16);
    Entry->SegmentOffset = HEAP_LFH_INDEX;
}

static
PRTLP_LFH_USER_BLOCK
RtlpLfhBlockFromEntry(PHEAP Heap, PHEAP_ENTRY Entry, PVOID Low, PVOID High)
{
    PRTLP_LFH_USER_BLOCK Block;
    ULONG_PTR Offset;
    SIZE_T Distance;

    if (Entry->SegmentOffset != HEAP_LFH_INDEX)
        return NULL;

    Offset = ((ULONG_PTR)Entry->PreviousSize << 16) | Entry->Size;
    Offset <<= HEAP_ENTRY_SHIFT;
    if (Offset > (ULONG_PTR)Entry || Offset < sizeof(RTLP_LFH_USER_BLOCK))
        return NULL;

    Block = (PRTLP_LFH_USER_BLOCK)((PUCHAR)Entry - Offset);
    if (Low && ((PVOID)Block < Low || (PVOID)(Block + 1) > High))
        return NULL;
    if (Block->Signature != RTLP_LFH_BLOCK_SIGNATURE)
        return NULL;
    if (Heap ? (Block->Heap != Heap) :
        (!Block->Heap || Block->Heap->Signature != HEAP_SIGNATURE || !Block->Heap->FrontEndHeap))
        return NULL;
    if ((PUCHAR)Entry < Block->FirstSlot)
        return NULL;

    Distance = (PUCHAR)Entry - Block->FirstSlot;
    if (Distance % Block->SlotSize || Distance / Block->SlotSize >= Block->SlotCount)
        return NULL;

    return Block;
}

static
PRTLP_LFH_HEAP
RtlpLfhCreate(PHEAP Heap)
{
    PRTLP_LFH_HEAP Lfh;
    PVOID Base = NULL;
    SIZE_T Reserve = RTLP_LFH_RESERVE;
    SIZE_T Commit;
    PVOID CommitBase;
    NTSTATUS Status;
    ULONG Index;

    if (Heap->RequestedFrontEndHeapType == HEAP_LFH_FRONT_END)
        return Heap->FrontEndHeap;
    Heap->RequestedFrontEndHeapType = HEAP_LFH_FRONT_END;

    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &Reserve, MEM_RESERVE, PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
        return NULL;

    CommitBase = Base;
    Commit = ROUND_UP(sizeof(RTLP_LFH_HEAP), PAGE_SIZE);
    Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &CommitBase, 0, &Commit, MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
    {
        Reserve = 0;
        ZwFreeVirtualMemory(NtCurrentProcess(), &Base, &Reserve, MEM_RELEASE);
        return NULL;
    }

    Lfh = Base;
    Lfh->Heap = Heap;
    Lfh->ReservedSize = Reserve;
    Lfh->CommittedSize = Commit;
    Lfh->Lock = &Lfh->LockStorage;
    Status = RtlInitializeHeapLock(&Lfh->Lock);
    if (!NT_SUCCESS(Status))
    {
        Reserve = 0;
        ZwFreeVirtualMemory(NtCurrentProcess(), &Base, &Reserve, MEM_RELEASE);
        return NULL;
    }

    for (Index = 0; Index < HEAP_LFH_BUCKETS; ++Index)
        InitializeListHead(&Lfh->Buckets[Index].UserBlocks);

    Lfh->Signature = RTLP_LFH_SIGNATURE;
    InterlockedExchangePointer(&Heap->FrontEndHeap, Lfh);
    return Lfh;
}

SIZE_T
RtlpLfhUsageDataSize(PHEAP Heap, ULONG Flags, PRTL_HEAP_PARAMETERS Parameters)
{
    UNREFERENCED_PARAMETER(Heap);

    if (RtlpGetMode() != UserMode)
        return 0;
    if (Flags & (HEAP_NO_SERIALIZE | HEAP_TAIL_CHECKING_ENABLED | HEAP_FREE_CHECKING_ENABLED))
        return 0;
    if (RtlpHeapIsSpecial(Flags))
        return 0;
    if (Parameters->CommitRoutine)
        return 0;
    return HEAP_LFH_BUCKETS * sizeof(USHORT);
}

VOID
RtlpLfhInitializeHeap(PHEAP Heap, ULONG Flags, PRTL_HEAP_PARAMETERS Parameters, PVOID UsageData)
{
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(Parameters);

    Heap->FrontEndHeap = NULL;
    Heap->FrontHeapLockCount = 0;
    Heap->FrontEndHeapType = 0;
    Heap->RequestedFrontEndHeapType = 0;
    Heap->FrontEndHeapUsageData = UsageData;
    Heap->FrontEndHeapMaximumIndex = UsageData ? HEAP_LFH_BUCKETS : 0;
    RtlZeroMemory(Heap->FrontEndHeapStatusBitmap, sizeof(Heap->FrontEndHeapStatusBitmap));
    if (UsageData)
        RtlZeroMemory(UsageData, HEAP_LFH_BUCKETS * sizeof(USHORT));
}

VOID
RtlpLfhDestroyHeap(PHEAP Heap)
{
    PRTLP_LFH_HEAP Lfh = Heap->FrontEndHeap;
    PVOID Base;
    SIZE_T Size = 0;

    if (!Lfh)
        return;

    Heap->FrontEndHeap = NULL;
    Heap->FrontEndHeapType = 0;
    RtlDeleteHeapLock(Lfh->Lock);
    Base = Lfh;
    ZwFreeVirtualMemory(NtCurrentProcess(), &Base, &Size, MEM_RELEASE);
}

NTSTATUS
RtlpLfhSetCompatibility(PHEAP Heap, ULONG Value)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Heap->FrontEndHeapType == HEAP_LFH_FRONT_END)
        return (Value == HEAP_LFH_FRONT_END) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;

    if (Value != HEAP_LFH_FRONT_END)
        return STATUS_UNSUCCESSFUL;

    if (!Heap->FrontEndHeapUsageData)
        return STATUS_INVALID_PARAMETER;

    Heap->FrontEndHeapType = HEAP_LFH_FRONT_END;
    return Status;
}

VOID
RtlpLfhNoteBackendAllocate(PHEAP Heap, ULONG Flags, SIZE_T Size)
{
    ULONG Bucket;
    USHORT Usage;

    if (!RtlpLfhFlagsEligible(Heap, Flags))
        return;

    Bucket = RtlpLfhBucketOfSize(Size);
    if (!Bucket || RtlpLfhBucketEnabled(Heap, Bucket))
        return;

    Usage = Heap->FrontEndHeapUsageData[Bucket];
    if (Usage <= RTLP_LFH_USAGE_MAX)
        Heap->FrontEndHeapUsageData[Bucket] = Usage + RTLP_LFH_USAGE_ALLOC;
}

VOID
RtlpLfhNoteBackendFree(PHEAP Heap, PHEAP_ENTRY HeapEntry)
{
    SIZE_T Size;
    ULONG Bucket;
    USHORT Usage;

    if (!Heap->FrontEndHeapUsageData)
        return;
    if (HeapEntry->Flags & (HEAP_ENTRY_EXTRA_PRESENT | HEAP_ENTRY_VIRTUAL_ALLOC))
        return;

    Size = ((SIZE_T)HeapEntry->Size << HEAP_ENTRY_SHIFT) - HeapEntry->UnusedBytes;
    Bucket = RtlpLfhBucketOfSize(Size);
    if (!Bucket || RtlpLfhBucketEnabled(Heap, Bucket))
        return;

    Usage = Heap->FrontEndHeapUsageData[Bucket];
    if (Usage)
        Heap->FrontEndHeapUsageData[Bucket] = Usage - 1;
}

static
PRTLP_LFH_USER_BLOCK
RtlpLfhCreateUserBlock(PHEAP Heap, ULONG Bucket, SIZE_T SlotSize)
{
    PRTLP_LFH_USER_BLOCK Block;
    SIZE_T Header, Bytes, Count;
    ULONG Index;
    PHEAP_ENTRY Entry;
    PVOID *Link;

    Header = ROUND_UP(sizeof(RTLP_LFH_USER_BLOCK), HEAP_ENTRY_SIZE);
    Count = (RTLP_LFH_USER_BLOCK_BYTES - Header) / SlotSize;
    if (Count < RTLP_LFH_MIN_SLOTS)
        Count = RTLP_LFH_MIN_SLOTS;
    Bytes = Header + Count * SlotSize + HEAP_ENTRY_SIZE;
    if (Bytes <= HEAP_LFH_MAX_BLOCK)
        Bytes = HEAP_LFH_MAX_BLOCK + HEAP_ENTRY_SIZE;

    Block = RtlAllocateHeap(Heap, 0, Bytes);
    if (!Block)
        return NULL;

    Block->Bucket = (USHORT)Bucket;
    Block->SlotSize = (USHORT)SlotSize;
    Block->SlotCount = (ULONG)Count;
    Block->FreeCount = (ULONG)Count;
    Block->Heap = Heap;
    Block->FirstSlot = (PUCHAR)Block + Header;
    Block->FreeList = NULL;

    for (Index = 0; Index < Count; ++Index)
    {
        Entry = RtlpLfhSlotEntry(Block, Index);
        RtlZeroMemory(Entry, sizeof(HEAP_ENTRY));
        RtlpLfhSetSlotOffset(Block, Entry);
    }

    Link = &Block->FreeList;
    for (Index = 0; Index < Count; ++Index)
    {
        Entry = RtlpLfhSlotEntry(Block, Index);
        *Link = Entry + 1;
        Link = (PVOID *)(Entry + 1);
    }
    *Link = NULL;
    Block->Signature = RTLP_LFH_BLOCK_SIGNATURE;
    return Block;
}

static
PVOID
RtlpLfhPopSlot(PRTLP_LFH_BUCKET BucketData, SIZE_T Size, ULONG Flags)
{
    PRTLP_LFH_USER_BLOCK Block;
    PHEAP_ENTRY Entry;
    PVOID Ptr;

    if (IsListEmpty(&BucketData->UserBlocks))
        return NULL;

    Block = CONTAINING_RECORD(BucketData->UserBlocks.Flink, RTLP_LFH_USER_BLOCK, Link);
    if (!Block->FreeList)
        return NULL;

    Ptr = Block->FreeList;
    Block->FreeList = *(PVOID *)Ptr;
    --Block->FreeCount;

    if (!Block->FreeList)
    {
        RemoveEntryList(&Block->Link);
        InsertTailList(&BucketData->UserBlocks, &Block->Link);
    }

    Entry = (PHEAP_ENTRY)Ptr - 1;
    Entry->Flags = HEAP_ENTRY_BUSY | (UCHAR)((Flags & HEAP_SETTABLE_USER_FLAGS) >> 4);
    Entry->SmallTagIndex = 0;
    Entry->UnusedBytes = (UCHAR)(RtlpLfhUsableSize(Block) - Size);
    return Ptr;
}

PVOID
RtlpLfhAllocate(PHEAP Heap, ULONG Flags, SIZE_T Size)
{
    PRTLP_LFH_HEAP Lfh;
    PRTLP_LFH_BUCKET BucketData;
    PRTLP_LFH_USER_BLOCK Block;
    ULONG Bucket;
    SIZE_T SlotSize;
    PVOID Ptr;

    if (!RtlpLfhFlagsEligible(Heap, Flags))
        return NULL;

    Bucket = RtlpLfhBucketOfSize(Size);
    if (!Bucket)
        return NULL;

    if (!RtlpLfhBucketEnabled(Heap, Bucket))
    {
        if ((Heap->FrontEndHeapUsageData[Bucket] & RTLP_LFH_USAGE_MASK) <= RTLP_LFH_USAGE_LIMIT)
            return NULL;

        RtlEnterHeapLock(Heap->LockVariable, TRUE);
        if (Heap->FrontEndHeap || RtlpLfhCreate(Heap))
        {
            Heap->FrontEndHeapStatusBitmap[Bucket >> 3] |= (UCHAR)(1 << (Bucket & 7));
            Heap->FrontEndHeapType = HEAP_LFH_FRONT_END;
        }
        RtlLeaveHeapLock(Heap->LockVariable);
    }

    Lfh = Heap->FrontEndHeap;
    if (!Lfh)
        return NULL;

    SlotSize = RtlpLfhSlotSize(Size);
    BucketData = &Lfh->Buckets[Bucket];

    RtlEnterHeapLock(Lfh->Lock, TRUE);
    Ptr = RtlpLfhPopSlot(BucketData, Size, Flags);
    RtlLeaveHeapLock(Lfh->Lock);

    if (!Ptr)
    {
        Block = RtlpLfhCreateUserBlock(Heap, Bucket, SlotSize);
        if (!Block)
            return NULL;

        RtlEnterHeapLock(Lfh->Lock, TRUE);
        InsertHeadList(&BucketData->UserBlocks, &Block->Link);
        ++BucketData->BlockCount;
        Ptr = RtlpLfhPopSlot(BucketData, Size, Flags);
        RtlLeaveHeapLock(Lfh->Lock);

        if (!Ptr)
            return NULL;
    }

    if (Flags & HEAP_ZERO_MEMORY)
        RtlZeroMemory(Ptr, Size);

    return Ptr;
}

PHEAP
RtlpLfhOwner(PVOID Ptr)
{
    PRTLP_LFH_USER_BLOCK Block;

    if (!Ptr || ((ULONG_PTR)Ptr & (HEAP_ENTRY_SIZE - 1)))
        return NULL;
    if (((PHEAP_ENTRY)Ptr - 1)->SegmentOffset != HEAP_LFH_INDEX)
        return NULL;
    Block = RtlpLfhBlockFromEntry(NULL, (PHEAP_ENTRY)Ptr - 1, NULL, NULL);
    return Block ? Block->Heap : NULL;
}

BOOLEAN
RtlpLfhFree(PHEAP Heap, PVOID Ptr)
{
    PRTLP_LFH_HEAP Lfh = Heap->FrontEndHeap;
    PRTLP_LFH_USER_BLOCK Block;
    PRTLP_LFH_BUCKET BucketData;
    PHEAP_ENTRY Entry = (PHEAP_ENTRY)Ptr - 1;
    BOOLEAN Release = FALSE;
    BOOLEAN WasFull;

    RtlEnterHeapLock(Lfh->Lock, TRUE);

    Block = RtlpLfhBlockFromEntry(Heap, Entry, NULL, NULL);
    if (!Block || !(Entry->Flags & HEAP_ENTRY_BUSY))
    {
        RtlLeaveHeapLock(Lfh->Lock);
        RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_INVALID_PARAMETER);
        return FALSE;
    }

    Entry->Flags = 0;
    Entry->UnusedBytes = 0;
    WasFull = Block->FreeList == NULL;
    *(PVOID *)Ptr = Block->FreeList;
    Block->FreeList = Ptr;
    ++Block->FreeCount;

    BucketData = &Lfh->Buckets[Block->Bucket];
    if (Block->FreeCount == Block->SlotCount && BucketData->BlockCount > 1)
    {
        RemoveEntryList(&Block->Link);
        --BucketData->BlockCount;
        Block->Signature = 0;
        Release = TRUE;
    }
    else if (WasFull)
    {
        RemoveEntryList(&Block->Link);
        InsertHeadList(&BucketData->UserBlocks, &Block->Link);
    }

    RtlLeaveHeapLock(Lfh->Lock);

    if (Release)
        RtlFreeHeap(Heap, 0, Block);

    return TRUE;
}

SIZE_T
RtlpLfhSize(PHEAP Heap, PVOID Ptr)
{
    PRTLP_LFH_HEAP Lfh = Heap->FrontEndHeap;
    PRTLP_LFH_USER_BLOCK Block;
    PHEAP_ENTRY Entry = (PHEAP_ENTRY)Ptr - 1;
    SIZE_T Size = (SIZE_T)-1;

    RtlEnterHeapLock(Lfh->Lock, TRUE);
    Block = RtlpLfhBlockFromEntry(Heap, Entry, NULL, NULL);
    if (Block && (Entry->Flags & HEAP_ENTRY_BUSY))
        Size = RtlpLfhUsableSize(Block) - Entry->UnusedBytes;
    RtlLeaveHeapLock(Lfh->Lock);

    if (Size == (SIZE_T)-1)
        RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_INVALID_PARAMETER);
    return Size;
}

BOOLEAN
RtlpLfhValidate(PHEAP Heap, PVOID Ptr)
{
    PRTLP_LFH_HEAP Lfh = Heap->FrontEndHeap;
    PRTLP_LFH_USER_BLOCK Block;
    PHEAP_ENTRY Entry = (PHEAP_ENTRY)Ptr - 1;
    BOOLEAN Valid;

    RtlEnterHeapLock(Lfh->Lock, TRUE);
    Block = RtlpLfhBlockFromEntry(Heap, Entry, NULL, NULL);
    Valid = Block && (Entry->Flags & HEAP_ENTRY_BUSY) &&
            Entry->UnusedBytes <= RtlpLfhUsableSize(Block);
    RtlLeaveHeapLock(Lfh->Lock);
    return Valid;
}

PVOID
RtlpLfhReAllocate(PHEAP Heap, ULONG Flags, PVOID Ptr, SIZE_T Size)
{
    EXCEPTION_RECORD ExceptionRecord;
    SIZE_T OldSize;
    PVOID NewPtr;

    OldSize = RtlpLfhSize(Heap, Ptr);
    if (OldSize == (SIZE_T)-1)
        return NULL;

    if (Flags & HEAP_REALLOC_IN_PLACE_ONLY)
    {
        if (Size == OldSize)
            return Ptr;

        if (Flags & HEAP_GENERATE_EXCEPTIONS)
        {
            ExceptionRecord.ExceptionCode = STATUS_NO_MEMORY;
            ExceptionRecord.ExceptionRecord = NULL;
            ExceptionRecord.NumberParameters = 1;
            ExceptionRecord.ExceptionFlags = 0;
            ExceptionRecord.ExceptionInformation[0] = Size;
            RtlRaiseException(&ExceptionRecord);
        }
        RtlSetLastWin32ErrorAndNtStatusFromNtStatus(STATUS_NO_MEMORY);
        return NULL;
    }

    NewPtr = RtlAllocateHeap(Heap, Flags & ~(HEAP_ZERO_MEMORY | HEAP_REALLOC_IN_PLACE_ONLY), Size);
    if (!NewPtr)
        return NULL;

    RtlCopyMemory(NewPtr, Ptr, min(OldSize, Size));
    if ((Flags & HEAP_ZERO_MEMORY) && Size > OldSize)
        RtlZeroMemory((PUCHAR)NewPtr + OldSize, Size - OldSize);

    RtlpLfhFree(Heap, Ptr);
    return NewPtr;
}

static
VOID
RtlpLfhFillWalkEntry(PRTLP_LFH_USER_BLOCK Block, ULONG Index, PRTL_HEAP_WALK_ENTRY WalkEntry)
{
    PHEAP_ENTRY Entry = RtlpLfhSlotEntry(Block, Index);

    WalkEntry->DataAddress = Entry + 1;
    if (Entry->Flags & HEAP_ENTRY_BUSY)
    {
        WalkEntry->DataSize = RtlpLfhUsableSize(Block) - Entry->UnusedBytes;
        WalkEntry->OverheadBytes = (UCHAR)(Block->SlotSize - WalkEntry->DataSize);
        WalkEntry->Flags = RTL_HEAP_ENTRY_LFH | RTL_HEAP_ENTRY_BUSY;
    }
    else
    {
        WalkEntry->DataSize = Block->SlotSize - 2 * sizeof(PVOID);
        WalkEntry->OverheadBytes = 2 * sizeof(PVOID);
        WalkEntry->Flags = RTL_HEAP_ENTRY_LFH;
    }
}

BOOLEAN
RtlpLfhWalkUserBlock(PHEAP Heap, PHEAP_ENTRY Entry, PRTL_HEAP_WALK_ENTRY WalkEntry)
{
    PRTLP_LFH_HEAP Lfh = Heap->FrontEndHeap;
    PRTLP_LFH_USER_BLOCK Block = (PRTLP_LFH_USER_BLOCK)(Entry + 1);
    BOOLEAN Found = FALSE;

    if (!Lfh || !(Entry->Flags & HEAP_ENTRY_BUSY) || (Entry->Flags & HEAP_ENTRY_EXTRA_PRESENT))
        return FALSE;
    if (((SIZE_T)Entry->Size << HEAP_ENTRY_SHIFT) <= sizeof(HEAP_ENTRY) + sizeof(RTLP_LFH_USER_BLOCK))
        return FALSE;

    RtlEnterHeapLock(Lfh->Lock, TRUE);
    if (Block->Signature == RTLP_LFH_BLOCK_SIGNATURE && Block->Heap == Heap && Block->SlotCount)
    {
        RtlpLfhFillWalkEntry(Block, 0, WalkEntry);
        Found = TRUE;
    }
    RtlLeaveHeapLock(Lfh->Lock);
    return Found;
}

NTSTATUS
RtlpLfhWalkNext(PHEAP Heap, PRTL_HEAP_WALK_ENTRY WalkEntry, PHEAP_ENTRY *Next)
{
    PRTLP_LFH_HEAP Lfh = Heap->FrontEndHeap;
    PRTLP_LFH_USER_BLOCK Block;
    PHEAP_ENTRY Entry, BlockEntry;
    PHEAP_SEGMENT Segment;
    ULONG Index;
    NTSTATUS Status = STATUS_NOT_FOUND;

    if (!Lfh || !WalkEntry->DataAddress || WalkEntry->SegmentIndex >= HEAP_SEGMENTS)
        return STATUS_NOT_FOUND;

    Segment = RtlpHeapSegmentFromAddress(Heap, WalkEntry->DataAddress);
    if (!Segment)
        return STATUS_NOT_FOUND;

    Entry = (PHEAP_ENTRY)WalkEntry->DataAddress - 1;
    if (Entry < Segment->FirstEntry || Entry >= Segment->LastValidEntry ||
        ((ULONG_PTR)Entry & (sizeof(HEAP_ENTRY) - 1)))
        return STATUS_NOT_FOUND;

    RtlEnterHeapLock(Lfh->Lock, TRUE);
    Block = RtlpLfhBlockFromEntry(Heap, Entry, Segment->FirstEntry, Segment->LastValidEntry);
    if (Block)
    {
        Index = (ULONG)(((PUCHAR)Entry - Block->FirstSlot) / Block->SlotSize) + 1;
        if (Index < Block->SlotCount)
        {
            RtlpLfhFillWalkEntry(Block, Index, WalkEntry);
            Status = STATUS_SUCCESS;
        }
        else
        {
            BlockEntry = (PHEAP_ENTRY)Block - 1;
            *Next = BlockEntry + BlockEntry->Size;
            Status = STATUS_MORE_ENTRIES;
        }
    }
    RtlLeaveHeapLock(Lfh->Lock);
    return Status;
}

BOOLEAN
RtlpLfhIsRegion(PHEAP Heap, PVOID Address)
{
    return Address && Address == Heap->FrontEndHeap;
}

BOOLEAN
RtlpLfhWalkRegion(PHEAP Heap, PRTL_HEAP_WALK_ENTRY WalkEntry)
{
    PRTLP_LFH_HEAP Lfh = Heap->FrontEndHeap;

    if (!Lfh)
        return FALSE;

    WalkEntry->DataAddress = Lfh;
    WalkEntry->DataSize = sizeof(RTLP_LFH_HEAP);
    WalkEntry->OverheadBytes = 0;
    WalkEntry->SegmentIndex = 0;
    WalkEntry->Flags = RTL_HEAP_ENTRY_LFH | RTL_HEAP_ENTRY_REGION;
    WalkEntry->Segment.CommittedSize = (ULONG)Lfh->CommittedSize;
    WalkEntry->Segment.UnCommittedSize = (ULONG)(Lfh->ReservedSize - Lfh->CommittedSize);
    WalkEntry->Segment.FirstEntry = Lfh;
    WalkEntry->Segment.LastEntry = (PUCHAR)Lfh + Lfh->ReservedSize;
    return TRUE;
}
