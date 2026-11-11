/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Telling a waiting caller that a key it watches has changed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PRIVATE DATA ***************************************************************/

/*
 * One answer owed to one caller. A notify that watches a master key and a
 * subordinate key has a post block on each of them, and both point here, so
 * whichever key moves first is the one that answers.
 */
typedef struct _CM_POST_COMPLETION
{
    LONG RefCount;
    LONG Completed;
    PETHREAD Thread;
    PKEVENT Event;
    PIO_APC_ROUTINE ApcRoutine;
    PVOID ApcContext;
    PIO_STATUS_BLOCK IoStatusBlock;
    BOOLEAN IoStatus32;
    BOOLEAN Synchronous;
    KPROCESSOR_MODE Mode;
    KEVENT Finished;
    KAPC Apc;
    KAPC UserApc;
    PCM_KEY_BODY SlaveBody;
    WORK_QUEUE_ITEM SlaveWorkItem;
    NTSTATUS Status;
} CM_POST_COMPLETION, *PCM_POST_COMPLETION;

typedef struct _CM_POST_BLOCK
{
    LIST_ENTRY NotifyList;
    LIST_ENTRY GlobalList;
    PCM_POST_COMPLETION Completion;
    PCM_NOTIFY_BLOCK Notify;
} CM_POST_BLOCK, *PCM_POST_BLOCK;

static LIST_ENTRY CmpPostBlockListHead;
static FAST_MUTEX CmpPostLock;
static BOOLEAN CmpNotifyInited = FALSE;

/* PRIVATE FUNCTIONS **********************************************************/

static VOID CmpReleaseCompletion(_In_ PCM_POST_COMPLETION Completion);

static
VOID
NTAPI
CmpUserApcKernelRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    CmpReleaseCompletion(CONTAINING_RECORD(Apc, CM_POST_COMPLETION, UserApc));
}

static
VOID
NTAPI
CmpUserApcRundownRoutine(
    _In_ PKAPC Apc)
{
    CmpReleaseCompletion(CONTAINING_RECORD(Apc, CM_POST_COMPLETION, UserApc));
}

static
VOID
NTAPI
CmpPostApcRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    PCM_POST_COMPLETION Completion = *(PCM_POST_COMPLETION*)SystemArgument1;
    IO_STATUS_BLOCK IoStatus;

    UNREFERENCED_PARAMETER(Apc);
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument2);

    IoStatus.Status = Completion->Status;
    IoStatus.Information = 0;

    if (Completion->IoStatusBlock != NULL)
    {
        _SEH2_TRY
        {
            if (Completion->Mode != KernelMode)
            {
                ProbeForWrite(Completion->IoStatusBlock,
                              Completion->IoStatus32 ? sizeof(NTSTATUS) + sizeof(ULONG) : sizeof(IO_STATUS_BLOCK),
                              sizeof(ULONG));
            }

            IopWriteIoStatusBlock(Completion->IoStatusBlock,
                                  &IoStatus,
                                  Completion->IoStatus32);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            NOTHING;
        }
        _SEH2_END;
    }

    if (Completion->Event != NULL)
    {
        KeSetEvent(Completion->Event, EVENT_INCREMENT, FALSE);
        ObDereferenceObject(Completion->Event);
        Completion->Event = NULL;
    }

    if (Completion->ApcRoutine != NULL && Completion->Mode == KernelMode)
    {
        ExQueueWorkItem((PWORK_QUEUE_ITEM)Completion->ApcRoutine,
                        (WORK_QUEUE_TYPE)(ULONG_PTR)Completion->ApcContext);
    }
    else if (Completion->ApcRoutine != NULL)
    {
        InterlockedIncrement(&Completion->RefCount);

        KeInitializeApc(&Completion->UserApc,
                        &Completion->Thread->Tcb,
                        OriginalApcEnvironment,
                        CmpUserApcKernelRoutine,
                        CmpUserApcRundownRoutine,
                        (PKNORMAL_ROUTINE)Completion->ApcRoutine,
                        UserMode,
                        Completion->ApcContext);

        if (!KeInsertQueueApc(&Completion->UserApc,
                              Completion->IoStatusBlock,
                              NULL,
                              IO_NO_INCREMENT))
        {
            CmpReleaseCompletion(Completion);
        }
    }

    if (Completion->Synchronous)
    {
        KeSetEvent(&Completion->Finished, EVENT_INCREMENT, FALSE);
    }

    CmpReleaseCompletion(Completion);
}

