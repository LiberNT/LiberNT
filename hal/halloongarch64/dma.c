/*
 * PROJECT:     LiberNT LoongArch64 HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Bus-master DMA adapters: address windows, bounce pages, sync
 *
 * The three DMW windows [V1-7.5.18] are virtual-address views of physical
 * memory (SUC at 0x8000_0000_0000_0000, CC at 0x9000_..., WUC at 0xA000_...).
 * They do not translate bus addresses: a device's view is the host bridge's,
 * which on this board is identity. A topology therefore only carries windows
 * for a bridge that applies an offset.
 *
 * Cache coherency is a CPU property, not a bridge property:
 * CPUCFG3.CCDMA [V1-4.2.6.5] says whether the hardware maintains it. When it
 * does not, every hand-off and completion runs CACOP through the direct map
 * (cache.c), including for bounce-buffer copies.
 */

#include <ntifs.h>
#include <arc/arc.h>
#include "halp.h"

#define HALP_LA64_DMA_SIGNATURE     0x4C444D41UL
#define HALP_LA64_DMA_MAP_SIGNATURE 0x4C444D52UL
#define HALP_LA64_DMA_POOL_PAGES    256UL
#define HALP_LA64_DMA_NO_PAGE       MAXULONG

typedef struct _HALP_LA64_DMA_ENTRY
{
    ULONG Page;
    ULONG Length;
    ULONG64 Original;
    ULONG64 Target;
} HALP_LA64_DMA_ENTRY;

typedef struct _HALP_LA64_DMA_MAP
{
    ULONG Signature;
    ULONG Count;
    ULONG Used;
    HALP_LA64_DMA_ENTRY Entry[ANYSIZE_ARRAY];
} HALP_LA64_DMA_MAP;

typedef struct _HALP_LA64_DMA_ADAPTER HALP_LA64_DMA_ADAPTER;

typedef struct _HALP_LA64_DMA_LIST
{
    HALP_LA64_DMA_ADAPTER *Adapter;
    BOOLEAN WriteToDevice;
    BOOLEAN Allocated;
    ULONG Count;
    HALP_LA64_DMA_ENTRY Entry[ANYSIZE_ARRAY];
} HALP_LA64_DMA_LIST;

typedef struct _HALP_LA64_DMA_WAIT
{
    LIST_ENTRY Link;
    BOOLEAN List;
    BOOLEAN WriteToDevice;
    ULONG Pages;
    PDEVICE_OBJECT DeviceObject;
    PVOID Context;
    PDRIVER_CONTROL ChannelRoutine;
    PDRIVER_LIST_CONTROL ListRoutine;
    PMDL Mdl;
    PVOID CurrentVa;
    ULONG Length;
    PVOID Buffer;
    ULONG BufferLength;
} HALP_LA64_DMA_WAIT;

struct _HALP_LA64_DMA_ADAPTER
{
    DMA_ADAPTER Header;
    ULONG Signature;
    BOOLEAN Coherent;
    BOOLEAN Dispatching;
    ULONG AddressBits;
    HALP_LA64_DMA_TOPOLOGY Topology;
    KSPIN_LOCK Lock;
    PUCHAR PoolVa;
    ULONG64 PoolPa;
    ULONG PoolPages;
    ULONG PoolFree;
    RTL_BITMAP PoolMap;
    ULONG PoolBits[HALP_LA64_DMA_POOL_PAGES / 32];
    LIST_ENTRY Waiters;
};

static ULONG64 HalpLa64HighestRamAddress;

static
ULONG64
HalpLa64DmaLimit(
    _In_ const HALP_LA64_DMA_ADAPTER *Adapter)
{
    return Adapter->AddressBits >= 64 ? MAXULONG64 :
           ((1ULL << Adapter->AddressBits) - 1);
}

static
BOOLEAN
HalpLa64DmaTranslate(
    _In_ const HALP_LA64_DMA_ADAPTER *Adapter,
    _In_ ULONG64 Cpu,
    _In_ ULONG Length,
    _Out_ PULONG64 Bus)
{
    const HALP_LA64_DMA_TOPOLOGY *Topology = &Adapter->Topology;
    ULONG64 Limit = HalpLa64DmaLimit(Adapter);
    ULONG64 Last = Cpu + Length - 1;
    ULONG Index;

    if (!Length || Last < Cpu)
        return FALSE;
    if (!Topology->WindowCount)
    {
        if (Last > Limit)
            return FALSE;
        *Bus = Cpu;
        return TRUE;
    }
    for (Index = 0; Index < Topology->WindowCount; ++Index)
    {
        ULONG64 Base = Topology->CpuBase[Index];

        if (Cpu < Base || Last - Base >= Topology->Size[Index])
            continue;
        {
            ULONG64 Address = Topology->BusBase[Index] + (Cpu - Base);

            if (Address + Length - 1 < Address || Address + Length - 1 > Limit)
                return FALSE;
            *Bus = Address;
            return TRUE;
        }
    }
    return FALSE;
}

static
PVOID
HalpLa64DmaAllocate(
    _In_ const HALP_LA64_DMA_ADAPTER *Adapter,
    _In_ SIZE_T Length,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _Out_ PULONG64 Physical)
{
    const HALP_LA64_DMA_TOPOLOGY *Topology = &Adapter->Topology;
    ULONG64 Limit = HalpLa64DmaLimit(Adapter);
    PHYSICAL_ADDRESS Low, High, Boundary;
    ULONG Index, Count = Topology->WindowCount ? Topology->WindowCount : 1;

    Boundary.QuadPart = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        ULONG64 First = 0, Last = Limit;
        PVOID Buffer;

        if (Topology->WindowCount)
        {
            if (Topology->BusBase[Index] > Limit)
                continue;
            First = Topology->CpuBase[Index];
            Last = First + min(Topology->Size[Index], Limit - Topology->BusBase[Index] + 1) - 1;
        }
        Low.QuadPart = (LONGLONG)First;
        High.QuadPart = Last > (ULONG64)MAXLONGLONG ? MAXLONGLONG : (LONGLONG)Last;
        Buffer = MmAllocateContiguousMemorySpecifyCache(Length, Low, High, Boundary,
                                                        CacheType);
        if (Buffer)
        {
            *Physical = (ULONG64)MmGetPhysicalAddress(Buffer).QuadPart;
            return Buffer;
        }
    }
    return NULL;
}

