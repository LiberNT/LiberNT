/*
 * PROJECT:     LiberNT Processor Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     CPPC registers in a Platform Communications Channel (ACPI 6.5, chapter 14)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "processr.h"
#include <aux_klib.h>

#define NDEBUG
#include <debug.h>

#define PROC_PCC_MAX_SUBSPACES          256
#define PROC_PCC_TABLE_HEADER           48
#define PROC_PCC_GENERIC_LENGTH         62
#define PROC_PCC_SIGNATURE              0x50434300
#define PROC_PCC_HEADER                 8
#define PROC_PCC_STATUS_COMPLETE        0x0001
#define PROC_PCC_STATUS_ERROR           0x0004
#define PROC_PCC_COMMAND_READ           0x00
#define PROC_PCC_COMMAND_WRITE          0x01
#define PROC_PCC_RETRIES                500
#define PROC_PCC_POLL_US                2

/* Generic, HW-reduced and HW-reduced type 2 subspaces share their layout from the base address on */
#define PROC_PCC_BASE_OFFSET            8
#define PROC_PCC_LENGTH_OFFSET          16
#define PROC_PCC_DOORBELL_OFFSET        24
#define PROC_PCC_PRESERVE_OFFSET        36
#define PROC_PCC_WRITE_OFFSET           44
#define PROC_PCC_LATENCY_OFFSET         52
#define PROC_PCC_TURNAROUND_OFFSET      60

typedef struct _PROC_PCC_SUBSPACE
{
    KSPIN_LOCK Lock;
    ULONG References;
    UCHAR Id;
    PUCHAR Shared;
    SIZE_T SharedLength;
    PROC_REGISTER Doorbell;
    ULONGLONG DoorbellPreserve;
    ULONGLONG DoorbellWrite;
    ULONG Latency;
    ULONG Turnaround;
    LARGE_INTEGER LastCompletion;
} PROC_PCC_SUBSPACE, *PPROC_PCC_SUBSPACE;

static PPROC_PCC_SUBSPACE ProcPccSubspaces[PROC_PCC_MAX_SUBSPACES];
static KMUTEX ProcPccMutex;
static volatile LONG ProcPccInitialized;

VOID
ProcPccInitialize(VOID)
{
    KeInitializeMutex(&ProcPccMutex, 0);
    InterlockedExchange(&ProcPccInitialized, 1);
}

static
BOOLEAN
ProcPccTableRegister(
    _In_reads_bytes_(12) const UCHAR *Gas,
    _Out_ PPROC_REGISTER Register)
{
    RtlZeroMemory(Register, sizeof(*Register));
    Register->SpaceId = Gas[0];
    Register->BitWidth = Gas[1];
    Register->BitOffset = Gas[2];
    Register->AccessSize = Gas[3];
    RtlCopyMemory(&Register->Address, &Gas[4], sizeof(Register->Address));
    Register->Valid = (Register->BitWidth != 0) && (Register->BitWidth <= 64) &&
                      ((ULONG)Register->BitWidth + Register->BitOffset <= 64);
    return Register->Valid && ProcPrepareRegister(Register);
}

