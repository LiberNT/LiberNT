/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Letting drivers watch, block and answer registry calls themselves
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* GLOBALS ********************************************************************/

ULONG CmpCallBackCount = 0;
LIST_ENTRY CmpContextListHead;

/* PRIVATE DATA ***************************************************************/

static LIST_ENTRY CmpCallbackListHead;
static EX_PUSH_LOCK CmpCallbackListLock;
static EX_PUSH_LOCK CmpContextListLock;
static ULONGLONG CmpCallbackCookie = 0;
static BOOLEAN CmpCallbackInited = FALSE;

static LIST_ENTRY CmpMachineHiveNotifyListHead;
static EX_PUSH_LOCK CmpMachineHiveNotifyLock;

/* PRIVATE FUNCTIONS **********************************************************/

static
PCM_CALLBACK_ENTRY
CmpFindCallbackByCookie(
    _In_ ULONGLONG Cookie)
{
    PCM_CALLBACK_ENTRY Entry;
    PLIST_ENTRY ListEntry;

    for (ListEntry = CmpCallbackListHead.Flink;
         ListEntry != &CmpCallbackListHead;
         ListEntry = ListEntry->Flink)
    {
        Entry = CONTAINING_RECORD(ListEntry, CM_CALLBACK_ENTRY, ListEntry);
        if (Entry->Cookie == Cookie) return Entry;
    }

    return NULL;
}

/*
 * The filter whose callback the thread is running. The registry work a filter
 * does for itself is told to the filters below it, never to itself or to one
 * above it, so a filter cannot be called about its own work.
 */
#define CMP_CALLBACK_SCOPE_MAX 16

typedef struct _CMP_CALLBACK_SCOPE
{
    PETHREAD Thread;
    PCUNICODE_STRING Altitude;
} CMP_CALLBACK_SCOPE;

static CMP_CALLBACK_SCOPE CmpCallbackScopes[CMP_CALLBACK_SCOPE_MAX];
static KSPIN_LOCK CmpCallbackScopeLock;

static
PCUNICODE_STRING
CmpQueryCallbackScope(VOID)
{
    PETHREAD Thread = PsGetCurrentThread();
    PCUNICODE_STRING Altitude = NULL;
    KIRQL OldIrql;
    ULONG Index;

    KeAcquireSpinLock(&CmpCallbackScopeLock, &OldIrql);

    for (Index = 0; Index < CMP_CALLBACK_SCOPE_MAX; Index++)
    {
        if (CmpCallbackScopes[Index].Thread == Thread)
        {
            Altitude = CmpCallbackScopes[Index].Altitude;
            break;
        }
    }

    KeReleaseSpinLock(&CmpCallbackScopeLock, OldIrql);

    return Altitude;
}

static
PCUNICODE_STRING
CmpSetCallbackScope(
    _In_opt_ PCUNICODE_STRING Altitude)
{
    PETHREAD Thread = PsGetCurrentThread();
    PCUNICODE_STRING Previous = NULL;
    KIRQL OldIrql;
    ULONG Index;
    ULONG Free = CMP_CALLBACK_SCOPE_MAX;

    KeAcquireSpinLock(&CmpCallbackScopeLock, &OldIrql);

    for (Index = 0; Index < CMP_CALLBACK_SCOPE_MAX; Index++)
    {
        if (CmpCallbackScopes[Index].Thread == Thread)
        {
            Previous = CmpCallbackScopes[Index].Altitude;

            CmpCallbackScopes[Index].Altitude = Altitude;
            if (Altitude == NULL) CmpCallbackScopes[Index].Thread = NULL;

            KeReleaseSpinLock(&CmpCallbackScopeLock, OldIrql);
            return Previous;
        }

        if (CmpCallbackScopes[Index].Thread == NULL &&
            Free == CMP_CALLBACK_SCOPE_MAX)
        {
            Free = Index;
        }
    }

    /* TODO: past this many threads at once the inner work is told to every filter */
    if (Altitude != NULL && Free != CMP_CALLBACK_SCOPE_MAX)
    {
        CmpCallbackScopes[Free].Thread = Thread;
        CmpCallbackScopes[Free].Altitude = Altitude;
    }

    KeReleaseSpinLock(&CmpCallbackScopeLock, OldIrql);

    return NULL;
}

