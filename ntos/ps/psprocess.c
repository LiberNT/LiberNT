/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Making a process, opening one, and the priority and quantum a
 *              process is given
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

volatile LONG64 PspProcessSequenceNumber = 1;

ULONG PsRawPrioritySeparation;
ULONG PsPrioritySeparation;
UCHAR PspForegroundQuantum[3];

/*
 * How long a thread runs before the scheduler looks for another one, in
 * thirds of a clock tick. The short tables are what a workstation uses and
 * the long ones what a server uses; with variable quanta a foreground thread
 * is given more than a background one, and with fixed quanta all three are
 * the same. (Russinovich, Solomon, Ionescu, "Windows Internals", quanta.)
 */
const UCHAR PspVariableQuantums[6] = { 6, 12, 18, 12, 24, 36 };
const UCHAR PspFixedQuantums[6] = { 18, 18, 18, 36, 36, 36 };

/* The base priority each priority class runs at */
const KPRIORITY PspPriorityTable[PROCESS_PRIORITY_CLASS_ABOVE_NORMAL + 1] =
{
    8,  /* PROCESS_PRIORITY_CLASS_INVALID, which is treated as normal */
    4,  /* PROCESS_PRIORITY_CLASS_IDLE */
    8,  /* PROCESS_PRIORITY_CLASS_NORMAL */
    13, /* PROCESS_PRIORITY_CLASS_HIGH */
    24, /* PROCESS_PRIORITY_CLASS_REALTIME */
    6,  /* PROCESS_PRIORITY_CLASS_BELOW_NORMAL */
    10  /* PROCESS_PRIORITY_CLASS_ABOVE_NORMAL */
};

/* What a caller named when it asked for a process to be made */
typedef struct _PSP_CREATE_OPERANDS
{
    PEPROCESS Parent;
    PVOID Section;
    PVOID DebugObject;
    PVOID ExceptionPort;
} PSP_CREATE_OPERANDS, *PPSP_CREATE_OPERANDS;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * The priority and the quantum a process runs at follow from its class and,
 * for a process of the normal class, from whether it is in the foreground.
 */
KPRIORITY
NTAPI
PspComputeQuantumAndPriority(
    _In_ PEPROCESS Process,
    _In_ PSPROCESSPRIORITYMODE Mode,
    _Out_ PUCHAR Quantum)
{
    KPRIORITY Priority;
    UCHAR Length;

    PAGED_CODE();

    Priority = PspPriorityTable[Process->PriorityClass];
    Length = PspForegroundQuantum[0];

    /*
     * Only a normal process is affected by being in the foreground: a class
     * the caller chose explicitly stays where the caller put it.
     */
    if (Process->PriorityClass == PROCESS_PRIORITY_CLASS_NORMAL)
    {
        if (Mode == PsProcessPriorityForeground)
        {
            Length = PspForegroundQuantum[PsPrioritySeparation];
            Priority = PROCESS_PRIORITY_NORMAL_FOREGROUND;
        }
        else
        {
            Priority = PROCESS_PRIORITY_NORMAL;
        }
    }

    if (Mode == PsProcessPrioritySpinning) Length = 0;

    *Quantum = Length;
    return Priority;
}

/*
 * Takes a process out of everything that can find it. A creation that failed
 * before the process had a thread undoes itself this way.
 */
static
VOID
PspUnlinkProcess(
    _In_ PEPROCESS Process)
{
    PAGED_CODE();

    if (Process->ProcessInserted)
    {
        KeEnterCriticalRegion();
        ExAcquirePushLockExclusive(&PspActiveProcessLock);
        RemoveEntryList(&Process->ActiveProcessLinks);
        ExReleasePushLockExclusive(&PspActiveProcessLock);
        KeLeaveCriticalRegion();
        Process->ProcessInserted = FALSE;
    }

    if (Process->UniqueProcessId != NULL)
    {
        PspRemoveCid(Process->UniqueProcessId);
        Process->UniqueProcessId = NULL;
    }
}

/*
 * Builds a process object that nothing can find yet. It has an address space
 * and a token, so a section can be mapped into it and a thread given to it,
 * but until it is inserted no identifier names it.
 */
