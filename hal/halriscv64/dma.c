/*
 * PROJECT:     LiberNT RISC-V HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Bus-master DMA adapters: coherency, address windows and bounce pages
 */

#include <ntifs.h>
#include <arc/arc.h>
#include "halp.h"

#define RISCV_DMA_TAG           'aDvR'
#define RISCV_DMA_SIGNATURE     0x52444D41UL
#define RISCV_DMA_MAP_SIGNATURE 0x52444D52UL
#define RISCV_DMA_POOL_PAGES    256
#define RISCV_DMA_NO_PAGE       MAXULONG
#define RISCV_DMA_FEATURE_SVPBMT 0x00000400

typedef struct _RISCV_DMA_ENTRY
{
    ULONG Page;
    ULONG Length;
    ULONG64 Original;
    ULONG64 Target;
} RISCV_DMA_ENTRY;

typedef struct _RISCV_DMA_MAP
{
    ULONG Signature;
    ULONG Count;
    ULONG Used;
    RISCV_DMA_ENTRY Entry[ANYSIZE_ARRAY];
} RISCV_DMA_MAP;

typedef struct _RISCV_DMA_ADAPTER RISCV_DMA_ADAPTER;

typedef struct _RISCV_DMA_LIST
{
    RISCV_DMA_ADAPTER *Adapter;
    BOOLEAN WriteToDevice;
    BOOLEAN Allocated;
    ULONG Count;
    RISCV_DMA_ENTRY Entry[ANYSIZE_ARRAY];
} RISCV_DMA_LIST;

typedef struct _RISCV_DMA_WAIT
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
} RISCV_DMA_WAIT;

struct _RISCV_DMA_ADAPTER
{
    DMA_ADAPTER Header;
    ULONG Signature;
    BOOLEAN Coherent;
    BOOLEAN Dispatching;
    ULONG AddressBits;
    ULONG Alignment;
    RISCV_DMA_TOPOLOGY Topology;
    KSPIN_LOCK Lock;
    PUCHAR PoolVa;
    ULONG64 PoolPa;
    ULONG PoolPages;
    ULONG PoolFree;
    RTL_BITMAP PoolMap;
    ULONG PoolBits[RISCV_DMA_POOL_PAGES / 32];
    LIST_ENTRY Waiters;
};

static ULONG64 HalpRiscvHighestRamAddress;

static ULONG64
HalpRiscvDmaLimit(_In_ const RISCV_DMA_ADAPTER *Adapter)
{
    return Adapter->AddressBits >= 64 ? MAXULONGLONG : ((1ULL << Adapter->AddressBits) - 1);
}

static BOOLEAN
HalpRiscvDmaTranslate(_In_ const RISCV_DMA_ADAPTER *Adapter, _In_ ULONG64 Cpu,
                      _In_ ULONG Length, _Out_ PULONG64 Bus)
{
    const RISCV_DMA_TOPOLOGY *Topology = &Adapter->Topology;
    ULONG64 Limit = HalpRiscvDmaLimit(Adapter), Last = Cpu + Length - 1;
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
        ULONG64 Base = Topology->CpuBase[Index], Address;

        if (Cpu < Base || Last - Base >= Topology->Size[Index])
            continue;
        Address = Topology->BusBase[Index] + (Cpu - Base);
        if (Address + Length - 1 < Address || Address + Length - 1 > Limit)
            return FALSE;
        *Bus = Address;
        return TRUE;
    }
    return FALSE;
}

static PVOID
HalpRiscvDmaAllocate(_In_ const RISCV_DMA_ADAPTER *Adapter, _In_ SIZE_T Length,
                     _In_ MEMORY_CACHING_TYPE CacheType, _Out_ PULONG64 Physical)
{
    const RISCV_DMA_TOPOLOGY *Topology = &Adapter->Topology;
    ULONG64 Limit = HalpRiscvDmaLimit(Adapter);
    PHYSICAL_ADDRESS Low, High, Boundary;
    ULONG Index, Count = Topology->WindowCount ? Topology->WindowCount : 1;
    PVOID Buffer;

    Boundary.QuadPart = 0;
    for (Index = 0; Index < Count; ++Index)
    {
        ULONG64 First = 0, Last = Limit;

        if (Topology->WindowCount)
        {
            if (Topology->BusBase[Index] > Limit)
                continue;
            First = Topology->CpuBase[Index];
            Last = First + min(Topology->Size[Index], Limit - Topology->BusBase[Index] + 1) - 1;
        }
        Low.QuadPart = (LONGLONG)First;
        High.QuadPart = Last > (ULONG64)MAXLONGLONG ? MAXLONGLONG : (LONGLONG)Last;
        Buffer = MmAllocateContiguousMemorySpecifyCache(Length, Low, High, Boundary, CacheType);
        if (Buffer)
        {
            *Physical = (ULONG64)MmGetPhysicalAddress(Buffer).QuadPart;
            return Buffer;
        }
    }
    return NULL;
}