static
VOID
CmpFreeCompletion(
    _In_ PCM_POST_COMPLETION Completion)
{
    if (Completion->Event != NULL) ObDereferenceObject(Completion->Event);
    if (Completion->Thread != NULL) ObDereferenceObject(Completion->Thread);
    if (Completion->SlaveBody != NULL) ObDereferenceObject(Completion->SlaveBody);

    ExFreePool(Completion);
}

static
VOID
NTAPI
CmpFreeCompletionWorker(
    _In_ PVOID Context)
{
    CmpFreeCompletion(Context);
}

static
VOID
CmpReleaseCompletion(
    _In_ PCM_POST_COMPLETION Completion)
{
    if (InterlockedDecrement(&Completion->RefCount) != 0) return;

    /* The subordinate key's last reference tears down its notify block, which takes the locks a poster may hold */
    if (Completion->SlaveBody != NULL)
    {
        ExInitializeWorkItem(&Completion->SlaveWorkItem,
                             CmpFreeCompletionWorker,
                             Completion);
        ExQueueWorkItem(&Completion->SlaveWorkItem, DelayedWorkQueue);
        return;
    }

    CmpFreeCompletion(Completion);
}

static
VOID
CmpUnlinkPostBlock(
    _In_ PCM_POST_BLOCK Post);

/* An answer given through one key takes back what it left waiting on the other */
static
VOID
CmpCancelSiblingPosts(
    _In_ PCM_POST_COMPLETION Completion)
{
    PCM_POST_BLOCK Post;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;
    ULONG Removed = 0;

    ExAcquireFastMutex(&CmpPostLock);

    for (Entry = CmpPostBlockListHead.Flink;
         Entry != &CmpPostBlockListHead;
         Entry = Next)
    {
        Next = Entry->Flink;
        Post = CONTAINING_RECORD(Entry, CM_POST_BLOCK, GlobalList);

        if (Post->Completion != Completion) continue;

        CmpUnlinkPostBlock(Post);
        ExFreePool(Post);
        Removed++;
    }

    ExReleaseFastMutex(&CmpPostLock);

    while (Removed-- != 0) CmpReleaseCompletion(Completion);
}

/* Hands the answer to the thread that asked for it, in its own context */
static
VOID
CmpDeliverCompletion(
    _In_ PCM_POST_COMPLETION Completion,
    _In_ NTSTATUS Status)
{
    if (InterlockedCompareExchange(&Completion->Completed, 1, 0) != 0) return;

    Completion->Status = Status;
    InterlockedIncrement(&Completion->RefCount);

    if (Completion->SlaveBody != NULL) CmpCancelSiblingPosts(Completion);

    /* TODO: a thread-agnostic answer only signals its event; its status block is left unwritten */
    if (Completion->Thread == NULL)
    {
        ASSERT(Completion->Event != NULL);
        ASSERT(Completion->ApcRoutine == NULL);
        ASSERT(!Completion->Synchronous);

        KeSetEvent(Completion->Event, EVENT_INCREMENT, FALSE);
        CmpReleaseCompletion(Completion);
        return;
    }

    KeInitializeApc(&Completion->Apc,
                    &Completion->Thread->Tcb,
                    OriginalApcEnvironment,
                    CmpPostApcRoutine,
                    NULL,
                    NULL,
                    KernelMode,
                    NULL);

    if (!KeInsertQueueApc(&Completion->Apc, Completion, NULL, IO_NO_INCREMENT))
    {
        if (Completion->Synchronous)
        {
            KeSetEvent(&Completion->Finished, EVENT_INCREMENT, FALSE);
        }

        CmpReleaseCompletion(Completion);
    }
}

/* A waiter that leaves early takes its posts back, unless the answer is already on its way */
static
VOID
CmpAbandonCompletion(
    _In_ PCM_POST_COMPLETION Completion)
{
    if (InterlockedCompareExchange(&Completion->Completed, 1, 0) != 0) return;

    CmpCancelSiblingPosts(Completion);
}