NTSTATUS
NTAPI
PspAllocateProcess(
    _In_opt_ PEPROCESS Parent,
    _In_opt_ PVOID Section,
    _In_opt_ PACCESS_TOKEN Token,
    _In_ ULONG Flags,
    _Outptr_ PEPROCESS *NewProcess)
{
    ULONG_PTR DirectoryTableBase[2] = { 0, 0 };
    POBJECT_NAME_INFORMATION AuditName = NULL;
    PEPROCESS Clone = NULL;
    ULONG AddressSpaceFlags = 0;
    PEPROCESS Process;
    NTSTATUS Status;
    UCHAR Quantum;

    PAGED_CODE();

    *NewProcess = NULL;

    Status = ObCreateObject(KernelMode,
                            PsProcessType,
                            NULL,
                            KernelMode,
                            NULL,
                            sizeof(EPROCESS),
                            0,
                            0,
                            (PVOID*)&Process);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(Process, sizeof(EPROCESS));

    InitializeListHead(&Process->ThreadListHead);
    InitializeListHead(&Process->JobLinks);
    InitializeListHead(&Process->SessionProcessLinks);
    InitializeListHead(&Process->MmProcessLinks);
    InitializeListHead(&Process->TimerResolutionLink);
    ExInitializePushLock(&Process->ProcessLock);
    ExInitializePushLock(&Process->ThreadListLock);
    ExInitializePushLock(&Process->AddressCreationLock);
    ExInitializeRundownProtection(&Process->RundownProtect);
    KeQuerySystemTime(&Process->CreateTime);
    Process->SequenceNumber =
        (ULONGLONG)InterlockedIncrement64(&PspProcessSequenceNumber);
    Process->ExitStatus = STATUS_PENDING;
    Process->Machine = IMAGE_FILE_MACHINE_NATIVE;

    if (Parent != NULL)
    {
        Process->InheritedFromUniqueProcessId = Parent->UniqueProcessId;
        Process->DefaultHardErrorProcessing = Parent->DefaultHardErrorProcessing;
        Process->PriorityClass = Parent->PriorityClass;
        Process->Machine = Parent->Machine;

        /* Real time is never inherited: it has to be asked for again */
        if (Process->PriorityClass == PROCESS_PRIORITY_CLASS_REALTIME)
            Process->PriorityClass = PROCESS_PRIORITY_CLASS_HIGH;

        if ((Flags & PROCESS_CREATE_FLAGS_NO_DEBUG_INHERIT) != 0)
            Process->NoDebugInherit = TRUE;
    }
    else
    {
        Process->PriorityClass = PROCESS_PRIORITY_CLASS_NORMAL;
        Process->DefaultHardErrorProcessing = SEM_FAILCRITICALERRORS;
    }

    PspInheritQuota(Process, Parent);
    ObInheritDeviceMap(Parent, Process);

    Status = PspInitializeProcessSecurity(Process, Parent);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Process);
        return Status;
    }

    if (Token != NULL)
    {
        Status = PspAssignPrimaryToken(Process, NULL, Token);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(Process);
            return Status;
        }
    }

    if (!MmCreateProcessAddressSpace(0, Process, DirectoryTableBase))
    {
        ObDereferenceObject(Process);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeInitializeProcess(&Process->Pcb,
                        PspPriorityTable[Process->PriorityClass],
                        (KAFFINITY)-1,
                        DirectoryTableBase,
                        (BOOLEAN)((Process->DefaultHardErrorProcessing &
                                   SEM_NOALIGNMENTFAULTEXCEPT) != 0));

    PspComputeQuantumAndPriority(Process, PsProcessPriorityBackground, &Quantum);
    KeSetQuantumProcess(&Process->Pcb, Quantum);

    if ((Flags & PROCESS_CREATE_FLAGS_OVERRIDE_ADDRESS_SPACE) != 0)
        Process->OverrideAddressSpace = TRUE;

    /*
     * With an image, the address space is laid out from it. With no image the
     * caller is forking, and the parent's space is copied instead; a child of
     * the system process is the one exception, because that process has no
     * user address space to copy and the child is only there to hold threads.
     */
    if (Section == NULL && Parent != NULL && Parent != PsInitialSystemProcess)
        Clone = Parent;

    Status = MmInitializeProcessAddressSpace(Process,
                                             Clone,
                                             Section,
                                             &AddressSpaceFlags,
                                             &AuditName);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Process);
        return Status;
    }

    Process->SeAuditProcessCreationInfo.ImageFileName = AuditName;
    Process->HasAddressSpace = TRUE;

    *NewProcess = Process;
    return STATUS_SUCCESS;
}

