/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/config/cmnotify.c
 * PURPOSE:         Configuration Manager - Registry Change Notifications
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include "ntoskrnl.h"
#define NDEBUG
#include "debug.h"

/* FUNCTIONS *****************************************************************/

#define TAG_CM_NOTIFY 'nNmC'
#define TAG_CM_POST   'pNmC'

/* All notification and thread post lists are protected by this mutex. Registry
 * and KCB locks, when needed, must be acquired before the notification mutex. */
static FAST_MUTEX CmpNotifyMutex;
static LIST_ENTRY CmpNotifyList;
/* ETHREAD::PostBlockList overlaps StartAddress in the NT10 layout. Keep
 * notification ownership here so registering a watch cannot overwrite it. */
static LIST_ENTRY CmpNotifyThreadList = { &CmpNotifyThreadList, &CmpNotifyThreadList };

typedef struct _CMP_NOTIFY_POST
{
    LIST_ENTRY KeyList;
    LIST_ENTRY ThreadList;
    LIST_ENTRY SlaveList;
    KEVENT WakeEvent;
    KAPC Apc;
    PKEVENT Event;
    PETHREAD Thread;
    PCM_NOTIFY_BLOCK SlaveNotify;
    PCM_KEY_BODY SlaveKeyBody;
    PIO_STATUS_BLOCK IoStatusBlock;
    PIO_APC_ROUTINE ApcRoutine;
    PVOID ApcContext;
    NTSTATUS Status;
    BOOLEAN Asynchronous;
    BOOLEAN IoStatus32;
} CMP_NOTIFY_POST, *PCMP_NOTIFY_POST;

CODE_SEG("INIT")
VOID
NTAPI
CmpInitNotify(VOID)
{
    ExInitializeFastMutex(&CmpNotifyMutex);
    InitializeListHead(&CmpNotifyList);
}

static VOID
CmpFreeNotifyPost(PCMP_NOTIFY_POST Post)
{
    if (Post->Event) ObDereferenceObjectDeferDelete(Post->Event);
    if (Post->Thread) ObDereferenceObjectDeferDelete(Post->Thread);
    ExFreePoolWithTag(Post, TAG_CM_POST);
}

static VOID
NTAPI
CmpNotifyApcRundown(PKAPC Apc)
{
    CmpFreeNotifyPost(CONTAINING_RECORD(Apc, CMP_NOTIFY_POST, Apc));
}

static VOID
NTAPI
CmpNotifyUserApcKernelRoutine(PKAPC Apc,
                              PKNORMAL_ROUTINE *NormalRoutine,
                              PVOID *NormalContext,
                              PVOID *SystemArgument1,
                              PVOID *SystemArgument2)
{
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    CmpFreeNotifyPost(CONTAINING_RECORD(Apc, CMP_NOTIFY_POST, Apc));
}

static VOID
NTAPI
CmpNotifyCompletionRoutine(PKAPC Apc,
                           PKNORMAL_ROUTINE *NormalRoutine,
                           PVOID *NormalContext,
                           PVOID *SystemArgument1,
                           PVOID *SystemArgument2)
{
    PCMP_NOTIFY_POST Post = CONTAINING_RECORD(Apc, CMP_NOTIFY_POST, Apc);
    IO_STATUS_BLOCK IoStatus;

    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Post->IoStatusBlock)
    {
        IoStatus.Status = Post->Status;
        IoStatus.Information = 0;
        _SEH2_TRY
        {
            IopWriteIoStatusBlock(Post->IoStatusBlock, &IoStatus, Post->IoStatus32);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }
    if (Post->Event) KeSetEvent(Post->Event, IO_NO_INCREMENT, FALSE);
    if (Post->ApcRoutine)
    {
        KeInitializeApc(&Post->Apc,
                        &Post->Thread->Tcb,
                        OriginalApcEnvironment,
                        CmpNotifyUserApcKernelRoutine,
                        CmpNotifyApcRundown,
                        (PKNORMAL_ROUTINE)Post->ApcRoutine,
                        UserMode,
                        Post->ApcContext);
        if (KeInsertQueueApc(&Post->Apc, Post->IoStatusBlock, NULL, IO_NO_INCREMENT))
            return;
    }
    CmpFreeNotifyPost(Post);
}

/* The caller holds CmpNotifyMutex. Synchronous posts belong to the waiting
 * system call; asynchronous posts complete in their requesting thread. */
