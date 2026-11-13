/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bringing the process manager up: its object types, its tables
 *              and the native image every user process starts from
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#include <reactos/wow64shared.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

POBJECT_TYPE PsProcessType;
POBJECT_TYPE PsThreadType;

PEPROCESS PsInitialSystemProcess;
PEPROCESS PsIdleProcess;
HANDLE PspInitialSystemProcessHandle;

LIST_ENTRY PsActiveProcessHead;
EX_PUSH_LOCK PspActiveProcessLock;
PHANDLE_TABLE PspCidTable;

LARGE_INTEGER ShortPsLockDelay;

ULONG PspTraceLevel;

PVOID PspWow64SystemDllSection;
ULONG PspWow64RtlUserThreadStartRva;

UNICODE_STRING PsNtDllPathName =
    RTL_CONSTANT_STRING(L"\\SystemRoot\\System32\\ntdll.dll");

PVOID PspSystemDllSection;
PVOID PspSystemDllBase;
PVOID PspSystemDllEntryPoint;
PVOID PspSystemDllInitBlock;
PVOID PspRtlUserThreadStart;

PVOID KeUserPopEntrySListEnd;
PVOID KeUserPopEntrySListFault;
PVOID KeUserPopEntrySListResume;

GENERIC_MAPPING PspProcessMapping =
{
    STANDARD_RIGHTS_READ | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
    STANDARD_RIGHTS_WRITE | PROCESS_CREATE_PROCESS | PROCESS_CREATE_THREAD |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_DUP_HANDLE |
        PROCESS_SET_QUOTA | PROCESS_SET_INFORMATION |
        PROCESS_SUSPEND_RESUME,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE | PROCESS_TERMINATE |
        PROCESS_QUERY_LIMITED_INFORMATION,
    PROCESS_ALL_ACCESS
};

GENERIC_MAPPING PspThreadMapping =
{
    STANDARD_RIGHTS_READ | THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT,
    STANDARD_RIGHTS_WRITE | THREAD_SET_CONTEXT | THREAD_SET_INFORMATION |
        THREAD_SET_LIMITED_INFORMATION | THREAD_SUSPEND_RESUME |
        THREAD_TERMINATE | THREAD_ALERT,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE | THREAD_QUERY_LIMITED_INFORMATION |
        THREAD_RESUME,
    THREAD_ALL_ACCESS
};

/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
PspProcessOpen(
    _In_ OB_OPEN_REASON Reason,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_opt_ PEPROCESS Process,
    _In_ PVOID ObjectBody,
    _Inout_ PACCESS_MASK GrantedAccess,
    _In_ ULONG HandleCount)
{
    UNREFERENCED_PARAMETER(Reason);
    UNREFERENCED_PARAMETER(AccessMode);
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(ObjectBody);
    UNREFERENCED_PARAMETER(HandleCount);

    PAGED_CODE();

    /* A handle that may query everything may also query the limited subset */
    if ((*GrantedAccess & PROCESS_QUERY_INFORMATION) ||
        ((*GrantedAccess & (PROCESS_VM_OPERATION | PROCESS_VM_WRITE)) ==
         (PROCESS_VM_OPERATION | PROCESS_VM_WRITE)))
    {
        *GrantedAccess |= PROCESS_QUERY_LIMITED_INFORMATION;
    }

    if (*GrantedAccess & PROCESS_SET_INFORMATION)
        *GrantedAccess |= PROCESS_SET_LIMITED_INFORMATION;

    /* TODO: hold a reference on the process's session space while handles live */
    return STATUS_SUCCESS;
}

VOID
NTAPI
PspProcessClose(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID ObjectBody,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount)
{
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(ObjectBody);
    UNREFERENCED_PARAMETER(ProcessHandleCount);
    UNREFERENCED_PARAMETER(SystemHandleCount);

    PAGED_CODE();
}

