/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Callback objects, and the blocks that keep a callback reachable
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

POBJECT_TYPE ExCallbackObjectType = NULL;

PCALLBACK_OBJECT SetSystemTimeCallback = NULL;
PCALLBACK_OBJECT SetSystemStateCallback = NULL;
PCALLBACK_OBJECT PowerStateCallback = NULL;
PCALLBACK_OBJECT ProcessorAddCallback = NULL;

GENERIC_MAPPING ExpCallbackMapping =
{
    STANDARD_RIGHTS_READ,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE,
    STANDARD_RIGHTS_ALL
};

KEVENT ExpCallbackEvent;
BOOLEAN ExpCallBackFlush = FALSE;

KSPIN_LOCK ExpProcessorAddLock;

/* PRIVATE DATA ***************************************************************/

#define EXP_CALLBACK_SIGNATURE 'llaC'
#define EXP_CALLBACK_TAG       'blaC'

/* The callback objects the system makes for itself before anyone asks */
SYSTEM_CALLBACKS ExpInitializeCallback[] =
{
    { &SetSystemTimeCallback, L"\\Callback\\SetSystemTime" },
    { &SetSystemStateCallback, L"\\Callback\\SetSystemState" },
    { &PowerStateCallback, L"\\Callback\\PowerState" },
    { &ProcessorAddCallback, L"\\Callback\\ProcessorAdd" },
    { NULL, NULL }
};

typedef struct _EXP_PROCESSOR_CHANGE_REGISTRATION
{
    LIST_ENTRY Link;
    PPROCESSOR_CALLBACK_FUNCTION Function;
    PVOID Context;
} EXP_PROCESSOR_CHANGE_REGISTRATION, *PEXP_PROCESSOR_CHANGE_REGISTRATION;

static LIST_ENTRY ExpProcessorChangeList;

/* PRIVATE FUNCTIONS **********************************************************/