static VOID
CmpCompleteNotify(PCMP_NOTIFY_POST Post, NTSTATUS Status)
{
    if (!IsListEmpty(&Post->KeyList))
    {
        RemoveEntryList(&Post->KeyList);
        InitializeListHead(&Post->KeyList);
    }
    if (!IsListEmpty(&Post->ThreadList))
    {
        RemoveEntryList(&Post->ThreadList);
        InitializeListHead(&Post->ThreadList);
    }
    if (Post->SlaveNotify)
    {
        RemoveEntryList(&Post->SlaveNotify->HiveList);
        ExFreePoolWithTag(Post->SlaveNotify, TAG_CM_NOTIFY);
        Post->SlaveNotify = NULL;
        InitializeListHead(&Post->SlaveList);
    }
    if (Post->SlaveKeyBody)
    {
        ObDereferenceObjectDeferDelete(Post->SlaveKeyBody);
        Post->SlaveKeyBody = NULL;
    }
    Post->Status = Status;
    if (Post->Asynchronous)
    {
        KeInitializeApc(&Post->Apc,
                        &Post->Thread->Tcb,
                        OriginalApcEnvironment,
                        CmpNotifyCompletionRoutine,
                        NULL,
                        NULL,
                        KernelMode,
                        NULL);
        if (!KeInsertQueueApc(&Post->Apc, NULL, NULL, IO_NO_INCREMENT))
        {
            if (Post->Event) KeSetEvent(Post->Event, IO_NO_INCREMENT, FALSE);
            CmpFreeNotifyPost(Post);
        }
    }
    else
    {
        KeSetEvent(&Post->WakeEvent, IO_NO_INCREMENT, FALSE);
    }
}

static VOID
CmpFlushNotifyLocked(PCM_KEY_BODY KeyBody)
{
    PCM_NOTIFY_BLOCK Notify = KeyBody->NotifyBlock;
    PCMP_NOTIFY_POST Post;

    if (!Notify) return;
    while (!IsListEmpty(&Notify->PostList))
    {
        Post = CONTAINING_RECORD(Notify->PostList.Flink, CMP_NOTIFY_POST, KeyList);
        CmpCompleteNotify(Post, STATUS_NOTIFY_CLEANUP);
    }
    RemoveEntryList(&Notify->HiveList);
    KeyBody->NotifyBlock = NULL;
    ExFreePoolWithTag(Notify, TAG_CM_NOTIFY);
}

VOID
NTAPI
CmpCloseNotify(PCM_KEY_BODY KeyBody)
{
    ExAcquireFastMutex(&CmpNotifyMutex);
    /* Remember the last handle close even if registration has not yet linked
     * its post. Referencing a key body does not keep its handles open. */
    KeyBody->NotifyClosed = TRUE;
    CmpFlushNotifyLocked(KeyBody);
    ExReleaseFastMutex(&CmpNotifyMutex);
}

VOID
NTAPI
CmpFlushNotifyThread(PETHREAD Thread)
{
    PCMP_NOTIFY_POST Post;
    PLIST_ENTRY Entry, Next;

    /* The statically initialized list is also safe before CM initialization. */
    if (IsListEmpty(&CmpNotifyThreadList)) return;
    ExAcquireFastMutex(&CmpNotifyMutex);
    for (Entry = CmpNotifyThreadList.Flink; Entry != &CmpNotifyThreadList; Entry = Next)
    {
        Next = Entry->Flink;
        Post = CONTAINING_RECORD(Entry, CMP_NOTIFY_POST, ThreadList);
        if (Post->Thread == Thread)
            CmpCompleteNotify(Post, STATUS_NOTIFY_CLEANUP);
    }
    ExReleaseFastMutex(&CmpNotifyMutex);
}