NTSTATUS
NTAPI
PspThreadOpen(
    _In_ OB_OPEN_REASON Reason,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_opt_ PEPROCESS Process,
    _In_ PVOID ObjectBody,
    _Inout_ PACCESS_MASK GrantedAccess,
    _In_ ULONG HandleCount)
{
    UNREFERENCED_PARAMETER(Reason);
    UNREFERENCED_PARAMETER(AccessMode);
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(ObjectBody);
    UNREFERENCED_PARAMETER(HandleCount);

    PAGED_CODE();

    /* The full query and set rights each bring their limited subset */
    if (*GrantedAccess & THREAD_QUERY_INFORMATION)
        *GrantedAccess |= THREAD_QUERY_LIMITED_INFORMATION;

    if (*GrantedAccess & THREAD_SET_INFORMATION)
        *GrantedAccess |= THREAD_SET_LIMITED_INFORMATION;

    if (*GrantedAccess & THREAD_SUSPEND_RESUME)
        *GrantedAccess |= THREAD_RESUME;

    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
static
BOOLEAN
PspCreateObjectTypes(VOID)
{
    /* The two masks that follow the initializer are what its longer length announces */
    struct
    {
        OBJECT_TYPE_INITIALIZER Initializer;
        ULONG SeMandatoryLabelMask;
        ULONG SeTrustConstraintMask;
    } Extended;
    POBJECT_TYPE_INITIALIZER Initializer = &Extended.Initializer;
    UNICODE_STRING Name;

    PAGED_CODE();

    RtlZeroMemory(&Extended, sizeof(Extended));
    Extended.SeMandatoryLabelMask = SYSTEM_MANDATORY_LABEL_NO_WRITE_UP |
                                    SYSTEM_MANDATORY_LABEL_NO_READ_UP;
    Initializer->Length = sizeof(Extended);
    Initializer->InvalidAttributes = OBJ_PERMANENT | OBJ_EXCLUSIVE | OBJ_OPENIF;
    Initializer->GenericMapping = PspProcessMapping;
    Initializer->PoolType = NonPagedPoolNx;
    Initializer->ValidAccessMask = PROCESS_ALL_ACCESS;
    Initializer->DefaultPagedPoolCharge = PAGE_SIZE;
    Initializer->DefaultNonPagedPoolCharge = sizeof(EPROCESS);
    Initializer->ObjectTypeCode = 0x20;
    Initializer->RetainAccess = SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION;
    Initializer->UnnamedObjectsOnly = TRUE;
    Initializer->SecurityRequired = TRUE;
    Initializer->SupportsObjectCallbacks = TRUE;
    Initializer->CacheAligned = TRUE;
    Initializer->OpenProcedure = PspProcessOpen;
    Initializer->CloseProcedure = PspProcessClose;
    Initializer->DeleteProcedure = PspDeleteProcess;
    Initializer->SecurityProcedure = SeDefaultObjectMethod;

    RtlInitUnicodeString(&Name, L"Process");
    if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                       Initializer,
                                       NULL,
                                       &PsProcessType)))
    {
        return FALSE;
    }

    RtlZeroMemory(&Extended, sizeof(Extended));
    Extended.SeMandatoryLabelMask = SYSTEM_MANDATORY_LABEL_NO_WRITE_UP |
                                    SYSTEM_MANDATORY_LABEL_NO_READ_UP;
    Initializer->Length = sizeof(Extended);
    Initializer->InvalidAttributes = OBJ_PERMANENT | OBJ_EXCLUSIVE | OBJ_OPENIF;
    Initializer->GenericMapping = PspThreadMapping;
    Initializer->PoolType = NonPagedPoolNx;
    Initializer->ValidAccessMask = THREAD_ALL_ACCESS;
    Initializer->DefaultNonPagedPoolCharge = 0x768;
    Initializer->ObjectTypeCode = 0x4;
    Initializer->RetainAccess = SYNCHRONIZE | THREAD_QUERY_LIMITED_INFORMATION | THREAD_RESUME;
    Initializer->UnnamedObjectsOnly = TRUE;
    Initializer->SecurityRequired = TRUE;
    Initializer->SupportsObjectCallbacks = TRUE;
    Initializer->CacheAligned = TRUE;
    Initializer->OpenProcedure = PspThreadOpen;
    Initializer->DeleteProcedure = PspDeleteThread;
    Initializer->SecurityProcedure = SeDefaultObjectMethod;

    RtlInitUnicodeString(&Name, L"Thread");
    if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                       Initializer,
                                       NULL,
                                       &PsThreadType)))
    {
        return FALSE;
    }

    return TRUE;
}

/*
 * The idle process is not created, it is already running: the loader handed
 * the kernel a page directory and a stack, and the boot processor's idle
 * thread is the thread we are on. All that is left is to give that pair a
 * name a caller can see.
 */
