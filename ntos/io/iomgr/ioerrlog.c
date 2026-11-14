/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Telling the event log, and the user, that a device went wrong
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

LIST_ENTRY IopErrorLogListHead;
KSPIN_LOCK IopLogListLock;
PVOID IopErrorLogObject = NULL;
HANDLE IopLogPort = NULL;
BOOLEAN IopLogPortConnected = FALSE;
BOOLEAN IopLogWorkerRunning = FALSE;
ULONG IopTotalLogSize = 0;
WORK_QUEUE_ITEM IopErrorLogWorkItem;
KDPC IopLogDpc;

/* PRIVATE DATA ***************************************************************/

#define IOP_MAXIMUM_LOG_SIZE (PAGE_SIZE * 2)

typedef struct _IOP_ERROR_LOG_MESSAGE
{
    PORT_MESSAGE Header;
    IO_ERROR_LOG_MESSAGE Message;
} IOP_ERROR_LOG_MESSAGE, *PIOP_ERROR_LOG_MESSAGE;

/* PRIVATE FUNCTIONS **********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
IopInitErrorLog(VOID)
{
    InitializeListHead(&IopErrorLogListHead);
    KeInitializeSpinLock(&IopLogListLock);
    KeInitializeDpc(&IopLogDpc, IopLogDpcRoutine, NULL);
}

PERROR_LOG_ENTRY
IopGetErrorLogEntry(VOID)
{
    PERROR_LOG_ENTRY Entry;
    PLIST_ENTRY ListEntry;
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopLogListLock, &OldIrql);

    if (IsListEmpty(&IopErrorLogListHead))
    {
        KeReleaseSpinLock(&IopLogListLock, OldIrql);
        return NULL;
    }

    ListEntry = RemoveHeadList(&IopErrorLogListHead);
    Entry = CONTAINING_RECORD(ListEntry, ERROR_LOG_ENTRY, ListEntry);

    IopTotalLogSize -= Entry->Size;

    KeReleaseSpinLock(&IopLogListLock, OldIrql);

    return Entry;
}

NTSTATUS
IopConnectLogPort(VOID)
{
    SECURITY_QUALITY_OF_SERVICE SecurityQos;
    UNICODE_STRING PortName;
    ULONG MaxMessageLength = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (IopLogPortConnected) return STATUS_SUCCESS;

    RtlInitUnicodeString(&PortName, L"\\ErrorLogPort");

    SecurityQos.Length = sizeof(SecurityQos);
    SecurityQos.ImpersonationLevel = SecurityImpersonation;
    SecurityQos.ContextTrackingMode = SECURITY_DYNAMIC_TRACKING;
    SecurityQos.EffectiveOnly = TRUE;

    Status = ZwConnectPort(&IopLogPort,
                           &PortName,
                           &SecurityQos,
                           NULL,
                           NULL,
                           &MaxMessageLength,
                           NULL,
                           NULL);
    if (!NT_SUCCESS(Status))
    {
        IopLogPort = NULL;
        return Status;
    }

    IopLogPortConnected = TRUE;

    return STATUS_SUCCESS;
}

static
NTSTATUS
IopSendLogEntry(
    _In_ PERROR_LOG_ENTRY Entry)
{
    PIO_ERROR_LOG_PACKET Packet;
    PIOP_ERROR_LOG_MESSAGE Message;
    PDRIVER_OBJECT DriverObject;
    PUNICODE_STRING DriverName = NULL;
    ULONG PacketSize;
    ULONG TotalSize;
    NTSTATUS Status;

    PAGED_CODE();

    Packet = (PIO_ERROR_LOG_PACKET)(Entry + 1);
    PacketSize = Entry->Size - (ULONG)sizeof(ERROR_LOG_ENTRY);

    DriverObject = Entry->DriverObject;
    if (DriverObject != NULL && DriverObject->DriverName.Length != 0)
    {
        DriverName = &DriverObject->DriverName;
    }

    TotalSize = (ULONG)FIELD_OFFSET(IOP_ERROR_LOG_MESSAGE, Message) +
                (ULONG)FIELD_OFFSET(IO_ERROR_LOG_MESSAGE, EntryData) +
                PacketSize +
                ((DriverName != NULL) ? DriverName->Length : 0);

    Message = ExAllocatePoolZero(PagedPool, TotalSize, TAG_ERROR_LOG);
    if (Message == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Message->Header.u1.s1.TotalLength = (CSHORT)TotalSize;
    Message->Header.u1.s1.DataLength =
        (CSHORT)(TotalSize - sizeof(PORT_MESSAGE));
    Message->Header.u2.s2.Type = LPC_DATAGRAM;

    Message->Message.Type = IO_TYPE_ERROR_LOG;
    Message->Message.Size = (USHORT)TotalSize;
    Message->Message.TimeStamp = Entry->TimeStamp;

    RtlCopyMemory(&Message->Message.EntryData, Packet, PacketSize);

    if (DriverName != NULL)
    {
        Message->Message.DriverNameLength = DriverName->Length;
        Message->Message.DriverNameOffset =
            (ULONG)FIELD_OFFSET(IO_ERROR_LOG_MESSAGE, EntryData) + PacketSize;

        RtlCopyMemory((PUCHAR)&Message->Message +
                          Message->Message.DriverNameOffset,
                      DriverName->Buffer,
                      DriverName->Length);
    }

    Status = ZwRequestPort(IopLogPort, &Message->Header);

    ExFreePoolWithTag(Message, TAG_ERROR_LOG);

    return Status;
}

VOID
NTAPI
IopLogWorker(
    IN PVOID Parameter)
{
    PERROR_LOG_ENTRY Entry;
    NTSTATUS Status;
    KIRQL OldIrql;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Parameter);

    Status = IopConnectLogPort();
    if (!NT_SUCCESS(Status))
    {
        KeAcquireSpinLock(&IopLogListLock, &OldIrql);
        IopLogWorkerRunning = FALSE;
        KeReleaseSpinLock(&IopLogListLock, OldIrql);
        return;
    }

    while (TRUE)
    {
        Entry = IopGetErrorLogEntry();
        if (Entry == NULL) break;

        Status = IopSendLogEntry(Entry);

        ExFreePoolWithTag(Entry, TAG_ERROR_LOG);

        if (NT_SUCCESS(Status)) continue;

        if (Status == STATUS_PORT_DISCONNECTED)
        {
            ZwClose(IopLogPort);
            IopLogPort = NULL;
            IopLogPortConnected = FALSE;
            break;
        }
    }

    KeAcquireSpinLock(&IopLogListLock, &OldIrql);
    IopLogWorkerRunning = FALSE;
    KeReleaseSpinLock(&IopLogListLock, OldIrql);
}

VOID
IopRestartLogWorker(VOID)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&IopLogListLock, &OldIrql);

    if (IopLogWorkerRunning || IsListEmpty(&IopErrorLogListHead))
    {
        KeReleaseSpinLock(&IopLogListLock, OldIrql);
        return;
    }

    IopLogWorkerRunning = TRUE;

    KeReleaseSpinLock(&IopLogListLock, OldIrql);

    ExInitializeWorkItem(&IopErrorLogWorkItem, IopLogWorker, NULL);
    ExQueueWorkItem(&IopErrorLogWorkItem, DelayedWorkQueue);
}

VOID
NTAPI
IopLogDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    IopRestartLogWorker();
}

/* PUBLIC FUNCTIONS ***********************************************************/

