/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The completion port a thread waits on for finished requests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

VOID
IopFreeMiniPacket(
    _In_ PIOP_MINI_COMPLETION_PACKET Packet)
{
    if (Packet->PacketType == IopCompletionPacketQuota)
    {
        ExFreePoolWithTag(Packet, TAG_IO);
        return;
    }

    IoCompletionPacketLookaside.TotalFrees++;

    if (ExQueryDepthSList(&IoCompletionPacketLookaside.ListHead) <
        IoCompletionPacketLookaside.Depth)
    {
        InterlockedPushEntrySList(&IoCompletionPacketLookaside.ListHead,
                                  (PSLIST_ENTRY)Packet);
        return;
    }

    IoCompletionPacketLookaside.FreeMisses++;
    ExFreePoolWithTag(Packet, TAG_IO);
}

static
PIOP_MINI_COMPLETION_PACKET
IopAllocateMiniPacket(
    _In_ BOOLEAN Quota)
{
    PIOP_MINI_COMPLETION_PACKET Packet;

    if (Quota)
    {
        Packet = ExAllocatePoolWithQuotaTag(
            NonPagedPool | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE,
            sizeof(IOP_MINI_COMPLETION_PACKET),
            TAG_IO);
        if (Packet == NULL) return NULL;

        Packet->PacketType = IopCompletionPacketQuota;
        return Packet;
    }

    IoCompletionPacketLookaside.TotalAllocates++;

    Packet = (PIOP_MINI_COMPLETION_PACKET)
        InterlockedPopEntrySList(&IoCompletionPacketLookaside.ListHead);
    if (Packet == NULL)
    {
        IoCompletionPacketLookaside.AllocateMisses++;
        Packet = ExAllocatePoolWithTag(NonPagedPool,
                                       sizeof(IOP_MINI_COMPLETION_PACKET),
                                       TAG_IO);
        if (Packet == NULL) return NULL;
    }

    Packet->PacketType = IopCompletionPacketMini;

    return Packet;
}

VOID
NTAPI
IopDeleteIoCompletion(
    PVOID ObjectBody)
{
    PKQUEUE Queue = ObjectBody;
    PIOP_MINI_COMPLETION_PACKET Packet;
    PLIST_ENTRY Entry;
    PIRP Irp;

    while (TRUE)
    {
        Entry = KeRundownQueue(Queue);
        if (Entry == NULL) return;

        while (Entry != NULL)
        {
            PLIST_ENTRY Next = Entry->Flink;

            Packet = CONTAINING_RECORD(Entry,
                                       IOP_MINI_COMPLETION_PACKET,
                                       ListEntry);

            if (Packet->PacketType == IopCompletionPacketIrp)
            {
                Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
                IoFreeIrp(Irp);
            }
            else if (Packet->PacketType == IopCompletionPacketWait)
            {
                IopReleaseWaitCompletionPacket(Packet);
            }
            else
            {
                IopFreeMiniPacket(Packet);
            }

            if (Next == Entry) return;
            Entry = Next;
        }

        return;
    }
}

VOID
NTAPI
IopCloseIoCompletion(
    IN PEPROCESS Process OPTIONAL,
    IN PVOID Object,
    IN ULONG_PTR ProcessHandleCount,
    IN ULONG_PTR SystemHandleCount)
{
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(ProcessHandleCount);
    UNREFERENCED_PARAMETER(SystemHandleCount);
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
IoSetIoCompletion(
    IN PVOID IoCompletion,
    IN PVOID KeyContext,
    IN PVOID ApcContext,
    IN NTSTATUS IoStatus,
    IN ULONG_PTR IoStatusInformation,
    IN BOOLEAN Quota)
{
    PIOP_MINI_COMPLETION_PACKET Packet;

    Packet = IopAllocateMiniPacket(Quota);
    if (Packet == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Packet->KeyContext = KeyContext;
    Packet->ApcContext = ApcContext;
    Packet->IoStatus = IoStatus;
    Packet->IoStatusInformation = IoStatusInformation;

    KeInsertQueue((PKQUEUE)IoCompletion, &Packet->ListEntry);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtCreateIoCompletion(
    _Out_ PHANDLE IoCompletionHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG NumberOfConcurrentThreads)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NTSTATUS Status;
    PKQUEUE Queue;
    HANDLE Handle;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(IoCompletionHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObCreateObject(PreviousMode,
                            IoCompletionType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(KQUEUE),
                            0,
                            0,
                            (PVOID *)&Queue);
    if (!NT_SUCCESS(Status)) return Status;

    KeInitializeQueue(Queue, NumberOfConcurrentThreads);

    Status = ObInsertObject(Queue,
                            NULL,
                            DesiredAccess,
                            0,
                            NULL,
                            &Handle);
    if (!NT_SUCCESS(Status)) return Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *IoCompletionHandle = Handle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status)) NtClose(Handle);
    }
    else
    {
        *IoCompletionHandle = Handle;
    }

    return Status;
}