CODE_SEG("INIT")
static
VOID
PspAdoptIdleProcess(VOID)
{
    PETHREAD Thread = PsGetCurrentThread();

    PsIdleProcess = &KiInitialProcess;

    InitializeListHead(&PsIdleProcess->ThreadListHead);
    InitializeListHead(&PsIdleProcess->ActiveProcessLinks);
    ExInitializePushLock(&PsIdleProcess->ProcessLock);
    ExInitializePushLock(&PsIdleProcess->ThreadListLock);
    ExInitializeRundownProtection(&PsIdleProcess->RundownProtect);

    PsIdleProcess->UniqueProcessId = 0;
    PsIdleProcess->Machine = IMAGE_FILE_MACHINE_NATIVE;
    PsIdleProcess->Pcb.BasePriority = 0;
    PsIdleProcess->Pcb.QuantumReset = 6;
    PsIdleProcess->ActiveThreads = 1;
    PsIdleProcess->HasAddressSpace = TRUE;
    RtlCopyMemory(PsIdleProcess->ImageFileName, "Idle", sizeof("Idle"));

    InsertTailList(&PsIdleProcess->ThreadListHead, &Thread->ThreadListEntry);
}

/*
 * The system process owns every kernel thread. Its address space is the one
 * the kernel already runs in, so it is handed that space rather than given
 * one of its own, and the booting thread becomes its first thread.
 */
CODE_SEG("INIT")
static
BOOLEAN
PspBuildSystemProcess(VOID)
{
    ULONG_PTR DirectoryTableBase[2] = { 0, 0 };
    PETHREAD Thread = PsGetCurrentThread();
    PEPROCESS Process;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObCreateObject(KernelMode,
                            PsProcessType,
                            NULL,
                            KernelMode,
                            NULL,
                            sizeof(EPROCESS),
                            0,
                            0,
                            (PVOID*)&Process);
    if (!NT_SUCCESS(Status)) return FALSE;

    RtlZeroMemory(Process, sizeof(EPROCESS));

    InitializeListHead(&Process->ThreadListHead);
    InitializeListHead(&Process->JobLinks);
    ExInitializePushLock(&Process->ProcessLock);
    ExInitializePushLock(&Process->ThreadListLock);
    ExInitializeRundownProtection(&Process->RundownProtect);
    Process->QuotaBlock = &PspDefaultQuotaBlock;

    /* A process that has not exited reports its exit as still pending */
    Process->ExitStatus = STATUS_PENDING;

    Status = MmInitializeHandBuiltProcess(Process, DirectoryTableBase);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Process);
        return FALSE;
    }

    KeInitializeProcess(&Process->Pcb,
                        PROCESS_PRIORITY_NORMAL,
                        (KAFFINITY)-1,
                        DirectoryTableBase,
                        FALSE);
    Process->Pcb.QuantumReset = 6;
    Process->PriorityClass = PROCESS_PRIORITY_CLASS_NORMAL;
    Process->SystemProcess = TRUE;
    Process->Machine = IMAGE_FILE_MACHINE_NATIVE;
    Process->DefaultHardErrorProcessing = SEM_FAILCRITICALERRORS;
    RtlCopyMemory(Process->ImageFileName, "System", sizeof("System"));
    KeQuerySystemTime(&Process->CreateTime);

    Status = PspInitializeProcessSecurity(Process, NULL);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Process);
        return FALSE;
    }

    /* The booting thread becomes the first processor's idle thread, which is
     * Idle's and no object, so it never joins the system process's list */
    /* FIXME: phase 0 runs in the system process's context; Windows keeps the boot thread in Idle */
    Thread->Tcb.ApcState.Process = &Process->Pcb;

    Status = PspInsertCid(Process, &Process->UniqueProcessId);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Process);
        return FALSE;
    }

    /*
     * The table the object manager built while booting becomes this process's
     * own: a kernel handle belongs to the system process, and nothing else
     * ever holds a handle in the one the boot started in.
     */
    Process->ObjectTable = PsIdleProcess->ObjectTable;
    PsIdleProcess->ObjectTable = NULL;

    PsInitialSystemProcess = Process;
    InsertTailList(&PsActiveProcessHead, &Process->ActiveProcessLinks);
    Process->ProcessInserted = TRUE;

    Status = MmInitializeHandBuiltProcess2(Process);
    if (!NT_SUCCESS(Status)) return FALSE;

    ObDeleteCapturedInsertInfo(Process);

    Status = ObOpenObjectByPointer(Process,
                                   OBJ_KERNEL_HANDLE,
                                   NULL,
                                   PROCESS_ALL_ACCESS,
                                   PsProcessType,
                                   KernelMode,
                                   &PspInitialSystemProcessHandle);
    return NT_SUCCESS(Status);
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
PspInitPhase0(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);

    PAGED_CODE();

    /* Ten milliseconds: how long a caller waits out a lock it could not take */
    ShortPsLockDelay.QuadPart = -100000LL;

    InitializeListHead(&PsActiveProcessHead);
    ExInitializePushLock(&PspActiveProcessLock);

    PsChangeQuantumTable(FALSE, PsRawPrioritySeparation);

    PsInitializeQuotaSystem();

    if (!PspCreateObjectTypes()) return FALSE;

    PspInitializeJobStructures();

    /*
     * Process and thread identifiers come out of one table, so an identifier
     * names exactly one of the two and never both.
     */
    PspCidTable = ExCreateHandleTable(NULL, FALSE);
    if (PspCidTable == NULL) return FALSE;

    /* Dead threads are handed to this item once nothing runs on their stacks */
    ExInitializeWorkItem(&PspReaperWorkItem, PspReapRoutine, NULL);

    PspAdoptIdleProcess();
    if (!PspBuildSystemProcess()) return FALSE;

    PspInitializeTls();

    if (!NT_SUCCESS(ApphelpCacheInitialize())) return FALSE;

    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