static
PPROC_PCC_SUBSPACE
ProcPccCreateSubspace(
    _In_ UCHAR Id)
{
    PPROC_PCC_SUBSPACE Subspace = NULL;
    PHYSICAL_ADDRESS Physical;
    ULONGLONG Base, Length;
    PUCHAR Table = NULL, Entry, End;
    ULONG TableLength = 0;
    ULONG Signature;
    UCHAR Index;
    NTSTATUS Status;

    PAGED_CODE();

    Status = AuxKlibGetSystemFirmwareTable(PROC_METHOD('I', 'P', 'C', 'A'), PROC_METHOD('P', 'C', 'C', 'T'),
                                           NULL, 0, &TableLength);
    if ((Status != STATUS_BUFFER_TOO_SMALL) || (TableLength <= PROC_PCC_TABLE_HEADER))
    {
        DPRINT1("No PCCT for PCC subspace %u: 0x%08lx\n", Id, Status);
        return NULL;
    }

    Table = ExAllocatePoolWithTag(PagedPool, TableLength, PROC_TAG);
    if (Table == NULL)
        return NULL;

    Status = AuxKlibGetSystemFirmwareTable(PROC_METHOD('I', 'P', 'C', 'A'), PROC_METHOD('P', 'C', 'C', 'T'),
                                           Table, TableLength, &TableLength);
    if (!NT_SUCCESS(Status))
        goto Exit;

    End = Table + TableLength;
    Entry = Table + PROC_PCC_TABLE_HEADER;
    for (Index = 0; (Entry + 2 <= End) && (Entry[1] >= 2) && (Entry + Entry[1] <= End); Index++, Entry += Entry[1])
    {
        if (Index != Id)
            continue;

        /* TODO: the extended subspaces of types 3 and 4 are not driven */
        if ((Entry[0] > 2) || (Entry[1] < PROC_PCC_GENERIC_LENGTH))
        {
            DPRINT1("PCC subspace %u has type %u\n", Id, Entry[0]);
            goto Exit;
        }

        Subspace = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Subspace), PROC_TAG);
        if (Subspace == NULL)
            goto Exit;

        KeInitializeSpinLock(&Subspace->Lock);
        Subspace->Id = Id;
        RtlCopyMemory(&Base, Entry + PROC_PCC_BASE_OFFSET, sizeof(Base));
        RtlCopyMemory(&Length, Entry + PROC_PCC_LENGTH_OFFSET, sizeof(Length));
        RtlCopyMemory(&Subspace->DoorbellPreserve, Entry + PROC_PCC_PRESERVE_OFFSET, sizeof(ULONGLONG));
        RtlCopyMemory(&Subspace->DoorbellWrite, Entry + PROC_PCC_WRITE_OFFSET, sizeof(ULONGLONG));
        RtlCopyMemory(&Subspace->Latency, Entry + PROC_PCC_LATENCY_OFFSET, sizeof(ULONG));
        Subspace->Turnaround = *(USHORT UNALIGNED *)(Entry + PROC_PCC_TURNAROUND_OFFSET);

        if ((Length <= PROC_PCC_HEADER) || (Length > MAXULONG) ||
            !ProcPccTableRegister(Entry + PROC_PCC_DOORBELL_OFFSET, &Subspace->Doorbell))
        {
            DPRINT1("PCC subspace %u is not usable\n", Id);
            ProcReleaseRegister(&Subspace->Doorbell);
            ExFreePoolWithTag(Subspace, PROC_TAG);
            Subspace = NULL;
            goto Exit;
        }

        Physical.QuadPart = (LONGLONG)Base;
        Subspace->SharedLength = (SIZE_T)Length;
        Subspace->Shared = MmMapIoSpace(Physical, Subspace->SharedLength, MmNonCached);
        Signature = (Subspace->Shared != NULL) ? READ_REGISTER_ULONG((PULONG)Subspace->Shared) : 0;
        if (Signature != (PROC_PCC_SIGNATURE | Id))
        {
            DPRINT1("PCC subspace %u signature 0x%08lx\n", Id, Signature);
            if (Subspace->Shared != NULL)
                MmUnmapIoSpace(Subspace->Shared, Subspace->SharedLength);
            ProcReleaseRegister(&Subspace->Doorbell);
            ExFreePoolWithTag(Subspace, PROC_TAG);
            Subspace = NULL;
        }

        goto Exit;
    }

    DPRINT1("The PCCT has no subspace %u\n", Id);

Exit:
    ExFreePoolWithTag(Table, PROC_TAG);
    return Subspace;
}

static
ULONG
ProcPccBytes(
    _In_ PPROC_REGISTER Register)
{
    ULONG Bits = (ULONG)Register->BitWidth + Register->BitOffset;

    return (Bits <= 8) ? 1 : (Bits <= 16) ? 2 : (Bits <= 32) ? 4 : 8;
}

