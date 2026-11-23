/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntcompat.c
 * PURPOSE:     NT memory manager compatibility interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <mm/nt/mint.h>

PMMPFN MmPfnDatabase;
MMPFNLIST MmZeroedPageListHead;
MMPFNLIST MmFreePageListHead;
MMPFNLIST MmStandbyPageListHead;
MMPFNLIST MmModifiedPageListHead;
MMPFNLIST MmModifiedNoWritePageListHead;
MMSUPPORT MmSystemCacheWs;
MM_MEMORY_CONSUMER MiMemoryConsumers[MC_MAXIMUM];
PFN_NUMBER MmAvailablePages;
PFN_NUMBER MmResidentAvailablePages;
PFN_NUMBER MmLowMemoryThreshold;
PFN_NUMBER MmHighMemoryThreshold;
PVOID MmSystemPtesStart[2];
PVOID MmSystemPtesEnd[2];
PVOID MmNonPagedSystemStart;
PVOID MmSystemCacheStart;
PVOID MmSystemCacheEnd;
SIZE_T MmAllocationFragment;
SIZE_T MmDriverCommit;
ULONG MmNumberOfSystemPtes;
ULONG MmSecondaryColors;
ULONG MmSecondaryColorMask;
ULONG MmSystemPageColor;
ULONG MmReadClusterSize;
ULONG_PTR MmSubsectionBase;
BOOLEAN MmDynamicPfn;
BOOLEAN MmMirroring;
BOOLEAN MmTrackPtes;
BOOLEAN MmLargeSystemCache;

static
BOOLEAN
MiExecuteOptionsArePermanent(
    _In_ PEPROCESS Process)
{
    return sizeof(ULONG_PTR) == sizeof(ULONG64) && PsGetProcessWow64Process(Process) == NULL;
}

NTSTATUS
NTAPI
MmGetExecuteOptions(
    _Out_ PULONG ExecuteOptions)
{
    PEPROCESS Process = PsGetCurrentProcess();

    if (MiExecuteOptionsArePermanent(Process))
    {
        *ExecuteOptions = MEM_EXECUTE_OPTION_DISABLE |
                          MEM_EXECUTE_OPTION_DISABLE_THUNK_EMULATION |
                          MEM_EXECUTE_OPTION_PERMANENT;
        return STATUS_SUCCESS;
    }

    *ExecuteOptions = ReadUCharAcquire(&Process->Pcb.ExecuteOptions);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
MmSetExecuteOptions(
    _In_ ULONG ExecuteOptions)
{
    PEPROCESS Process = PsGetCurrentProcess();
    NTSTATUS Status = STATUS_SUCCESS;
    PMI_ADDRESS_SPACE Space;
    UCHAR Current;
    UCHAR Updated;

    if (MiExecuteOptionsArePermanent(Process) || MI_PROCESS_OF(Process) == NULL ||
        (ExecuteOptions & ~MEM_EXECUTE_OPTION_VALID_FLAGS))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Space = MiSpaceOfProcess(Process);
    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    Current = ReadUCharAcquire(&Process->Pcb.ExecuteOptions);
    if (Current & MEM_EXECUTE_OPTION_PERMANENT)
    {
        Status = STATUS_ACCESS_DENIED;
    }
    else
    {
        Updated = (UCHAR)((Current & ~(MEM_EXECUTE_OPTION_DISABLE | MEM_EXECUTE_OPTION_ENABLE)) | ExecuteOptions);
        if (!(ExecuteOptions & MEM_EXECUTE_OPTION_DISABLE))
            Updated |= Current & MEM_EXECUTE_OPTION_ENABLE;
        WriteUCharRelease(&Process->Pcb.ExecuteOptions, Updated);
        MiSetUserExecuteDataLocked(Space, (BOOLEAN)((Updated & MEM_EXECUTE_OPTION_ENABLE) != 0));
    }
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);

    return Status;
}