static
PUCHAR
HalpLa64DirectMap(
    _In_ ULONG64 Physical)
{
    return (PUCHAR)(ULONG_PTR)(HalpLa64DirectMapBase + Physical);
}

/* Write back/invalidate the range so both sides observe the same bytes. */
static
VOID
HalpLa64DmaSync(
    _In_ const HALP_LA64_DMA_ADAPTER *Adapter,
    _In_ ULONG64 Physical,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(Adapter);
    HalpLa64FlushDmaPhysical(Physical, Length);
}

/* A bounce is needed only when the device cannot address the page: the cache
 * maintenance problem is solved by flushing whole lines, which is safe for a
 * partial line because the flush writes the line back before invalidating it. */
static
BOOLEAN
HalpLa64DmaNeedsBounce(
    _In_ const HALP_LA64_DMA_ADAPTER *Adapter,
    _In_ ULONG64 Physical,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    ULONG64 Bus;

    UNREFERENCED_PARAMETER(WriteToDevice);
    return !HalpLa64DmaTranslate(Adapter, Physical, Length, &Bus);
}

static
BOOLEAN
HalpLa64MdlFragment(
    _Inout_ PMDL *Mdl,
    _Inout_ PULONG_PTR Offset,
    _In_ ULONG Remaining,
    _Out_ PULONG64 Physical,
    _Out_ PULONG Chunk)
{
    PMDL Current = *Mdl;
    ULONG_PTR InMdl;

    while (Current && *Offset >= Current->ByteOffset &&
           *Offset - Current->ByteOffset >= Current->ByteCount)
    {
        Current = Current->Next;
        if (Current)
            *Offset = Current->ByteOffset;
    }
    if (!Current || *Offset < Current->ByteOffset || !Remaining)
        return FALSE;
    InMdl = *Offset - Current->ByteOffset;
    *Physical = ((ULONG64)MmGetMdlPfnArray(Current)[*Offset >> PAGE_SHIFT] << PAGE_SHIFT) +
                (*Offset & (PAGE_SIZE - 1));
    *Chunk = min(Remaining, PAGE_SIZE - (ULONG)(*Offset & (PAGE_SIZE - 1)));
    *Chunk = min(*Chunk, Current->ByteCount - (ULONG)InMdl);
    *Mdl = Current;
    return TRUE;
}

static
ULONG_PTR
HalpLa64MdlOffset(
    _In_ PMDL Mdl,
    _In_opt_ PVOID CurrentVa)
{
    if (Mdl->StartVa)
        return (ULONG_PTR)CurrentVa - (ULONG_PTR)Mdl->StartVa;
    return CurrentVa ? (ULONG_PTR)CurrentVa : Mdl->ByteOffset;
}

static
ULONG
HalpLa64ReservePages(
    _Inout_ HALP_LA64_DMA_ADAPTER *Adapter,
    _In_ ULONG Count,
    _Out_writes_(Count) PULONG Pages)
{
    ULONG Index;

    if (Count > Adapter->PoolFree)
        return 0;
    for (Index = 0; Index < Count; ++Index)
    {
        Pages[Index] = RtlFindClearBitsAndSet(&Adapter->PoolMap, 1, 0);
        ASSERT(Pages[Index] != MAXULONG);
    }
    Adapter->PoolFree -= Count;
    return Count;
}

static
VOID
HalpLa64ReleasePage(
    _Inout_ HALP_LA64_DMA_ADAPTER *Adapter,
    _In_ ULONG Page)
{
    if (Page == HALP_LA64_DMA_NO_PAGE)
        return;
    RtlClearBits(&Adapter->PoolMap, Page, 1);
    Adapter->PoolFree++;
}

static
ULONG64
HalpLa64PoolPage(
    _In_ const HALP_LA64_DMA_ADAPTER *Adapter,
    _In_ ULONG Page)
{
    return Adapter->PoolPa + ((ULONG64)Page << PAGE_SHIFT);
}

static
HALP_LA64_DMA_ADAPTER *
HalpLa64Adapter(
    _In_opt_ PDMA_ADAPTER DmaAdapter)
{
    HALP_LA64_DMA_ADAPTER *Adapter = (HALP_LA64_DMA_ADAPTER *)DmaAdapter;

    if (!Adapter || Adapter->Signature != HALP_LA64_DMA_SIGNATURE ||
        Adapter->Header.Size != sizeof(*Adapter))
        return NULL;
    return Adapter;
}

static
NTSTATUS
HalpLa64QueueWait(
    _Inout_ HALP_LA64_DMA_ADAPTER *Adapter,
    _In_ const HALP_LA64_DMA_WAIT *Template)
{
    HALP_LA64_DMA_WAIT *Wait = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Wait),
                                                      HALP_LA64_DMA_TAG);

    if (!Wait)
        return STATUS_INSUFFICIENT_RESOURCES;
    *Wait = *Template;
    InsertTailList(&Adapter->Waiters, &Wait->Link);
    return STATUS_SUCCESS;
}

static NTSTATUS
HalpLa64StartChannel(
    _Inout_ HALP_LA64_DMA_ADAPTER *Adapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG NumberOfMapRegisters,
    _In_ PDRIVER_CONTROL ExecutionRoutine,
    _In_ PVOID Context);