/* For a PCC register the access size field names the subspace and the address is an offset in its communication space */
BOOLEAN
ProcPccPrepare(
    _Inout_ PPROC_REGISTER Register)
{
    PPROC_PCC_SUBSPACE Subspace;
    UCHAR Id = Register->AccessSize;

    PAGED_CODE();
    ASSERT(Register->SpaceId == PROC_SPACE_PCC);

    if (InterlockedCompareExchange(&ProcPccInitialized, 1, 1) != 1)
        return FALSE;

    KeWaitForSingleObject(&ProcPccMutex, Executive, KernelMode, FALSE, NULL);
    Subspace = ProcPccSubspaces[Id];
    if (Subspace == NULL)
    {
        Subspace = ProcPccCreateSubspace(Id);
        ProcPccSubspaces[Id] = Subspace;
    }

    if ((Subspace == NULL) ||
        (Register->Address > Subspace->SharedLength - PROC_PCC_HEADER - ProcPccBytes(Register)))
    {
        KeReleaseMutex(&ProcPccMutex, FALSE);
        return FALSE;
    }

    Subspace->References++;
    KeReleaseMutex(&ProcPccMutex, FALSE);

    Register->Mapped = Subspace->Shared + PROC_PCC_HEADER + (SIZE_T)Register->Address;
    return TRUE;
}

VOID
ProcPccRelease(
    _Inout_ PPROC_REGISTER Register)
{
    PPROC_PCC_SUBSPACE Subspace;

    PAGED_CODE();
    ASSERT(Register->SpaceId == PROC_SPACE_PCC);

    KeWaitForSingleObject(&ProcPccMutex, Executive, KernelMode, FALSE, NULL);
    Subspace = ProcPccSubspaces[Register->AccessSize];
    ASSERT((Subspace != NULL) && (Subspace->References != 0));
    Subspace->References--;
    KeReleaseMutex(&ProcPccMutex, FALSE);
}

_Requires_lock_held_(Subspace->Lock)
static
BOOLEAN
ProcPccCommand(
    _In_ PPROC_PCC_SUBSPACE Subspace,
    _In_ USHORT Command)
{
    PUSHORT CommandField = (PUSHORT)(Subspace->Shared + sizeof(ULONG));
    PUSHORT StatusField = CommandField + 1;
    LARGE_INTEGER Now, Frequency;
    ULONGLONG Doorbell, Elapsed;
    ULONG Retry, Limit;
    USHORT Status;

    Now = KeQueryPerformanceCounter(&Frequency);
    if ((Subspace->Turnaround != 0) && (Subspace->LastCompletion.QuadPart != 0) && (Frequency.QuadPart != 0))
    {
        Elapsed = ((ULONGLONG)(Now.QuadPart - Subspace->LastCompletion.QuadPart) * 1000000) / (ULONGLONG)Frequency.QuadPart;
        if (Elapsed < Subspace->Turnaround)
            KeStallExecutionProcessor((ULONG)(Subspace->Turnaround - Elapsed));
    }

    /* TODO: the maximum periodic access rate of the subspace is not enforced */
    WRITE_REGISTER_USHORT(CommandField, Command);
    WRITE_REGISTER_USHORT(StatusField, 0);

    if (!ProcReadRegister(&Subspace->Doorbell, &Doorbell))
        return FALSE;
    Doorbell = (Doorbell & Subspace->DoorbellPreserve) | Subspace->DoorbellWrite;
    if (!ProcWriteRegister(&Subspace->Doorbell, Doorbell))
        return FALSE;

    Limit = max(Subspace->Latency, 1UL) * PROC_PCC_RETRIES / PROC_PCC_POLL_US;
    for (Retry = 0; Retry < Limit; Retry++)
    {
        Status = READ_REGISTER_USHORT(StatusField);
        if (Status & PROC_PCC_STATUS_COMPLETE)
        {
            Subspace->LastCompletion = KeQueryPerformanceCounter(NULL);
            return (Status & PROC_PCC_STATUS_ERROR) == 0;
        }

        KeStallExecutionProcessor(PROC_PCC_POLL_US);
    }

    DPRINT1("PCC subspace %u did not complete command %u\n", Subspace->Id, Command);
    return FALSE;
}

