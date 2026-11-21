/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel services the shared run-time library calls back into
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern ULONG NtGlobalFlag;

ULONG
NTAPI
DebugPrint(
    _In_ PSTRING DebugString,
    _In_ ULONG ComponentId,
    _In_ ULONG Level);

NTSTATUS
NTAPI
RtlpImageNtHeaderEx(
    _In_ ULONG Flags,
    _In_ PVOID Base,
    _In_ ULONG64 Size,
    _Out_ PIMAGE_NT_HEADERS *OutHeaders);

IMAGE_RESOURCE_DIRECTORY *find_entry_by_name(IMAGE_RESOURCE_DIRECTORY *dir, LPCWSTR name, void *root, int want_dir);
IMAGE_RESOURCE_DIRECTORY *find_first_entry(IMAGE_RESOURCE_DIRECTORY *dir, void *root, int want_dir);


/* MODE AND MEMORY ***********************************************************/

KPROCESSOR_MODE
NTAPI
RtlpGetMode(VOID)
{
    return KernelMode;
}

PVOID
NTAPI
RtlpAllocateMemory(
    _In_ ULONG Bytes,
    _In_ ULONG Tag)
{
    return ExAllocatePoolWithTag(PagedPool, Bytes, Tag);
}

VOID
NTAPI
RtlpFreeMemory(
    _In_ PVOID Mem,
    _In_ ULONG Tag)
{
    /* String buffers reach the run-time library from callers that allocated them with any tag */
    if (Tag == TAG_ASTR || Tag == TAG_OSTR || Tag == TAG_USTR)
        ExFreePool(Mem);
    else
        ExFreePoolWithTag(Mem, Tag);
}

