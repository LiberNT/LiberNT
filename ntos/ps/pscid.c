/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Turning a process or thread identifier back into its object,
 *              and walking the processes and their threads in order
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * An identifier is a handle into one table shared by processes and threads,
 * so what it names has to be checked against the type the caller asked for.
 * The table entry is locked while we look at it, and the reference taken on
 * the object is what keeps it alive once the entry is let go.
 */
static
NTSTATUS
PspLookupCidObject(
    _In_ HANDLE Cid,
    _In_ POBJECT_TYPE ObjectType,
    _Outptr_ PVOID *Object)
{
    PHANDLE_TABLE_ENTRY Entry;
    PVOID Found;

    PAGED_CODE();

    *Object = NULL;

    KeEnterCriticalRegion();

    Entry = ExMapHandleToPointer(PspCidTable, Cid);
    if (Entry == NULL)
    {
        KeLeaveCriticalRegion();
        return STATUS_INVALID_CID;
    }

    Found = (PVOID)((ULONG_PTR)Entry->Object & ~OBJ_HANDLE_ATTRIBUTES);

    if (Found == NULL || ObGetObjectType(Found) != ObjectType)
    {
        ExUnlockHandleTableEntry(PspCidTable, Entry);
        KeLeaveCriticalRegion();
        return STATUS_INVALID_CID;
    }

    /*
     * A process or thread that is being torn down is already gone as far as
     * a caller is concerned, so its rundown protection decides whether the
     * identifier still names anything.
     */
    if (ObjectType == PsProcessType)
    {
        if (!ExAcquireRundownProtection(&((PEPROCESS)Found)->RundownProtect))
        {
            ExUnlockHandleTableEntry(PspCidTable, Entry);
            KeLeaveCriticalRegion();
            return STATUS_INVALID_CID;
        }

        ObReferenceObject(Found);
        ExReleaseRundownProtection(&((PEPROCESS)Found)->RundownProtect);
    }
    else
    {
        if (!ExAcquireRundownProtection(&((PETHREAD)Found)->RundownProtect))
        {
            ExUnlockHandleTableEntry(PspCidTable, Entry);
            KeLeaveCriticalRegion();
            return STATUS_INVALID_CID;
        }

        ObReferenceObject(Found);
        ExReleaseRundownProtection(&((PETHREAD)Found)->RundownProtect);
    }

    ExUnlockHandleTableEntry(PspCidTable, Entry);
    KeLeaveCriticalRegion();

    *Object = Found;
    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Gives an identifier an entry in the shared table. The object is not visible
 * to a caller until it has one, so this is the point a process or thread
 * becomes something another one can find.
 */
NTSTATUS
NTAPI
PspInsertCid(
    _In_ PVOID Object,
    _Out_ PHANDLE Cid)
{
    HANDLE_TABLE_ENTRY Entry;
    HANDLE Handle;

    PAGED_CODE();

    RtlZeroMemory(&Entry, sizeof(Entry));
    Entry.Object = Object;
    Entry.GrantedAccess = 0;

    Handle = ExCreateHandle(PspCidTable, &Entry);
    if (Handle == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    *Cid = Handle;
    return STATUS_SUCCESS;
}

VOID
NTAPI
PspRemoveCid(
    _In_ HANDLE Cid)
{
    PAGED_CODE();

    if (Cid == NULL) return;

    KeEnterCriticalRegion();
    ExDestroyHandle(PspCidTable, Cid, NULL);
    KeLeaveCriticalRegion();
}

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS
NTAPI
PsLookupProcessByProcessId(
    _In_ HANDLE ProcessId,
    _Outptr_ PEPROCESS *Process)
{
    PAGED_CODE();

    return PspLookupCidObject(ProcessId, PsProcessType, (PVOID*)Process);
}

_IRQL_requires_max_(APC_LEVEL)
NTSTATUS
NTAPI
PsLookupThreadByThreadId(
    _In_ HANDLE ThreadId,
    _Outptr_ PETHREAD *Thread)
{
    PAGED_CODE();

    return PspLookupCidObject(ThreadId, PsThreadType, (PVOID*)Thread);
}

/*
 * Looks a thread up and hands back its process too. The pair has to agree:
 * a thread identifier that belongs to another process does not name this
 * one, and both references are given up on that failure.
 */
_IRQL_requires_max_(APC_LEVEL)
NTSTATUS
NTAPI
PsLookupProcessThreadByCid(
    _In_ PCLIENT_ID Cid,
    _Outptr_opt_ PEPROCESS *Process,
    _Outptr_ PETHREAD *Thread)
{
    PETHREAD FoundThread;
    PEPROCESS FoundProcess;
    NTSTATUS Status;

    PAGED_CODE();

    Status = PspLookupCidObject(Cid->UniqueThread,
                                PsThreadType,
                                (PVOID*)&FoundThread);
    if (!NT_SUCCESS(Status)) return Status;

    FoundProcess = THREAD_TO_PROCESS(FoundThread);

    if (FoundProcess->UniqueProcessId != Cid->UniqueProcess)
    {
        ObDereferenceObject(FoundThread);
        return STATUS_INVALID_CID;
    }

    if (Process != NULL)
    {
        ObReferenceObject(FoundProcess);
        *Process = FoundProcess;
    }

    *Thread = FoundThread;
    return STATUS_SUCCESS;
}

/*
 * Walks the processes. The caller holds a reference on the one it was given
 * last and gets it back; passing none starts at the first. Every process on
 * the list is handed out once, even one that is exiting, because the walk is
 * what a caller uses to see such a process before it is gone.
 */
PEPROCESS
NTAPI
PsGetNextProcess(
    _In_opt_ PEPROCESS OldProcess)
{
    PLIST_ENTRY Entry;
    PEPROCESS Process = NULL;

    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&PspActiveProcessLock);

    Entry = (OldProcess == NULL) ? PsActiveProcessHead.Flink
                                 : OldProcess->ActiveProcessLinks.Flink;

    while (Entry != &PsActiveProcessHead)
    {
        PEPROCESS Candidate = CONTAINING_RECORD(Entry,
                                                EPROCESS,
                                                ActiveProcessLinks);

        if (ExAcquireRundownProtection(&Candidate->RundownProtect))
        {
            ObReferenceObject(Candidate);
            ExReleaseRundownProtection(&Candidate->RundownProtect);
            Process = Candidate;
            break;
        }

        Entry = Entry->Flink;
    }

    ExReleasePushLockShared(&PspActiveProcessLock);
    KeLeaveCriticalRegion();

    if (OldProcess != NULL) ObDereferenceObject(OldProcess);

    return Process;
}

/*
 * Walks the threads of one process the same way. The process's thread list
 * is held against change while a thread is picked off it.
 */
PETHREAD
NTAPI
PsGetNextProcessThread(
    _In_ PEPROCESS Process,
    _In_opt_ PETHREAD Thread)
{
    PLIST_ENTRY Entry;
    PETHREAD Found = NULL;

    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&Process->ProcessLock);

    Entry = (Thread == NULL) ? Process->ThreadListHead.Flink
                             : Thread->ThreadListEntry.Flink;

    while (Entry != &Process->ThreadListHead)
    {
        PETHREAD Candidate = CONTAINING_RECORD(Entry,
                                               ETHREAD,
                                               ThreadListEntry);

        if (ExAcquireRundownProtection(&Candidate->RundownProtect))
        {
            ObReferenceObject(Candidate);
            ExReleaseRundownProtection(&Candidate->RundownProtect);
            Found = Candidate;
            break;
        }

        Entry = Entry->Flink;
    }

    ExReleasePushLockShared(&Process->ProcessLock);
    KeLeaveCriticalRegion();

    if (Thread != NULL) ObDereferenceObject(Thread);

    return Found;
}