/*
 * Gives the process an identifier and a place on the active list, in that
 * order, so that by the time it can be found by walking the list it can also
 * be found by its identifier.
 */
NTSTATUS
NTAPI
PspInsertProcess(
    _In_ PEPROCESS Process)
{
    NTSTATUS Status;

    PAGED_CODE();

    Status = PspInsertCid(Process, &Process->UniqueProcessId);
    if (!NT_SUCCESS(Status)) return Status;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&PspActiveProcessLock);
    InsertTailList(&PsActiveProcessHead, &Process->ActiveProcessLinks);
    Process->ProcessInserted = TRUE;
    ExReleasePushLockExclusive(&PspActiveProcessLock);
    KeLeaveCriticalRegion();

    return STATUS_SUCCESS;
}

static
VOID
PspReleaseCreateOperands(
    _Inout_ PPSP_CREATE_OPERANDS Operands)
{
    if (Operands->ExceptionPort != NULL)
    {
        ObDereferenceObject(Operands->ExceptionPort);
        Operands->ExceptionPort = NULL;
    }

    if (Operands->DebugObject != NULL)
    {
        ObDereferenceObject(Operands->DebugObject);
        Operands->DebugObject = NULL;
    }

    if (Operands->Section != NULL)
    {
        ObDereferenceObject(Operands->Section);
        Operands->Section = NULL;
    }

    if (Operands->Parent != NULL)
    {
        ObDereferenceObject(Operands->Parent);
        Operands->Parent = NULL;
    }
}

/*
 * Takes hold of everything the caller named by handle. Nothing else happens
 * until all of it is held, so a bad handle costs nothing.
 */
static
NTSTATUS
PspCaptureCreateOperands(
    _In_opt_ HANDLE ParentProcess,
    _In_opt_ HANDLE SectionHandle,
    _In_opt_ HANDLE DebugPort,
    _In_opt_ HANDLE ExceptionPort,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PPSP_CREATE_OPERANDS Operands)
{
    NTSTATUS Status;

    PAGED_CODE();

    RtlZeroMemory(Operands, sizeof(*Operands));

    if (ParentProcess != NULL)
    {
        Status = ObReferenceObjectByHandle(ParentProcess,
                                           PROCESS_CREATE_PROCESS,
                                           PsProcessType,
                                           PreviousMode,
                                           (PVOID*)&Operands->Parent,
                                           NULL);
        if (!NT_SUCCESS(Status)) return Status;
    }

    if (SectionHandle != NULL)
    {
        Status = ObReferenceObjectByHandle(SectionHandle,
                                           SECTION_MAP_EXECUTE,
                                           MmSectionObjectType,
                                           PreviousMode,
                                           &Operands->Section,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            PspReleaseCreateOperands(Operands);
            return Status;
        }
    }

    if (DebugPort != NULL)
    {
        Status = ObReferenceObjectByHandle(DebugPort,
                                           DEBUG_OBJECT_ADD_REMOVE_PROCESS,
                                           DbgkDebugObjectType,
                                           PreviousMode,
                                           &Operands->DebugObject,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            PspReleaseCreateOperands(Operands);
            return Status;
        }
    }

    if (ExceptionPort != NULL)
    {
        Status = ObReferenceObjectByHandle(ExceptionPort,
                                           0,
                                           LpcPortObjectType,
                                           PreviousMode,
                                           &Operands->ExceptionPort,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            PspReleaseCreateOperands(Operands);
            return Status;
        }
    }

    return STATUS_SUCCESS;
}