/* Highest altitude first, so the outermost filter sees a call first; no two filters share one */
static
NTSTATUS
CmpInsertCallbackInListByAltitude(
    _In_ PCM_CALLBACK_ENTRY New)
{
    PCM_CALLBACK_ENTRY Entry;
    PLIST_ENTRY ListEntry;
    LONG Order;

    for (ListEntry = CmpCallbackListHead.Flink;
         ListEntry != &CmpCallbackListHead;
         ListEntry = ListEntry->Flink)
    {
        Entry = CONTAINING_RECORD(ListEntry, CM_CALLBACK_ENTRY, ListEntry);

        Order = RtlCompareUnicodeString(&New->Altitude, &Entry->Altitude, FALSE);

        if (Order == 0 && !Entry->Legacy)
        {
            return STATUS_FLT_INSTANCE_ALTITUDE_COLLISION;
        }

        if (Order > 0)
        {
            InsertTailList(ListEntry, &New->ListEntry);
            return STATUS_SUCCESS;
        }
    }

    InsertTailList(&CmpCallbackListHead, &New->ListEntry);
    return STATUS_SUCCESS;
}

static
PVOID
CmpGetCallbackObjectContext(
    _In_opt_ PCM_KEY_BODY KeyBody,
    _In_ ULONGLONG Cookie)
{
    PCM_CALLBACK_OBJECT_CONTEXT Context;
    PLIST_ENTRY Entry;
    PVOID Value = NULL;

    if (KeyBody == NULL) return NULL;

    ExAcquirePushLockShared(&CmpContextListLock);

    for (Entry = KeyBody->ContextListHead.Flink;
         Entry != &KeyBody->ContextListHead;
         Entry = Entry->Flink)
    {
        Context = CONTAINING_RECORD(Entry,
                                    CM_CALLBACK_OBJECT_CONTEXT,
                                    ListEntry);
        if (Context->Cookie != Cookie) continue;

        Value = Context->Context;
        break;
    }

    ExReleasePushLockShared(&CmpContextListLock);
    return Value;
}

/*
 * The notes a filter left on keys that are still open go with the filter: it
 * is told about each one so it can let go of whatever the note stands for.
 */