static NTSTATUS
HalpLa64StartList(
    _Inout_ HALP_LA64_DMA_ADAPTER *Adapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMDL Mdl,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ PDRIVER_LIST_CONTROL ExecutionRoutine,
    _In_ PVOID Context,
    _In_ BOOLEAN WriteToDevice,
    _In_opt_ PVOID Buffer,
    _In_ ULONG BufferLength);

static
VOID
HalpLa64RunWaiters(
    _Inout_ HALP_LA64_DMA_ADAPTER *Adapter)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    if (Adapter->Dispatching)
    {
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
        return;
    }
    Adapter->Dispatching = TRUE;
    while (!IsListEmpty(&Adapter->Waiters))
    {
        HALP_LA64_DMA_WAIT *Wait =
            CONTAINING_RECORD(Adapter->Waiters.Flink, HALP_LA64_DMA_WAIT, Link);

        if (Wait->Pages > Adapter->PoolFree)
            break;
        RemoveEntryList(&Wait->Link);
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
        if (Wait->List)
            HalpLa64StartList(Adapter, Wait->DeviceObject, Wait->Mdl, Wait->CurrentVa,
                              Wait->Length, Wait->ListRoutine, Wait->Context,
                              Wait->WriteToDevice, Wait->Buffer, Wait->BufferLength);
        else
            HalpLa64StartChannel(Adapter, Wait->DeviceObject, Wait->Pages,
                                 Wait->ChannelRoutine, Wait->Context);
        ExFreePoolWithTag(Wait, HALP_LA64_DMA_TAG);
        KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    }
    Adapter->Dispatching = FALSE;
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);
}

static
VOID
NTAPI
HalpLa64PutDmaAdapter(
    _In_ PDMA_ADAPTER DmaAdapter)
{
    HALP_LA64_DMA_ADAPTER *Adapter = HalpLa64Adapter(DmaAdapter);

    if (!Adapter)
        return;
    ASSERT(IsListEmpty(&Adapter->Waiters));
    if (Adapter->PoolVa)
        MmFreeContiguousMemory(Adapter->PoolVa);
    Adapter->Signature = 0;
    ExFreePoolWithTag(Adapter, HALP_LA64_DMA_TAG);
}

/* ---------------------------------------------------------------------------
 * Common buffers
 * ------------------------------------------------------------------------ */

static
PVOID
NTAPI
HalpLa64AllocateCommonBuffer(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ ULONG Length,
    _Out_ PPHYSICAL_ADDRESS LogicalAddress,
    _In_ BOOLEAN CacheEnabled)
{
    HALP_LA64_DMA_ADAPTER *Adapter = HalpLa64Adapter(DmaAdapter);
    ULONG64 Physical, Bus;
    PVOID Buffer;

    if (!Adapter || !LogicalAddress || !Length)
        return NULL;
    /* A coherent machine may keep common buffers cached. A machine that needs
     * software maintenance gets an uncached buffer unless the driver asked for
     * caching, in which case FlushAdapterBuffers maintains it. */
    UNREFERENCED_PARAMETER(CacheEnabled);
    Buffer = HalpLa64DmaAllocate(Adapter, Length,
                                 Adapter->Coherent ? MmCached : MmNonCached, &Physical);
    if (!Buffer)
        return NULL;
    if (!HalpLa64DmaTranslate(Adapter, Physical, Length, &Bus))
    {
        MmFreeContiguousMemory(Buffer);
        return NULL;
    }
    LogicalAddress->QuadPart = (LONGLONG)Bus;
    return Buffer;
}

static
VOID
NTAPI
HalpLa64FreeCommonBuffer(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ ULONG Length,
    _In_ PHYSICAL_ADDRESS LogicalAddress,
    _In_ PVOID VirtualAddress,
    _In_ BOOLEAN CacheEnabled)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(LogicalAddress);
    UNREFERENCED_PARAMETER(CacheEnabled);
    if (VirtualAddress)
        MmFreeContiguousMemory(VirtualAddress);
}

/* ---------------------------------------------------------------------------
 * Channel allocation and map registers
 * ------------------------------------------------------------------------ */