static VOID
HalpRiscvDmaSync(_In_ const RISCV_DMA_ADAPTER *Adapter, _In_ ULONG64 Physical,
                 _In_ ULONG Length, _In_ BOOLEAN Invalidate)
{
    if (!Adapter->Coherent && KiRiscvIsPhysicalCached(Physical) &&
        !KiRiscvFlushDmaRange(Physical, Length, Invalidate))
        KeBugCheckEx(HAL_MEMORY_ALLOCATION, (ULONG_PTR)Physical, Length, Invalidate, 0x444D41);
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
}

static PUCHAR
HalpRiscvDirectMap(_In_ ULONG64 Physical)
{
    return (PUCHAR)(ULONG_PTR)(RISCV64_LOADER_DIRECT_MAP_BASE + Physical);
}

static BOOLEAN
HalpRiscvDmaNeedsBounce(_In_ const RISCV_DMA_ADAPTER *Adapter, _In_ ULONG64 Physical,
                        _In_ ULONG Length, _In_ BOOLEAN WriteToDevice)
{
    ULONG64 Bus;

    if (!HalpRiscvDmaTranslate(Adapter, Physical, Length, &Bus))
        return TRUE;
    return !Adapter->Coherent && !WriteToDevice &&
           ((Physical | (Physical + Length)) & (Adapter->Alignment - 1)) &&
           KiRiscvIsPhysicalCached(Physical);
}

static BOOLEAN
HalpRiscvMdlFragment(_Inout_ PMDL *Mdl, _Inout_ PULONG_PTR Offset, _In_ ULONG Remaining,
                     _Out_ PULONG64 Physical, _Out_ PULONG Chunk)
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

static ULONG_PTR
HalpRiscvMdlOffset(_In_ PMDL Mdl, _In_opt_ PVOID CurrentVa)
{
    if (Mdl->StartVa)
        return (ULONG_PTR)CurrentVa - (ULONG_PTR)Mdl->StartVa;
    return CurrentVa ? (ULONG_PTR)CurrentVa : Mdl->ByteOffset;
}

static ULONG
HalpRiscvReservePages(_Inout_ RISCV_DMA_ADAPTER *Adapter, _In_ ULONG Count,
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

static VOID
HalpRiscvReleasePage(_Inout_ RISCV_DMA_ADAPTER *Adapter, _In_ ULONG Page)
{
    if (Page == RISCV_DMA_NO_PAGE)
        return;
    RtlClearBits(&Adapter->PoolMap, Page, 1);
    Adapter->PoolFree++;
}

static ULONG64
HalpRiscvPoolPage(_In_ const RISCV_DMA_ADAPTER *Adapter, _In_ ULONG Page)
{
    return Adapter->PoolPa + ((ULONG64)Page << PAGE_SHIFT);
}

static NTSTATUS
HalpRiscvQueueWait(_Inout_ RISCV_DMA_ADAPTER *Adapter, _In_ const RISCV_DMA_WAIT *Template)
{
    RISCV_DMA_WAIT *Wait = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Wait), RISCV_DMA_TAG);

    if (!Wait)
        return STATUS_INSUFFICIENT_RESOURCES;
    *Wait = *Template;
    InsertTailList(&Adapter->Waiters, &Wait->Link);
    return STATUS_SUCCESS;
}

static NTSTATUS
HalpRiscvStartChannel(_Inout_ RISCV_DMA_ADAPTER *Adapter, _In_ PDEVICE_OBJECT DeviceObject,
                      _In_ ULONG NumberOfMapRegisters, _In_ PDRIVER_CONTROL ExecutionRoutine,
                      _In_ PVOID Context);

static NTSTATUS
HalpRiscvStartList(_Inout_ RISCV_DMA_ADAPTER *Adapter, _In_ PDEVICE_OBJECT DeviceObject,
                   _In_ PMDL Mdl, _In_ PVOID CurrentVa, _In_ ULONG Length,
                   _In_ PDRIVER_LIST_CONTROL ExecutionRoutine, _In_ PVOID Context,
                   _In_ BOOLEAN WriteToDevice, _In_opt_ PVOID Buffer, _In_ ULONG BufferLength);

static VOID
HalpRiscvRunWaiters(_Inout_ RISCV_DMA_ADAPTER *Adapter)
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
        RISCV_DMA_WAIT *Wait = CONTAINING_RECORD(Adapter->Waiters.Flink, RISCV_DMA_WAIT, Link);

        if (Wait->Pages > Adapter->PoolFree)
            break;
        RemoveEntryList(&Wait->Link);
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
        if (Wait->List)
            HalpRiscvStartList(Adapter, Wait->DeviceObject, Wait->Mdl, Wait->CurrentVa, Wait->Length,
                               Wait->ListRoutine, Wait->Context, Wait->WriteToDevice,
                               Wait->Buffer, Wait->BufferLength);
        else
            HalpRiscvStartChannel(Adapter, Wait->DeviceObject, Wait->Pages,
                                  Wait->ChannelRoutine, Wait->Context);
        ExFreePoolWithTag(Wait, RISCV_DMA_TAG);
        KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    }
    Adapter->Dispatching = FALSE;
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);
}

static RISCV_DMA_ADAPTER *
HalpRiscvAdapter(_In_opt_ PDMA_ADAPTER DmaAdapter)
{
    RISCV_DMA_ADAPTER *Adapter = (RISCV_DMA_ADAPTER *)DmaAdapter;

    return (Adapter && Adapter->Signature == RISCV_DMA_SIGNATURE) ? Adapter : NULL;
}