PspInitPhase1(VOID)
{
    PAGED_CODE();

    /*
     * The system library is found later in this phase, once there is a file
     * system to find it on; nothing here may touch a file yet.
     */
    return PspInitializeApiSetSchema() && PspInitializeApiSetSection();
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
PsInitSystem(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PAGED_CODE();

    if (ExpInitializationPhase == 0) return PspInitPhase0(LoaderBlock);
    if (ExpInitializationPhase == 1) return PspInitPhase1();

    return FALSE;
}

/* The 32-bit ntdll is kept as one section too; a system without it runs no 32-bit process */
CODE_SEG("INIT")
static
VOID
PspLocateWow64SystemDll(VOID)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\SystemRoot\\SysWOW64\\ntdll.dll");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER Offset;
    SIZE_T ViewSize = 0;
    PVOID Base = NULL;
    PVOID Routine = NULL;
    HANDLE FileHandle;
    NTSTATUS Status;

    PAGED_CODE();

    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenFile(&FileHandle,
                        SYNCHRONIZE | FILE_EXECUTE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_DELETE,
                        0);
    if (!NT_SUCCESS(Status)) return;

    Status = MmCreateSection(&PspWow64SystemDllSection,
                             SECTION_ALL_ACCESS,
                             NULL,
                             NULL,
                             PAGE_EXECUTE,
                             SEC_IMAGE,
                             FileHandle,
                             NULL);
    ZwClose(FileHandle);
    if (!NT_SUCCESS(Status))
    {
        PspWow64SystemDllSection = NULL;
        return;
    }

    Offset.QuadPart = 0;
    Status = MmMapViewOfSection(PspWow64SystemDllSection,
                                PsGetCurrentProcess(),
                                &Base,
                                0,
                                0,
                                &Offset,
                                &ViewSize,
                                ViewShare,
                                0,
                                PAGE_READWRITE);
    if (Status == STATUS_IMAGE_NOT_AT_BASE) Status = STATUS_SUCCESS;
    if (NT_SUCCESS(Status))
    {
        Routine = RtlFindExportedRoutineByName(Base, "RtlUserThreadStart");
        MmUnmapViewOfSection(PsGetCurrentProcess(), Base);
    }

    if (Routine == NULL)
    {
        ObDereferenceObject(PspWow64SystemDllSection);
        PspWow64SystemDllSection = NULL;
        return;
    }

    PspWow64RtlUserThreadStartRva = (ULONG)((ULONG_PTR)Routine - (ULONG_PTR)Base);
}