static
NTSTATUS
HalpLa64StartChannel(
    _Inout_ HALP_LA64_DMA_ADAPTER *Adapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG NumberOfMapRegisters,
    _In_ PDRIVER_CONTROL ExecutionRoutine,
    _In_ PVOID Context)
{
    HALP_LA64_DMA_MAP *Map = NULL;
    IO_ALLOCATION_ACTION Action;
    KIRQL OldIrql;
    ULONG Index;

    if (Adapter->PoolPages && NumberOfMapRegisters)
    {
        ULONG Pages[HALP_LA64_DMA_POOL_PAGES];

        Map = ExAllocatePoolWithTag(NonPagedPool,
                                    FIELD_OFFSET(HALP_LA64_DMA_MAP,
                                                 Entry[NumberOfMapRegisters]),
                                    HALP_LA64_DMA_TAG);
        if (!Map)
            return STATUS_INSUFFICIENT_RESOURCES;
        KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
        if (!HalpLa64ReservePages(Adapter, NumberOfMapRegisters, Pages))
        {
            HALP_LA64_DMA_WAIT Wait;
            NTSTATUS Status;

            RtlZeroMemory(&Wait, sizeof(Wait));
            Wait.Pages = NumberOfMapRegisters;
            Wait.DeviceObject = DeviceObject;
            Wait.Context = Context;
            Wait.ChannelRoutine = ExecutionRoutine;
            Status = HalpLa64QueueWait(Adapter, &Wait);
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
            ExFreePoolWithTag(Map, HALP_LA64_DMA_TAG);
            return Status;
        }
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
        Map->Signature = HALP_LA64_DMA_MAP_SIGNATURE;
        Map->Count = NumberOfMapRegisters;
        Map->Used = 0;
        for (Index = 0; Index < NumberOfMapRegisters; ++Index)
            Map->Entry[Index].Page = Pages[Index];
    }

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    Action = ExecutionRoutine(DeviceObject, DeviceObject->CurrentIrp, Map, Context);
    KeLowerIrql(OldIrql);
    if (Action == DeallocateObject && Map)
        Adapter->Header.DmaOperations->FreeMapRegisters(&Adapter->Header, Map,
                                                        NumberOfMapRegisters);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
HalpLa64AllocateAdapterChannel(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG NumberOfMapRegisters,
    _In_ PDRIVER_CONTROL ExecutionRoutine,
    _In_ PVOID Context)
{
    HALP_LA64_DMA_ADAPTER *Adapter = HalpLa64Adapter(DmaAdapter);

    if (!Adapter || !DeviceObject || !ExecutionRoutine ||
        KeGetCurrentIrql() > DISPATCH_LEVEL)
        return STATUS_INVALID_PARAMETER;
    if (Adapter->PoolPages && NumberOfMapRegisters > Adapter->PoolPages)
        return STATUS_INSUFFICIENT_RESOURCES;
    return HalpLa64StartChannel(Adapter, DeviceObject, NumberOfMapRegisters,
                                ExecutionRoutine, Context);
}

static
BOOLEAN
NTAPI
HalpLa64FlushAdapterBuffers(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    HALP_LA64_DMA_ADAPTER *Adapter = HalpLa64Adapter(DmaAdapter);
    HALP_LA64_DMA_MAP *Map = MapRegisterBase;
    ULONG Index;

    if (!Adapter)
        return FALSE;
    if (Map && Map->Signature != HALP_LA64_DMA_MAP_SIGNATURE)
        return FALSE;
    if (Mdl)
    {
        ULONG_PTR Offset = HalpLa64MdlOffset(Mdl, CurrentVa);
        ULONG Remaining = Length, Chunk;
        ULONG64 Physical;
        PMDL Current = Mdl;

        while (Remaining && HalpLa64MdlFragment(&Current, &Offset, Remaining,
                                                &Physical, &Chunk))
        {
            if (!HalpLa64DmaNeedsBounce(Adapter, Physical, Chunk, FALSE))
                HalpLa64DmaSync(Adapter, Physical, Chunk);
            Offset += Chunk;
            Remaining -= Chunk;
        }
    }
    if (Map)
    {
        for (Index = 0; Index < Map->Used; ++Index)
        {
            HALP_LA64_DMA_ENTRY *Entry = &Map->Entry[Index];

            if (!WriteToDevice)
            {
                HalpLa64DmaSync(Adapter, Entry->Target, Entry->Length);
                RtlCopyMemory(HalpLa64DirectMap(Entry->Original),
                              HalpLa64DirectMap(Entry->Target), Entry->Length);
            }
        }
        Map->Used = 0;
    }
    __asm__ __volatile__("dbar 0" ::: "memory");
    return TRUE;
}

static
VOID
NTAPI
HalpLa64FreeAdapterChannel(
    _In_ PDMA_ADAPTER DmaAdapter)
{
    HALP_LA64_DMA_ADAPTER *Adapter = HalpLa64Adapter(DmaAdapter);

    if (Adapter)
        HalpLa64RunWaiters(Adapter);
}

static
VOID
NTAPI
HalpLa64FreeMapRegisters(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PVOID MapRegisterBase,
    _In_ ULONG NumberOfMapRegisters)
{
    HALP_LA64_DMA_ADAPTER *Adapter = HalpLa64Adapter(DmaAdapter);
    HALP_LA64_DMA_MAP *Map = MapRegisterBase;
    KIRQL OldIrql;
    ULONG Index;

    UNREFERENCED_PARAMETER(NumberOfMapRegisters);
    if (!Adapter || !Map || Map->Signature != HALP_LA64_DMA_MAP_SIGNATURE)
        return;
    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    for (Index = 0; Index < Map->Count; ++Index)
        HalpLa64ReleasePage(Adapter, Map->Entry[Index].Page);
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    Map->Signature = 0;
    ExFreePoolWithTag(Map, HALP_LA64_DMA_TAG);
    HalpLa64RunWaiters(Adapter);
}

static
PHYSICAL_ADDRESS
NTAPI
HalpLa64MapTransfer(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _Inout_ PULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    HALP_LA64_DMA_ADAPTER *Adapter = HalpLa64Adapter(DmaAdapter);
    HALP_LA64_DMA_MAP *Map = MapRegisterBase;
    PHYSICAL_ADDRESS Address;
    ULONG_PTR Offset;
    ULONG64 Physical, Bus;
    ULONG Chunk;
    PMDL Current = Mdl;

    Address.QuadPart = 0;
    if (!Adapter || !Length || !*Length || !Mdl ||
        (Map && Map->Signature != HALP_LA64_DMA_MAP_SIGNATURE))
        goto Fail;
    Offset = HalpLa64MdlOffset(Mdl, CurrentVa);
    if (Offset < Mdl->ByteOffset || Offset - Mdl->ByteOffset >= Mdl->ByteCount ||
        !HalpLa64MdlFragment(&Current, &Offset, *Length, &Physical, &Chunk) ||
        Current != Mdl)
        goto Fail;

    if (HalpLa64DmaNeedsBounce(Adapter, Physical, Chunk, WriteToDevice))
    {
        HALP_LA64_DMA_ENTRY *Entry;

        if (!Map || Map->Used >= Map->Count)
            goto Fail;
        Entry = &Map->Entry[Map->Used];
        Entry->Original = Physical;
        Entry->Length = Chunk;
        Entry->Target = HalpLa64PoolPage(Adapter, Entry->Page) +
                        (Physical & (PAGE_SIZE - 1));
        if (WriteToDevice)
            RtlCopyMemory(HalpLa64DirectMap(Entry->Target),
                          HalpLa64DirectMap(Physical), Chunk);
        Physical = Entry->Target;
        Map->Used++;
    }
    else
    {
        /* One map register covers a physically contiguous run of pages. */
        while (Chunk < *Length)
        {
            ULONG_PTR NextOffset = Offset + Chunk;
            ULONG64 NextPhysical, NextBus;
            ULONG NextChunk;
            PMDL Next = Current;

            if (!HalpLa64MdlFragment(&Next, &NextOffset, *Length - Chunk,
                                     &NextPhysical, &NextChunk) ||
                Next != Current || NextPhysical != Physical + Chunk ||
                !HalpLa64DmaTranslate(Adapter, Physical, Chunk + NextChunk, &NextBus))
                break;
            Chunk += NextChunk;
        }
    }
    if (!HalpLa64DmaTranslate(Adapter, Physical, Chunk, &Bus))
        goto Fail;
    HalpLa64DmaSync(Adapter, Physical, Chunk);
    *Length = Chunk;
    Address.QuadPart = (LONGLONG)Bus;
    return Address;

Fail:
    if (Length)
        *Length = 0;
    Address.QuadPart = 0;
    return Address;
}

static
ULONG
NTAPI
HalpLa64GetDmaAlignment(
    _In_ PDMA_ADAPTER DmaAdapter)
{
    HALP_LA64_DMA_ADAPTER *Adapter = HalpLa64Adapter(DmaAdapter);

    /* Any byte alignment is acceptable: a partial cache line is handled by a
     * line-granular writeback before the hand-off. */
    return Adapter ? 1 : 0;
}

static
ULONG
NTAPI
HalpLa64ReadDmaCounter(
    _In_ PDMA_ADAPTER DmaAdapter)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Scatter/gather
 * ------------------------------------------------------------------------ */

static
ULONG
HalpLa64CountFragments(
    _In_opt_ PMDL Mdl,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length)
{
    ULONG_PTR Offset;
    ULONG64 Physical;
    ULONG Count = 0, Chunk;
    PMDL Current = Mdl;

    if (!Mdl)
        return ADDRESS_AND_SIZE_TO_SPAN_PAGES(CurrentVa, Length);
    Offset = HalpLa64MdlOffset(Mdl, CurrentVa);
    while (Length && HalpLa64MdlFragment(&Current, &Offset, Length, &Physical, &Chunk))
    {
        Count++;
        Offset += Chunk;
        Length -= Chunk;
    }
    return Length ? 0 : Count;
}

static
SIZE_T
HalpLa64ListSize(
    _In_ ULONG Count,
    _Out_opt_ PSIZE_T PrivateOffset)
{
    SIZE_T Offset = ALIGN_UP_BY(FIELD_OFFSET(SCATTER_GATHER_LIST, Elements[Count]),
                                sizeof(ULONG64));

    if (PrivateOffset)
        *PrivateOffset = Offset;
    return Offset + FIELD_OFFSET(HALP_LA64_DMA_LIST, Entry[Count]);
}

static
NTSTATUS
NTAPI
HalpLa64CalculateScatterGatherList(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PMDL Mdl,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _Out_ PULONG ScatterGatherListSize,
    _Out_opt_ PULONG NumberOfMapRegisters)
{
    ULONG Count = HalpLa64CountFragments(Mdl, CurrentVa, Length);

    if (!HalpLa64Adapter(DmaAdapter) || !ScatterGatherListSize || !Count)
        return STATUS_INVALID_PARAMETER;
    *ScatterGatherListSize = (ULONG)HalpLa64ListSize(Count, NULL);
    if (NumberOfMapRegisters)
        *NumberOfMapRegisters = Count;
    return STATUS_SUCCESS;
}

static
NTSTATUS
HalpLa64StartList(
    _Inout_ HALP_LA64_DMA_ADAPTER *Adapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMDL Mdl,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ PDRIVER_LIST_CONTROL ExecutionRoutine,
    _In_ PVOID Context,
    _In_ BOOLEAN WriteToDevice,
    _In_opt_ PVOID Buffer,
    _In_ ULONG BufferLength)
{
    ULONG Count = HalpLa64CountFragments(Mdl, CurrentVa, Length), Bounce = 0, Index, Chunk;
    ULONG Pages[HALP_LA64_DMA_POOL_PAGES];
    PSCATTER_GATHER_LIST List;
    HALP_LA64_DMA_LIST *Private;
    SIZE_T Size, PrivateOffset;
    ULONG_PTR Offset;
    ULONG64 Physical;
    PMDL Current;
    KIRQL OldIrql;

    if (!Count)
        return STATUS_INVALID_PARAMETER;
    Size = HalpLa64ListSize(Count, &PrivateOffset);
    if (Buffer && BufferLength < Size)
        return STATUS_BUFFER_TOO_SMALL;

    Offset = HalpLa64MdlOffset(Mdl, CurrentVa);
    Current = Mdl;
    for (Index = 0, Chunk = Length; Chunk;)
    {
        ULONG Piece;

        if (!HalpLa64MdlFragment(&Current, &Offset, Chunk, &Physical, &Piece))
            return STATUS_INVALID_PARAMETER;
        if (HalpLa64DmaNeedsBounce(Adapter, Physical, Piece, WriteToDevice))
            Bounce++;
        Offset += Piece;
        Chunk -= Piece;
    }
    if (Bounce > Adapter->PoolPages)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (Bounce)
    {
        KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
        if (!HalpLa64ReservePages(Adapter, Bounce, Pages))
        {
            HALP_LA64_DMA_WAIT Wait;
            NTSTATUS Status;

            RtlZeroMemory(&Wait, sizeof(Wait));
            Wait.List = TRUE;
            Wait.WriteToDevice = WriteToDevice;
            Wait.Pages = Bounce;
            Wait.DeviceObject = DeviceObject;
            Wait.Context = Context;
            Wait.ListRoutine = ExecutionRoutine;
            Wait.Mdl = Mdl;
            Wait.CurrentVa = CurrentVa;
            Wait.Length = Length;
            Wait.Buffer = Buffer;
            Wait.BufferLength = BufferLength;
            Status = HalpLa64QueueWait(Adapter, &Wait);
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
            return Status;
        }
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    }

    List = Buffer ? Buffer : ExAllocatePoolWithTag(NonPagedPool, Size,
                                                   HALP_LA64_DMA_TAG);
    if (!List)
    {
        KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
        for (Index = 0; Index < Bounce; ++Index)
            HalpLa64ReleasePage(Adapter, Pages[Index]);
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Private = (HALP_LA64_DMA_LIST *)((PUCHAR)List + PrivateOffset);
    Private->Adapter = Adapter;
    Private->WriteToDevice = WriteToDevice;
    Private->Allocated = (Buffer == NULL);
    Private->Count = Count;
    List->NumberOfElements = Count;
    List->Reserved = (ULONG_PTR)Private;

    Offset = HalpLa64MdlOffset(Mdl, CurrentVa);
    Current = Mdl;
    Bounce = 0;
    for (Index = 0, Chunk = Length; Index < Count; ++Index)
    {
        HALP_LA64_DMA_ENTRY *Entry = &Private->Entry[Index];
        ULONG Piece;
        ULONG64 Bus;

        HalpLa64MdlFragment(&Current, &Offset, Chunk, &Physical, &Piece);
        Entry->Original = Physical;
        Entry->Length = Piece;
        Entry->Page = HALP_LA64_DMA_NO_PAGE;
        Entry->Target = Physical;
        if (HalpLa64DmaNeedsBounce(Adapter, Physical, Piece, WriteToDevice))
        {
            Entry->Page = Pages[Bounce++];
            Entry->Target = HalpLa64PoolPage(Adapter, Entry->Page) +
                            (Physical & (PAGE_SIZE - 1));
            if (WriteToDevice)
                RtlCopyMemory(HalpLa64DirectMap(Entry->Target),
                              HalpLa64DirectMap(Physical), Piece);
        }
        HalpLa64DmaTranslate(Adapter, Entry->Target, Piece, &Bus);
        HalpLa64DmaSync(Adapter, Entry->Target, Piece);
        List->Elements[Index].Address.QuadPart = (LONGLONG)Bus;
        List->Elements[Index].Length = Piece;
        List->Elements[Index].Reserved = 0;
        Offset += Piece;
        Chunk -= Piece;
    }

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    ExecutionRoutine(DeviceObject, DeviceObject->CurrentIrp, List, Context);
    KeLowerIrql(OldIrql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
HalpLa64GetScatterGatherList(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMDL Mdl,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ PDRIVER_LIST_CONTROL ExecutionRoutine,
    _In_ PVOID Context,
    _In_ BOOLEAN WriteToDevice)
{
    HALP_LA64_DMA_ADAPTER *Adapter = HalpLa64Adapter(DmaAdapter);

    if (!Adapter || !DeviceObject || !Mdl || !Length || !ExecutionRoutine)
        return STATUS_INVALID_PARAMETER;
    return HalpLa64StartList(Adapter, DeviceObject, Mdl, CurrentVa, Length,
                             ExecutionRoutine, Context, WriteToDevice, NULL, 0);
}

static
NTSTATUS
NTAPI
HalpLa64BuildScatterGatherList(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMDL Mdl,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ PDRIVER_LIST_CONTROL ExecutionRoutine,
    _In_ PVOID Context,
    _In_ BOOLEAN WriteToDevice,
    _In_ PVOID ScatterGatherBuffer,
    _In_ ULONG ScatterGatherLength)
{
    HALP_LA64_DMA_ADAPTER *Adapter = HalpLa64Adapter(DmaAdapter);

    if (!Adapter || !DeviceObject || !Mdl || !Length || !ExecutionRoutine ||
        !ScatterGatherBuffer)
        return STATUS_INVALID_PARAMETER;
    return HalpLa64StartList(Adapter, DeviceObject, Mdl, CurrentVa, Length,
                             ExecutionRoutine, Context, WriteToDevice,
                             ScatterGatherBuffer, ScatterGatherLength);
}

static
VOID
NTAPI
HalpLa64PutScatterGatherList(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PSCATTER_GATHER_LIST ScatterGather,
    _In_ BOOLEAN WriteToDevice)
{
    HALP_LA64_DMA_ADAPTER *Adapter = HalpLa64Adapter(DmaAdapter);
    HALP_LA64_DMA_LIST *Private;
    KIRQL OldIrql;
    ULONG Index;

    UNREFERENCED_PARAMETER(WriteToDevice);
    if (!Adapter || !ScatterGather)
        return;
    Private = (HALP_LA64_DMA_LIST *)ScatterGather->Reserved;
    if (!Private || Private->Adapter != Adapter)
        return;
    for (Index = 0; Index < Private->Count; ++Index)
    {
        HALP_LA64_DMA_ENTRY *Entry = &Private->Entry[Index];

        if (Private->WriteToDevice)
            continue;
        HalpLa64DmaSync(Adapter, Entry->Target, Entry->Length);
        if (Entry->Page != HALP_LA64_DMA_NO_PAGE)
            RtlCopyMemory(HalpLa64DirectMap(Entry->Original),
                          HalpLa64DirectMap(Entry->Target), Entry->Length);
    }
    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    for (Index = 0; Index < Private->Count; ++Index)
        HalpLa64ReleasePage(Adapter, Private->Entry[Index].Page);
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    ScatterGather->Reserved = 0;
    if (Private->Allocated)
        ExFreePoolWithTag(ScatterGather, HALP_LA64_DMA_TAG);
    HalpLa64RunWaiters(Adapter);
}

static
NTSTATUS
NTAPI
HalpLa64BuildMdlFromScatterGatherList(
    _In_ PDMA_ADAPTER DmaAdapter,
    _In_ PSCATTER_GATHER_LIST ScatterGather,
    _In_ PMDL OriginalMdl,
    _Out_ PMDL *TargetMdl)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    UNREFERENCED_PARAMETER(ScatterGather);
    UNREFERENCED_PARAMETER(OriginalMdl);
    if (TargetMdl)
        *TargetMdl = NULL;
    return STATUS_NOT_SUPPORTED;
}

static DMA_OPERATIONS HalpLa64DmaOperations =
{
    .Size = FIELD_OFFSET(DMA_OPERATIONS, GetDmaAdapterInfo),
    .PutDmaAdapter = HalpLa64PutDmaAdapter,
    .AllocateCommonBuffer = HalpLa64AllocateCommonBuffer,
    .FreeCommonBuffer = HalpLa64FreeCommonBuffer,
    .AllocateAdapterChannel = HalpLa64AllocateAdapterChannel,
    .FlushAdapterBuffers = HalpLa64FlushAdapterBuffers,
    .FreeAdapterChannel = HalpLa64FreeAdapterChannel,
    .FreeMapRegisters = HalpLa64FreeMapRegisters,
    .MapTransfer = HalpLa64MapTransfer,
    .GetDmaAlignment = HalpLa64GetDmaAlignment,
    .ReadDmaCounter = HalpLa64ReadDmaCounter,
    .GetScatterGatherList = HalpLa64GetScatterGatherList,
    .PutScatterGatherList = HalpLa64PutScatterGatherList,
    .CalculateScatterGatherList = HalpLa64CalculateScatterGatherList,
    .BuildScatterGatherList = HalpLa64BuildScatterGatherList,
    .BuildMdlFromScatterGatherList = HalpLa64BuildMdlFromScatterGatherList
};

PDMA_ADAPTER
HalpLa64CreateDmaAdapter(
    _In_ PDEVICE_DESCRIPTION Description,
    _In_ const HALP_LA64_DMA_TOPOLOGY *Topology,
    _Out_opt_ PULONG NumberOfMapRegisters)
{
    HALP_LA64_DMA_ADAPTER *Adapter;
    ULONG Registers;

    if (NumberOfMapRegisters)
        *NumberOfMapRegisters = 0;
    if (!Description || !Description->Master ||
        Description->Version > DEVICE_DESCRIPTION_VERSION3)
        return NULL;

    Adapter = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Adapter),
                                    HALP_LA64_DMA_TAG);
    if (!Adapter)
        return NULL;
    RtlZeroMemory(Adapter, sizeof(*Adapter));
    Adapter->Header.Version = (USHORT)Description->Version;
    Adapter->Header.Size = sizeof(*Adapter);
    Adapter->Header.DmaOperations = &HalpLa64DmaOperations;
    Adapter->Signature = HALP_LA64_DMA_SIGNATURE;
    Adapter->Coherent = Topology->Coherent;
    Adapter->Topology = *Topology;
    Adapter->AddressBits = (Description->Dma64BitAddresses ||
                            (Description->Version >= DEVICE_DESCRIPTION_VERSION3 &&
                             Description->DmaAddressWidth >= 64)) ? 64 :
                           (Description->Version >= DEVICE_DESCRIPTION_VERSION3 &&
                            Description->DmaAddressWidth > 0 &&
                            Description->DmaAddressWidth < 32)
                               ? Description->DmaAddressWidth : 32;
    KeInitializeSpinLock(&Adapter->Lock);
    InitializeListHead(&Adapter->Waiters);

    /* Map registers and bounce pages come from one contiguous, device
     * addressable pool. It is needed whenever the device cannot reach all of
     * RAM or the bridge translates addresses. */
    if (Topology->WindowCount || HalpLa64HighestRamAddress > HalpLa64DmaLimit(Adapter))
    {
        Adapter->PoolVa = HalpLa64DmaAllocate(Adapter,
                                               HALP_LA64_DMA_POOL_PAGES * PAGE_SIZE,
                                               MmCached, &Adapter->PoolPa);
        if (!Adapter->PoolVa)
        {
            ExFreePoolWithTag(Adapter, HALP_LA64_DMA_TAG);
            return NULL;
        }
        Adapter->PoolPages = HALP_LA64_DMA_POOL_PAGES;
        Adapter->PoolFree = HALP_LA64_DMA_POOL_PAGES;
        RtlInitializeBitMap(&Adapter->PoolMap, Adapter->PoolBits,
                            HALP_LA64_DMA_POOL_PAGES);
        RtlClearAllBits(&Adapter->PoolMap);
    }

    Registers = Description->MaximumLength ?
                BYTES_TO_PAGES(Description->MaximumLength) + 1 : 64;
    if (Adapter->PoolPages)
        Registers = min(Registers, Adapter->PoolPages);
    else
        Registers = min(Registers, HALP_LA64_DMA_POOL_PAGES);
    if (NumberOfMapRegisters)
        *NumberOfMapRegisters = Registers;
    return &Adapter->Header;
}

BOOLEAN
HalpLa64GetPlatformDmaTopology(
    _In_opt_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ HALP_LA64_DMA_TOPOLOGY *Topology)
{
    UNREFERENCED_PARAMETER(PhysicalDeviceObject);
    RtlZeroMemory(Topology, sizeof(*Topology));
    Topology->Coherent = HalpLa64Platform.DmaCoherent;
    return TRUE;
}

static
PDMA_ADAPTER
NTAPI
HalpLa64GetDmaAdapter(
    _In_ PVOID Context,
    _In_ PDEVICE_DESCRIPTION Description,
    _Out_ PULONG NumberOfMapRegisters)
{
    HALP_LA64_DMA_TOPOLOGY Topology;
    ULONG FirstBus, LastBus;

    if (NumberOfMapRegisters)
        *NumberOfMapRegisters = 0;
    if (!Description)
        return NULL;
    RtlZeroMemory(&Topology, sizeof(Topology));
    if (Description->InterfaceType == PCIBus)
    {
        if (!HalpLa64GetPciBusRange(&FirstBus, &LastBus) ||
            Description->BusNumber < FirstBus || Description->BusNumber > LastBus)
            return NULL;
        Topology.Coherent = HalpLa64PciDmaCoherent();
    }
    else if (!HalpLa64GetPlatformDmaTopology((PDEVICE_OBJECT)Context, &Topology))
    {
        return NULL;
    }
    return HalpLa64CreateDmaAdapter(Description, &Topology, NumberOfMapRegisters);
}

VOID
HalpLa64InitializeDma(
    _In_ struct _LOADER_PARAMETER_BLOCK *LoaderBlock)
{
    PLIST_ENTRY Entry;

    for (Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
         Entry != &LoaderBlock->MemoryDescriptorListHead;
         Entry = Entry->Flink)
    {
        PMEMORY_ALLOCATION_DESCRIPTOR Descriptor =
            CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);
        ULONG64 End = ((ULONG64)Descriptor->BasePage + Descriptor->PageCount) << PAGE_SHIFT;

        if (Descriptor->PageCount && End - 1 > HalpLa64HighestRamAddress)
            HalpLa64HighestRamAddress = End - 1;
    }
    HalGetDmaAdapter = HalpLa64GetDmaAdapter;
}