static VOID NTAPI
HalpRiscvPutDmaAdapter(PDMA_ADAPTER DmaAdapter)
{
    RISCV_DMA_ADAPTER *Adapter = HalpRiscvAdapter(DmaAdapter);

    if (!Adapter)
        return;
    ASSERT(IsListEmpty(&Adapter->Waiters));
    if (Adapter->PoolVa)
        MmFreeContiguousMemory(Adapter->PoolVa);
    Adapter->Signature = 0;
    ExFreePoolWithTag(Adapter, RISCV_DMA_TAG);
}

static PVOID NTAPI
HalpRiscvAllocateCommonBuffer(PDMA_ADAPTER DmaAdapter, ULONG Length,
                              PPHYSICAL_ADDRESS LogicalAddress, BOOLEAN CacheEnabled)
{
    RISCV_DMA_ADAPTER *Adapter = HalpRiscvAdapter(DmaAdapter);
    ULONG64 Physical, Bus;
    PVOID Buffer;

    UNREFERENCED_PARAMETER(CacheEnabled);
    if (!Adapter || !LogicalAddress || !Length)
        return NULL;
    Buffer = HalpRiscvDmaAllocate(Adapter, Length, Adapter->Coherent ? MmCached : MmNonCached, &Physical);
    if (!Buffer)
        return NULL;
    if (!HalpRiscvDmaTranslate(Adapter, Physical, Length, &Bus))
    {
        MmFreeContiguousMemory(Buffer);
        return NULL;
    }
    LogicalAddress->QuadPart = (LONGLONG)Bus;
    return Buffer;
}

static VOID NTAPI
HalpRiscvFreeCommonBuffer(PDMA_ADAPTER DmaAdapter, ULONG Length,
                          PHYSICAL_ADDRESS LogicalAddress, PVOID VirtualAddress,
                          BOOLEAN CacheEnabled)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(LogicalAddress);
    UNREFERENCED_PARAMETER(CacheEnabled);
    if (VirtualAddress)
        MmFreeContiguousMemory(VirtualAddress);
}