VOID
NTAPI
ExpDeleteCallback(
    _In_ PVOID ObjectBody)
{
    PCALLBACK_OBJECT Callback = ObjectBody;

    ASSERT(IsListEmpty(&Callback->RegisteredCallbacks));

    Callback->Signature = 0;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
ExInitializeCallBack(
    _Inout_ PEX_CALLBACK Callback)
{
    ExInitializeFastReference(&Callback->RoutineBlock, NULL);
}

PEX_CALLBACK_ROUTINE_BLOCK
NTAPI
ExAllocateCallBack(
    _In_ PEX_CALLBACK_FUNCTION Function,
    _In_opt_ PVOID Context)
{
    PEX_CALLBACK_ROUTINE_BLOCK Block;

    Block = ExAllocatePoolWithTag(PagedPool,
                                  sizeof(EX_CALLBACK_ROUTINE_BLOCK),
                                  EXP_CALLBACK_TAG);
    if (Block == NULL)
    {
        return NULL;
    }

    Block->Function = Function;
    Block->Context = Context;
    ExInitializeRundownProtection(&Block->RundownProtect);

    return Block;
}

VOID
NTAPI
ExFreeCallBack(
    _In_ PEX_CALLBACK_ROUTINE_BLOCK CallbackRoutineBlock)
{
    ExFreePoolWithTag(CallbackRoutineBlock, EXP_CALLBACK_TAG);
}

PEX_CALLBACK_FUNCTION
NTAPI
ExGetCallBackBlockRoutine(
    _In_ PEX_CALLBACK_ROUTINE_BLOCK CallbackRoutineBlock)
{
    return CallbackRoutineBlock->Function;
}

PVOID
NTAPI
ExGetCallBackBlockContext(
    _In_ PEX_CALLBACK_ROUTINE_BLOCK CallbackRoutineBlock)
{
    return CallbackRoutineBlock->Context;
}

/*
 * Takes a reference on whatever block the callback names now, so the block
 * cannot be freed while the call is being made.
 */
PEX_CALLBACK_ROUTINE_BLOCK
NTAPI
ExReferenceCallBackBlock(
    _Inout_ PEX_CALLBACK Callback)
{
    EX_FAST_REF OldRef;
    PEX_CALLBACK_ROUTINE_BLOCK Block;

    OldRef = ExAcquireFastReference(&Callback->RoutineBlock);

    Block = ExGetObjectFastReference(OldRef);
    if (Block == NULL)
    {
        return NULL;
    }

    /* The fast path had references to hand out. */
    if (ExGetCountFastReference(OldRef) != 0)
    {
        return Block;
    }

    /*
     * The references the pointer itself can carry are used up, so one is taken
     * the slow way instead.
     */
    if (!ExAcquireRundownProtection(&Block->RundownProtect))
    {
        return NULL;
    }

    return Block;
}

VOID
NTAPI
ExDereferenceCallBackBlock(
    _Inout_ PEX_CALLBACK Callback,
    _In_ PEX_CALLBACK_ROUTINE_BLOCK CallbackRoutineBlock)
{
    if (CallbackRoutineBlock == NULL)
    {
        return;
    }

    /* Give the reference back where it came from, if it still fits. */
    if (ExReleaseFastReference(&Callback->RoutineBlock, CallbackRoutineBlock))
    {
        return;
    }

    ExReleaseRundownProtection(&CallbackRoutineBlock->RundownProtect);
}

BOOLEAN
NTAPI
ExCompareExchangeCallBack(
    _Inout_ PEX_CALLBACK CallBack,
    _In_opt_ PEX_CALLBACK_ROUTINE_BLOCK NewBlock,
    _In_opt_ PEX_CALLBACK_ROUTINE_BLOCK OldBlock)
{
    EX_FAST_REF OldRef;
    PEX_CALLBACK_ROUTINE_BLOCK Block;
    ULONG Count;

    if (NewBlock != NULL)
    {
        if (!ExfAcquireRundownProtectionEx(&NewBlock->RundownProtect,
                                          MAX_FAST_REFS + 1))
        {
            return FALSE;
        }
    }

    OldRef = ExCompareSwapFastReference(&CallBack->RoutineBlock,
                                        NewBlock,
                                        OldBlock);

    Block = ExGetObjectFastReference(OldRef);
    Count = ExGetCountFastReference(OldRef);

    if (Block != OldBlock)
    {
        if (NewBlock != NULL)
        {
            ExfReleaseRundownProtectionEx(&NewBlock->RundownProtect,
                                         MAX_FAST_REFS + 1);
        }

        return FALSE;
    }

    if (Block == NULL)
    {
        return TRUE;
    }

    /*
     * The references the old pointer was still carrying were never handed out,
     * so they go back now, and the one the pointer itself stood for with them.
     */
    if (Count != 0)
    {
        ExfReleaseRundownProtectionEx(&Block->RundownProtect, Count);
    }

    ExReleaseRundownProtection(&Block->RundownProtect);

    return TRUE;
}

VOID
NTAPI
ExWaitForCallBacks(
    _In_ PEX_CALLBACK_ROUTINE_BLOCK CallbackRoutineBlock)
{
    ExWaitForRundownProtectionRelease(&CallbackRoutineBlock->RundownProtect);
}

/* CALLBACK OBJECTS ***********************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeCallbacks(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"Callback");

    RtlZeroMemory(&Initializer, sizeof(Initializer));

    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_OPENLINK | OBJ_PERMANENT;
    Initializer.ValidAccessMask = STANDARD_RIGHTS_ALL;
    Initializer.GenericMapping = ExpCallbackMapping;
    Initializer.PoolType = NonPagedPool;
    Initializer.DefaultNonPagedPoolCharge = sizeof(CALLBACK_OBJECT);
    Initializer.DeleteProcedure = ExpDeleteCallback;

    if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                       &Initializer,
                                       NULL,
                                       &ExCallbackObjectType)))
    {
        return FALSE;
    }

    KeInitializeEvent(&ExpCallbackEvent, NotificationEvent, FALSE);
    KeInitializeSpinLock(&ExpProcessorAddLock);
    InitializeListHead(&ExpProcessorChangeList);

    return TRUE;
}

/*
 * The callbacks a driver reaches by name. They are made once the object
 * namespace holds a place for them and the descriptors that protect them say
 * who may use them.
 */
CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeCallbackObjects(VOID)
{
    UNICODE_STRING DirectoryName = RTL_CONSTANT_STRING(L"\\Callback");
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING ObjectName;
    HANDLE DirectoryHandle;
    NTSTATUS Status;
    ULONG Index;

    /* The directory the named callbacks live in belongs to the system */
    InitializeObjectAttributes(&ObjectAttributes,
                               &DirectoryName,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT |
                                   OBJ_KERNEL_HANDLE,
                               NULL,
                               SePublicDefaultUnrestrictedSd);

    Status = ZwCreateDirectoryObject(&DirectoryHandle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("EX: could not make the callback directory (0x%08lx)\n", Status);
        return FALSE;
    }

    ZwClose(DirectoryHandle);

    for (Index = 0; ExpInitializeCallback[Index].CallbackObject != NULL; Index++)
    {
        RtlInitUnicodeString(&ObjectName, ExpInitializeCallback[Index].Name);

        InitializeObjectAttributes(&ObjectAttributes,
                                   &ObjectName,
                                   OBJ_CASE_INSENSITIVE | OBJ_PERMANENT |
                                   OBJ_KERNEL_HANDLE,
                                   NULL,
                                   SePublicDefaultSd);

        if (!NT_SUCCESS(ExCreateCallback(
                            ExpInitializeCallback[Index].CallbackObject,
                            &ObjectAttributes,
                            TRUE,
                            TRUE)))
        {
            return FALSE;
        }
    }

    return TRUE;
}

NTSTATUS
NTAPI
ExCreateCallback(
    _Outptr_ PCALLBACK_OBJECT *CallbackObject,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ BOOLEAN Create,
    _In_ BOOLEAN AllowMultipleCallbacks)
{
    PCALLBACK_OBJECT Callback;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    /* An existing object is enough when the caller is not insisting on a new one. */
    Status = ObOpenObjectByName(ObjectAttributes,
                                ExCallbackObjectType,
                                KernelMode,
                                NULL,
                                FILE_READ_DATA,
                                NULL,
                                &Handle);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(Handle,
                                           0,
                                           ExCallbackObjectType,
                                           KernelMode,
                                           (PVOID *)&Callback,
                                           NULL);
        ObCloseHandle(Handle, KernelMode);

        if (NT_SUCCESS(Status))
        {
            *CallbackObject = Callback;
        }

        return Status;
    }

    if (!Create)
    {
        return Status;
    }

    Status = ObCreateObject(KernelMode,
                            ExCallbackObjectType,
                            ObjectAttributes,
                            KernelMode,
                            NULL,
                            sizeof(CALLBACK_OBJECT),
                            0,
                            0,
                            (PVOID *)&Callback);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Callback->Signature = EXP_CALLBACK_SIGNATURE;
    Callback->AllowMultipleCallbacks = AllowMultipleCallbacks;
    KeInitializeSpinLock(&Callback->Lock);
    InitializeListHead(&Callback->RegisteredCallbacks);

    Status = ObInsertObject(Callback,
                            NULL,
                            FILE_READ_DATA,
                            0,
                            (PVOID *)&Callback,
                            &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    ObCloseHandle(Handle, KernelMode);

    *CallbackObject = Callback;

    return STATUS_SUCCESS;
}

PVOID
NTAPI
ExRegisterCallback(
    _Inout_ PCALLBACK_OBJECT CallbackObject,
    _In_ PCALLBACK_FUNCTION CallbackFunction,
    _In_opt_ PVOID CallbackContext)
{
    PCALLBACK_REGISTRATION Registration;
    KIRQL OldIrql;

    PAGED_CODE();

    Registration = ExAllocatePoolWithTag(NonPagedPool,
                                         sizeof(CALLBACK_REGISTRATION),
                                         EXP_CALLBACK_TAG);
    if (Registration == NULL)
    {
        return NULL;
    }

    Registration->CallbackObject = CallbackObject;
    Registration->CallbackFunction = CallbackFunction;
    Registration->CallbackContext = CallbackContext;
    Registration->Busy = 0;
    Registration->UnregisterWaiting = FALSE;

    KeAcquireSpinLock(&CallbackObject->Lock, &OldIrql);

    if (!CallbackObject->AllowMultipleCallbacks &&
        !IsListEmpty(&CallbackObject->RegisteredCallbacks))
    {
        KeReleaseSpinLock(&CallbackObject->Lock, OldIrql);
        ExFreePoolWithTag(Registration, EXP_CALLBACK_TAG);
        return NULL;
    }

    ObReferenceObject(CallbackObject);
    InsertTailList(&CallbackObject->RegisteredCallbacks, &Registration->Link);

    KeReleaseSpinLock(&CallbackObject->Lock, OldIrql);

    return Registration;
}

/*
 * Takes a callback back off the object. A callback that is running when this is
 * called is waited for, so the caller may free whatever the callback was using.
 */
VOID
NTAPI
ExUnregisterCallback(
    _Inout_ PVOID CbRegistration)
{
    PCALLBACK_REGISTRATION Registration = CbRegistration;
    PCALLBACK_OBJECT CallbackObject = Registration->CallbackObject;
    KIRQL OldIrql;

    PAGED_CODE();

    KeAcquireSpinLock(&CallbackObject->Lock, &OldIrql);

    RemoveEntryList(&Registration->Link);

    while (Registration->Busy != 0)
    {
        Registration->UnregisterWaiting = TRUE;
        ExpCallBackFlush = TRUE;

        KeReleaseSpinLock(&CallbackObject->Lock, OldIrql);

        KeWaitForSingleObject(&ExpCallbackEvent,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);

        KeAcquireSpinLock(&CallbackObject->Lock, &OldIrql);
    }

    KeReleaseSpinLock(&CallbackObject->Lock, OldIrql);

    ObDereferenceObject(CallbackObject);

    ExFreePoolWithTag(Registration, EXP_CALLBACK_TAG);
}

VOID
NTAPI
ExNotifyCallback(
    _In_ PCALLBACK_OBJECT CallbackObject,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PCALLBACK_OBJECT Callback = CallbackObject;
    PCALLBACK_REGISTRATION Registration;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    if (Callback == NULL)
    {
        return;
    }

    KeAcquireSpinLock(&Callback->Lock, &OldIrql);

    Entry = Callback->RegisteredCallbacks.Flink;

    while (Entry != &Callback->RegisteredCallbacks)
    {
        Registration = CONTAINING_RECORD(Entry, CALLBACK_REGISTRATION, Link);

        Registration->Busy++;

        KeReleaseSpinLock(&Callback->Lock, OldIrql);

        Registration->CallbackFunction(Registration->CallbackContext,
                                       Argument1,
                                       Argument2);

        KeAcquireSpinLock(&Callback->Lock, &OldIrql);

        Entry = Entry->Flink;

        if (--Registration->Busy == 0)
        {
            if (Registration->UnregisterWaiting)
            {
                KeSetEvent(&ExpCallbackEvent, IO_NO_INCREMENT, FALSE);
                KeClearEvent(&ExpCallbackEvent);
            }
        }
    }

    KeReleaseSpinLock(&Callback->Lock, OldIrql);
}

/* PROCESSOR CHANGE NOTIFICATIONS *********************************************/

PVOID
NTAPI
KeRegisterProcessorChangeCallback(
    _In_ PPROCESSOR_CALLBACK_FUNCTION CallbackFunction,
    _In_opt_ PVOID CallbackContext,
    _In_ ULONG Flags)
{
    PEXP_PROCESSOR_CHANGE_REGISTRATION Registration;
    KE_PROCESSOR_CHANGE_NOTIFY_CONTEXT Context;
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;
    ULONG Index;

    PAGED_CODE();

    Registration = ExAllocatePoolWithTag(
                       NonPagedPool,
                       sizeof(EXP_PROCESSOR_CHANGE_REGISTRATION),
                       EXP_CALLBACK_TAG);
    if (Registration == NULL)
    {
        return NULL;
    }

    Registration->Function = CallbackFunction;
    Registration->Context = CallbackContext;

    /*
     * A caller that asks to be told about the processors already running is
     * told about each of them before the registration is of any further use.
     */
    if (Flags & KE_PROCESSOR_CHANGE_ADD_EXISTING)
    {
        for (Index = 0; Index < (ULONG)KeNumberProcessors; Index++)
        {
            Context.State = KeProcessorAddCompleteNotify;
            Context.NtNumber = Index;
            Context.ProcNumber.Group = 0;
            Context.ProcNumber.Number = (UCHAR)Index;
            Context.ProcNumber.Reserved = 0;
            Context.Status = STATUS_SUCCESS;

            CallbackFunction(CallbackContext, &Context, &Status);

            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(Registration, EXP_CALLBACK_TAG);
                return NULL;
            }
        }
    }

    KeAcquireSpinLock(&ExpProcessorAddLock, &OldIrql);
    InsertTailList(&ExpProcessorChangeList, &Registration->Link);
    KeReleaseSpinLock(&ExpProcessorAddLock, OldIrql);

    return Registration;
}

VOID
NTAPI
KeDeregisterProcessorChangeCallback(
    _In_ PVOID CallbackHandle)
{
    PEXP_PROCESSOR_CHANGE_REGISTRATION Registration = CallbackHandle;
    KIRQL OldIrql;

    PAGED_CODE();

    KeAcquireSpinLock(&ExpProcessorAddLock, &OldIrql);
    RemoveEntryList(&Registration->Link);
    KeReleaseSpinLock(&ExpProcessorAddLock, OldIrql);

    ExFreePoolWithTag(Registration, EXP_CALLBACK_TAG);
}

/* EOF */