/* ---------------------------------------------------------------------------
 * Legacy HAL exports
 * ------------------------------------------------------------------------ */

PADAPTER_OBJECT
NTAPI
HalGetAdapter(
    _In_ PDEVICE_DESCRIPTION DeviceDescription,
    _Out_ PULONG NumberOfMapRegisters)
{
    return (PADAPTER_OBJECT)HalpLa64GetDmaAdapter(NULL, DeviceDescription,
                                                  NumberOfMapRegisters);
}

PVOID
NTAPI
HalAllocateCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Length,
    _Out_ PPHYSICAL_ADDRESS LogicalAddress,
    _In_ BOOLEAN CacheEnabled)
{
    return HalpLa64AllocateCommonBuffer((PDMA_ADAPTER)AdapterObject, Length,
                                        LogicalAddress, CacheEnabled);
}

VOID
NTAPI
HalFreeCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Length,
    _In_ PHYSICAL_ADDRESS LogicalAddress,
    _In_ PVOID VirtualAddress,
    _In_ BOOLEAN CacheEnabled)
{
    HalpLa64FreeCommonBuffer((PDMA_ADAPTER)AdapterObject, Length, LogicalAddress,
                             VirtualAddress, CacheEnabled);
}

VOID
NTAPI
HalFlushCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PVOID VirtualAddress,
    _In_ PHYSICAL_ADDRESS LogicalAddress,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(WriteToDevice);
    HalpLa64FlushDmaPhysical((ULONG64)LogicalAddress.QuadPart, Length);
}