/*
 * Attaches what the caller named to the new process, keeping a reference of
 * its own for each, and gives the process the handle table it starts with.
 */
static
NTSTATUS
PspAttachCreateOperands(
    _In_ PEPROCESS Process,
    _In_ PPSP_CREATE_OPERANDS Operands,
    _In_ ULONG Flags,
    _In_opt_ const HANDLE *HandleList,
    _In_ SIZE_T HandleCount)
{
    PEPROCESS HandleSource;

    PAGED_CODE();

    if (Operands->Section != NULL)
    {
        ObReferenceObject(Operands->Section);
        Process->SectionObject = Operands->Section;
    }

    if (Operands->DebugObject != NULL)
    {
        ObReferenceObject(Operands->DebugObject);
        Process->DebugPort = Operands->DebugObject;
    }
    else if (Operands->Parent != NULL && !Process->NoDebugInherit)
    {
        DbgkCopyProcessDebugPort(Process, Operands->Parent);
    }

    if (Operands->ExceptionPort != NULL)
    {
        ObReferenceObject(Operands->ExceptionPort);
        Process->ExceptionPortData = Operands->ExceptionPort;
    }

    /*
     * Without inheritance the child gets an empty handle table; with a handle
     * list only the handles on it cross over.
     */
    HandleSource = NULL;
    if ((Flags & PROCESS_CREATE_FLAGS_INHERIT_HANDLES) != 0 ||
        HandleList != NULL)
    {
        HandleSource = Operands->Parent;
    }

    return ObInitProcess(HandleSource, Process, HandleList, HandleCount);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Rebuilds the foreground quantum table from the mask the registry holds, and
 * when asked, applies the new quantum to every process already running.
 */
VOID
NTAPI
PsChangeQuantumTable(
    _In_ BOOLEAN Immediate,
    _In_ ULONG PrioritySeparation)
{
    const UCHAR *Table;
    PEPROCESS Process;
    BOOLEAN Server;
    ULONG Length;
    ULONG Type;

    PAGED_CODE();

    Server = MmIsThisAnNtAsSystem();

    PsRawPrioritySeparation = PrioritySeparation;

    /* Three separations are defined; the fourth value means the third */
    PsPrioritySeparation = PspPrioritySeparationFromMask(PrioritySeparation);
    if (PsPrioritySeparation == 3) PsPrioritySeparation = 2;

    Type = PspQuantumTypeFromMask(PrioritySeparation);
    if (Type == PSP_FIXED_QUANTUMS) Table = PspFixedQuantums;
    else if (Type == PSP_VARIABLE_QUANTUMS) Table = PspVariableQuantums;
    else Table = Server ? PspFixedQuantums : PspVariableQuantums;

    Length = PspQuantumLengthFromMask(PrioritySeparation);
    if (Length == PSP_LONG_QUANTUMS) Table += 3;
    else if (Length != PSP_SHORT_QUANTUMS && Server) Table += 3;

    RtlCopyMemory(PspForegroundQuantum, Table, sizeof(PspForegroundQuantum));

    if (!Immediate) return;

    Process = PsGetNextProcess(NULL);
    while (Process != NULL)
    {
        UCHAR Quantum;

        PspComputeQuantumAndPriority(Process,
                                     PsProcessPriorityBackground,
                                     &Quantum);
        KeSetQuantumProcess(&Process->Pcb, Quantum);

        Process = PsGetNextProcess(Process);
    }
}

/*
 * Moves a process to another priority class. Going to real time needs the
 * privilege for it, because a real-time process can starve everything else.
 */
NTSTATUS
NTAPI
PspSetProcessPriorityClass(
    _In_ PEPROCESS Process,
    _In_ UCHAR PriorityClass)
{
    KPRIORITY Priority;
    UCHAR Quantum;

    PAGED_CODE();

    if (PriorityClass > PROCESS_PRIORITY_CLASS_ABOVE_NORMAL)
        return STATUS_INVALID_PARAMETER;

    if (PriorityClass == PROCESS_PRIORITY_CLASS_REALTIME &&
        !SeSinglePrivilegeCheck(SeIncreaseBasePriorityPrivilege,
                                ExGetPreviousMode()))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    Process->PriorityClass = PriorityClass;

    Priority = PspComputeQuantumAndPriority(Process,
                                            PsProcessPriorityBackground,
                                            &Quantum);
    KeSetPriorityAndQuantumProcess(&Process->Pcb, Priority, Quantum);

    return STATUS_SUCCESS;
}

VOID
NTAPI
PsSetProcessPriorityClass(
    _In_ PEPROCESS Process,
    _In_ ULONG PriorityClass)
{
    PspSetProcessPriorityClass(Process, (UCHAR)PriorityClass);
}

/*
 * Tells a process that it moved into or out of the foreground, which changes
 * how long its threads run but never which class it is in.
 */
VOID
NTAPI
PsSetProcessPriorityByClass(
    _In_ PEPROCESS Process,
    _In_ PSPROCESSPRIORITYMODE Type)
{
    KPRIORITY Priority;
    UCHAR Quantum;

    PAGED_CODE();

    Priority = PspComputeQuantumAndPriority(Process, Type, &Quantum);
    KeSetPriorityAndQuantumProcess(&Process->Pcb, Priority, Quantum);
}

/*
 * Reserves a user stack in another process. The guard page sits inside the
 * reservation, below what is committed, so a stack that still has room grows
 * instead of overflowing.
 */
NTSTATUS
PspAllocateUserStack(
    _In_ HANDLE ProcessHandle,
    _In_ ULONG_PTR ZeroBits,
    _In_ SIZE_T StackReserve,
    _In_ SIZE_T StackCommit,
    _In_ SIZE_T GuardSize,
    _Out_ PINITIAL_TEB InitialTeb)
{
    PVOID GuardBase;
    SIZE_T GuardLength;
    PVOID Base = NULL;
    SIZE_T Reserve;
    SIZE_T Commit;
    SIZE_T Guard;
    NTSTATUS Status;

    PAGED_CODE();

    RtlZeroMemory(InitialTeb, sizeof(*InitialTeb));

    Guard = (GuardSize != 0) ? ROUND_TO_PAGES(GuardSize) : PAGE_SIZE;
    Reserve = ROUND_TO_PAGES(StackReserve);
    Commit = ROUND_TO_PAGES(StackCommit);

    if (Reserve == 0) return STATUS_INVALID_PARAMETER;
    if (Commit == 0) Commit = PAGE_SIZE;
    if (Commit + Guard > Reserve) Reserve = ROUND_TO_PAGES(Commit + Guard);

    Status = ZwAllocateVirtualMemory(ProcessHandle,
                                     &Base,
                                     ZeroBits,
                                     &Reserve,
                                     MEM_RESERVE,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status)) return Status;

    InitialTeb->AllocatedStackBase = Base;
    InitialTeb->StackBase = (PVOID)((ULONG_PTR)Base + Reserve);
    InitialTeb->StackLimit = (PVOID)((ULONG_PTR)InitialTeb->StackBase - Commit);

    /* A stack grows down, so what is committed sits at its top */
    Status = ZwAllocateVirtualMemory(ProcessHandle,
                                     &InitialTeb->StackLimit,
                                     0,
                                     &Commit,
                                     MEM_COMMIT,
                                     PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
    {
        ZwFreeVirtualMemory(ProcessHandle, &Base, &Reserve, MEM_RELEASE);
        RtlZeroMemory(InitialTeb, sizeof(*InitialTeb));
        return Status;
    }

    GuardBase = (PVOID)((ULONG_PTR)InitialTeb->StackLimit - Guard);
    GuardLength = Guard;

    Status = ZwAllocateVirtualMemory(ProcessHandle,
                                     &GuardBase,
                                     0,
                                     &GuardLength,
                                     MEM_COMMIT,
                                     PAGE_READWRITE | PAGE_GUARD);
    if (!NT_SUCCESS(Status))
    {
        ZwFreeVirtualMemory(ProcessHandle, &Base, &Reserve, MEM_RELEASE);
        RtlZeroMemory(InitialTeb, sizeof(*InitialTeb));
        return Status;
    }

    InitialTeb->StackLimit = GuardBase;
    return STATUS_SUCCESS;
}

/*
 * The whole of making a process. Up to the point the process is inserted a
 * failure drops it; after that a caller can already see it, so a failure has
 * to terminate it the way a running process is terminated.
 */
NTSTATUS
NTAPI
PspCreateProcess(
    _Out_ PHANDLE ProcessHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ HANDLE ParentProcess,
    _In_ ULONG Flags,
    _In_opt_ HANDLE SectionHandle,
    _In_opt_ HANDLE DebugPort,
    _In_opt_ HANDLE ExceptionPort,
    _In_ BOOLEAN InJob,
    _In_opt_ const HANDLE *HandleList,
    _In_ SIZE_T HandleCount)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PSP_CREATE_OPERANDS Operands;
    INITIAL_PEB InitialPeb;
    PEPROCESS Process;
    HANDLE Handle;
    NTSTATUS Status;
    PPEB Peb;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(InJob);

    if ((Flags & ~PROCESS_CREATE_FLAGS_LEGAL_MASK) != 0)
        return STATUS_INVALID_PARAMETER;

    /*
     * A process with no parent gets no address space of its own and exists
     * only to carry kernel threads, which is not something a caller outside
     * the kernel may ask for.
     */
    if (ParentProcess == NULL && PreviousMode != KernelMode)
        return STATUS_INVALID_PARAMETER;

    Status = PspCaptureCreateOperands(ParentProcess,
                                      SectionHandle,
                                      DebugPort,
                                      ExceptionPort,
                                      PreviousMode,
                                      &Operands);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PspAllocateProcess(Operands.Parent,
                                Operands.Section,
                                NULL,
                                Flags,
                                &Process);
    if (!NT_SUCCESS(Status))
    {
        PspReleaseCreateOperands(&Operands);
        return Status;
    }

    Status = PspAttachCreateOperands(Process,
                                     &Operands,
                                     Flags,
                                     HandleList,
                                     HandleCount);
    if (!NT_SUCCESS(Status))
    {
        PspUnlinkProcess(Process);
        ObDereferenceObject(Process);
        PspReleaseCreateOperands(&Operands);
        return Status;
    }

    Status = PspInsertProcess(Process);
    if (!NT_SUCCESS(Status))
    {
        PspUnlinkProcess(Process);
        ObDereferenceObject(Process);
        PspReleaseCreateOperands(&Operands);
        return Status;
    }

    /* A process without an image has no user-mode state to describe */
    if (Operands.Section != NULL)
    {
        RtlZeroMemory(&InitialPeb, sizeof(InitialPeb));
        InitialPeb.Mutant = (HANDLE)-1;
        InitialPeb.ImageUsesLargePages =
            ((Flags & PROCESS_CREATE_FLAGS_LARGE_PAGES) != 0);

        Status = MmCreatePeb(Process, &InitialPeb, &Peb);
        if (NT_SUCCESS(Status)) Process->Peb = Peb;
    }

    if (NT_SUCCESS(Status))
    {
        Status = ObOpenObjectByPointer(Process,
                                       (ObjectAttributes != NULL) ?
                                           ObjectAttributes->Attributes :
                                           OBJ_KERNEL_HANDLE,
                                       NULL,
                                       DesiredAccess,
                                       PsProcessType,
                                       PreviousMode,
                                       &Handle);
    }

    PspReleaseCreateOperands(&Operands);

    if (!NT_SUCCESS(Status))
    {
        PspTerminateProcess(Process, Status);
        ObDereferenceObject(Process);
        return Status;
    }

    *ProcessHandle = Handle;
    ObDereferenceObject(Process);
    return STATUS_SUCCESS;
}

