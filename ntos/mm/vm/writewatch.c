/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Write-watch tracking for private allocations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <mm/include/mm.h>

#define MI_VAD_START(v) ((v)->Node.StartingVpn << PAGE_SHIFT)
#define MI_VAD_END(v) (((v)->Node.EndingVpn + 1) << PAGE_SHIFT)
#define MI_PAGE_ALIGN_DOWN(a)   ((a) & ~((ULONG64)PAGE_SIZE - 1))
#define MI_PAGE_ALIGN_UP(a)     (((a) + PAGE_SIZE - 1) & ~((ULONG64)PAGE_SIZE - 1))

static
VOID
MiAssertWriteWatchVad(
    _In_ PMI_VAD Vad)
{
    UNREFERENCED_PARAMETER(Vad);
    MI_ASSERT(Vad->WriteWatchBits != NULL);
    MI_ASSERT((Vad->WriteWatchBase & (PAGE_SIZE - 1)) == 0);
    MI_ASSERT(Vad->WriteWatchBase <= MI_VAD_START(Vad));
    MI_ASSERT(MI_VAD_START(Vad) < MI_VAD_END(Vad));
}

static
SIZE_T
MiWriteWatchBytes(
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    return (SIZE_T)((((End - Start) >> PAGE_SHIFT) + 7) / 8);
}