static
VOID
CmpCleanupFilterContexts(
    _In_ PCM_CALLBACK_ENTRY Entry)
{
    REG_CALLBACK_CONTEXT_CLEANUP_INFORMATION Cleanup;
    PCM_CALLBACK_OBJECT_CONTEXT Context;
    PLIST_ENTRY ListEntry;

    for (;;)
    {
        Context = NULL;

        ExAcquirePushLockExclusive(&CmpContextListLock);

        for (ListEntry = CmpContextListHead.Flink;
             ListEntry != &CmpContextListHead;
             ListEntry = ListEntry->Flink)
        {
            Context = CONTAINING_RECORD(ListEntry,
                                        CM_CALLBACK_OBJECT_CONTEXT,
                                        FilterListEntry);

            if (Context->Cookie == Entry->Cookie) break;

            Context = NULL;
        }

        if (Context != NULL)
        {
            RemoveEntryList(&Context->FilterListEntry);
            RemoveEntryList(&Context->ListEntry);
        }

        ExReleasePushLockExclusive(&CmpContextListLock);

        if (Context == NULL) return;

        RtlZeroMemory(&Cleanup, sizeof(Cleanup));
        Cleanup.Object = Context->KeyBody;
        Cleanup.ObjectContext = Context->Context;

        Entry->Function(Entry->Context,
                        (PVOID)(ULONG_PTR)RegNtCallbackObjectContextCleanup,
                        &Cleanup);

        ExFreePool(Context);
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
CmpInitCallback(VOID)
{
    InitializeListHead(&CmpCallbackListHead);
    InitializeListHead(&CmpContextListHead);
    InitializeListHead(&CmpMachineHiveNotifyListHead);
    KeInitializeSpinLock(&CmpCallbackScopeLock);
    ExInitializePushLock(&CmpCallbackListLock);
    ExInitializePushLock(&CmpContextListLock);
    ExInitializePushLock(&CmpMachineHiveNotifyLock);
    CmpCallBackCount = 0;
    CmpCallbackInited = TRUE;
}

/*
 * Runs the pre-notification of one registry call down the filter stack and
 * says whether the call may go ahead. A filter that fails the call, or that
 * says it handled the call itself, stops the walk and the lower filters never
 * see it. Either way the caller must still call CmpPostCallbacks.
 */
BOOLEAN
NTAPI
CmpPreCallbacks(
    _Out_ PCMP_CALLBACK_CALL Call,
    _In_ REG_NOTIFY_CLASS PreClass,
    _In_ REG_NOTIFY_CLASS PostClass,
    _In_ PVOID PreInformation,
    _In_opt_ PVOID *CallContext,
    _In_opt_ PVOID *ObjectContext,
    _In_opt_ PCM_KEY_BODY KeyBody,
    _In_opt_ PVOID *ResultObject,
    _Out_ PNTSTATUS Status)
{
    PCUNICODE_STRING Scope;
    PCUNICODE_STRING Saved;
    PCM_CALLBACK_ENTRY Entry;
    PLIST_ENTRY ListEntry;
    PCMP_CALLBACK_ITEM Items;
    NTSTATUS Result;
    ULONG Count = 0;
    ULONG Total = 0;
    ULONG i;

    Call->Count = 0;
    Call->PostCount = 0;
    Call->PostClass = PostClass;
    Call->PreInformation = PreInformation;
    Call->CallContext = CallContext;
    Call->ObjectContext = ObjectContext;
    Call->KeyBody = KeyBody;
    Call->ResultObject = ResultObject;
    Call->Items = Call->InlineItems;

    *Status = STATUS_SUCCESS;

    if (!CmpCallbackInited || CmpCallBackCount == 0) return TRUE;

    Scope = CmpQueryCallbackScope();

    ExAcquirePushLockShared(&CmpCallbackListLock);

    Total = CmpCallBackCount;

    if (Total > RTL_NUMBER_OF(Call->InlineItems))
    {
        Items = ExAllocatePoolZero(NonPagedPool,
                                   Total * sizeof(CMP_CALLBACK_ITEM),
                                   TAG_CM);
        if (Items != NULL) Call->Items = Items;
        else Total = RTL_NUMBER_OF(Call->InlineItems);
    }

    for (ListEntry = CmpCallbackListHead.Flink;
         ListEntry != &CmpCallbackListHead && Count < Total;
         ListEntry = ListEntry->Flink)
    {
        Entry = CONTAINING_RECORD(ListEntry, CM_CALLBACK_ENTRY, ListEntry);

        if (Scope != NULL &&
            RtlCompareUnicodeString(&Entry->Altitude, Scope, FALSE) >= 0)
        {
            continue;
        }

        if (!ExAcquireRundownProtection(&Entry->Rundown)) continue;

        Call->Items[Count].Entry = Entry;
        Call->Items[Count].CallContext = NULL;
        Count++;
    }

    ExReleasePushLockShared(&CmpCallbackListLock);

    Call->Count = Count;

    for (i = 0; i < Count; i++)
    {
        Entry = Call->Items[i].Entry;

        if (CallContext != NULL) *CallContext = Call->Items[i].CallContext;
        if (ObjectContext != NULL)
        {
            *ObjectContext = CmpGetCallbackObjectContext(KeyBody,
                                                         Entry->Cookie);
        }

        Saved = CmpSetCallbackScope(&Entry->Altitude);

        Result = Entry->Function(Entry->Context,
                                 (PVOID)(ULONG_PTR)PreClass,
                                 PreInformation);

        CmpSetCallbackScope(Saved);

        if (CallContext != NULL) Call->Items[i].CallContext = *CallContext;

        /*
         * A filter that answers the call itself, or refuses it, is not told
         * the result of what it did not let happen; the filters above it are.
         */
        if (Result == STATUS_CALLBACK_BYPASS)
        {
            *Status = STATUS_SUCCESS;
            Call->PostCount = i;
            return FALSE;
        }

        if (!NT_SUCCESS(Result))
        {
            *Status = Result;
            Call->PostCount = i;
            return FALSE;
        }
    }

    Call->PostCount = Count;
    return TRUE;
}

/*
 * Runs the post-notification back up the stack, so a filter sees the result
 * of what it let through and may change what the caller is told.
 */
NTSTATUS
NTAPI
CmpPostCallbacks(
    _Inout_ PCMP_CALLBACK_CALL Call,
    _In_ NTSTATUS Status)
{
    REG_POST_OPERATION_INFORMATION Post;
    PCM_CALLBACK_ENTRY Entry;
    NTSTATUS Final = Status;
    NTSTATUS Result;
    ULONG i;

    if (Call->CallContext != NULL) *Call->CallContext = NULL;

    if (Call->PostClass != MaxRegNtNotifyClass && Call->PostCount != 0)
    {
        RtlZeroMemory(&Post, sizeof(Post));

        Post.Object = Call->KeyBody;
        Post.Status = Status;
        Post.PreInformation = Call->PreInformation;
        Post.ReturnStatus = Status;

        if (Call->ResultObject != NULL && *Call->ResultObject != NULL)
        {
            Post.Object = *Call->ResultObject;
        }

        for (i = Call->PostCount; i > 0; i--)
        {
            PCUNICODE_STRING Saved;

            Entry = Call->Items[i - 1].Entry;

            Post.CallContext = Call->Items[i - 1].CallContext;
            Post.ObjectContext = CmpGetCallbackObjectContext(Call->KeyBody,
                                                             Entry->Cookie);
            Post.ReturnStatus = Final;
            Post.Status = Final;

            Saved = CmpSetCallbackScope(&Entry->Altitude);

            Result = Entry->Function(Entry->Context,
                                     (PVOID)(ULONG_PTR)Call->PostClass,
                                     &Post);

            CmpSetCallbackScope(Saved);

            if (Result == STATUS_CALLBACK_BYPASS) Final = Post.ReturnStatus;
            else if (!NT_SUCCESS(Result)) Final = Result;
        }
    }

    for (i = 0; i < Call->Count; i++)
    {
        ExReleaseRundownProtection(&Call->Items[i].Entry->Rundown);
    }

    if (Call->Items != Call->InlineItems) ExFreePool(Call->Items);

    Call->Count = 0;
    Call->Items = Call->InlineItems;
    return Final;
}

NTSTATUS
NTAPI
CmRegisterCallbackEx(
    _In_ PEX_CALLBACK_FUNCTION Function,
    _In_ PCUNICODE_STRING Altitude,
    _In_ PVOID Driver,
    _In_opt_ PVOID Context,
    _Out_ PLARGE_INTEGER Cookie,
    _Reserved_ PVOID Reserved)
{
    PCM_CALLBACK_ENTRY Entry;
    NTSTATUS Status;
    PWCHAR Buffer;

    UNREFERENCED_PARAMETER(Reserved);

    PAGED_CODE();

    if (Function == NULL || Altitude == NULL || Cookie == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!CmpCallbackInited) return STATUS_INSUFFICIENT_RESOURCES;

    Entry = ExAllocatePoolZero(NonPagedPool,
                               sizeof(CM_CALLBACK_ENTRY),
                               TAG_CM);
    if (Entry == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Buffer = ExAllocatePoolZero(NonPagedPool, Altitude->Length, TAG_CM);
    if (Buffer == NULL && Altitude->Length != 0)
    {
        ExFreePool(Entry);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Altitude->Length != 0)
    {
        RtlCopyMemory(Buffer, Altitude->Buffer, Altitude->Length);
    }

    Entry->Altitude.Buffer = Buffer;
    Entry->Altitude.Length = Altitude->Length;
    Entry->Altitude.MaximumLength = Altitude->Length;
    Entry->Function = Function;
    Entry->Context = Context;
    Entry->Driver = Driver;
    Entry->Legacy = FALSE;
    ExInitializeRundownProtection(&Entry->Rundown);

    ExAcquirePushLockExclusive(&CmpCallbackListLock);

    Entry->Cookie = ++CmpCallbackCookie;

    Status = CmpInsertCallbackInListByAltitude(Entry);
    if (NT_SUCCESS(Status)) CmpCallBackCount++;

    ExReleasePushLockExclusive(&CmpCallbackListLock);

    if (!NT_SUCCESS(Status))
    {
        if (Entry->Altitude.Buffer != NULL) ExFreePool(Entry->Altitude.Buffer);
        ExFreePool(Entry);
        return Status;
    }

    Cookie->QuadPart = (LONGLONG)Entry->Cookie;
    return STATUS_SUCCESS;
}

/*
 * The call a driver written before altitudes existed makes. Such a driver
 * sits near the top of the stack, above every driver that named an altitude.
 */
NTSTATUS
NTAPI
CmRegisterCallback(
    _In_ PEX_CALLBACK_FUNCTION Function,
    _In_opt_ PVOID Context,
    _Out_ PLARGE_INTEGER Cookie)
{
    UNICODE_STRING Altitude = RTL_CONSTANT_STRING(L"999999");
    NTSTATUS Status;
    LARGE_INTEGER Local;
    PCM_CALLBACK_ENTRY Entry;

    Status = CmRegisterCallbackEx(Function,
                                  &Altitude,
                                  NULL,
                                  Context,
                                  &Local,
                                  NULL);
    if (!NT_SUCCESS(Status)) return Status;

    ExAcquirePushLockExclusive(&CmpCallbackListLock);
    Entry = CmpFindCallbackByCookie((ULONGLONG)Local.QuadPart);
    if (Entry != NULL) Entry->Legacy = TRUE;
    ExReleasePushLockExclusive(&CmpCallbackListLock);

    *Cookie = Local;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CmUnRegisterCallback(
    _In_ LARGE_INTEGER Cookie)
{
    PCM_CALLBACK_ENTRY Entry;

    PAGED_CODE();

    if (!CmpCallbackInited) return STATUS_INVALID_PARAMETER;

    ExAcquirePushLockExclusive(&CmpCallbackListLock);

    Entry = CmpFindCallbackByCookie((ULONGLONG)Cookie.QuadPart);
    if (Entry == NULL)
    {
        ExReleasePushLockExclusive(&CmpCallbackListLock);
        return STATUS_INVALID_PARAMETER;
    }

    RemoveEntryList(&Entry->ListEntry);
    CmpCallBackCount--;

    ExReleasePushLockExclusive(&CmpCallbackListLock);

    ExWaitForRundownProtectionRelease(&Entry->Rundown);

    CmpCleanupFilterContexts(Entry);

    if (Entry->LastName != NULL) ExFreePool(Entry->LastName);
    if (Entry->Altitude.Buffer != NULL) ExFreePool(Entry->Altitude.Buffer);
    ExFreePool(Entry);
    return STATUS_SUCCESS;
}

VOID
NTAPI
CmGetCallbackVersion(
    _Out_opt_ PULONG Major,
    _Out_opt_ PULONG Minor)
{
    if (Major != NULL) *Major = CMP_CALLBACK_MAJOR;
    if (Minor != NULL) *Minor = CMP_CALLBACK_MINOR;
}

/*
 * Attaches a driver's own note to one open key, which the driver gets handed
 * back on every later notification about that key.
 */
NTSTATUS
NTAPI
CmSetCallbackObjectContext(
    _Inout_ PVOID Object,
    _In_ PLARGE_INTEGER Cookie,
    _In_ PVOID NewContext,
    _Out_opt_ PVOID *OldContext)
{
    PCM_CALLBACK_OBJECT_CONTEXT Context;
    PCM_KEY_BODY KeyBody = Object;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (KeyBody == NULL || Cookie == NULL) return STATUS_INVALID_PARAMETER;
    if (KeyBody->Type != CM_KEY_BODY_TYPE) return STATUS_INVALID_PARAMETER;

    if (OldContext != NULL) *OldContext = NULL;

    ExAcquirePushLockExclusive(&CmpContextListLock);

    for (Entry = KeyBody->ContextListHead.Flink;
         Entry != &KeyBody->ContextListHead;
         Entry = Entry->Flink)
    {
        Context = CONTAINING_RECORD(Entry,
                                    CM_CALLBACK_OBJECT_CONTEXT,
                                    ListEntry);
        if (Context->Cookie != (ULONGLONG)Cookie->QuadPart) continue;

        if (OldContext != NULL) *OldContext = Context->Context;
        Context->Context = NewContext;

        ExReleasePushLockExclusive(&CmpContextListLock);
        return STATUS_SUCCESS;
    }

    Context = ExAllocatePoolZero(NonPagedPool,
                                 sizeof(CM_CALLBACK_OBJECT_CONTEXT),
                                 TAG_CM);
    if (Context == NULL)
    {
        ExReleasePushLockExclusive(&CmpContextListLock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Context->Cookie = (ULONGLONG)Cookie->QuadPart;
    Context->Context = NewContext;
    Context->KeyBody = KeyBody;
    InsertTailList(&CmpContextListHead, &Context->FilterListEntry);
    InsertTailList(&KeyBody->ContextListHead, &Context->ListEntry);

    ExReleasePushLockExclusive(&CmpContextListLock);
    return STATUS_SUCCESS;
}

/*
 * A key going away is the last chance a driver gets to drop what it attached
 * to that key, so each owner is told before its note is thrown away.
 */
VOID
NTAPI
CmpCleanupKeyBodyContexts(
    _In_ PCM_KEY_BODY KeyBody)
{
    REG_CALLBACK_CONTEXT_CLEANUP_INFORMATION Cleanup;
    PCM_CALLBACK_OBJECT_CONTEXT Context;
    PCM_CALLBACK_ENTRY Entry;
    PLIST_ENTRY ListEntry;

    if (!CmpCallbackInited) return;
    if (IsListEmpty(&KeyBody->ContextListHead)) return;

    for (;;)
    {
        ExAcquirePushLockExclusive(&CmpContextListLock);

        if (IsListEmpty(&KeyBody->ContextListHead))
        {
            ExReleasePushLockExclusive(&CmpContextListLock);
            return;
        }

        ListEntry = RemoveHeadList(&KeyBody->ContextListHead);

        Context = CONTAINING_RECORD(ListEntry,
                                    CM_CALLBACK_OBJECT_CONTEXT,
                                    ListEntry);

        RemoveEntryList(&Context->FilterListEntry);

        ExReleasePushLockExclusive(&CmpContextListLock);

        RtlZeroMemory(&Cleanup, sizeof(Cleanup));
        Cleanup.Object = KeyBody;
        Cleanup.ObjectContext = Context->Context;

        ExAcquirePushLockShared(&CmpCallbackListLock);
        Entry = CmpFindCallbackByCookie(Context->Cookie);

        if (Entry != NULL && ExAcquireRundownProtection(&Entry->Rundown))
        {
            ExReleasePushLockShared(&CmpCallbackListLock);

            Entry->Function(Entry->Context,
                            (PVOID)(ULONG_PTR)RegNtCallbackObjectContextCleanup,
                            &Cleanup);

            ExReleaseRundownProtection(&Entry->Rundown);
        }
        else
        {
            ExReleasePushLockShared(&CmpCallbackListLock);
        }

        ExFreePool(Context);
    }
}

NTSTATUS
NTAPI
CmpCallbackObjectContextCleanup(
    _In_ PCM_KEY_BODY KeyBody)
{
    CmpCleanupKeyBodyContexts(KeyBody);
    return STATUS_SUCCESS;
}

/*
 * Names the key a notification is about. The name is built fresh and the
 * previous one built for the same driver is let go, so a driver that looks at
 * the name during its callback always sees a name it may read.
 */
NTSTATUS
NTAPI
CmCallbackGetKeyObjectIDEx(
    _In_ PLARGE_INTEGER Cookie,
    _In_ PVOID Object,
    _Out_opt_ PULONG_PTR ObjectID,
    _Outptr_opt_ PCUNICODE_STRING *ObjectName,
    _In_ ULONG Flags)
{
    PCM_KEY_BODY KeyBody = Object;
    PUNICODE_STRING Name;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Cookie);

    if (Flags != 0) return STATUS_INVALID_PARAMETER;
    if (KeyBody == NULL) return STATUS_INVALID_PARAMETER;
    if (KeyBody->Type != CM_KEY_BODY_TYPE) return STATUS_INVALID_PARAMETER;
    if (KeyBody->KeyControlBlock == NULL) return STATUS_KEY_DELETED;

    if (ObjectID != NULL)
    {
        *ObjectID = (ULONG_PTR)KeyBody->KeyControlBlock;
    }

    if (ObjectName == NULL) return STATUS_SUCCESS;

    CmpLockRegistry();
    Name = CmpConstructName(KeyBody->KeyControlBlock);
    CmpUnlockRegistry();

    if (Name == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    *ObjectName = Name;
    return STATUS_SUCCESS;
}

VOID
NTAPI
CmCallbackReleaseKeyObjectIDEx(
    _In_ PCUNICODE_STRING ObjectName)
{
    PAGED_CODE();

    if (ObjectName != NULL) ExFreePool((PVOID)ObjectName);
}

/*
 * The older call, which hands back a name the driver never releases. The name
 * belongs to the driver's own registration and lives until that driver asks
 * for another one, which is as long as a callback can read it.
 */
NTSTATUS
NTAPI
CmCallbackGetKeyObjectID(
    _In_ PLARGE_INTEGER Cookie,
    _In_ PVOID Object,
    _Out_opt_ PULONG_PTR ObjectID,
    _Outptr_opt_ PCUNICODE_STRING *ObjectName)
{
    PCM_CALLBACK_ENTRY Entry;
    PUNICODE_STRING Previous;
    NTSTATUS Status;

    PAGED_CODE();

    Status = CmCallbackGetKeyObjectIDEx(Cookie,
                                        Object,
                                        ObjectID,
                                        ObjectName,
                                        0);
    if (!NT_SUCCESS(Status) || ObjectName == NULL) return Status;

    ExAcquirePushLockExclusive(&CmpCallbackListLock);

    Entry = CmpFindCallbackByCookie((ULONGLONG)Cookie->QuadPart);
    if (Entry == NULL)
    {
        ExReleasePushLockExclusive(&CmpCallbackListLock);
        CmCallbackReleaseKeyObjectIDEx(*ObjectName);
        *ObjectName = NULL;
        return STATUS_INVALID_PARAMETER;
    }

    Previous = Entry->LastName;
    Entry->LastName = (PUNICODE_STRING)*ObjectName;

    ExReleasePushLockExclusive(&CmpCallbackListLock);

    if (Previous != NULL) ExFreePool(Previous);
    return STATUS_SUCCESS;
}

PVOID
NTAPI
CmGetBoundTransaction(
    _In_ PLARGE_INTEGER Cookie,
    _In_ PVOID Object)
{
    PCM_KEY_BODY KeyBody = Object;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Cookie);

    if (KeyBody == NULL) return NULL;
    if (KeyBody->Type != CM_KEY_BODY_TYPE) return NULL;

    return (PVOID)((ULONG_PTR)KeyBody->Trans.TransPtr & ~(ULONG_PTR)1);
}

/*
 * Asks to be told each time one of the hives the machine is made of has been
 * read in. The registration itself belongs to the caller, so it has to stay
 * where it is for as long as it is registered.
 */
NTSTATUS
NTAPI
CmRegisterMachineHiveLoadedNotification(
    _In_ PCM_MACHINE_HIVE_LOADED_NOTIFICATION Notification)
{
    PAGED_CODE();

    if (Notification == NULL) return STATUS_INVALID_PARAMETER;
    if (Notification->Callback == NULL) return STATUS_INVALID_PARAMETER;
    if (!CmpCallbackInited) return STATUS_INSUFFICIENT_RESOURCES;

    ExAcquirePushLockExclusive(&CmpMachineHiveNotifyLock);
    InsertTailList(&CmpMachineHiveNotifyListHead, &Notification->ListEntry);
    ExReleasePushLockExclusive(&CmpMachineHiveNotifyLock);

    return STATUS_SUCCESS;
}

VOID
NTAPI
CmpNotifyMachineHiveLoaded(
    _In_ PCUNICODE_STRING HiveName,
    _In_ PCMHIVE Hive)
{
    PCM_MACHINE_HIVE_LOADED_NOTIFICATION Notification;
    PLIST_ENTRY Entry;

    if (!CmpCallbackInited) return;

    ExAcquirePushLockShared(&CmpMachineHiveNotifyLock);

    for (Entry = CmpMachineHiveNotifyListHead.Flink;
         Entry != &CmpMachineHiveNotifyListHead;
         Entry = Entry->Flink)
    {
        Notification = CONTAINING_RECORD(Entry,
                                         CM_MACHINE_HIVE_LOADED_NOTIFICATION,
                                         ListEntry);

        Notification->Callback(Notification->Context, HiveName, Hive);
    }

    ExReleasePushLockShared(&CmpMachineHiveNotifyLock);
}