PVOID
NTAPI
IoAllocateErrorLogEntry(
    _In_ PVOID IoObject,
    _In_ UCHAR EntrySize)
{
    PERROR_LOG_ENTRY Entry;
    ULONG TotalSize;
    KIRQL OldIrql;

    if (IoObject == NULL) return NULL;
    if (EntrySize < sizeof(IO_ERROR_LOG_PACKET)) return NULL;

    TotalSize = (ULONG)sizeof(ERROR_LOG_ENTRY) + EntrySize;
    if (TotalSize > ERROR_LOG_MAXIMUM_SIZE) return NULL;

    KeAcquireSpinLock(&IopLogListLock, &OldIrql);

    if (IopTotalLogSize + TotalSize > IOP_MAXIMUM_LOG_SIZE)
    {
        KeReleaseSpinLock(&IopLogListLock, OldIrql);
        return NULL;
    }

    IopTotalLogSize += TotalSize;

    KeReleaseSpinLock(&IopLogListLock, OldIrql);

    Entry = ExAllocatePoolZero(NonPagedPool, TotalSize, TAG_ERROR_LOG);
    if (Entry == NULL)
    {
        KeAcquireSpinLock(&IopLogListLock, &OldIrql);
        IopTotalLogSize -= TotalSize;
        KeReleaseSpinLock(&IopLogListLock, OldIrql);
        return NULL;
    }

    Entry->Type = IO_TYPE_ERROR_LOG;
    Entry->Size = (CSHORT)TotalSize;

    if (((PDEVICE_OBJECT)IoObject)->Type == IO_TYPE_DEVICE)
    {
        Entry->DeviceObject = IoObject;
        Entry->DriverObject = ((PDEVICE_OBJECT)IoObject)->DriverObject;
    }
    else
    {
        Entry->DeviceObject = NULL;
        Entry->DriverObject = IoObject;
    }

    ObReferenceObject(Entry->DriverObject);
    if (Entry->DeviceObject != NULL) ObReferenceObject(Entry->DeviceObject);

    return Entry + 1;
}