/* The native loader takes the view the kernel made instead of mapping its own */
NTSTATUS
NTAPI
PspMapWow64SystemDll(
    _In_ PEPROCESS Process,
    _Out_ PULONG RtlUserThreadStart)
{
    PSYSTEM_DLL_INIT_BLOCK InitBlock = PspSystemDllInitBlock;
    LARGE_INTEGER Offset;
    KAPC_STATE ApcState;
    SIZE_T ViewSize = 0;
    PVOID Base = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    *RtlUserThreadStart = 0;
    if (PspWow64SystemDllSection == NULL || InitBlock == NULL) return STATUS_INVALID_IMAGE_FORMAT;

    Offset.QuadPart = 0;
    Status = MmMapViewOfSection(PspWow64SystemDllSection,
                                Process,
                                &Base,
                                0,
                                0,
                                &Offset,
                                &ViewSize,
                                ViewShare,
                                0,
                                PAGE_READWRITE);
    if (Status == STATUS_IMAGE_NOT_AT_BASE) Status = STATUS_SUCCESS;
    if (!NT_SUCCESS(Status)) return Status;

    if ((ULONG_PTR)Base + ViewSize > MAXLONG)
    {
        MmUnmapViewOfSection(Process, Base);
        return STATUS_CONFLICTING_ADDRESSES;
    }

    KeStackAttachProcess(&Process->Pcb, &ApcState);
    _SEH2_TRY
    {
        InitBlock->ntdll_handle = (ULONG_PTR)Base;
        InitBlock->pRtlUserThreadStart = (ULONG_PTR)Base + PspWow64RtlUserThreadStartRva;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    KeUnstackDetachProcess(&ApcState);

    if (!NT_SUCCESS(Status))
    {
        MmUnmapViewOfSection(Process, Base);
        return Status;
    }

    *RtlUserThreadStart = (ULONG)((ULONG_PTR)Base + PspWow64RtlUserThreadStartRva);
    return STATUS_SUCCESS;
}

/* A WoW64 process's 32-bit ntdll and the 32-bit entry its threads start on, as the loader keeps them for the process */
NTSTATUS
NTAPI
PsGetWow64SystemDll(
    _In_ PEPROCESS Process,
    _Out_opt_ PVOID *DllBase,
    _Out_opt_ PULONG RtlUserThreadStart)
{
    PSYSTEM_DLL_INIT_BLOCK InitBlock = PspSystemDllInitBlock;
    KAPC_STATE ApcState;
    ULONG64 Base = 0;
    ULONG64 Start = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (DllBase != NULL) *DllBase = NULL;
    if (RtlUserThreadStart != NULL) *RtlUserThreadStart = 0;
    if (InitBlock == NULL || Process->WoW64Process == NULL) return STATUS_INVALID_IMAGE_FORMAT;

    KeStackAttachProcess(&Process->Pcb, &ApcState);
    _SEH2_TRY
    {
        Base = InitBlock->ntdll_handle;
        Start = InitBlock->pRtlUserThreadStart;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    KeUnstackDetachProcess(&ApcState);

    if (!NT_SUCCESS(Status)) return Status;
    if (Base == 0 || Base > MAXULONG || Start == 0 || Start > MAXULONG) return STATUS_INVALID_IMAGE_FORMAT;

    if (DllBase != NULL) *DllBase = (PVOID)(ULONG_PTR)Base;
    if (RtlUserThreadStart != NULL) *RtlUserThreadStart = (ULONG)Start;
    return STATUS_SUCCESS;
}

/*
 * Finds the native image and keeps a section of it. Every user process is
 * given a view of that one section, so the image is read once for the life
 * of the system.
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
PsLocateSystemDll(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;
    NTSTATUS Status;

    PAGED_CODE();

    InitializeObjectAttributes(&ObjectAttributes,
                               &PsNtDllPathName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenFile(&FileHandle,
                        SYNCHRONIZE | FILE_EXECUTE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_DELETE,
                        0);
    if (!NT_SUCCESS(Status)) return Status;

    Status = MmCheckSystemImage(FileHandle);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(FileHandle);
        return Status;
    }

    Status = MmCreateSection(&PspSystemDllSection,
                             SECTION_ALL_ACCESS,
                             NULL,
                             NULL,
                             PAGE_EXECUTE,
                             SEC_IMAGE,
                             FileHandle,
                             NULL);
    ZwClose(FileHandle);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PspMapSystemDll(PsGetCurrentProcess(), NULL, FALSE);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PspInitializeSystemDll();
    if (!NT_SUCCESS(Status)) return Status;

    if (sizeof(PVOID) > sizeof(ULONG)) PspLocateWow64SystemDll();

    return STATUS_SUCCESS;
}

/*
 * Maps the native image into a process. A caller that does not need to know
 * where the image landed asks for no base back.
 */
NTSTATUS
NTAPI
PspMapSystemDll(
    _In_ PEPROCESS Process,
    _Out_opt_ PVOID *DllBase,
    _In_ BOOLEAN UseLargePages)
{
    LARGE_INTEGER Offset;
    PVOID ImageBase = NULL;
    SIZE_T ViewSize = 0;
    NTSTATUS Status;

    PAGED_CODE();

    Offset.QuadPart = 0;

    Status = MmMapViewOfSection(PspSystemDllSection,
                                Process,
                                &ImageBase,
                                0,
                                0,
                                &Offset,
                                &ViewSize,
                                ViewShare,
                                UseLargePages ? MEM_LARGE_PAGES : 0,
                                PAGE_READWRITE);
    if (Status == STATUS_IMAGE_NOT_AT_BASE) Status = STATUS_SUCCESS;
    if (!NT_SUCCESS(Status)) return Status;

    if (DllBase != NULL) *DllBase = ImageBase;
    if (PspSystemDllBase == NULL) PspSystemDllBase = ImageBase;

    return STATUS_SUCCESS;
}

/*
 * The kernel needs the addresses the native image publishes for it: where a
 * user APC lands, where an exception is dispatched, where a callback returns
 * and the entry every user thread is started on. A missing one is fatal,
 * because nothing the kernel does for a user thread would work without it.
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
PspInitializeSystemDll(VOID)
{
    PIMAGE_NT_HEADERS NtHeaders;

    PAGED_CODE();

    if (PspSystemDllBase == NULL) return STATUS_PROCEDURE_NOT_FOUND;

    NtHeaders = RtlImageNtHeader(PspSystemDllBase);
    if (NtHeaders == NULL) return STATUS_INVALID_IMAGE_FORMAT;

    /* A new thread's first user APC goes to the loader, not to an image entry */
    PspSystemDllEntryPoint =
        RtlFindExportedRoutineByName(PspSystemDllBase, "LdrInitializeThunk");

    KeUserApcDispatcher =
        RtlFindExportedRoutineByName(PspSystemDllBase, "KiUserApcDispatcher");
    KeUserExceptionDispatcher =
        RtlFindExportedRoutineByName(PspSystemDllBase,
                                     "KiUserExceptionDispatcher");
    KeUserCallbackDispatcher =
        RtlFindExportedRoutineByName(PspSystemDllBase,
                                     "KiUserCallbackDispatcher");
    KeRaiseUserExceptionDispatcher =
        RtlFindExportedRoutineByName(PspSystemDllBase,
                                     "KiRaiseUserExceptionDispatcher");
    KeUserExceptionDispatcherWorker =
        RtlFindExportedRoutineByName(PspSystemDllBase,
                                     "KiUserExceptionDispatcherWorker");
    KeUserEmulationDispatcher =
        RtlFindExportedRoutineByName(PspSystemDllBase,
                                     "KiUserEmulationDispatcher");
    PspRtlUserThreadStart =
        RtlFindExportedRoutineByName(PspSystemDllBase, "RtlUserThreadStart");
    PspSystemDllInitBlock =
        RtlFindExportedRoutineByName(PspSystemDllBase, "LdrSystemDllInitBlock");

    KeUserPopEntrySListEnd =
        RtlFindExportedRoutineByName(PspSystemDllBase,
                                     "ExpInterlockedPopEntrySListEnd");
    KeUserPopEntrySListFault =
        RtlFindExportedRoutineByName(PspSystemDllBase,
                                     "ExpInterlockedPopEntrySListFault");
    KeUserPopEntrySListResume =
        RtlFindExportedRoutineByName(PspSystemDllBase,
                                     "ExpInterlockedPopEntrySListResume");

    if (PspSystemDllEntryPoint == NULL ||
        KeUserApcDispatcher == NULL ||
        KeUserExceptionDispatcher == NULL ||
        KeUserCallbackDispatcher == NULL ||
        KeRaiseUserExceptionDispatcher == NULL ||
        PspRtlUserThreadStart == NULL ||
        KeUserPopEntrySListEnd == NULL ||
        KeUserPopEntrySListFault == NULL ||
        KeUserPopEntrySListResume == NULL)
    {
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    return KiArchPublishSystemCallStubs(PspSystemDllBase);
}

/*
 * The version of the running system. A caller that wants the service-pack
 * string is given the kernel's own, which it may read but never write. The
 * answer the call itself gives is whether this is a checked build.
 */
BOOLEAN
NTAPI
PsGetVersion(
    _Out_opt_ PULONG MajorVersion,
    _Out_opt_ PULONG MinorVersion,
    _Out_opt_ PULONG BuildNumber,
    _Inout_opt_ PUNICODE_STRING CSDVersion)
{
    ULONG ReportedMajor = NtMajorVersion;
    ULONG ReportedMinor = NtMinorVersion;
    ULONG ReportedBuild = NtBuildNumber & 0xFFFF;

    PspGetLegacyXpdmVersion(_ReturnAddress(), &ReportedMajor, &ReportedMinor, &ReportedBuild);

    if (MajorVersion != NULL) *MajorVersion = ReportedMajor;
    if (MinorVersion != NULL) *MinorVersion = ReportedMinor;
    if (BuildNumber != NULL) *BuildNumber = ReportedBuild;

    if (CSDVersion != NULL)
    {
        if (CmCSDVersionString.Length != 0) *CSDVersion = CmCSDVersionString;
        else RtlZeroMemory(CSDVersion, sizeof(*CSDVersion));
    }

#if DBG
    return TRUE;
#else
    return FALSE;
#endif
}

/*
 * A display miniport written before WDDM reads the version through the kernel
 * and acts on what its own era reported.
 */
BOOLEAN
NTAPI
PspGetLegacyXpdmVersion(
    _In_ PVOID CallerAddress,
    _Out_ PULONG MajorVersion,
    _Out_ PULONG MinorVersion,
    _Out_ PULONG BuildNumber)
{
    UNICODE_STRING VideoPortName = RTL_CONSTANT_STRING(L"VIDEOPRT.SYS");
    PIMAGE_NT_HEADERS NtHeaders;
    PVOID ImageBase = NULL;
    ULONG TargetMajor;
    ULONG TargetMinor;
    ULONG TargetBuild;

    PAGED_CODE();

    if ((CallerAddress == NULL) || (RtlPcToFileHeader(CallerAddress, &ImageBase) == NULL))
        return FALSE;

    NtHeaders = RtlImageNtHeader(ImageBase);
    if ((NtHeaders == NULL) || (NtHeaders->OptionalHeader.Subsystem != IMAGE_SUBSYSTEM_NATIVE))
        return FALSE;

    /* XPDM drivers keep a 5.2 subsystem version even when they target another release, so the OS version decides */
    TargetMajor = NtHeaders->OptionalHeader.MajorOperatingSystemVersion;
    TargetMinor = NtHeaders->OptionalHeader.MinorOperatingSystemVersion;

    if ((TargetMajor == 4) && (TargetMinor == 0))
        TargetBuild = 1381;
    else if ((TargetMajor == 5) && (TargetMinor == 0))
        TargetBuild = 2195;
    else if ((TargetMajor == 5) && (TargetMinor == 1))
        TargetBuild = 2600;
    else if ((TargetMajor == 5) && (TargetMinor == 2))
        TargetBuild = 3790;
    else
        return FALSE;

    if ((TargetMajor > NtMajorVersion) || ((TargetMajor == NtMajorVersion) && (TargetMinor >= NtMinorVersion)))
        return FALSE;

    /* A miniport may discard its PE import names after DriverEntry, so the loader's import list decides */
    if (!MmIsSystemImageImportingModule(ImageBase, &VideoPortName))
        return FALSE;

    *MajorVersion = TargetMajor;
    *MinorVersion = TargetMinor;
    *BuildNumber = TargetBuild;

    DPRINT("XPDM caller %p in image %p is told NT %lu.%lu build %lu\n",
           CallerAddress, ImageBase, TargetMajor, TargetMinor, TargetBuild);
    return TRUE;
}
