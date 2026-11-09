/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bringing the process manager up: its object types, its tables
 *              and the native image every user process starts from
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
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
        PROCESS_TERMINATE | PROCESS_SET_QUOTA | PROCESS_SET_INFORMATION |
        PROCESS_SUSPEND_RESUME,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
    PROCESS_ALL_ACCESS
};

GENERIC_MAPPING PspThreadMapping =
{
    STANDARD_RIGHTS_READ | THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT,
    STANDARD_RIGHTS_WRITE | THREAD_SET_CONTEXT | THREAD_SET_INFORMATION |
        THREAD_SET_THREAD_TOKEN | THREAD_SUSPEND_RESUME | THREAD_TERMINATE |
        THREAD_IMPERSONATE | THREAD_DIRECT_IMPERSONATION,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
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
    UNREFERENCED_PARAMETER(GrantedAccess);
    UNREFERENCED_PARAMETER(HandleCount);

    PAGED_CODE();

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
    UNREFERENCED_PARAMETER(GrantedAccess);
    UNREFERENCED_PARAMETER(HandleCount);

    PAGED_CODE();

    return STATUS_SUCCESS;
}

/*
 * Neither a process nor a thread is ever given a name in the namespace, so
 * the object manager has nothing to report for one.
 */
static
NTSTATUS
NTAPI
PspQueryObjectName(
    _In_ PVOID Object,
    _In_ BOOLEAN HasObjectName,
    _Out_ POBJECT_NAME_INFORMATION ObjectNameInfo,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength,
    _In_ KPROCESSOR_MODE AccessMode)
{
    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(HasObjectName);
    UNREFERENCED_PARAMETER(ObjectNameInfo);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(AccessMode);

    PAGED_CODE();

    *ReturnLength = 0;
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

CODE_SEG("INIT")
static
BOOLEAN
PspCreateObjectTypes(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name;

    PAGED_CODE();

    RtlZeroMemory(&Initializer, sizeof(Initializer));
    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_PERMANENT | OBJ_OPENLINK;
    Initializer.GenericMapping = PspProcessMapping;
    Initializer.PoolType = NonPagedPool;
    Initializer.ValidAccessMask = PROCESS_ALL_ACCESS;
    Initializer.DefaultNonPagedPoolCharge = sizeof(EPROCESS);
    Initializer.MaintainHandleCount = TRUE;
    Initializer.SecurityRequired = TRUE;
    Initializer.OpenProcedure = PspProcessOpen;
    Initializer.CloseProcedure = PspProcessClose;
    Initializer.DeleteProcedure = PspDeleteProcess;
    Initializer.QueryNameProcedure = PspQueryObjectName;
    Initializer.SecurityProcedure = SeDefaultObjectMethod;

    RtlInitUnicodeString(&Name, L"Process");
    if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                       &Initializer,
                                       NULL,
                                       &PsProcessType)))
    {
        return FALSE;
    }

    RtlZeroMemory(&Initializer, sizeof(Initializer));
    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_PERMANENT | OBJ_OPENLINK;
    Initializer.GenericMapping = PspThreadMapping;
    Initializer.PoolType = NonPagedPool;
    Initializer.ValidAccessMask = THREAD_ALL_ACCESS;
    Initializer.DefaultNonPagedPoolCharge = sizeof(ETHREAD);
    Initializer.MaintainHandleCount = TRUE;
    Initializer.SecurityRequired = TRUE;
    Initializer.OpenProcedure = PspThreadOpen;
    Initializer.DeleteProcedure = PspDeleteThread;
    Initializer.QueryNameProcedure = PspQueryObjectName;
    Initializer.SecurityProcedure = SeDefaultObjectMethod;

    RtlInitUnicodeString(&Name, L"Thread");
    if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                       &Initializer,
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
    Process->DefaultHardErrorProcessing = SEM_FAILCRITICALERRORS;
    RtlCopyMemory(Process->ImageFileName, "System", sizeof("System"));
    KeQuerySystemTime(&Process->CreateTime);

    Status = PspInitializeProcessSecurity(Process, NULL);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Process);
        return FALSE;
    }

    /* The booting thread leaves Idle and becomes the system process's own */
    RemoveEntryList(&Thread->ThreadListEntry);
    InitializeListHead(&PsIdleProcess->ThreadListHead);
    PsIdleProcess->ActiveThreads = 0;

    Thread->Tcb.ApcState.Process = &Process->Pcb;
    Process->ActiveThreads = 1;
    InsertTailList(&Process->ThreadListHead, &Thread->ThreadListEntry);
    Thread->ThreadInserted = TRUE;

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
    return PspInitializeApiSetSchema();
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

    return PspInitializeSystemDll();
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

    PspSystemDllEntryPoint =
        (PVOID)((ULONG_PTR)PspSystemDllBase +
                NtHeaders->OptionalHeader.AddressOfEntryPoint);

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
    PspRtlUserThreadStart =
        RtlFindExportedRoutineByName(PspSystemDllBase, "RtlUserThreadStart");
    PspSystemDllInitBlock =
        RtlFindExportedRoutineByName(PspSystemDllBase, "LdrSystemDllInitBlock");

    KeUserPopEntrySListEnd =
        RtlFindExportedRoutineByName(PspSystemDllBase,
                                     "RtlpInterlockedPopEntrySListEnd");
    KeUserPopEntrySListFault =
        RtlFindExportedRoutineByName(PspSystemDllBase,
                                     "RtlpInterlockedPopEntrySListFault");
    KeUserPopEntrySListResume =
        RtlFindExportedRoutineByName(PspSystemDllBase,
                                     "RtlpInterlockedPopEntrySListResume");

    if (KeUserApcDispatcher == NULL ||
        KeUserExceptionDispatcher == NULL ||
        KeUserCallbackDispatcher == NULL ||
        KeRaiseUserExceptionDispatcher == NULL ||
        PspRtlUserThreadStart == NULL)
    {
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    return STATUS_SUCCESS;
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
    if (MajorVersion != NULL) *MajorVersion = NtMajorVersion;
    if (MinorVersion != NULL) *MinorVersion = NtMinorVersion;
    if (BuildNumber != NULL) *BuildNumber = NtBuildNumber & 0x3FFF;

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
    UNREFERENCED_PARAMETER(CallerAddress);
    UNREFERENCED_PARAMETER(MajorVersion);
    UNREFERENCED_PARAMETER(MinorVersion);
    UNREFERENCED_PARAMETER(BuildNumber);

    /* TODO: recognise an XPDM miniport caller and report 5.1.2600 to it */
    return FALSE;
}