VOID
NTAPI
IoFreeErrorLogEntry(
    _In_ PVOID ElEntry)
{
    PERROR_LOG_ENTRY Entry;
    KIRQL OldIrql;

    if (ElEntry == NULL) return;

    Entry = (PERROR_LOG_ENTRY)ElEntry - 1;

    if (Entry->DeviceObject != NULL) ObDereferenceObject(Entry->DeviceObject);
    if (Entry->DriverObject != NULL) ObDereferenceObject(Entry->DriverObject);

    KeAcquireSpinLock(&IopLogListLock, &OldIrql);
    IopTotalLogSize -= Entry->Size;
    KeReleaseSpinLock(&IopLogListLock, OldIrql);

    ExFreePoolWithTag(Entry, TAG_ERROR_LOG);
}

VOID
NTAPI
IoWriteErrorLogEntry(
    _In_ PVOID ElEntry)
{
    PERROR_LOG_ENTRY Entry;
    KIRQL OldIrql;

    if (ElEntry == NULL) return;

    Entry = (PERROR_LOG_ENTRY)ElEntry - 1;

    KeQuerySystemTime(&Entry->TimeStamp);

    KeAcquireSpinLock(&IopLogListLock, &OldIrql);

    InsertTailList(&IopErrorLogListHead, &Entry->ListEntry);

    if (IopLogWorkerRunning)
    {
        KeReleaseSpinLock(&IopLogListLock, OldIrql);
        return;
    }

    KeReleaseSpinLock(&IopLogListLock, OldIrql);

    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
    {
        KeInsertQueueDpc(&IopLogDpc, NULL, NULL);
        return;
    }

    IopRestartLogWorker();
}

/* HARD ERRORS ****************************************************************/

/* FIXME: Microsoft documents that too many queued boxes are refused without giving the number; this one is arbitrary */
#define IOP_MAXIMUM_INFORMATIONAL_ERRORS 25

typedef struct _IOP_INFORMATIONAL_ERROR
{
    WORK_QUEUE_ITEM WorkItem;
    LIST_ENTRY ListEntry;
    NTSTATUS ErrorStatus;
    UNICODE_STRING String;
} IOP_INFORMATIONAL_ERROR, *PIOP_INFORMATIONAL_ERROR;

static LIST_ENTRY IopInformationalErrorList =
{
    &IopInformationalErrorList,
    &IopInformationalErrorList
};
static KSPIN_LOCK IopInformationalErrorLock;
static ULONG IopInformationalErrorCount;

/*
 * Shows one queued box. The box is told about from a system thread, so the
 * driver that reported the error never waits for anyone to read it.
 */
static
VOID
NTAPI
IopInformationalErrorWorker(
    _In_ PVOID Context)
{
    PIOP_INFORMATIONAL_ERROR Error = Context;
    ULONG_PTR Parameters[1];
    ULONG Response;
    KIRQL OldIrql;

    PAGED_CODE();

    /* Before anybody listens for errors there is nobody to show this one to */
    if (ExReadyForErrors)
    {
        Parameters[0] = (ULONG_PTR)&Error->String;

        ExRaiseHardError(Error->ErrorStatus,
                         (Error->String.Length != 0) ? 1 : 0,
                         (Error->String.Length != 0) ? 1 : 0,
                         Parameters,
                         OptionOkNoWait,
                         &Response);
    }

    KeAcquireSpinLock(&IopInformationalErrorLock, &OldIrql);
    RemoveEntryList(&Error->ListEntry);
    IopInformationalErrorCount--;
    KeReleaseSpinLock(&IopInformationalErrorLock, OldIrql);

    ExFreePoolWithTag(Error, TAG_ERROR_LOG);
}