NTSTATUS
NTAPI
CmpNotifyChangeKey(PCM_KEY_BODY KeyBody,
                   PCM_KEY_BODY SlaveKeyBody,
                   PKEVENT Event,
                   PIO_APC_ROUTINE ApcRoutine,
                   PVOID ApcContext,
                   PIO_STATUS_BLOCK IoStatusBlock,
                   BOOLEAN IoStatus32,
                   ULONG Filter,
                   BOOLEAN WatchTree,
                   BOOLEAN Asynchronous,
                   KPROCESSOR_MODE PreviousMode)
{
    PCM_KEY_CONTROL_BLOCK Kcb = KeyBody->KeyControlBlock;
    PCM_NOTIFY_BLOCK Notify, NewNotify, SlaveNotify = NULL;
    PCMP_NOTIFY_POST Post;
    NTSTATUS Status;

    PAGED_CODE();

    Post = ExAllocatePoolZero(NonPagedPool, sizeof(*Post), TAG_CM_POST);
    if (!Post) return STATUS_INSUFFICIENT_RESOURCES;
    NewNotify = ExAllocatePoolWithTag(PagedPool, sizeof(*NewNotify), TAG_CM_NOTIFY);
    if (SlaveKeyBody)
        SlaveNotify = ExAllocatePoolWithTag(PagedPool, sizeof(*SlaveNotify), TAG_CM_NOTIFY);
    if (!NewNotify || (SlaveKeyBody && !SlaveNotify))
    {
        if (NewNotify) ExFreePoolWithTag(NewNotify, TAG_CM_NOTIFY);
        if (SlaveNotify) ExFreePoolWithTag(SlaveNotify, TAG_CM_NOTIFY);
        ExFreePoolWithTag(Post, TAG_CM_POST);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    InitializeListHead(&Post->KeyList);
    InitializeListHead(&Post->ThreadList);
    InitializeListHead(&Post->SlaveList);
    KeInitializeEvent(&Post->WakeEvent, NotificationEvent, FALSE);
    Post->Thread = PsGetCurrentThread();
    Post->Status = STATUS_PENDING;
    Post->Asynchronous = Asynchronous;
    if (Asynchronous)
    {
        Post->IoStatusBlock = IoStatusBlock;
        Post->IoStatus32 = IoStatus32;
        Post->ApcRoutine = ApcRoutine;
        Post->ApcContext = ApcContext;
    }

    CmpLockRegistry();
    CmpAcquireKcbLockShared(Kcb);
    ExAcquireFastMutex(&CmpNotifyMutex);
    if (Kcb->Delete || (SlaveKeyBody && SlaveKeyBody->KeyControlBlock->Delete))
    {
        Status = STATUS_KEY_DELETED;
        goto Unlock;
    }
    if (KeyBody->NotifyClosed)
    {
        Status = STATUS_NOTIFY_CLEANUP;
        goto Unlock;
    }

    Notify = KeyBody->NotifyBlock;
    if (!Notify)
    {
        Notify = NewNotify;
        NewNotify = NULL;
        InitializeListHead(&Notify->PostList);
        Notify->KeyControlBlock = Kcb;
        Notify->KeyBody = KeyBody;
        Notify->Filter = Filter & REG_LEGAL_CHANGE_FILTER;
        Notify->WatchTree = !!WatchTree;
        Notify->NotifyPending = FALSE;
        InsertTailList(&CmpNotifyList, &Notify->HiveList);
        KeyBody->NotifyBlock = Notify;
    }
    if (Asynchronous)
    {
        ObReferenceObject(Post->Thread);
        if (Event)
        {
            ObReferenceObject(Event);
            Post->Event = Event;
            KeClearEvent(Event);
        }
    }
    if (SlaveKeyBody)
    {
        InitializeListHead(&SlaveNotify->PostList);
        SlaveNotify->KeyControlBlock = SlaveKeyBody->KeyControlBlock;
        SlaveNotify->KeyBody = NULL;
        SlaveNotify->Filter = Filter & REG_LEGAL_CHANGE_FILTER;
        SlaveNotify->WatchTree = !!WatchTree;
        SlaveNotify->NotifyPending = FALSE;
        InsertTailList(&CmpNotifyList, &SlaveNotify->HiveList);
        InsertTailList(&SlaveNotify->PostList, &Post->SlaveList);
        ObReferenceObject(SlaveKeyBody);
        Post->SlaveNotify = SlaveNotify;
        Post->SlaveKeyBody = SlaveKeyBody;
        SlaveNotify = NULL;
    }
    InsertTailList(&Notify->PostList, &Post->KeyList);
    if (!(Filter & REG_NOTIFY_THREAD_AGNOSTIC))
        InsertTailList(&CmpNotifyThreadList, &Post->ThreadList);
    Status = STATUS_PENDING;

Unlock:
    ExReleaseFastMutex(&CmpNotifyMutex);
    CmpReleaseKcbLock(Kcb);
    CmpUnlockRegistry();
    if (NewNotify) ExFreePoolWithTag(NewNotify, TAG_CM_NOTIFY);
    if (SlaveNotify) ExFreePoolWithTag(SlaveNotify, TAG_CM_NOTIFY);
    if (Status != STATUS_PENDING)
    {
        ExFreePoolWithTag(Post, TAG_CM_POST);
        return Status;
    }
    if (Asynchronous) return STATUS_PENDING;

    /* Never wait while holding registry or KCB locks: the writer needs them. */
    Status = KeWaitForSingleObject(&Post->WakeEvent, Executive, PreviousMode, TRUE, NULL);
    ExAcquireFastMutex(&CmpNotifyMutex);
    if (Post->Status != STATUS_PENDING)
        Status = Post->Status;
    else
        CmpCompleteNotify(Post, Status);
    ExReleaseFastMutex(&CmpNotifyMutex);
    ExFreePoolWithTag(Post, TAG_CM_POST);
    return Status;
}

VOID
NTAPI
CmpReportNotify(IN PCM_KEY_CONTROL_BLOCK Kcb,
                IN PHHIVE Hive,
                IN HCELL_INDEX Cell,
                IN ULONG Filter)
{
    PLIST_ENTRY Entry;
    PCM_NOTIFY_BLOCK Notify;
    PCM_KEY_CONTROL_BLOCK Changed;
    PCMP_NOTIFY_POST Post;

    UNREFERENCED_PARAMETER(Hive);
    UNREFERENCED_PARAMETER(Cell);

    /* A key name change changes the contents of its parent. Value changes
     * belong to the key itself. Parent KCBs also span mounted hive roots. */
    if (Filter & REG_NOTIFY_CHANGE_NAME) Kcb = Kcb->ParentKcb;
    if (!Kcb) return;

    ExAcquireFastMutex(&CmpNotifyMutex);
Restart:
    for (Entry = CmpNotifyList.Flink; Entry != &CmpNotifyList; Entry = Entry->Flink)
    {
        Notify = CONTAINING_RECORD(Entry, CM_NOTIFY_BLOCK, HiveList);
        if (IsListEmpty(&Notify->PostList)) continue;
        if (!(Notify->Filter & Filter)) continue;
        Changed = Kcb;
        if (Notify->WatchTree)
        {
            while (Changed && Changed != Notify->KeyControlBlock)
                Changed = Changed->ParentKcb;
        }
        if (Changed != Notify->KeyControlBlock) continue;
        if (!Notify->KeyBody)
        {
            Post = CONTAINING_RECORD(Notify->PostList.Flink, CMP_NOTIFY_POST, SlaveList);
            CmpCompleteNotify(Post, STATUS_NOTIFY_ENUM_DIR);
            goto Restart;
        }
        while (!IsListEmpty(&Notify->PostList))
        {
            Post = CONTAINING_RECORD(Notify->PostList.Flink, CMP_NOTIFY_POST, KeyList);
            CmpCompleteNotify(Post, STATUS_NOTIFY_ENUM_DIR);
        }
        goto Restart;
    }
    ExReleaseFastMutex(&CmpNotifyMutex);
}

VOID
NTAPI
CmpFlushNotify(IN PCM_KEY_BODY KeyBody,
               IN BOOLEAN LockHeld)
{
    UNREFERENCED_PARAMETER(LockHeld);

    ExAcquireFastMutex(&CmpNotifyMutex);
    CmpFlushNotifyLocked(KeyBody);
    ExReleaseFastMutex(&CmpNotifyMutex);
}

VOID
NTAPI
CmpFlushNotifyOnKcb(IN PCM_KEY_CONTROL_BLOCK Kcb)
{
    PLIST_ENTRY Entry;
    PCM_NOTIFY_BLOCK Notify;

    ExAcquireFastMutex(&CmpNotifyMutex);
Restart:
    for (Entry = CmpNotifyList.Flink; Entry != &CmpNotifyList; Entry = Entry->Flink)
    {
        Notify = CONTAINING_RECORD(Entry, CM_NOTIFY_BLOCK, HiveList);
        if (Notify->KeyControlBlock != Kcb) continue;
        if (Notify->KeyBody)
            CmpFlushNotifyLocked(Notify->KeyBody);
        else
            CmpCompleteNotify(CONTAINING_RECORD(Notify->PostList.Flink, CMP_NOTIFY_POST, SlaveList),
                              STATUS_NOTIFY_CLEANUP);
        goto Restart;
    }
    ExReleaseFastMutex(&CmpNotifyMutex);
}
