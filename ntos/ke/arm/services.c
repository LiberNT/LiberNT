/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARMv7 kernel services: caches, IPIs, topology, freezing and user exceptions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern ULONG KiArmDcacheLineSize;
ULONG KiArmDmaIoCoherency;

/* Clean and invalidate every data cache level by set/way (ARM ARM B4.2.1, CLIDR/CCSIDR). */
static
VOID
KiArmCleanInvalidateDcacheAll(VOID)
{
    ULONG Clidr = KiArmReadCp15(0, 1, 0, 1);
    ULONG Level, Ccsidr, LineShift, Ways, Sets, WayShift, Way, Set;

    KiArmDsb();
    for (Level = 0; Level < ((Clidr >> 24) & 7); Level++)
    {
        if (((Clidr >> (Level * 3)) & 7) < 2)
            continue;
        KiArmWriteCp15(0, 2, 0, 0, Level << 1);
        KiArmIsb();
        Ccsidr = KiArmReadCp15(0, 1, 0, 0);
        LineShift = (Ccsidr & 7) + 4;
        Ways = ((Ccsidr >> 3) & 0x3FF) + 1;
        Sets = ((Ccsidr >> 13) & 0x7FFF) + 1;
        WayShift = __builtin_clz(Ways - 1);
        for (Way = 0; Way < Ways; Way++)
        {
            for (Set = 0; Set < Sets; Set++)
                KiArmWriteCp15(7, 0, 14, 2, ((Ways > 1) ? (Way << WayShift) : 0) | (Set << LineShift) | (Level << 1));
        }
    }
    KiArmDsb();
    KiArmIsb();
}

BOOLEAN
NTAPI
KeInvalidateAllCaches(VOID)
{
    KiArmCleanInvalidateDcacheAll();
    KeSweepICache(NULL, 0);
    return TRUE;
}

VOID
NTAPI
KeInvalidateRangeAllCaches(
    _In_ PVOID BaseAddress,
    _In_ ULONG Length)
{
    KiArmFlushDmaRange(BaseAddress, Length, TRUE);
    KeSweepICache(NULL, 0);
}

VOID
NTAPI
KeFlushIoBuffers(
    _In_ PMDL Mdl,
    _In_ BOOLEAN ReadOperation,
    _In_ BOOLEAN DmaOperation)
{
    PVOID Address;

    /* A request that carries no buffer of its own has none to maintain. */
    if (!Mdl || (MmGetMdlByteCount(Mdl) == 0))
        return;

    if (!DmaOperation && !ReadOperation)
        return;

    /* A mapped buffer is maintained by range; anything else by set/way. */
    if (Mdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL))
    {
        Address = (PUCHAR)Mdl->MappedSystemVa;
        KiArmFlushDmaRange(Address, MmGetMdlByteCount(Mdl), ReadOperation);
    }
    else
    {
        KiArmCleanInvalidateDcacheAll();
    }
    if (ReadOperation && !DmaOperation)
        KeSweepICache(NULL, 0);
}

ULONG
NTAPI
KeGetRecommendedSharedDataAlignment(VOID)
{
    return KiArmDcacheLineSize;
}

VOID
NTAPI
KeSetDmaIoCoherency(
    _In_ ULONG Coherency)
{
    KiArmDmaIoCoherency = Coherency;
}

VOID
NTAPI
KeFlushEntireTb(
    _In_ BOOLEAN Invalid,
    _In_ BOOLEAN AllProcessors)
{
    UNREFERENCED_PARAMETER(Invalid);
    KiArmDsbIsh();
    if (AllProcessors)
        KiArmWriteCp15(8, 0, 3, 0, 0);
    else
        KiArmWriteCp15(8, 0, 7, 0, 0);
    KiArmDsbIsh();
    KiArmIsb();
}

VOID
FASTCALL
KeZeroPages(
    _In_ PVOID Address,
    _In_ ULONG Size)
{
    RtlZeroMemory(Address, Size);
}

VOID
__cdecl
KiUnexpectedInterrupt(VOID)
{
    KeBugCheckEx(TRAP_CAUSE_UNKNOWN, 0, 0, 0, 0);
}



BOOLEAN
NTAPI
KiSetClockDeadline(
    _In_ ULONGLONG InterruptTime)
{
    UNREFERENCED_PARAMETER(InterruptTime);
    /* The generic timer runs a periodic tick. */
    return FALSE;
}

VOID
NTAPI
KiQueryProcessorDomains(
    _Out_ PKI_PROCESSOR_DOMAINS Domains)
{
    /* TODO: clusters from the device tree cpu-map */
    RtlZeroMemory(Domains, sizeof(*Domains));
}