static NTSTATUS
HalpRiscvStartChannel(_Inout_ RISCV_DMA_ADAPTER *Adapter, _In_ PDEVICE_OBJECT DeviceObject,
                      _In_ ULONG NumberOfMapRegisters, _In_ PDRIVER_CONTROL ExecutionRoutine,
                      _In_ PVOID Context)
{
    RISCV_DMA_MAP *Map = NULL;
    IO_ALLOCATION_ACTION Action;
    KIRQL OldIrql;
    ULONG Index;

    if (Adapter->PoolPages)
    {
        ULONG Pages[RISCV_DMA_POOL_PAGES];

        Map = ExAllocatePoolWithTag(NonPagedPool,
                                    FIELD_OFFSET(RISCV_DMA_MAP, Entry[NumberOfMapRegisters]),
                                    RISCV_DMA_TAG);
        if (!Map)
            return STATUS_INSUFFICIENT_RESOURCES;
        KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
        if (!HalpRiscvReservePages(Adapter, NumberOfMapRegisters, Pages))
        {
            RISCV_DMA_WAIT Wait;
            NTSTATUS Status;

            RtlZeroMemory(&Wait, sizeof(Wait));
            Wait.Pages = NumberOfMapRegisters;
            Wait.DeviceObject = DeviceObject;
            Wait.Context = Context;
            Wait.ChannelRoutine = ExecutionRoutine;
            Status = HalpRiscvQueueWait(Adapter, &Wait);
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
            ExFreePoolWithTag(Map, RISCV_DMA_TAG);
            return Status;
        }
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
        Map->Signature = RISCV_DMA_MAP_SIGNATURE;
        Map->Count = NumberOfMapRegisters;
        Map->Used = 0;
        for (Index = 0; Index < NumberOfMapRegisters; ++Index)
            Map->Entry[Index].Page = Pages[Index];
    }

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    Action = ExecutionRoutine(DeviceObject, DeviceObject->CurrentIrp, Map, Context);
    KeLowerIrql(OldIrql);
    if (Action == DeallocateObject && Map)
        Adapter->Header.DmaOperations->FreeMapRegisters(&Adapter->Header, Map, NumberOfMapRegisters);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
HalpRiscvAllocateAdapterChannel(PDMA_ADAPTER DmaAdapter, PDEVICE_OBJECT DeviceObject,
                                ULONG NumberOfMapRegisters,
                                PDRIVER_CONTROL ExecutionRoutine, PVOID Context)
{
    RISCV_DMA_ADAPTER *Adapter = HalpRiscvAdapter(DmaAdapter);

    if (!Adapter || !DeviceObject || !ExecutionRoutine || KeGetCurrentIrql() > DISPATCH_LEVEL ||
        !NumberOfMapRegisters)
        return STATUS_INVALID_PARAMETER;
    if (Adapter->PoolPages && NumberOfMapRegisters > Adapter->PoolPages)
        return STATUS_INSUFFICIENT_RESOURCES;
    return HalpRiscvStartChannel(Adapter, DeviceObject, NumberOfMapRegisters, ExecutionRoutine, Context);
}

NTSTATUS NTAPI
HalAllocateAdapterChannel(PADAPTER_OBJECT AdapterObject, PWAIT_CONTEXT_BLOCK Wcb,
                          ULONG NumberOfMapRegisters, PDRIVER_CONTROL ExecutionRoutine)
{
    if (!Wcb)
        return STATUS_INVALID_PARAMETER;
    return HalpRiscvAllocateAdapterChannel((PDMA_ADAPTER)AdapterObject, Wcb->DeviceObject,
                                           NumberOfMapRegisters, ExecutionRoutine,
                                           Wcb->DeviceContext);
}

static BOOLEAN NTAPI
HalpRiscvFlushAdapterBuffers(PDMA_ADAPTER DmaAdapter, PMDL Mdl,
                             PVOID MapRegisterBase, PVOID CurrentVa,
                             ULONG Length, BOOLEAN WriteToDevice)
{
    RISCV_DMA_ADAPTER *Adapter = HalpRiscvAdapter(DmaAdapter);
    RISCV_DMA_MAP *Map = MapRegisterBase;
    ULONG Index;

    if (!Adapter)
        return FALSE;
    if (Map && Map->Signature != RISCV_DMA_MAP_SIGNATURE)
        return FALSE;
    if (!WriteToDevice && Mdl && !Adapter->Coherent)
    {
        ULONG_PTR Offset = HalpRiscvMdlOffset(Mdl, CurrentVa);
        ULONG Remaining = Length, Chunk;
        ULONG64 Physical;
        PMDL Current = Mdl;

        while (Remaining && HalpRiscvMdlFragment(&Current, &Offset, Remaining, &Physical, &Chunk))
        {
            if (!HalpRiscvDmaNeedsBounce(Adapter, Physical, Chunk, FALSE))
                HalpRiscvDmaSync(Adapter, Physical, Chunk, TRUE);
            Offset += Chunk;
            Remaining -= Chunk;
        }
    }
    if (Map)
    {
        for (Index = 0; Index < Map->Used; ++Index)
        {
            RISCV_DMA_ENTRY *Entry = &Map->Entry[Index];

            if (!WriteToDevice)
            {
                HalpRiscvDmaSync(Adapter, Entry->Target, Entry->Length, TRUE);
                RtlCopyMemory(HalpRiscvDirectMap(Entry->Original), HalpRiscvDirectMap(Entry->Target),
                              Entry->Length);
            }
        }
        Map->Used = 0;
    }
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
    return TRUE;
}

static VOID NTAPI
HalpRiscvFreeAdapterChannel(PDMA_ADAPTER DmaAdapter)
{
    RISCV_DMA_ADAPTER *Adapter = HalpRiscvAdapter(DmaAdapter);

    if (Adapter)
        HalpRiscvRunWaiters(Adapter);
}

static VOID NTAPI
HalpRiscvFreeMapRegisters(PDMA_ADAPTER DmaAdapter, PVOID MapRegisterBase,
                          ULONG NumberOfMapRegisters)
{
    RISCV_DMA_ADAPTER *Adapter = HalpRiscvAdapter(DmaAdapter);
    RISCV_DMA_MAP *Map = MapRegisterBase;
    KIRQL OldIrql;
    ULONG Index;

    UNREFERENCED_PARAMETER(NumberOfMapRegisters);
    if (!Adapter || !Map || Map->Signature != RISCV_DMA_MAP_SIGNATURE)
        return;
    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    for (Index = 0; Index < Map->Count; ++Index)
        HalpRiscvReleasePage(Adapter, Map->Entry[Index].Page);
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    Map->Signature = 0;
    ExFreePoolWithTag(Map, RISCV_DMA_TAG);
    HalpRiscvRunWaiters(Adapter);
}

static PHYSICAL_ADDRESS NTAPI
HalpRiscvMapTransfer(PDMA_ADAPTER DmaAdapter, PMDL Mdl,
                     PVOID MapRegisterBase, PVOID CurrentVa,
                     PULONG Length, BOOLEAN WriteToDevice)
{
    RISCV_DMA_ADAPTER *Adapter = HalpRiscvAdapter(DmaAdapter);
    RISCV_DMA_MAP *Map = MapRegisterBase;
    PHYSICAL_ADDRESS Address;
    ULONG_PTR Offset;
    ULONG64 Physical, Bus;
    ULONG Chunk;
    PMDL Current = Mdl;

    Address.QuadPart = 0;
    if (!Adapter || !Length || !*Length || !Mdl ||
        (Map && Map->Signature != RISCV_DMA_MAP_SIGNATURE))
        goto Fail;
    Offset = HalpRiscvMdlOffset(Mdl, CurrentVa);
    if (Offset < Mdl->ByteOffset || Offset - Mdl->ByteOffset >= Mdl->ByteCount ||
        !HalpRiscvMdlFragment(&Current, &Offset, *Length, &Physical, &Chunk) || Current != Mdl)
        goto Fail;

    if (HalpRiscvDmaNeedsBounce(Adapter, Physical, Chunk, WriteToDevice))
    {
        RISCV_DMA_ENTRY *Entry;

        if (!Map || Map->Used >= Map->Count)
            goto Fail;
        Entry = &Map->Entry[Map->Used];
        Entry->Original = Physical;
        Entry->Length = Chunk;
        Entry->Target = HalpRiscvPoolPage(Adapter, Entry->Page) + (Physical & (PAGE_SIZE - 1));
        if (WriteToDevice)
            RtlCopyMemory(HalpRiscvDirectMap(Entry->Target), HalpRiscvDirectMap(Physical), Chunk);
        Physical = Entry->Target;
        Map->Used++;
    }
    else if (Adapter->Coherent)
    {
        while (Chunk < *Length)
        {
            ULONG_PTR NextOffset = Offset + Chunk;
            ULONG64 NextPhysical;
            ULONG NextChunk;
            PMDL Next = Current;

            if (!HalpRiscvMdlFragment(&Next, &NextOffset, *Length - Chunk, &NextPhysical, &NextChunk) ||
                Next != Current || NextPhysical != Physical + Chunk ||
                !HalpRiscvDmaTranslate(Adapter, Physical, Chunk + NextChunk, &Bus))
            {
                break;
            }
            Chunk += NextChunk;
        }
    }
    if (!HalpRiscvDmaTranslate(Adapter, Physical, Chunk, &Bus))
        goto Fail;
    HalpRiscvDmaSync(Adapter, Physical, Chunk, !WriteToDevice);
    *Length = Chunk;
    Address.QuadPart = (LONGLONG)Bus;
    return Address;

Fail:
    if (Length)
        *Length = 0;
    Address.QuadPart = 0;
    return Address;
}

static ULONG NTAPI
HalpRiscvGetDmaAlignment(PDMA_ADAPTER DmaAdapter)
{
    RISCV_DMA_ADAPTER *Adapter = HalpRiscvAdapter(DmaAdapter);

    return Adapter ? Adapter->Alignment : 1;
}

static ULONG NTAPI
HalpRiscvReadDmaCounter(PDMA_ADAPTER DmaAdapter)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    return 0;
}