BOOLEAN
NTAPI
IoRaiseInformationalHardError(
    _In_ NTSTATUS ErrorStatus,
    _In_opt_ PUNICODE_STRING String,
    _In_opt_ PKTHREAD Thread)
{
    PIOP_INFORMATIONAL_ERROR Error, Pending;
    PLIST_ENTRY Entry;
    USHORT Length;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    /* A service notification carries what it says in its parameters, which this routine has no room for */
    if (ErrorStatus == STATUS_SERVICE_NOTIFICATION) return FALSE;

    if (Thread == NULL) Thread = KeGetCurrentThread();

    if (CONTAINING_RECORD(Thread, ETHREAD, Tcb)->HardErrorsAreDisabled)
    {
        return FALSE;
    }

    Length = (String != NULL) ? String->Length : 0;

    Error = ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(*Error) + Length,
                                  TAG_ERROR_LOG);
    if (Error == NULL) return FALSE;

    Error->ErrorStatus = ErrorStatus;
    Error->String.Buffer = (PWCHAR)(Error + 1);
    Error->String.Length = Length;
    Error->String.MaximumLength = Length;

    if (Length != 0) RtlCopyMemory(Error->String.Buffer, String->Buffer, Length);

    KeAcquireSpinLock(&IopInformationalErrorLock, &OldIrql);

    if (IopInformationalErrorCount >= IOP_MAXIMUM_INFORMATIONAL_ERRORS)
    {
        KeReleaseSpinLock(&IopInformationalErrorLock, OldIrql);
        ExFreePoolWithTag(Error, TAG_ERROR_LOG);
        return FALSE;
    }

    /* The same thing is not said twice while the first box is still waiting */
    for (Entry = IopInformationalErrorList.Flink;
         Entry != &IopInformationalErrorList;
         Entry = Entry->Flink)
    {
        Pending = CONTAINING_RECORD(Entry, IOP_INFORMATIONAL_ERROR, ListEntry);

        if (Pending->ErrorStatus == ErrorStatus &&
            Pending->String.Length == Length &&
            RtlCompareMemory(Pending->String.Buffer,
                             Error->String.Buffer,
                             Length) == Length)
        {
            KeReleaseSpinLock(&IopInformationalErrorLock, OldIrql);
            ExFreePoolWithTag(Error, TAG_ERROR_LOG);
            return FALSE;
        }
    }

    InsertTailList(&IopInformationalErrorList, &Error->ListEntry);
    IopInformationalErrorCount++;

    KeReleaseSpinLock(&IopInformationalErrorLock, OldIrql);

    ExInitializeWorkItem(&Error->WorkItem, IopInformationalErrorWorker, Error);
    ExQueueWorkItem(&Error->WorkItem, DelayedWorkQueue);

    return TRUE;
}

VOID
NTAPI
IoRaiseHardError(
    _In_ PIRP Irp,
    _In_opt_ PVPB Vpb,
    _In_ PDEVICE_OBJECT RealDeviceObject)
{
    PIO_STACK_LOCATION StackPtr;
    UNICODE_STRING Name;
    ULONG_PTR Parameters[1];
    ULONG Response = 0;

    PAGED_CODE();

    StackPtr = IoGetCurrentIrpStackLocation(Irp);

    if (PsGetCurrentThread()->HardErrorsAreDisabled)
    {
        Irp->IoStatus.Status = STATUS_VERIFY_REQUIRED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return;
    }

    IoSetHardErrorOrVerifyDevice(Irp, RealDeviceObject);

    RtlInitEmptyUnicodeString(&Name, NULL, 0);

    if (Vpb != NULL && Vpb->VolumeLabelLength != 0)
    {
        Name.Buffer = Vpb->VolumeLabel;
        Name.Length = Vpb->VolumeLabelLength;
        Name.MaximumLength = Vpb->VolumeLabelLength;
    }

    Parameters[0] = (ULONG_PTR)&Name;

    ExRaiseHardError(STATUS_DEVICE_NOT_READY,
                     1,
                     1,
                     Parameters,
                     OptionCancelTryContinue,
                     &Response);

    if (Response == ResponseContinue || Response == ResponseTryAgain)
    {
        Irp->IoStatus.Status = STATUS_VERIFY_REQUIRED;
    }
    else
    {
        Irp->IoStatus.Status = STATUS_CANCELLED;
    }

    Irp->IoStatus.Information = 0;

    UNREFERENCED_PARAMETER(StackPtr);

    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

BOOLEAN
NTAPI
IoSetThreadHardErrorMode(
    _In_ BOOLEAN EnableHardErrors)
{
    PETHREAD Thread = PsGetCurrentThread();
    BOOLEAN Previous;

    Previous = !Thread->HardErrorsAreDisabled;
    Thread->HardErrorsAreDisabled = !EnableHardErrors;

    return Previous;
}

VOID
NTAPI
IoSetHardErrorOrVerifyDevice(
    _In_ PIRP Irp,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PETHREAD Thread = Irp->Tail.Overlay.Thread;

    if (Thread == NULL) return;

    Thread->DeviceToVerify = DeviceObject;
}

PDEVICE_OBJECT
NTAPI
IoGetDeviceToVerify(
    _In_ PETHREAD Thread)
{
    return Thread->DeviceToVerify;
}

VOID
NTAPI
IoSetDeviceToVerify(
    _In_ PETHREAD Thread,
    _In_opt_ PDEVICE_OBJECT DeviceObject)
{
    Thread->DeviceToVerify = DeviceObject;
}