/*
 * Makes a process that carries kernel threads only. It has no image and no
 * user address space, so nothing of a user process applies to it.
 */
NTSTATUS
NTAPI
PsCreateSystemProcess(
    _Out_ PHANDLE ProcessHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    PAGED_CODE();

    return PspCreateProcess(ProcessHandle,
                            DesiredAccess,
                            ObjectAttributes,
                            PspInitialSystemProcessHandle,
                            0,
                            NULL,
                            NULL,
                            NULL,
                            FALSE,
                            NULL,
                            0);
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtCreateProcessEx(
    _Out_ PHANDLE ProcessHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ HANDLE ParentProcess,
    _In_ ULONG Flags,
    _In_opt_ HANDLE SectionHandle,
    _In_opt_ HANDLE DebugPort,
    _In_opt_ HANDLE ExceptionPort,
    _In_ BOOLEAN InJob)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE Handle = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(ProcessHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = PspCreateProcess(&Handle,
                              DesiredAccess,
                              ObjectAttributes,
                              ParentProcess,
                              Flags,
                              SectionHandle,
                              DebugPort,
                              ExceptionPort,
                              InJob,
                              NULL,
                              0);
    if (!NT_SUCCESS(Status)) return Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *ProcessHandle = Handle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *ProcessHandle = Handle;
    }

    return Status;
}