static ULONG
HalpRiscvCountFragments(_In_opt_ PMDL Mdl, _In_ PVOID CurrentVa, _In_ ULONG Length)
{
    ULONG_PTR Offset;
    ULONG64 Physical;
    ULONG Count = 0, Chunk;
    PMDL Current = Mdl;

    if (!Mdl)
        return ADDRESS_AND_SIZE_TO_SPAN_PAGES(CurrentVa, Length);
    Offset = HalpRiscvMdlOffset(Mdl, CurrentVa);
    while (Length && HalpRiscvMdlFragment(&Current, &Offset, Length, &Physical, &Chunk))
    {
        Count++;
        Offset += Chunk;
        Length -= Chunk;
    }
    return Length ? 0 : Count;
}

static SIZE_T
HalpRiscvListSize(_In_ ULONG Count, _Out_opt_ PSIZE_T PrivateOffset)
{
    SIZE_T Offset = ALIGN_UP_BY(FIELD_OFFSET(SCATTER_GATHER_LIST, Elements[Count]), sizeof(ULONG64));

    if (PrivateOffset)
        *PrivateOffset = Offset;
    return Offset + FIELD_OFFSET(RISCV_DMA_LIST, Entry[Count]);
}

static NTSTATUS NTAPI
HalpRiscvCalculateScatterGatherList(PDMA_ADAPTER DmaAdapter, PMDL Mdl, PVOID CurrentVa,
                                    ULONG Length, PULONG ScatterGatherListSize,
                                    PULONG NumberOfMapRegisters)
{
    ULONG Count = HalpRiscvCountFragments(Mdl, CurrentVa, Length);

    if (!HalpRiscvAdapter(DmaAdapter) || !ScatterGatherListSize || !Count)
        return STATUS_INVALID_PARAMETER;
    *ScatterGatherListSize = (ULONG)HalpRiscvListSize(Count, NULL);
    if (NumberOfMapRegisters)
        *NumberOfMapRegisters = Count;
    return STATUS_SUCCESS;
}