static
VOID
CmpUnlinkPostBlock(
    _In_ PCM_POST_BLOCK Post)
{
    RemoveEntryList(&Post->NotifyList);
    RemoveEntryList(&Post->GlobalList);
}

/* Answers everything still waiting on one notify block */
static
VOID
CmpPostAllOnBlock(
    _In_ PCM_NOTIFY_BLOCK Notify,
    _In_ NTSTATUS Status)
{
    PCM_POST_COMPLETION Completion;
    PCM_POST_BLOCK Post;
    PLIST_ENTRY Entry;

    ExAcquireFastMutex(&CmpPostLock);

    while (!IsListEmpty(&Notify->PostList))
    {
        Entry = Notify->PostList.Flink;
        Post = CONTAINING_RECORD(Entry, CM_POST_BLOCK, NotifyList);
        Completion = Post->Completion;

        CmpUnlinkPostBlock(Post);
        ExFreePool(Post);

        ExReleaseFastMutex(&CmpPostLock);

        CmpDeliverCompletion(Completion, Status);
        CmpReleaseCompletion(Completion);

        ExAcquireFastMutex(&CmpPostLock);
    }

    ExReleaseFastMutex(&CmpPostLock);
}

static
BOOLEAN
CmpKcbIsUnder(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PCM_KEY_CONTROL_BLOCK Root)
{
    PCM_KEY_CONTROL_BLOCK Current;

    for (Current = Kcb; Current != NULL; Current = Current->ParentKcb)
    {
        if (Current == Root) return TRUE;
    }

    return FALSE;
}

static
VOID
CmpFreeNotifyBlock(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ NTSTATUS Status)
{
    PCM_NOTIFY_BLOCK Notify = KeyBody->NotifyBlock;

    if (Notify == NULL) return;

    CmpPostAllOnBlock(Notify, Status);

    ExAcquirePushLockExclusive(&CmpHiveListHeadLock);
    RemoveEntryList(&Notify->HiveList);
    ExReleasePushLockExclusive(&CmpHiveListHeadLock);

    KeyBody->NotifyBlock = NULL;
    KeyBody->NotifyClosed = TRUE;
    ExFreePool(Notify);
}

static
PCM_NOTIFY_BLOCK
CmpCreateNotifyBlock(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ ULONG Filter,
    _In_ BOOLEAN WatchTree)
{
    PCM_KEY_CONTROL_BLOCK Kcb = KeyBody->KeyControlBlock;
    PCM_NOTIFY_BLOCK Notify;
    PCMHIVE CmHive;

    Notify = ExAllocatePoolZero(PagedPool, sizeof(CM_NOTIFY_BLOCK), TAG_CM);
    if (Notify == NULL) return NULL;

    InitializeListHead(&Notify->PostList);
    Notify->KeyControlBlock = Kcb;
    Notify->KeyBody = KeyBody;
    Notify->Filter = Filter & REG_LEGAL_CHANGE_FILTER;
    Notify->WatchTree = WatchTree ? 1 : 0;
    Notify->NotifyPending = 0;

    CmHive = CONTAINING_RECORD(Kcb->KeyHive, CMHIVE, Hive);

    ExAcquirePushLockExclusive(&CmpHiveListHeadLock);
    InsertTailList(&CmHive->NotifyList, &Notify->HiveList);
    ExReleasePushLockExclusive(&CmpHiveListHeadLock);

    KeyBody->NotifyBlock = Notify;
    KeyBody->NotifyClosed = FALSE;
    return Notify;
}