#ifdef _M_AMD64
static NTSTATUS
MiUpdateCfgTargetsLocked(PMI_ADDRESS_SPACE Space, const MEMORY_RANGE_ENTRY *Range,
                         const CFG_CALL_TARGET_LIST_INFORMATION *List, const CFG_CALL_TARGET_INFO *Targets,
                         PMI_SECTION_OBJECT Section, PULONG Processed)
{
    ULONG64 Base = (ULONG_PTR)Range->VirtualAddress;
    PMI_VAD Vad;
    ULONG Index;
    NTSTATUS Status;

    if (((Base | Range->NumberOfBytes) & (PAGE_SIZE - 1)) || Base > Space->HighestVa ||
        Range->NumberOfBytes - 1 > Space->HighestVa - Base)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < List->NumberOfEntries; ++Index)
    {
        const CFG_CALL_TARGET_INFO *Target = &Targets[Index];
        if ((Target->Offset & 15) || Target->Offset >= Range->NumberOfBytes ||
            (Index && Target->Offset < Targets[Index - 1].Offset) || (Target->Flags & ~(ULONG_PTR)0x1f))
            return STATUS_INVALID_PARAMETER;
    }
    if (!Space->CfgBitmap)
        return STATUS_INVALID_PAGE_PROTECTION;
    Vad = MiVadLocate(Space, Base);
    if (!Vad)
        return STATUS_MEMORY_NOT_ALLOCATED;
    if (Range->NumberOfBytes > ((Vad->Node.EndingVpn + 1) << PAGE_SHIFT) - Base)
        return STATUS_INVALID_PARAMETER;
    if (Section && (Vad->Segment == NULL || Section->Control->Segment != Vad->Segment ||
                    Base != Vad->Node.StartingVpn << PAGE_SHIFT ||
                    List->FileOffset != Vad->SegmentPageOffset << PAGE_SHIFT))
        return STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < List->NumberOfEntries; ++Index)
    {
        ULONG Protection;
        ULONG64 Address = Base + Targets[Index].Offset;
        BOOLEAN Committed;
        if (Targets[Index].Flags & 0x18)
            return STATUS_NOT_SUPPORTED;
        if ((Targets[Index].Flags & 4) && !MiCfgIsSuppressedExport(Vad, Address))
            return STATUS_ACCESS_DENIED;
        MiPageStatus(Space, Vad, Address & ~(ULONG64)(PAGE_SIZE - 1), FALSE, &Committed, &Protection);
        if (!Committed || !MI_PROT_IS_EXECUTE(Protection) || Vad->NoExecuteImage ||
            (Vad->CfgScp && (Address >> PAGE_SHIFT) == Vad->Node.EndingVpn))
            return STATUS_ACCESS_VIOLATION;
        if (Targets[Index].Flags & (CFG_CALL_TARGET_VALID | 4))
        {
            ULONG Frame;
            Status = MiCfgGetBitmapPageLocked(Space, Address >> 6, TRUE, &Frame);
            if (!NT_SUCCESS(Status))
                return Status;
        }
    }
    for (Index = 0; Index < List->NumberOfEntries; ++Index)
    {
        Status = MiCfgSetTargetLocked(Space, Base + Targets[Index].Offset,
                                      (UCHAR)((Targets[Index].Flags & (CFG_CALL_TARGET_VALID | 4)) != 0));
        if (!NT_SUCCESS(Status))
            return Status;
        ++*Processed;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
MiSetCfgCallTargetInformation(HANDLE ProcessHandle, ULONG_PTR Count, PMEMORY_RANGE_ENTRY Ranges,
                              PVOID Information, ULONG Length)
{
    CFG_CALL_TARGET_LIST_INFORMATION List;
    PCFG_CALL_TARGET_INFO Targets = NULL;
    MEMORY_RANGE_ENTRY Range;
    MI_PROCESS_REFERENCE Reference;
    PMI_SECTION_OBJECT Section = NULL;
    PMI_ADDRESS_SPACE Space;
    KPROCESSOR_MODE Mode = ExGetPreviousMode();
    ULONG Processed = 0;
    SIZE_T Bytes;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Count)
        return STATUS_INVALID_PARAMETER_3;
    if (Count != 1)
        return STATUS_INVALID_PARAMETER_4;
    if (Length != sizeof(List))
        return STATUS_INVALID_PARAMETER_6;
    _SEH2_TRY
    {
        if (Mode != KernelMode)
        {
            ProbeForRead(Ranges, sizeof(Range), TYPE_ALIGNMENT(MEMORY_RANGE_ENTRY));
            ProbeForRead(Information, sizeof(List), TYPE_ALIGNMENT(CFG_CALL_TARGET_LIST_INFORMATION));
        }
        Range = *Ranges;
        RtlCopyMemory(&List, Information, sizeof(List));
        if (!Range.NumberOfBytes)
            Status = STATUS_INVALID_PARAMETER_4;
        else if (!List.NumberOfEntries || List.Reserved)
            Status = STATUS_INVALID_PARAMETER_5;
        else
        {
            Bytes = (SIZE_T)List.NumberOfEntries * sizeof(*Targets);
            if (Mode != KernelMode)
            {
                ProbeForWrite(List.NumberOfEntriesProcessed, sizeof(Processed), TYPE_ALIGNMENT(ULONG));
                ProbeForRead(List.CallTargetInfo, Bytes, TYPE_ALIGNMENT(CFG_CALL_TARGET_INFO));
            }
            Targets = ExAllocatePoolWithQuotaTag(PagedPool | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE, Bytes, 'gCfM');
            if (Targets)
                RtlCopyMemory(Targets, List.CallTargetInfo, Bytes);
            else
                Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (!NT_SUCCESS(Status))
        goto Done;
    if (List.Section)
    {
        Status = ObReferenceObjectByHandle(List.Section, SECTION_QUERY, MmSectionObjectType, Mode,
                                           (PVOID *)&Section, NULL);
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_OPERATION, &Reference);
    if (!NT_SUCCESS(Status))
        goto Done;
    Space = MiSpaceOfProcess(Reference.Process);
    MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);
    Status = MiUpdateCfgTargetsLocked(Space, &Range, &List, Targets, Section, &Processed);
    MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
    MiReleaseTargetProcess(&Reference);
    _SEH2_TRY
    {
        *List.NumberOfEntriesProcessed = Processed;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
Done:
    if (Section)
        ObDereferenceObject(Section);
    if (Targets)
        ExFreePoolWithTag(Targets, 'gCfM');
    return Status;
}
#endif

NTSTATUS
NTAPI
NtSetInformationVirtualMemory(
    _In_ HANDLE ProcessHandle,
    _In_ VIRTUAL_MEMORY_INFORMATION_CLASS VmInformationClass,
    _In_ ULONG_PTR NumberOfEntries,
    _In_ PMEMORY_RANGE_ENTRY VirtualAddresses,
    _In_ PVOID VmInformation,
    _In_ ULONG VmInformationLength)
{
    MI_PROCESS_REFERENCE Target;
    PMEMORY_RANGE_ENTRY Ranges;
    ULONG Flag = 0;
    SIZE_T Bytes;
    ULONG_PTR Index;
    NTSTATUS Status;

    PAGED_CODE();

    if ((ULONG)VmInformationClass > VmRemoveFromWorkingSetInformation)
        return STATUS_INVALID_PARAMETER_2;

#ifdef _M_AMD64
    if (VmInformationClass == VmCfgCallTargetInformation)
        return MiSetCfgCallTargetInformation(ProcessHandle, NumberOfEntries, VirtualAddresses,
                                             VmInformation, VmInformationLength);
#endif
    if (VmInformationClass == VmPrefetchInformation)
    {
        if (VmInformation == NULL)
            return STATUS_INVALID_PARAMETER_5;
        if (VmInformationLength != sizeof(ULONG))
            return STATUS_INVALID_PARAMETER_6;
        if (NumberOfEntries == 0 || NumberOfEntries > MAXULONG / sizeof(MEMORY_RANGE_ENTRY))
            return STATUS_INVALID_PARAMETER_3;
        if (VirtualAddresses == NULL)
            return STATUS_ACCESS_VIOLATION;

        Status = STATUS_SUCCESS;
        _SEH2_TRY
        {
            if (ExGetPreviousMode() != KernelMode)
                ProbeForRead(VirtualAddresses, NumberOfEntries * sizeof(MEMORY_RANGE_ENTRY), sizeof(PVOID));
            for (Index = 0; Index < NumberOfEntries; Index++)
            {
                if (VirtualAddresses[Index].NumberOfBytes == 0)
                {
                    Status = STATUS_INVALID_PARAMETER_4;
                    break;
                }
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        return Status;
    }

    if (VmInformationClass != VmPageDirtyStateInformation)
        return STATUS_SUCCESS;
    if (NumberOfEntries == 0 || NumberOfEntries > MAXULONG / sizeof(MEMORY_RANGE_ENTRY))
        return STATUS_INVALID_PARAMETER_3;
    if (VmInformation == NULL)
        return STATUS_INVALID_PARAMETER_5;
    if (VmInformationLength != sizeof(Flag))
        return STATUS_INVALID_PARAMETER_6;
    if (VirtualAddresses == NULL)
        return STATUS_ACCESS_VIOLATION;

    Bytes = NumberOfEntries * sizeof(MEMORY_RANGE_ENTRY);
    Ranges = ExAllocatePoolWithTag(PagedPool, Bytes, 'rMvN');
    if (Ranges == NULL)
        return STATUS_NO_MEMORY;

    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
        {
            ProbeForRead(VmInformation, sizeof(Flag), sizeof(Flag));
            ProbeForRead(VirtualAddresses, Bytes, sizeof(PVOID));
        }
        Flag = *(PULONG)VmInformation;
        RtlCopyMemory(Ranges, VirtualAddresses, Bytes);
        Status = STATUS_SUCCESS;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
        goto Cleanup;
    if (Flag != 0)
    {
        Status = STATUS_INVALID_PARAMETER_5;
        goto Cleanup;
    }

    Status = MiReferenceTargetProcess(ProcessHandle, PROCESS_VM_OPERATION, &Target);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    for (Index = 0; Index < NumberOfEntries; Index++)
    {
        Status = MiResetExecutableWriteTracking(MiSpaceOfProcess(Target.Process),
                                               (ULONG64)(ULONG_PTR)Ranges[Index].VirtualAddress,
                                               (ULONG64)Ranges[Index].NumberOfBytes);
        if (!NT_SUCCESS(Status))
            break;
    }

    MiReleaseTargetProcess(&Target);

Cleanup:
    ExFreePoolWithTag(Ranges, 'rMvN');
    return Status;
}

NTSTATUS
MmSetProcessExecutableWriteTracking(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Enable)
{
    if (MI_PROCESS_OF(Process) == NULL)
        return STATUS_PROCESS_IS_TERMINATING;

    return MiSetExecutableWriteTracking(MiSpaceOfProcess(Process), Enable);
}

/* EcCodeBitMap is present only in the native 64-bit PEB. */
#ifdef _WIN64
#define MI_EC_CODE_BITMAP_SIZE (1ULL << 32)

BOOLEAN
MiIsEcCodeAddress(
    _In_ PEPROCESS Process,
    _In_ PVOID Address)
{
#if defined(_WIN64) && (NTDDI_VERSION >= NTDDI_WIN10)
    volatile ULONGLONG *BitmapWord;
    ULONG_PTR AddressValue = (ULONG_PTR)Address;
    ULONG_PTR BitmapAddress;
    ULONG_PTR BitmapOffset;
    BOOLEAN IsEcCode = FALSE;
    PVOID BitmapBase;

    if (Process != PsGetCurrentProcess() || Process->Peb == NULL ||
        AddressValue > (ULONG_PTR)MM_HIGHEST_USER_ADDRESS)
    {
        return FALSE;
    }

    BitmapOffset = (AddressValue >> 15) & (MI_EC_CODE_BITMAP_SIZE - sizeof(*BitmapWord));

    _SEH2_TRY
    {
        if (!MmIsAddressValid(Process->Peb))
            _SEH2_YIELD(return FALSE);

        BitmapBase = Process->Peb->EcCodeBitMap;
        BitmapAddress = (ULONG_PTR)BitmapBase + BitmapOffset;

        if (BitmapBase == NULL || BitmapAddress < (ULONG_PTR)BitmapBase ||
            BitmapAddress > (ULONG_PTR)MM_HIGHEST_USER_ADDRESS - sizeof(*BitmapWord) + 1)
        {
            _SEH2_YIELD(return FALSE);
        }

        BitmapWord = (volatile ULONGLONG *)BitmapAddress;
        if (!MmIsAddressValid((PVOID)BitmapWord))
            _SEH2_YIELD(return FALSE);

        IsEcCode = (BOOLEAN)((*BitmapWord & (1ULL << ((AddressValue >> PAGE_SHIFT) & 63))) != 0);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        IsEcCode = FALSE;
    }
    _SEH2_END;

    return IsEcCode;
#else
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(Address);
    return FALSE;
#endif
}
#endif