VOID
NTAPI
KiQueryProcessorEfficiencyClasses(
    _Out_writes_all_(MAXIMUM_PROCESSORS) PUCHAR EfficiencyClasses)
{
    RtlZeroMemory(EfficiencyClasses, MAXIMUM_PROCESSORS);
}

VOID
NTAPI
KiQueryProcessorTopology(
    _Out_writes_to_opt_(MaxRecords, *RecordCount) PKI_CACHE_RECORD Records,
    _In_ ULONG MaxRecords,
    _Out_ PULONG RecordCount,
    _Out_writes_to_opt_(MaxSets, *SetCount) PKAFFINITY Sets,
    _In_ ULONG MaxSets,
    _Out_ PULONG SetCount)
{
    UNREFERENCED_PARAMETER(Records);
    UNREFERENCED_PARAMETER(MaxRecords);
    UNREFERENCED_PARAMETER(Sets);
    UNREFERENCED_PARAMETER(MaxSets);
    /* TODO: describe the caches from CLIDR/CCSIDR */
    *RecordCount = 0;
    *SetCount = 0;
}

VOID
FASTCALL
KiCallOnKernelStack(
    _In_ PVOID Context,
    _In_ PVOID StackTop)
{
    KiArmCallOnStack(Context, NULL, (PVOID)KiRunStackCallout, StackTop);
}

BOOLEAN
NTAPI
KiUnwindFrame(
    _Inout_ PCONTEXT Context,
    _In_ BOOLEAN CallerFrame)
{
    PRUNTIME_FUNCTION FunctionEntry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    ULONG ImageBase, EstablisherFrame, ControlPc;
    PVOID HandlerData, Base;
    BOOLEAN InKernel;

    ControlPc = Context->Pc & ~1UL;
    if (CallerFrame)
        ControlPc -= 2;

    /* The function table of an image this address space does not map, such as a session image seen from another process, cannot be read. */
    Base = KiPcToFileHeader((PVOID)(ULONG_PTR)ControlPc, &LdrEntry, FALSE, &InKernel);
    if (!Base || MmIsSessionAddress(Base) || !MmIsAddressValid(Base))
        return KiUnwindFrameRecord(Context);

    FunctionEntry = RtlLookupFunctionEntry(ControlPc, &ImageBase, NULL);
    if (!FunctionEntry && ((Context->Lr & ~1UL) == (Context->Pc & ~1UL)))
        return FALSE;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, ControlPc, FunctionEntry, Context,
                     &HandlerData, &EstablisherFrame, NULL);
    return Context->Pc != 0;
}

NTSTATUS
NTAPI
KeRaiseUserException(
    _In_ NTSTATUS ExceptionCode)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKTRAP_FRAME TrapFrame = Thread->TrapFrame;

    if (!TrapFrame || !KiIsUserModeTrap(TrapFrame))
        return ExceptionCode;

    _SEH2_TRY
    {
        ((PTEB)Thread->Teb)->ExceptionCode = ExceptionCode;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return ExceptionCode);
    }
    _SEH2_END;

    /* The dispatcher returns to the interrupted code through lr. */
    TrapFrame->Lr = TrapFrame->Pc | ((TrapFrame->Cpsr & CPSR_THUMB) ? 1 : 0);
    TrapFrame->Pc = (ULONG)(ULONG_PTR)KeRaiseUserExceptionDispatcher & ~1UL;
    TrapFrame->Cpsr = (TrapFrame->Cpsr & ~CPSR_IT_MASK) | CPSR_THUMB;
    return ExceptionCode;
}