static
NTSTATUS
CmpAttachPostBlock(
    _In_ PCM_NOTIFY_BLOCK Notify,
    _In_ PCM_POST_COMPLETION Completion)
{
    PCM_POST_BLOCK Post;

    Post = ExAllocatePoolZero(NonPagedPool, sizeof(CM_POST_BLOCK), TAG_CM);
    if (Post == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Post->Completion = Completion;
    Post->Notify = Notify;
    InterlockedIncrement(&Completion->RefCount);

    ExAcquireFastMutex(&CmpPostLock);
    InsertTailList(&Notify->PostList, &Post->NotifyList);
    InsertTailList(&CmpPostBlockListHead, &Post->GlobalList);
    ExReleaseFastMutex(&CmpPostLock);

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
CmpInitNotify(VOID)
{
    InitializeListHead(&CmpPostBlockListHead);
    ExInitializeFastMutex(&CmpPostLock);
    CmpNotifyInited = TRUE;
}

/*
 * Registers one caller's interest in a key. The filter and the subtree flag
 * belong to the handle and are taken from the first call only; every later
 * call on the same handle just queues another answer to be given.
 */
NTSTATUS
NTAPI
CmpNotifyChangeKey(
    _In_ PCM_KEY_BODY KeyBody,
    _In_opt_ PCM_KEY_BODY SlaveKeyBody,
    _In_opt_ PKEVENT Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _In_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ BOOLEAN IoStatus32,
    _In_ ULONG Filter,
    _In_ BOOLEAN WatchTree,
    _In_ BOOLEAN Asynchronous,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PCM_POST_COMPLETION Completion;
    PCM_NOTIFY_BLOCK Notify;
    PCM_NOTIFY_BLOCK Slave = NULL;
    NTSTATUS Status;

    if ((Filter & ~(REG_LEGAL_CHANGE_FILTER | REG_NOTIFY_THREAD_AGNOSTIC)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!CmpNotifyInited) return STATUS_INSUFFICIENT_RESOURCES;

    if (KeyBody->KeyControlBlock == NULL) return STATUS_KEY_DELETED;
    if (KeyBody->KeyControlBlock->Delete) return STATUS_KEY_DELETED;

    Notify = KeyBody->NotifyBlock;

    if (Notify == NULL)
    {
        Notify = CmpCreateNotifyBlock(KeyBody, Filter, WatchTree);
        if (Notify == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (SlaveKeyBody != NULL)
    {
        Slave = SlaveKeyBody->NotifyBlock;

        if (Slave == NULL)
        {
            Slave = CmpCreateNotifyBlock(SlaveKeyBody, Filter, WatchTree);
            if (Slave == NULL) return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    Completion = ExAllocatePoolZero(NonPagedPool,
                                    sizeof(CM_POST_COMPLETION),
                                    TAG_CM);
    if (Completion == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Completion->RefCount = 1;
    Completion->Event = Event;
    Completion->ApcRoutine = ApcRoutine;
    Completion->ApcContext = ApcContext;
    Completion->IoStatusBlock = IoStatusBlock;
    Completion->IoStatus32 = IoStatus32;
    Completion->Synchronous = !Asynchronous;
    Completion->Mode = PreviousMode;
    KeInitializeEvent(&Completion->Finished, NotificationEvent, FALSE);

    /* TODO: REG_NOTIFY_THREAD_AGNOSTIC frees only asynchronous event notifications from the thread; the other forms stay tied to it */
    if (!(Filter & REG_NOTIFY_THREAD_AGNOSTIC) || !Asynchronous || (Event == NULL) || (ApcRoutine != NULL))
    {
        Completion->Thread = PsGetCurrentThread();
        ObReferenceObject(Completion->Thread);
    }

    if (SlaveKeyBody != NULL)
    {
        ObReferenceObject(SlaveKeyBody);
        Completion->SlaveBody = SlaveKeyBody;
    }

    if (Notify->NotifyPending != 0)
    {
        Notify->NotifyPending = 0;
        if (Slave != NULL) Slave->NotifyPending = 0;

        CmpDeliverCompletion(Completion, STATUS_NOTIFY_ENUM_DIR);

        if (!Asynchronous)
        {
            Status = KeWaitForSingleObject(&Completion->Finished,
                                           Executive,
                                           PreviousMode,
                                           FALSE,
                                           NULL);
            CmpReleaseCompletion(Completion);
            return Status;
        }

        CmpReleaseCompletion(Completion);
        return STATUS_SUCCESS;
    }

    Status = CmpAttachPostBlock(Notify, Completion);

    if (NT_SUCCESS(Status) && Slave != NULL)
    {
        Status = CmpAttachPostBlock(Slave, Completion);
    }

    if (!NT_SUCCESS(Status))
    {
        CmpPostAllOnBlock(Notify, STATUS_NOTIFY_CLEANUP);
        CmpReleaseCompletion(Completion);
        return Status;
    }

    if (Asynchronous)
    {
        CmpReleaseCompletion(Completion);
        return STATUS_PENDING;
    }

    CmpUnlockRegistry();

    Status = KeWaitForSingleObject(&Completion->Finished,
                                   Executive,
                                   PreviousMode,
                                   FALSE,
                                   NULL);

    CmpLockRegistry();

    if (Status != STATUS_SUCCESS)
    {
        CmpAbandonCompletion(Completion);
        CmpReleaseCompletion(Completion);
        return Status;
    }

    Status = Completion->Status;
    CmpReleaseCompletion(Completion);
    return Status;
}

/*
 * Called from every place that changes a hive. Walks the watchers of that
 * hive and answers the ones whose key, or whose subtree, the change touched.
 */
VOID
NTAPI
CmpReportNotify(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb,
    _In_ PHHIVE Hive,
    _In_ HCELL_INDEX Cell,
    _In_ ULONG Filter)
{
    PCM_NOTIFY_BLOCK Notify;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;
    PCMHIVE CmHive;

    UNREFERENCED_PARAMETER(Cell);

    if (!CmpNotifyInited) return;

    CmHive = CONTAINING_RECORD(Hive, CMHIVE, Hive);

    ExAcquirePushLockShared(&CmpHiveListHeadLock);

    for (Entry = CmHive->NotifyList.Flink;
         Entry != &CmHive->NotifyList;
         Entry = Next)
    {
        Next = Entry->Flink;
        Notify = CONTAINING_RECORD(Entry, CM_NOTIFY_BLOCK, HiveList);

        if ((Notify->Filter & Filter) == 0) continue;
        if (Notify->KeyControlBlock == NULL) continue;

        if (Notify->KeyControlBlock != Kcb)
        {
            if (Notify->WatchTree == 0) continue;
            if (!CmpKcbIsUnder(Kcb, Notify->KeyControlBlock)) continue;
        }

        if (IsListEmpty(&Notify->PostList))
        {
            Notify->NotifyPending = 1;
            continue;
        }

        CmpPostAllOnBlock(Notify, STATUS_NOTIFY_ENUM_DIR);
    }

    ExReleasePushLockShared(&CmpHiveListHeadLock);
}

VOID
NTAPI
CmpFlushNotify(
    _In_ PCM_KEY_BODY KeyBody,
    _In_ BOOLEAN LockHeld)
{
    UNREFERENCED_PARAMETER(LockHeld);

    CmpFreeNotifyBlock(KeyBody, STATUS_NOTIFY_CLEANUP);
}

VOID
NTAPI
CmpCloseNotify(
    _In_ PCM_KEY_BODY KeyBody)
{
    CmpFreeNotifyBlock(KeyBody, STATUS_NOTIFY_CLEANUP);
}

VOID
NTAPI
CmpFlushNotifyOnKcb(
    _In_ PCM_KEY_CONTROL_BLOCK Kcb)
{
    CmpFlushNotifiesOnKeyBodyList(Kcb, TRUE);
}

/*
 * A thread that goes away must not leave an answer owed to a stack it no
 * longer owns, so everything it asked for is cancelled here.
 */
VOID
NTAPI
CmpFlushNotifyThread(
    _In_ PETHREAD Thread)
{
    PCM_POST_COMPLETION Completion;
    PCM_POST_BLOCK Post;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;

    if (!CmpNotifyInited) return;

    for (;;)
    {
        Completion = NULL;

        ExAcquireFastMutex(&CmpPostLock);

        for (Entry = CmpPostBlockListHead.Flink;
             Entry != &CmpPostBlockListHead;
             Entry = Next)
        {
            Next = Entry->Flink;
            Post = CONTAINING_RECORD(Entry, CM_POST_BLOCK, GlobalList);

            if (Post->Completion->Thread != Thread) continue;

            Completion = Post->Completion;
            CmpUnlinkPostBlock(Post);
            ExFreePool(Post);
            break;
        }

        ExReleaseFastMutex(&CmpPostLock);

        if (Completion == NULL) return;

        CmpDeliverCompletion(Completion, STATUS_NOTIFY_CLEANUP);
        CmpReleaseCompletion(Completion);
    }
}

VOID
NTAPI
CmpPostNotifyBlock(
    _In_ PCM_NOTIFY_BLOCK NotifyBlock,
    _In_ NTSTATUS Status)
{
    CmpPostAllOnBlock(NotifyBlock, Status);
}