static NTSTATUS
HalpRiscvStartList(_Inout_ RISCV_DMA_ADAPTER *Adapter, _In_ PDEVICE_OBJECT DeviceObject,
                   _In_ PMDL Mdl, _In_ PVOID CurrentVa, _In_ ULONG Length,
                   _In_ PDRIVER_LIST_CONTROL ExecutionRoutine, _In_ PVOID Context,
                   _In_ BOOLEAN WriteToDevice, _In_opt_ PVOID Buffer, _In_ ULONG BufferLength)
{
    ULONG Count = HalpRiscvCountFragments(Mdl, CurrentVa, Length), Bounce = 0, Index, Chunk;
    ULONG Pages[RISCV_DMA_POOL_PAGES];
    PSCATTER_GATHER_LIST List;
    RISCV_DMA_LIST *Private;
    SIZE_T Size, PrivateOffset;
    ULONG_PTR Offset;
    ULONG64 Physical;
    PMDL Current;
    KIRQL OldIrql;

    if (!Count)
        return STATUS_INVALID_PARAMETER;
    Size = HalpRiscvListSize(Count, &PrivateOffset);
    if (Buffer && BufferLength < Size)
        return STATUS_BUFFER_TOO_SMALL;

    Offset = HalpRiscvMdlOffset(Mdl, CurrentVa);
    Current = Mdl;
    for (Index = 0, Chunk = Length; Chunk;)
    {
        ULONG Piece;

        if (!HalpRiscvMdlFragment(&Current, &Offset, Chunk, &Physical, &Piece))
            return STATUS_INVALID_PARAMETER;
        if (HalpRiscvDmaNeedsBounce(Adapter, Physical, Piece, WriteToDevice))
            Bounce++;
        Offset += Piece;
        Chunk -= Piece;
    }
    if (Bounce > Adapter->PoolPages)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (Bounce)
    {
        KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
        if (!HalpRiscvReservePages(Adapter, Bounce, Pages))
        {
            RISCV_DMA_WAIT Wait;
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
            Status = HalpRiscvQueueWait(Adapter, &Wait);
            KeReleaseSpinLock(&Adapter->Lock, OldIrql);
            return Status;
        }
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    }

    List = Buffer ? Buffer : ExAllocatePoolWithTag(NonPagedPool, Size, RISCV_DMA_TAG);
    if (!List)
    {
        KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
        for (Index = 0; Index < Bounce; ++Index)
            HalpRiscvReleasePage(Adapter, Pages[Index]);
        KeReleaseSpinLock(&Adapter->Lock, OldIrql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Private = (RISCV_DMA_LIST *)((PUCHAR)List + PrivateOffset);
    Private->Adapter = Adapter;
    Private->WriteToDevice = WriteToDevice;
    Private->Allocated = (Buffer == NULL);
    Private->Count = Count;
    List->NumberOfElements = Count;
    List->Reserved = (ULONG_PTR)Private;

    Offset = HalpRiscvMdlOffset(Mdl, CurrentVa);
    Current = Mdl;
    Bounce = 0;
    for (Index = 0, Chunk = Length; Index < Count; ++Index)
    {
        RISCV_DMA_ENTRY *Entry = &Private->Entry[Index];
        ULONG Piece;
        ULONG64 Bus;

        HalpRiscvMdlFragment(&Current, &Offset, Chunk, &Physical, &Piece);
        Entry->Original = Physical;
        Entry->Length = Piece;
        Entry->Page = RISCV_DMA_NO_PAGE;
        Entry->Target = Physical;
        if (HalpRiscvDmaNeedsBounce(Adapter, Physical, Piece, WriteToDevice))
        {
            Entry->Page = Pages[Bounce++];
            Entry->Target = HalpRiscvPoolPage(Adapter, Entry->Page) + (Physical & (PAGE_SIZE - 1));
            if (WriteToDevice)
                RtlCopyMemory(HalpRiscvDirectMap(Entry->Target), HalpRiscvDirectMap(Physical), Piece);
        }
        HalpRiscvDmaTranslate(Adapter, Entry->Target, Piece, &Bus);
        HalpRiscvDmaSync(Adapter, Entry->Target, Piece, !WriteToDevice);
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

static NTSTATUS NTAPI
HalpRiscvGetScatterGatherList(PDMA_ADAPTER DmaAdapter, PDEVICE_OBJECT DeviceObject, PMDL Mdl,
                              PVOID CurrentVa, ULONG Length, PDRIVER_LIST_CONTROL ExecutionRoutine,
                              PVOID Context, BOOLEAN WriteToDevice)
{
    RISCV_DMA_ADAPTER *Adapter = HalpRiscvAdapter(DmaAdapter);

    if (!Adapter || !DeviceObject || !Mdl || !Length || !ExecutionRoutine)
        return STATUS_INVALID_PARAMETER;
    return HalpRiscvStartList(Adapter, DeviceObject, Mdl, CurrentVa, Length, ExecutionRoutine,
                              Context, WriteToDevice, NULL, 0);
}

static NTSTATUS NTAPI
HalpRiscvBuildScatterGatherList(PDMA_ADAPTER DmaAdapter, PDEVICE_OBJECT DeviceObject, PMDL Mdl,
                                PVOID CurrentVa, ULONG Length, PDRIVER_LIST_CONTROL ExecutionRoutine,
                                PVOID Context, BOOLEAN WriteToDevice, PVOID ScatterGatherBuffer,
                                ULONG ScatterGatherLength)
{
    RISCV_DMA_ADAPTER *Adapter = HalpRiscvAdapter(DmaAdapter);

    if (!Adapter || !DeviceObject || !Mdl || !Length || !ExecutionRoutine || !ScatterGatherBuffer)
        return STATUS_INVALID_PARAMETER;
    return HalpRiscvStartList(Adapter, DeviceObject, Mdl, CurrentVa, Length, ExecutionRoutine,
                              Context, WriteToDevice, ScatterGatherBuffer, ScatterGatherLength);
}

static VOID NTAPI
HalpRiscvPutScatterGatherList(PDMA_ADAPTER DmaAdapter, PSCATTER_GATHER_LIST ScatterGather,
                              BOOLEAN WriteToDevice)
{
    RISCV_DMA_ADAPTER *Adapter = HalpRiscvAdapter(DmaAdapter);
    RISCV_DMA_LIST *Private;
    KIRQL OldIrql;
    ULONG Index;

    UNREFERENCED_PARAMETER(WriteToDevice);
    if (!Adapter || !ScatterGather)
        return;
    Private = (RISCV_DMA_LIST *)ScatterGather->Reserved;
    if (!Private || Private->Adapter != Adapter)
        return;
    for (Index = 0; Index < Private->Count; ++Index)
    {
        RISCV_DMA_ENTRY *Entry = &Private->Entry[Index];

        if (Private->WriteToDevice)
            continue;
        HalpRiscvDmaSync(Adapter, Entry->Target, Entry->Length, TRUE);
        if (Entry->Page != RISCV_DMA_NO_PAGE)
            RtlCopyMemory(HalpRiscvDirectMap(Entry->Original), HalpRiscvDirectMap(Entry->Target),
                          Entry->Length);
    }
    KeAcquireSpinLock(&Adapter->Lock, &OldIrql);
    for (Index = 0; Index < Private->Count; ++Index)
        HalpRiscvReleasePage(Adapter, Private->Entry[Index].Page);
    KeReleaseSpinLock(&Adapter->Lock, OldIrql);
    ScatterGather->Reserved = 0;
    if (Private->Allocated)
        ExFreePoolWithTag(ScatterGather, RISCV_DMA_TAG);
    HalpRiscvRunWaiters(Adapter);
}

static NTSTATUS NTAPI
HalpRiscvBuildMdlFromScatterGatherList(PDMA_ADAPTER DmaAdapter, PSCATTER_GATHER_LIST ScatterGather,
                                       PMDL OriginalMdl, PMDL *TargetMdl)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    UNREFERENCED_PARAMETER(ScatterGather);
    UNREFERENCED_PARAMETER(OriginalMdl);
    if (TargetMdl)
        *TargetMdl = NULL;
    return STATUS_NOT_SUPPORTED;
}

static DMA_OPERATIONS HalpRiscvDmaOperations =
{
    .Size = FIELD_OFFSET(DMA_OPERATIONS, GetDmaAdapterInfo),
    .PutDmaAdapter = HalpRiscvPutDmaAdapter,
    .AllocateCommonBuffer = HalpRiscvAllocateCommonBuffer,
    .FreeCommonBuffer = HalpRiscvFreeCommonBuffer,
    .AllocateAdapterChannel = HalpRiscvAllocateAdapterChannel,
    .FlushAdapterBuffers = HalpRiscvFlushAdapterBuffers,
    .FreeAdapterChannel = HalpRiscvFreeAdapterChannel,
    .FreeMapRegisters = HalpRiscvFreeMapRegisters,
    .MapTransfer = HalpRiscvMapTransfer,
    .GetDmaAlignment = HalpRiscvGetDmaAlignment,
    .ReadDmaCounter = HalpRiscvReadDmaCounter,
    .GetScatterGatherList = HalpRiscvGetScatterGatherList,
    .PutScatterGatherList = HalpRiscvPutScatterGatherList,
    .CalculateScatterGatherList = HalpRiscvCalculateScatterGatherList,
    .BuildScatterGatherList = HalpRiscvBuildScatterGatherList,
    .BuildMdlFromScatterGatherList = HalpRiscvBuildMdlFromScatterGatherList
};

PDMA_ADAPTER
HalpRiscvCreateDmaAdapter(_In_ PDEVICE_DESCRIPTION Description,
                          _In_ const RISCV_DMA_TOPOLOGY *Topology,
                          _Out_opt_ PULONG NumberOfMapRegisters)
{
    RISCV_DMA_ADAPTER *Adapter;
    ULONG Block = KiRiscvQueryCacheBlockSize(), Registers;

    if (NumberOfMapRegisters)
        *NumberOfMapRegisters = 0;
    if (!Description || !Description->Master || Description->Version > DEVICE_DESCRIPTION_VERSION3)
        return NULL;
    if (!Topology->Coherent &&
        (!Block || !(KiRiscvQueryFeatureFlags() & RISCV_DMA_FEATURE_SVPBMT)))
        return NULL;

    Adapter = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Adapter), RISCV_DMA_TAG);
    if (!Adapter)
        return NULL;
    RtlZeroMemory(Adapter, sizeof(*Adapter));
    Adapter->Header.Version = (USHORT)Description->Version;
    Adapter->Header.Size = sizeof(*Adapter);
    Adapter->Header.DmaOperations = &HalpRiscvDmaOperations;
    Adapter->Signature = RISCV_DMA_SIGNATURE;
    Adapter->Coherent = Topology->Coherent;
    Adapter->Topology = *Topology;
    Adapter->AddressBits = (Description->Dma64BitAddresses ||
                            (Description->Version >= DEVICE_DESCRIPTION_VERSION3 &&
                             Description->DmaAddressWidth >= 64)) ? 64 :
                           (Description->Version >= DEVICE_DESCRIPTION_VERSION3 &&
                            Description->DmaAddressWidth > 0 && Description->DmaAddressWidth < 32)
                               ? Description->DmaAddressWidth : 32;
    Adapter->Alignment = Adapter->Coherent ? 1 : Block;
    KeInitializeSpinLock(&Adapter->Lock);
    InitializeListHead(&Adapter->Waiters);

    if (!Adapter->Coherent || Topology->WindowCount ||
        HalpRiscvHighestRamAddress > HalpRiscvDmaLimit(Adapter))
    {
        Adapter->PoolVa = HalpRiscvDmaAllocate(Adapter, RISCV_DMA_POOL_PAGES * PAGE_SIZE, MmCached,
                                               &Adapter->PoolPa);
        if (!Adapter->PoolVa)
        {
            ExFreePoolWithTag(Adapter, RISCV_DMA_TAG);
            return NULL;
        }
        Adapter->PoolPages = RISCV_DMA_POOL_PAGES;
        Adapter->PoolFree = RISCV_DMA_POOL_PAGES;
        RtlInitializeBitMap(&Adapter->PoolMap, Adapter->PoolBits, RISCV_DMA_POOL_PAGES);
        RtlClearAllBits(&Adapter->PoolMap);
    }

    Registers = Description->MaximumLength ? BYTES_TO_PAGES(Description->MaximumLength) + 1 : 64;
    if (Adapter->PoolPages)
        Registers = min(Registers, Adapter->PoolPages);
    else
        Registers = min(Registers, RISCV_DMA_POOL_PAGES);
    if (NumberOfMapRegisters)
        *NumberOfMapRegisters = Registers;
    return &Adapter->Header;
}

static PDMA_ADAPTER NTAPI
HalpRiscvGetDmaAdapter(_In_ PVOID Context, _In_ PDEVICE_DESCRIPTION Description,
                       _Out_ PULONG NumberOfMapRegisters)
{
    RISCV_DMA_TOPOLOGY Topology;
    ULONG FirstBus, LastBus;

    if (NumberOfMapRegisters)
        *NumberOfMapRegisters = 0;
    if (!Description)
        return NULL;
    RtlZeroMemory(&Topology, sizeof(Topology));
    if (Description->InterfaceType == PCIBus)
    {
        if (!HalpRiscvGetPciBusRange(&FirstBus, &LastBus) ||
            Description->BusNumber < FirstBus || Description->BusNumber > LastBus)
            return NULL;
        Topology.Coherent = HalpRiscvPciDmaCoherent();
    }
    else if (!HalpRiscvGetPlatformDmaTopology((PDEVICE_OBJECT)Context, &Topology))
    {
        return NULL;
    }
    return HalpRiscvCreateDmaAdapter(Description, &Topology, NumberOfMapRegisters);
}

VOID
HalpRiscvInitializeDma(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLIST_ENTRY Entry;

    for (Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
         Entry != &LoaderBlock->MemoryDescriptorListHead;
         Entry = Entry->Flink)
    {
        PMEMORY_ALLOCATION_DESCRIPTOR Descriptor =
            CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);
        ULONG64 End = ((ULONG64)Descriptor->BasePage + Descriptor->PageCount) << PAGE_SHIFT;

        if (Descriptor->PageCount && End - 1 > HalpRiscvHighestRamAddress)
            HalpRiscvHighestRamAddress = End - 1;
    }
    HalGetDmaAdapter = HalpRiscvGetDmaAdapter;
}