NTSTATUS
NTAPI
RtlpSafeCopyMemory(
    _Out_writes_bytes_all_(Length) VOID UNALIGNED *Destination,
    _In_reads_bytes_(Length) CONST VOID UNALIGNED *Source,
    _In_ SIZE_T Length)
{
    NTSTATUS Status = STATUS_SUCCESS;

    _SEH2_TRY
    {
        RtlCopyMemory(Destination, Source, Length);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

#if DBG
VOID
FASTCALL
CHECK_PAGED_CODE_RTL(
    _In_ char *file,
    _In_ int line)
{
    KIRQL Irql = KeGetCurrentIrql();

    if (Irql > APC_LEVEL)
    {
        DbgPrint("%s:%i: pageable code runs at IRQL %u\n", file, line, Irql);
        ASSERT(Irql <= APC_LEVEL);
    }
}
#endif

/* DEBUG OUTPUT ***************************************************************/

BOOLEAN
NTAPI
RtlpCheckForActiveDebugger(VOID)
{
    return FALSE;
}

BOOLEAN
NTAPI
RtlpDebugPrintWanted(
    _Out_ PNTSTATUS Status)
{
    *Status = STATUS_SUCCESS;
    return (BOOLEAN)(KdpDebuggerStructuresInitialized || !KdpPrintDeferred);
}

NTSTATUS
NTAPI
RtlpDebugPrint(
    _In_ PSTRING DebugString,
    _In_ ULONG ComponentId,
    _In_ ULONG Level)
{
    BOOLEAN Handled;
    NTSTATUS Status;

    if (!KdpPrintDeferred)
        return DebugPrint(DebugString, ComponentId, Level);

    Status = KdpPrint(ComponentId, Level, DebugString->Buffer, DebugString->Length, KernelMode, NULL, NULL, &Handled);
    return (Status == STATUS_DEVICE_NOT_CONNECTED) ? STATUS_SUCCESS : Status;
}

BOOLEAN
NTAPI
RtlpSetInDbgPrint(VOID)
{
    return FALSE;
}

VOID
NTAPI
RtlpClearInDbgPrint(VOID)
{
}

/* PROCESS STATE THE KERNEL DOES NOT KEEP *************************************/

PPEB
NTAPI
RtlGetCurrentPeb(VOID)
{
    return PsGetCurrentProcess()->Peb;
}

VOID
NTAPI
RtlAcquirePebLock(VOID)
{
}

VOID
NTAPI
RtlReleasePebLock(VOID)
{
}

NTSTATUS
NTAPI
LdrShutdownThread(VOID)
{
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
RtlCallVectoredExceptionHandlers(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(Context);
    return FALSE;
}

VOID
NTAPI
RtlCallVectoredContinueHandlers(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(Context);
}

VOID
NTAPI
RtlpCheckLogException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PCONTEXT ContextRecord,
    _In_ PVOID ContextData,
    _In_ ULONG Size)
{
    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(ContextRecord);
    UNREFERENCED_PARAMETER(ContextData);
    UNREFERENCED_PARAMETER(Size);

    /* TODO: exceptions are not logged, even with FLG_ENABLE_EXCEPTION_LOGGING */
}

/* HEAPS **********************************************************************/

NTSTATUS
NTAPI
RtlInitializeHeapLock(
    _Inout_ PHEAP_LOCK *Lock)
{
    PHEAP_LOCK NewLock;

    NewLock = ExAllocatePoolWithTag(NonPagedPool, sizeof(*NewLock), TAG_RTHL);
    if (NewLock == NULL)
        return STATUS_NO_MEMORY;

    ExInitializeResourceLite(&NewLock->Resource);
    *Lock = NewLock;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlDeleteHeapLock(
    _Inout_ PHEAP_LOCK Lock)
{
    ExDeleteResourceLite(&Lock->Resource);
    ExFreePoolWithTag(Lock, TAG_RTHL);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlEnterHeapLock(
    _Inout_ PHEAP_LOCK Lock,
    _In_ BOOLEAN Exclusive)
{
    KeEnterCriticalRegion();

    if (Exclusive)
        ExAcquireResourceExclusiveLite(&Lock->Resource, TRUE);
    else
        ExAcquireResourceSharedLite(&Lock->Resource, TRUE);

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
RtlTryEnterHeapLock(
    _Inout_ PHEAP_LOCK Lock,
    _In_ BOOLEAN Exclusive)
{
    BOOLEAN Acquired;

    KeEnterCriticalRegion();

    Acquired = Exclusive ? ExAcquireResourceExclusiveLite(&Lock->Resource, FALSE)
                         : ExAcquireResourceSharedLite(&Lock->Resource, FALSE);
    if (!Acquired)
        KeLeaveCriticalRegion();

    return Acquired;
}

NTSTATUS
NTAPI
RtlLeaveHeapLock(
    _Inout_ PHEAP_LOCK Lock)
{
    ExReleaseResourceLite(&Lock->Resource);
    KeLeaveCriticalRegion();
    return STATUS_SUCCESS;
}

VOID
NTAPI
RtlpSetHeapParameters(
    _Inout_ PRTL_HEAP_PARAMETERS Parameters)
{
    if (Parameters->SegmentReserve == 0)
        Parameters->SegmentReserve = MmHeapSegmentReserve;
    if (Parameters->SegmentCommit == 0)
        Parameters->SegmentCommit = MmHeapSegmentCommit;
    if (Parameters->DeCommitFreeBlockThreshold == 0)
        Parameters->DeCommitFreeBlockThreshold = MmHeapDeCommitFreeBlockThreshold;
    if (Parameters->DeCommitTotalFreeThreshold == 0)
        Parameters->DeCommitTotalFreeThreshold = MmHeapDeCommitTotalFreeThreshold;
}

struct _HEAP;

VOID
NTAPI
RtlpAddHeapToProcessList(
    _In_ struct _HEAP *Heap)
{
    UNREFERENCED_PARAMETER(Heap);
}

VOID
NTAPI
RtlpRemoveHeapFromProcessList(
    _In_ struct _HEAP *Heap)
{
    UNREFERENCED_PARAMETER(Heap);
}

VOID
RtlInitializeHeapManager(VOID)
{
}

/* STACKS AND IMAGES **********************************************************/

VOID
NTAPI
RtlpGetStackLimits(
    _Out_ PULONG_PTR LowLimit,
    _Out_ PULONG_PTR HighLimit)
{
    PKTHREAD Thread = KeGetCurrentThread();

    *LowLimit = (ULONG_PTR)Thread->StackLimit;
    *HighLimit = (ULONG_PTR)Thread->StackBase;
}

BOOLEAN
NTAPI
RtlpHandleDpcStackException(
    _In_ PEXCEPTION_REGISTRATION_RECORD RegistrationFrame,
    _In_ ULONG_PTR RegistrationFrameEnd,
    _Inout_ PULONG_PTR StackLow,
    _Inout_ PULONG_PTR StackHigh)
{
    PKPRCB Prcb;
    ULONG_PTR DpcTop;

    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
        return FALSE;

    Prcb = KeGetCurrentPrcb();
    DpcTop = (ULONG_PTR)Prcb->DpcStack;
    if (!Prcb->DpcRoutineActive || RegistrationFrameEnd > DpcTop ||
        (ULONG_PTR)RegistrationFrame < DpcTop - KERNEL_STACK_SIZE)
    {
        return FALSE;
    }

    *StackLow = DpcTop - KERNEL_STACK_SIZE;
    *StackHigh = DpcTop;
    return TRUE;
}

PVOID
NTAPI
RtlPcToFileHeader(
    _In_ PVOID PcValue,
    _Out_ PVOID *BaseOfImage)
{
    PLDR_DATA_TABLE_ENTRY Entry;
    BOOLEAN InSystem;
    KIRQL OldIrql;

    *BaseOfImage = NULL;
    if ((ULONG_PTR)PcValue <= (ULONG_PTR)MmHighestUserAddress)
        return NULL;

    /* FIXME: above DISPATCH_LEVEL a held module lock makes the lookup fail; Windows reads an inverted function table there */
    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
    {
        if (!KeTryToAcquireSpinLockAtDpcLevel(&PsLoadedModuleSpinLock))
            return NULL;
        *BaseOfImage = KiPcToFileHeader(PcValue, &Entry, FALSE, &InSystem);
        KeReleaseSpinLockFromDpcLevel(&PsLoadedModuleSpinLock);
        return *BaseOfImage;
    }

    KeAcquireSpinLock(&PsLoadedModuleSpinLock, &OldIrql);
    *BaseOfImage = KiPcToFileHeader(PcValue, &Entry, FALSE, &InSystem);
    KeReleaseSpinLock(&PsLoadedModuleSpinLock, OldIrql);

    return *BaseOfImage;
}

NTSTATUS
NTAPI
RtlImageNtHeaderEx(
    _In_ ULONG Flags,
    _In_ PVOID Base,
    _In_ ULONG64 Size,
    _Out_ PIMAGE_NT_HEADERS *OutHeaders)
{
    NTSTATUS Status;

    if (OutHeaders != NULL)
        *OutHeaders = NULL;

    _SEH2_TRY
    {
        Status = RtlpImageNtHeaderEx(Flags, Base, Size, OutHeaders);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

PVOID
NTAPI
RtlpGetResourceModuleForEntry(
    _In_ PVOID BaseAddress,
    _In_ PVOID Entry)
{
    UNREFERENCED_PARAMETER(Entry);
    return BaseAddress;
}

NTSTATUS
find_entry(
    _In_ PVOID BaseAddress,
    _In_ LDR_RESOURCE_INFO *Info,
    _In_ ULONG Level,
    _Out_ void **Found,
    _In_ int WantDirectory)
{
    static const NTSTATUS Missing[] =
    {
        STATUS_RESOURCE_TYPE_NOT_FOUND,
        STATUS_RESOURCE_NAME_NOT_FOUND,
        STATUS_RESOURCE_DATA_NOT_FOUND,
    };
    PIMAGE_RESOURCE_DIRECTORY Root, Directory;
    ULONG Size, Depth;
    BOOLEAN Inner;

    if (Level > RTL_NUMBER_OF(Missing))
        return STATUS_INVALID_PARAMETER;

    Root = RtlImageDirectoryEntryToData(BaseAddress, TRUE, IMAGE_DIRECTORY_ENTRY_RESOURCE, &Size);
    if (Root == NULL || Size < sizeof(*Root))
        return STATUS_RESOURCE_DATA_NOT_FOUND;

    Directory = Root;
    for (Depth = 0; Depth < Level; Depth++)
    {
        Inner = (BOOLEAN)(Depth + 1 < Level || WantDirectory);

        if (Depth == 0)
            Directory = find_entry_by_name(Directory, (LPCWSTR)Info->Type, Root, Inner);
        else if (Depth == 1)
            Directory = find_entry_by_name(Directory, (LPCWSTR)Info->Name, Root, Inner);
        else
            /* TODO: the resource language is not matched, the first one is taken */
            Directory = find_first_entry(Directory, Root, Inner);

        if (Directory == NULL)
            return Missing[Depth];
    }

    *Found = Directory;
    return STATUS_SUCCESS;
}

/* ATOM TABLES ****************************************************************/

#define RTLP_ATOM_HANDLE_SHIFT 2
#define RTLP_FIRST_STRING_ATOM 0xC000

NTSTATUS
RtlpInitAtomTableLock(
    _Inout_ PRTL_ATOM_TABLE AtomTable)
{
    ExInitializeFastMutex(&AtomTable->FastMutex);
    return STATUS_SUCCESS;
}

VOID
RtlpDestroyAtomTableLock(
    _Inout_ PRTL_ATOM_TABLE AtomTable)
{
    UNREFERENCED_PARAMETER(AtomTable);
}

BOOLEAN
RtlpLockAtomTable(
    _Inout_ PRTL_ATOM_TABLE AtomTable)
{
    ExAcquireFastMutex(&AtomTable->FastMutex);
    return TRUE;
}

VOID
RtlpUnlockAtomTable(
    _Inout_ PRTL_ATOM_TABLE AtomTable)
{
    ExReleaseFastMutex(&AtomTable->FastMutex);
}

PRTL_ATOM_TABLE
RtlpAllocAtomTable(
    _In_ ULONG Size)
{
    PRTL_ATOM_TABLE Table = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_ATMT);

    if (Table != NULL)
        RtlZeroMemory(Table, Size);

    return Table;
}

VOID
RtlpFreeAtomTable(
    _In_ PRTL_ATOM_TABLE AtomTable)
{
    ExFreePoolWithTag(AtomTable, TAG_ATMT);
}

PRTL_ATOM_TABLE_ENTRY
RtlpAllocAtomTableEntry(
    _In_ ULONG Size)
{
    PRTL_ATOM_TABLE_ENTRY Entry = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_ATMT);

    if (Entry != NULL)
        RtlZeroMemory(Entry, Size);

    return Entry;
}

VOID
RtlpFreeAtomTableEntry(
    _In_ PRTL_ATOM_TABLE_ENTRY Entry)
{
    ExFreePoolWithTag(Entry, TAG_ATMT);
}

BOOLEAN
RtlpCreateAtomHandleTable(
    _Inout_ PRTL_ATOM_TABLE AtomTable)
{
    AtomTable->ExHandleTable = ExCreateHandleTable(NULL, FALSE);
    return (BOOLEAN)(AtomTable->ExHandleTable != NULL);
}

static
BOOLEAN
NTAPI
RtlpDestroyAtomHandle(
    _In_ PHANDLE_TABLE_ENTRY HandleTableEntry,
    _In_ HANDLE Handle,
    _In_ PVOID HandleTable)
{
    return ExDestroyHandle(HandleTable, Handle, HandleTableEntry);
}

VOID
RtlpDestroyAtomHandleTable(
    _Inout_ PRTL_ATOM_TABLE AtomTable)
{
    PHANDLE_TABLE Handles = AtomTable->ExHandleTable;

    if (Handles == NULL)
        return;

    ExSweepHandleTable(Handles, RtlpDestroyAtomHandle, Handles);
    ExDestroyHandleTable(Handles, NULL);
    AtomTable->ExHandleTable = NULL;
}

BOOLEAN
RtlpCreateAtomHandle(
    _Inout_ PRTL_ATOM_TABLE AtomTable,
    _Inout_ PRTL_ATOM_TABLE_ENTRY Entry)
{
    HANDLE_TABLE_ENTRY HandleEntry;
    ULONG_PTR Index;
    HANDLE Handle;

    HandleEntry.Object = Entry;
    HandleEntry.GrantedAccess = 1;

    Handle = ExCreateHandle(AtomTable->ExHandleTable, &HandleEntry);
    if (Handle == NULL)
        return FALSE;

    Index = (ULONG_PTR)Handle >> RTLP_ATOM_HANDLE_SHIFT;
    if (Index >= RTLP_FIRST_STRING_ATOM)
    {
        ExDestroyHandle(AtomTable->ExHandleTable, Handle, NULL);
        return FALSE;
    }

    Entry->HandleIndex = (USHORT)Index;
    Entry->Atom = (RTL_ATOM)(RTLP_FIRST_STRING_ATOM + Index);
    return TRUE;
}

VOID
RtlpFreeAtomHandle(
    _Inout_ PRTL_ATOM_TABLE AtomTable,
    _In_ PRTL_ATOM_TABLE_ENTRY Entry)
{
    ExDestroyHandle(AtomTable->ExHandleTable, (HANDLE)((ULONG_PTR)Entry->HandleIndex << RTLP_ATOM_HANDLE_SHIFT), NULL);
}

PRTL_ATOM_TABLE_ENTRY
RtlpGetAtomEntry(
    _In_ PRTL_ATOM_TABLE AtomTable,
    _In_ ULONG Index)
{
    PHANDLE_TABLE_ENTRY HandleEntry;
    PRTL_ATOM_TABLE_ENTRY Entry;

    /* The caller holds the atom table mutex, so APCs are already off */
    HandleEntry = ExMapHandleToPointer(AtomTable->ExHandleTable, (HANDLE)((ULONG_PTR)Index << RTLP_ATOM_HANDLE_SHIFT));
    if (HandleEntry == NULL)
        return NULL;

    Entry = HandleEntry->Object;
    ExUnlockHandleTableEntry(AtomTable->ExHandleTable, HandleEntry);
    return Entry;
}