NTSTATUS
NTAPI
NtSetLdtEntries(
    _In_ ULONG Selector1,
    _In_ LDT_ENTRY LdtEntry1,
    _In_ ULONG Selector2,
    _In_ LDT_ENTRY LdtEntry2)
{
    UNREFERENCED_PARAMETER(Selector1);
    UNREFERENCED_PARAMETER(LdtEntry1);
    UNREFERENCED_PARAMETER(Selector2);
    UNREFERENCED_PARAMETER(LdtEntry2);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtVdmControl(
    _In_ ULONG ControlCode,
    _In_ PVOID ControlData)
{
    UNREFERENCED_PARAMETER(ControlCode);
    UNREFERENCED_PARAMETER(ControlData);
    return STATUS_NOT_IMPLEMENTED;
}

/* SList operations: the 64-bit header swaps as a whole through LDREXD/STREXD. */
PSLIST_ENTRY
NTAPI
ExpInterlockedPushEntrySList(
    _Inout_ PSLIST_HEADER ListHead,
    _Inout_ PSLIST_ENTRY ListEntry)
{
    SLIST_HEADER Old, New;

    do
    {
        Old.Alignment = ReadULong64Acquire(&ListHead->Alignment);
        ListEntry->Next = Old.Next.Next;
        New.Next.Next = ListEntry;
        New.Depth = Old.Depth + 1;
        New.Sequence = Old.Sequence + 1;
    } while (InterlockedCompareExchange64((PLONG64)&ListHead->Alignment, New.Alignment, Old.Alignment) != (LONG64)Old.Alignment);
    return Old.Next.Next;
}

PSLIST_ENTRY
NTAPI
ExpInterlockedPopEntrySList(
    _Inout_ PSLIST_HEADER ListHead)
{
    SLIST_HEADER Old, New;
    PSLIST_ENTRY Entry;

    do
    {
        Old.Alignment = ReadULong64Acquire(&ListHead->Alignment);
        Entry = Old.Next.Next;
        if (!Entry)
            return NULL;
        New.Next.Next = Entry->Next;
        New.Depth = Old.Depth - 1;
        New.Sequence = Old.Sequence + 1;
    } while (InterlockedCompareExchange64((PLONG64)&ListHead->Alignment, New.Alignment, Old.Alignment) != (LONG64)Old.Alignment);
    return Entry;
}

/* Device register access: strongly ordered device mappings, bracketed by DSB as the WDK ARM forms are. */
UCHAR NTAPI READ_REGISTER_UCHAR(_In_ volatile UCHAR *Register) { UCHAR Value = *Register; KiArmDsb(); return Value; }
USHORT NTAPI READ_REGISTER_USHORT(_In_ volatile USHORT *Register) { USHORT Value = *Register; KiArmDsb(); return Value; }
ULONG NTAPI READ_REGISTER_ULONG(_In_ volatile ULONG *Register) { ULONG Value = *Register; KiArmDsb(); return Value; }
VOID NTAPI WRITE_REGISTER_UCHAR(_In_ volatile UCHAR *Register, _In_ UCHAR Value) { KiArmDsb(); *Register = Value; }
VOID NTAPI WRITE_REGISTER_USHORT(_In_ volatile USHORT *Register, _In_ USHORT Value) { KiArmDsb(); *Register = Value; }
VOID NTAPI WRITE_REGISTER_ULONG(_In_ volatile ULONG *Register, _In_ ULONG Value) { KiArmDsb(); *Register = Value; }
VOID NTAPI READ_REGISTER_BUFFER_UCHAR(_In_ volatile UCHAR *Register, _Out_writes_(Count) PUCHAR Buffer, _In_ ULONG Count) { while (Count--) *Buffer++ = *Register; KiArmDsb(); }
VOID NTAPI READ_REGISTER_BUFFER_USHORT(_In_ volatile USHORT *Register, _Out_writes_(Count) PUSHORT Buffer, _In_ ULONG Count) { while (Count--) *Buffer++ = *Register; KiArmDsb(); }
VOID NTAPI READ_REGISTER_BUFFER_ULONG(_In_ volatile ULONG *Register, _Out_writes_(Count) PULONG Buffer, _In_ ULONG Count) { while (Count--) *Buffer++ = *Register; KiArmDsb(); }
VOID NTAPI WRITE_REGISTER_BUFFER_UCHAR(_In_ volatile UCHAR *Register, _In_reads_(Count) PUCHAR Buffer, _In_ ULONG Count) { KiArmDsb(); while (Count--) *Register = *Buffer++; }
VOID NTAPI WRITE_REGISTER_BUFFER_USHORT(_In_ volatile USHORT *Register, _In_reads_(Count) PUSHORT Buffer, _In_ ULONG Count) { KiArmDsb(); while (Count--) *Register = *Buffer++; }
VOID NTAPI WRITE_REGISTER_BUFFER_ULONG(_In_ volatile ULONG *Register, _In_reads_(Count) PULONG Buffer, _In_ ULONG Count) { KiArmDsb(); while (Count--) *Register = *Buffer++; }

/* Every trap saves the whole VFP bank, so kernel floating point needs no saving (WDK 16299 inline forms). */
NTSTATUS
NTAPI
KiArmSaveFloatingPointState(
    _Out_ PKFLOATING_SAVE FloatSave)
{
    FloatSave->Dummy = 0;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KiArmRestoreFloatingPointState(
    _In_ PKFLOATING_SAVE FloatSave)
{
    UNREFERENCED_PARAMETER(FloatSave);
    return STATUS_SUCCESS;
}