static
ULONGLONG
ProcPccMask(
    _In_ PPROC_REGISTER Register)
{
    return (Register->BitWidth >= 64) ? MAXULONGLONG : ((1ULL << Register->BitWidth) - 1);
}

static
ULONGLONG
ProcPccReadRaw(
    _In_ PPROC_REGISTER Register)
{
    switch (ProcPccBytes(Register))
    {
        case 1: return READ_REGISTER_UCHAR(Register->Mapped);
        case 2: return READ_REGISTER_USHORT(Register->Mapped);
        case 4: return READ_REGISTER_ULONG(Register->Mapped);
        default: return *(volatile ULONGLONG *)Register->Mapped;
    }
}

static
VOID
ProcPccWriteRaw(
    _In_ PPROC_REGISTER Register,
    _In_ ULONGLONG Value)
{
    switch (ProcPccBytes(Register))
    {
        case 1: WRITE_REGISTER_UCHAR(Register->Mapped, (UCHAR)Value); break;
        case 2: WRITE_REGISTER_USHORT(Register->Mapped, (USHORT)Value); break;
        case 4: WRITE_REGISTER_ULONG(Register->Mapped, (ULONG)Value); break;
        default: *(volatile ULONGLONG *)Register->Mapped = Value; break;
    }
}

BOOLEAN
ProcPccRead(
    _In_ PPROC_REGISTER Register,
    _Out_ PULONGLONG Value)
{
    PPROC_PCC_SUBSPACE Subspace = ProcPccSubspaces[Register->AccessSize];
    BOOLEAN Done;
    KIRQL OldIrql;

    ASSERT((Subspace != NULL) && (Register->Mapped != NULL));

    KeAcquireSpinLock(&Subspace->Lock, &OldIrql);
    Done = ProcPccCommand(Subspace, PROC_PCC_COMMAND_READ);
    if (Done)
        *Value = (ProcPccReadRaw(Register) >> Register->BitOffset) & ProcPccMask(Register);
    KeReleaseSpinLock(&Subspace->Lock, OldIrql);

    return Done;
}

/* The registers of one request are written together and handed over with a single write command */
BOOLEAN
ProcPccWrite(
    _In_ ULONG Count,
    _In_reads_(Count) PPROC_REGISTER *Registers,
    _In_reads_(Count) const ULONGLONG *Values)
{
    PPROC_PCC_SUBSPACE Subspace;
    ULONGLONG Raw, Mask;
    BOOLEAN Done;
    KIRQL OldIrql;
    ULONG Index;

    if (Count == 0)
        return TRUE;

    Subspace = ProcPccSubspaces[Registers[0]->AccessSize];
    ASSERT(Subspace != NULL);

    KeAcquireSpinLock(&Subspace->Lock, &OldIrql);
    for (Index = 0; Index < Count; Index++)
    {
        ASSERT(Registers[Index]->AccessSize == Subspace->Id);
        Mask = ProcPccMask(Registers[Index]);
        Raw = ProcPccReadRaw(Registers[Index]);
        Raw &= ~(Mask << Registers[Index]->BitOffset);
        Raw |= (Values[Index] & Mask) << Registers[Index]->BitOffset;
        ProcPccWriteRaw(Registers[Index], Raw);
    }

    Done = ProcPccCommand(Subspace, PROC_PCC_COMMAND_WRITE);
    KeReleaseSpinLock(&Subspace->Lock, OldIrql);

    return Done;
}