NTSTATUS
MiWriteWatchAttach(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_VAD Vad)
{
    SIZE_T Bytes = MiWriteWatchBytes(MI_VAD_START(Vad), MI_VAD_END(Vad));

    MI_ASSERT(Vad->WriteWatchBits == NULL && Bytes != 0);
    Vad->WriteWatchBits = MI_ALLOCATE(Bytes);
    if (Vad->WriteWatchBits == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Vad->WriteWatchBits, Bytes);
    Vad->WriteWatchBase = MI_VAD_START(Vad);
    MiAssertWriteWatchVad(Vad);
    Space->HasWriteWatch = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
MiWriteWatchDuplicate(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_VAD Vad)
{
    PUCHAR Source = Vad->WriteWatchBits;
    SIZE_T Bytes;

    if (Source == NULL)
        return STATUS_SUCCESS;

    MiAssertWriteWatchVad(Vad);
    Bytes = MiWriteWatchBytes(Vad->WriteWatchBase, MI_VAD_END(Vad));
    MI_ASSERT(Bytes != 0);
    Vad->WriteWatchBits = MI_ALLOCATE(Bytes);
    if (Vad->WriteWatchBits == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Vad->WriteWatchBits, Source, Bytes);
    Space->HasWriteWatch = TRUE;
    return STATUS_SUCCESS;
}

VOID
MiWriteWatchRelease(
    _Inout_ PMI_VAD Vad)
{
    if (Vad->WriteWatchBits != NULL)
    {
        MiAssertWriteWatchVad(Vad);
        MI_FREE(Vad->WriteWatchBits);
        Vad->WriteWatchBits = NULL;
    }
}

VOID
MiWriteWatchNoteWriteLocked(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress)
{
    ULONG64 Page;
    PMI_VAD Vad;

    if (!Space->HasWriteWatch)
        return;

    Vad = MiVadLocate(Space, VirtualAddress);
    if (Vad != NULL && Vad->WriteWatchBits != NULL)
    {
        MiAssertWriteWatchVad(Vad);
        MI_ASSERT(MI_PAGE_ALIGN_DOWN(VirtualAddress) >= MI_VAD_START(Vad) &&
                  MI_PAGE_ALIGN_DOWN(VirtualAddress) < MI_VAD_END(Vad));
        Page = (MI_PAGE_ALIGN_DOWN(VirtualAddress) - Vad->WriteWatchBase) >> PAGE_SHIFT;
        MI_ATOMIC_OR8(&Vad->WriteWatchBits[Page / 8], 1u << (Page % 8));
    }
}

VOID
MiWriteWatchNoteWrite(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress)
{
    MI_RW_ACQUIRE_SHARED(&Space->Lock);
    MiWriteWatchNoteWriteLocked(Space, VirtualAddress);
    MI_RW_RELEASE_SHARED(&Space->Lock);
}

static
BOOLEAN
MiWriteWatchTestPage(
    _In_ PMI_VAD Vad,
    _In_ ULONG64 Va)
{
    ULONG64 Page = (Va - Vad->WriteWatchBase) >> PAGE_SHIFT;

    MiAssertWriteWatchVad(Vad);
    MI_ASSERT((Va & (PAGE_SIZE - 1)) == 0 && Va >= MI_VAD_START(Vad) && Va < MI_VAD_END(Vad));
    return (BOOLEAN)((MI_ATOMIC_READ8(&Vad->WriteWatchBits[Page / 8]) >> (Page % 8)) & 1);
}

static
BOOLEAN
MiWriteWatchTestPte(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Va)
{
    ULONG TableFrame;
    PMI_PTE Slot;
    MI_PTE Pte;
    KIRQL OldIrql;

    Slot = MiPtLookup(Space, Va, &TableFrame);
    if (Slot == NULL)
        return FALSE;

    OldIrql = MiPfnLock(&Space->System->Pfn, TableFrame);
    Pte = MiArchPteRead(Slot);
    MiPfnUnlock(&Space->System->Pfn, TableFrame, OldIrql);

    return MiArchPteIsLeafDescriptor(Pte) && MiArchPteIsWritable(Pte) && MiArchPteIsDirty(Pte);
}

static
VOID
MiWriteWatchResetLocked(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _Inout_ PMI_VAD Vad,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    ULONG64 Va;
    ULONG64 Page;
    ULONG TableFrame;
    PMI_PTE Slot;
    MI_PTE Pte;
    KIRQL OldIrql;

    MiAssertWriteWatchVad(Vad);
    MI_ASSERT(Space->HasWriteWatch);
    MI_ASSERT((Start & (PAGE_SIZE - 1)) == 0 && (End & (PAGE_SIZE - 1)) == 0);
    MI_ASSERT(Start >= MI_VAD_START(Vad) && Start <= End && End <= MI_VAD_END(Vad));
    for (Va = Start; Va < End; Va += PAGE_SIZE)
    {
        Page = (Va - Vad->WriteWatchBase) >> PAGE_SHIFT;
        MI_ATOMIC_AND8(&Vad->WriteWatchBits[Page / 8], ~(1u << (Page % 8)));

        Slot = MiPtLookup(Space, Va, &TableFrame);
        if (Slot == NULL)
            continue;

        OldIrql = MiPfnLock(&Space->System->Pfn, TableFrame);
        Pte = MiArchPteRead(Slot);
        if (MiArchPteIsLeafDescriptor(Pte) && MiArchPteIsWritable(Pte) && MiArchPteIsDirty(Pte))
        {
            MI_PTE Clean = MiArchPteSetDirty(Pte, FALSE);

            MI_ASSERT(MiArchPteFrame(Pte) < Space->System->Pfn.FrameCount);
            MI_ASSERT(MiArchPteIsLeafDescriptor(Clean) && MiArchPteIsWritable(Clean));
            MI_ASSERT(!MiArchPteIsDirty(Clean) && MiArchPteFrame(Clean) == MiArchPteFrame(Pte));
            MiPfnSetModified(&Space->System->Pfn, (ULONG)MiArchPteFrame(Pte));
            MiArchPteWrite(Slot, Clean);
            MiArchTlbInvalidate(Va, 1, TRUE);
        }
        MiPfnUnlock(&Space->System->Pfn, TableFrame, OldIrql);
    }
}

static
NTSTATUS
MiWriteWatchRange(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Base,
    _In_ ULONG64 Size,
    _Out_ PULONG64 Start,
    _Out_ PULONG64 End,
    _Out_ PMI_VAD *Vad)
{
    if (Size == 0 || Base > ~(ULONG64)0 - Size || Base + Size > ~(ULONG64)0 - (PAGE_SIZE - 1))
        return STATUS_INVALID_PARAMETER;

    *Start = MI_PAGE_ALIGN_DOWN(Base);
    *End = MI_PAGE_ALIGN_UP(Base + Size);
    *Vad = MiVadLocate(Space, *Start);
    if (*Vad == NULL || (*Vad)->WriteWatchBits == NULL || *End > MI_VAD_END(*Vad))
        return STATUS_INVALID_PARAMETER;

    MiAssertWriteWatchVad(*Vad);
    MI_ASSERT(Space->HasWriteWatch);
    MI_ASSERT(*Start >= MI_VAD_START(*Vad) && *Start < *End && *End <= MI_VAD_END(*Vad));
    return STATUS_SUCCESS;
}

NTSTATUS
MiGetWriteWatch(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Base,
    _In_ ULONG64 Size,
    _In_ BOOLEAN Reset,
    _Out_ PULONG64 Addresses,
    _Inout_ PULONG64 Count)
{
    ULONG64 Start, End, Va;
    ULONG64 Found = 0;
    PMI_VAD Vad;
    NTSTATUS Status;

    if (*Count == 0)
        return STATUS_INVALID_PARAMETER;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    Status = MiWriteWatchRange(Space, Base, Size, &Start, &End, &Vad);
    if (NT_SUCCESS(Status))
    {
        for (Va = Start; Found < *Count && Va < End; Va += PAGE_SIZE)
        {
            if (MiWriteWatchTestPage(Vad, Va) || MiWriteWatchTestPte(Space, Va))
                Addresses[Found++] = Va;
        }

        MI_ASSERT(Found <= *Count && Va >= Start && Va <= End);
        if (Reset)
            MiWriteWatchResetLocked(Space, Vad, Start, Va);

        *Count = Found;
    }

    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Status;
}

NTSTATUS
MiResetWriteWatch(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 Base,
    _In_ ULONG64 Size)
{
    ULONG64 Start, End;
    PMI_VAD Vad;
    NTSTATUS Status;

    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

    Status = MiWriteWatchRange(Space, Base, Size, &Start, &End, &Vad);
    if (NT_SUCCESS(Status))
        MiWriteWatchResetLocked(Space, Vad, Start, End);

    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    return Status;
}