NTSTATUS
NTAPI
NtOpenIoCompletion(
    _Out_ PHANDLE IoCompletionHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NTSTATUS Status;
    HANDLE Handle;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(IoCompletionHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObOpenObjectByName(ObjectAttributes,
                                IoCompletionType,
                                PreviousMode,
                                NULL,
                                DesiredAccess,
                                NULL,
                                &Handle);
    if (!NT_SUCCESS(Status)) return Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *IoCompletionHandle = Handle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status)) NtClose(Handle);
    }
    else
    {
        *IoCompletionHandle = Handle;
    }

    return Status;
}

NTSTATUS
NTAPI
NtQueryIoCompletion(
    _In_ HANDLE IoCompletionHandle,
    _In_ IO_COMPLETION_INFORMATION_CLASS IoCompletionInformationClass,
    _Out_ PVOID IoCompletionInformation,
    _In_ ULONG IoCompletionInformationLength,
    _Out_opt_ PULONG ResultLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NTSTATUS Status;
    PKQUEUE Queue;
    LONG Depth;

    PAGED_CODE();

    if (IoCompletionInformationClass != IoCompletionBasicInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (IoCompletionInformationLength < sizeof(IO_COMPLETION_BASIC_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(IoCompletionInformation,
                          sizeof(IO_COMPLETION_BASIC_INFORMATION),
                          sizeof(ULONG));
            if (ResultLength != NULL) ProbeForWriteUlong(ResultLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(IoCompletionHandle,
                                       IO_COMPLETION_QUERY_STATE,
                                       IoCompletionType,
                                       PreviousMode,
                                       (PVOID *)&Queue,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Depth = KeReadStateQueue(Queue);

    _SEH2_TRY
    {
        ((PIO_COMPLETION_BASIC_INFORMATION)IoCompletionInformation)->Depth =
            Depth;
        if (ResultLength != NULL)
        {
            *ResultLength = sizeof(IO_COMPLETION_BASIC_INFORMATION);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ObDereferenceObject(Queue);

    return Status;
}

NTSTATUS
NTAPI
NtSetIoCompletion(
    _In_ HANDLE IoCompletionPortHandle,
    _In_ PVOID CompletionKey,
    _In_ PVOID CompletionContext,
    _In_ NTSTATUS CompletionStatus,
    _In_ SIZE_T CompletionInformation)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    NTSTATUS Status;
    PKQUEUE Queue;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(IoCompletionPortHandle,
                                       IO_COMPLETION_MODIFY_STATE,
                                       IoCompletionType,
                                       PreviousMode,
                                       (PVOID *)&Queue,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = IoSetIoCompletion(Queue,
                               CompletionKey,
                               CompletionContext,
                               CompletionStatus,
                               CompletionInformation,
                               TRUE);

    ObDereferenceObject(Queue);

    return Status;
}

NTSTATUS
NTAPI
NtSetIoCompletionEx(
    _In_ HANDLE IoCompletionPortHandle,
    _In_ HANDLE IoCompletionReserveHandle,
    _In_ PVOID CompletionKey,
    _In_ PVOID CompletionContext,
    _In_ NTSTATUS CompletionStatus,
    _In_ SIZE_T CompletionInformation)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PIOP_MINI_COMPLETION_PACKET Reserve;
    NTSTATUS Status;
    PKQUEUE Queue;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(IoCompletionPortHandle,
                                       IO_COMPLETION_MODIFY_STATE,
                                       IoCompletionType,
                                       PreviousMode,
                                       (PVOID *)&Queue,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(IoCompletionReserveHandle,
                                       0,
                                       IopWaitCompletionPacketType,
                                       PreviousMode,
                                       (PVOID *)&Reserve,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Queue);
        return Status;
    }

    Reserve->KeyContext = CompletionKey;
    Reserve->ApcContext = CompletionContext;
    Reserve->IoStatus = CompletionStatus;
    Reserve->IoStatusInformation = CompletionInformation;

    KeInsertQueue(Queue, &Reserve->ListEntry);

    ObDereferenceObject(Reserve);
    ObDereferenceObject(Queue);

    return STATUS_SUCCESS;
}

static
VOID
IopUnpackCompletionEntry(
    _In_ PLIST_ENTRY Entry,
    _Out_ PVOID *Key,
    _Out_ PVOID *Context,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock)
{
    PIOP_MINI_COMPLETION_PACKET Packet;
    PIRP Irp;

    Packet = CONTAINING_RECORD(Entry, IOP_MINI_COMPLETION_PACKET, ListEntry);

    if (Packet->PacketType == IopCompletionPacketIrp)
    {
        Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

        *Key = Irp->Tail.CompletionKey;
        *Context = Irp->Overlay.AsynchronousParameters.UserApcContext;
        *IoStatusBlock = Irp->IoStatus;

        IoFreeIrp(Irp);
        return;
    }

    *Key = Packet->KeyContext;
    *Context = Packet->ApcContext;
    IoStatusBlock->Status = Packet->IoStatus;
    IoStatusBlock->Information = Packet->IoStatusInformation;

    if (Packet->PacketType == IopCompletionPacketWait)
    {
        IopReleaseWaitCompletionPacket(Packet);
        return;
    }

    IopFreeMiniPacket(Packet);
}

NTSTATUS
NTAPI
NtRemoveIoCompletion(
    _In_ HANDLE IoCompletionHandle,
    _Out_ PVOID *CompletionKey,
    _Out_ PVOID *CompletionContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    LARGE_INTEGER CapturedTimeout;
    IO_STATUS_BLOCK KernelIosb;
    PVOID Key = NULL;
    PVOID Context = NULL;
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    PKQUEUE Queue;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWritePointer(CompletionKey);
            ProbeForWritePointer(CompletionContext);
            ProbeForWriteIoStatusBlock(IoStatusBlock);

            if (Timeout != NULL)
            {
                CapturedTimeout = ProbeForReadLargeInteger(Timeout);
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else if (Timeout != NULL)
    {
        CapturedTimeout = *Timeout;
    }

    Status = ObReferenceObjectByHandle(IoCompletionHandle,
                                       IO_COMPLETION_MODIFY_STATE,
                                       IoCompletionType,
                                       PreviousMode,
                                       (PVOID *)&Queue,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Entry = KeRemoveQueue(Queue,
                          PreviousMode,
                          (Timeout != NULL) ? &CapturedTimeout : NULL);

    if ((NTSTATUS)(ULONG_PTR)Entry == STATUS_TIMEOUT ||
        (NTSTATUS)(ULONG_PTR)Entry == STATUS_USER_APC ||
        (NTSTATUS)(ULONG_PTR)Entry == STATUS_ABANDONED)
    {
        ObDereferenceObject(Queue);
        return (NTSTATUS)(ULONG_PTR)Entry;
    }

    IopUnpackCompletionEntry(Entry, &Key, &Context, &KernelIosb);

    ObDereferenceObject(Queue);

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *CompletionKey = Key;
            *CompletionContext = Context;
            *IoStatusBlock = KernelIosb;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *CompletionKey = Key;
        *CompletionContext = Context;
        *IoStatusBlock = KernelIosb;
    }

    return Status;
}

NTSTATUS
NTAPI
NtRemoveIoCompletionEx(
    _In_ HANDLE IoCompletionHandle,
    _Out_writes_to_(Count, *NumEntriesRemoved)
        struct _FILE_IO_COMPLETION_INFORMATION *IoCompletionInformation,
    _In_ ULONG Count,
    _Out_ PULONG NumEntriesRemoved,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ BOOLEAN Alertable)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    LARGE_INTEGER CapturedTimeout;
    IO_STATUS_BLOCK KernelIosb;
    ULONG Removed = 0;
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    PKQUEUE Queue;
    PVOID Key;
    PVOID Context;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Alertable);

    if (Count == 0) return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(IoCompletionInformation,
                          Count * sizeof(FILE_IO_COMPLETION_INFORMATION),
                          sizeof(ULONG_PTR));
            ProbeForWriteUlong(NumEntriesRemoved);

            if (Timeout != NULL)
            {
                CapturedTimeout = ProbeForReadLargeInteger(Timeout);
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else if (Timeout != NULL)
    {
        CapturedTimeout = *Timeout;
    }

    Status = ObReferenceObjectByHandle(IoCompletionHandle,
                                       IO_COMPLETION_MODIFY_STATE,
                                       IoCompletionType,
                                       PreviousMode,
                                       (PVOID *)&Queue,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = STATUS_SUCCESS;

    while (Removed < Count)
    {
        LARGE_INTEGER NoWait;

        NoWait.QuadPart = 0;

        Entry = KeRemoveQueue(Queue,
                              PreviousMode,
                              (Removed == 0)
                                  ? ((Timeout != NULL) ? &CapturedTimeout
                                                       : NULL)
                                  : &NoWait);
        if ((NTSTATUS)(ULONG_PTR)Entry == STATUS_TIMEOUT ||
            (NTSTATUS)(ULONG_PTR)Entry == STATUS_USER_APC ||
            (NTSTATUS)(ULONG_PTR)Entry == STATUS_ALERTED ||
            (NTSTATUS)(ULONG_PTR)Entry == STATUS_ABANDONED)
        {
            if (Removed == 0) Status = (NTSTATUS)(ULONG_PTR)Entry;
            break;
        }

        IopUnpackCompletionEntry(Entry, &Key, &Context, &KernelIosb);

        _SEH2_TRY
        {
            IoCompletionInformation[Removed].KeyContext = Key;
            IoCompletionInformation[Removed].ApcContext = Context;
            IoCompletionInformation[Removed].IoStatusBlock = KernelIosb;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status)) break;

        Removed++;
    }

    ObDereferenceObject(Queue);

    _SEH2_TRY
    {
        *NumEntriesRemoved = Removed;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}