/*
 * The older form, which says whether handles are inherited with a flag of its
 * own instead of in a flags word.
 */
NTSTATUS
NTAPI
NtCreateProcess(
    _Out_ PHANDLE ProcessHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ HANDLE ParentProcess,
    _In_ BOOLEAN InheritObjectTable,
    _In_opt_ HANDLE SectionHandle,
    _In_opt_ HANDLE DebugPort,
    _In_opt_ HANDLE ExceptionPort)
{
    ULONG Flags = 0;

    PAGED_CODE();

    if (ParentProcess == NULL) return STATUS_INVALID_PARAMETER;
    if (InheritObjectTable) Flags |= PROCESS_CREATE_FLAGS_INHERIT_HANDLES;

    return NtCreateProcessEx(ProcessHandle,
                             DesiredAccess,
                             ObjectAttributes,
                             ParentProcess,
                             Flags,
                             SectionHandle,
                             DebugPort,
                             ExceptionPort,
                             0);
}

/*
 * Opens a process by identifier or by name, never by both. A thread
 * identifier may be given with the process it belongs to, and the two have
 * to agree.
 */
NTSTATUS
NTAPI
NtOpenProcess(
    _Out_ PHANDLE ProcessHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PCLIENT_ID ClientId)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    CLIENT_ID SafeCid = { NULL, NULL };
    BOOLEAN HasCid = FALSE;
    BOOLEAN HasName;
    PEPROCESS Process;
    PETHREAD Thread;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(ProcessHandle);
            if (ClientId != NULL)
            {
                ProbeForRead(ClientId, sizeof(CLIENT_ID), sizeof(ULONG));
                SafeCid = *ClientId;
                HasCid = TRUE;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else if (ClientId != NULL)
    {
        SafeCid = *ClientId;
        HasCid = TRUE;
    }

    HasName = (ObjectAttributes != NULL &&
               ObjectAttributes->ObjectName != NULL &&
               ObjectAttributes->ObjectName->Length != 0);

    if (HasCid == HasName) return STATUS_INVALID_PARAMETER_MIX;

    if (HasCid)
    {
        if (SafeCid.UniqueThread != NULL)
        {
            Status = PsLookupProcessThreadByCid(&SafeCid, &Process, &Thread);
            if (NT_SUCCESS(Status)) ObDereferenceObject(Thread);
        }
        else
        {
            Status = PsLookupProcessByProcessId(SafeCid.UniqueProcess, &Process);
        }

        if (!NT_SUCCESS(Status)) return Status;

        Status = ObOpenObjectByPointer(Process,
                                       (ObjectAttributes != NULL) ?
                                           ObjectAttributes->Attributes : 0,
                                       NULL,
                                       DesiredAccess,
                                       PsProcessType,
                                       PreviousMode,
                                       &Handle);
        ObDereferenceObject(Process);
    }
    else
    {
        Status = ObOpenObjectByName(ObjectAttributes,
                                    PsProcessType,
                                    PreviousMode,
                                    NULL,
                                    DesiredAccess,
                                    NULL,
                                    &Handle);
    }

    if (!NT_SUCCESS(Status)) return Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *ProcessHandle = Handle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *ProcessHandle = Handle;
    }

    return Status;
}