PADAPTER_OBJECT NTAPI
HalGetAdapter(PDEVICE_DESCRIPTION DeviceDescription, PULONG NumberOfMapRegisters)
{
    return (PADAPTER_OBJECT)HalpRiscvGetDmaAdapter(NULL, DeviceDescription, NumberOfMapRegisters);
}

PVOID
NTAPI
HalAllocateCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Length,
    _Out_ PPHYSICAL_ADDRESS LogicalAddress,
    _In_ BOOLEAN CacheEnabled)
{
    return HalpRiscvAllocateCommonBuffer((PDMA_ADAPTER)AdapterObject, Length,
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
    HalpRiscvFreeCommonBuffer((PDMA_ADAPTER)AdapterObject, Length, LogicalAddress,
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
    UNREFERENCED_PARAMETER(LogicalAddress);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(WriteToDevice);
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
}

ULONG
NTAPI
HalReadDmaCounter(
    _In_ PADAPTER_OBJECT AdapterObject)
{
    return HalpRiscvReadDmaCounter((PDMA_ADAPTER)AdapterObject);
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
IoFlushAdapterBuffers(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    return HalpRiscvFlushAdapterBuffers((PDMA_ADAPTER)AdapterObject, Mdl, MapRegisterBase,
                                        CurrentVa, Length, WriteToDevice);
}

VOID
NTAPI
IoFreeAdapterChannel(
    _In_ PADAPTER_OBJECT AdapterObject)
{
    HalpRiscvFreeAdapterChannel((PDMA_ADAPTER)AdapterObject);
}

VOID
NTAPI
IoFreeMapRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PVOID MapRegisterBase,
    _In_ ULONG NumberOfMapRegisters)
{
    HalpRiscvFreeMapRegisters((PDMA_ADAPTER)AdapterObject, MapRegisterBase,
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
    return HalpRiscvMapTransfer((PDMA_ADAPTER)AdapterObject, Mdl, MapRegisterBase,
                                CurrentVa, Length, WriteToDevice);
}