ULONG
NTAPI
HalReadDmaCounter(
    _In_ PADAPTER_OBJECT AdapterObject)
{
    return HalpLa64ReadDmaCounter((PDMA_ADAPTER)AdapterObject);
}

PVOID
NTAPI
HalAllocateCrashDumpRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _Inout_ PULONG NumberOfMapRegisters)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    if (NumberOfMapRegisters)
        *NumberOfMapRegisters = 0;
    return NULL;
}

BOOLEAN
NTAPI
HalpLa64AllocateAdapterChannelWrapper(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PWAIT_CONTEXT_BLOCK Wcb,
    _In_ ULONG NumberOfMapRegisters,
    _In_ PDRIVER_CONTROL ExecutionRoutine)
{
    if (!Wcb)
        return FALSE;
    return NT_SUCCESS(HalpLa64AllocateAdapterChannel((PDMA_ADAPTER)AdapterObject,
                                                     Wcb->DeviceObject,
                                                     NumberOfMapRegisters,
                                                     ExecutionRoutine,
                                                     Wcb->DeviceContext));
}

NTSTATUS
NTAPI
HalAllocateAdapterChannel(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PWAIT_CONTEXT_BLOCK Wcb,
    _In_ ULONG NumberOfMapRegisters,
    _In_ PDRIVER_CONTROL ExecutionRoutine)
{
    return HalpLa64AllocateAdapterChannel((PDMA_ADAPTER)AdapterObject,
                                          Wcb ? Wcb->DeviceObject : NULL,
                                          NumberOfMapRegisters, ExecutionRoutine,
                                          Wcb ? Wcb->DeviceContext : NULL);
}

BOOLEAN
NTAPI
IoFlushAdapterBuffers(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    return HalpLa64FlushAdapterBuffers((PDMA_ADAPTER)AdapterObject, Mdl,
                                       MapRegisterBase, CurrentVa, Length,
                                       WriteToDevice);
}

VOID
NTAPI
IoFreeAdapterChannel(
    _In_ PADAPTER_OBJECT AdapterObject)
{
    HalpLa64FreeAdapterChannel((PDMA_ADAPTER)AdapterObject);
}

VOID
NTAPI
IoFreeMapRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PVOID MapRegisterBase,
    _In_ ULONG NumberOfMapRegisters)
{
    HalpLa64FreeMapRegisters((PDMA_ADAPTER)AdapterObject, MapRegisterBase,
                             NumberOfMapRegisters);
}

PHYSICAL_ADDRESS
NTAPI
IoMapTransfer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _Inout_ PULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    return HalpLa64MapTransfer((PDMA_ADAPTER)AdapterObject, Mdl, MapRegisterBase,
                               CurrentVa, Length, WriteToDevice);
}
