/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         Serial port driver
 * FILE:            drivers/dd/serial/create.c
 * PURPOSE:         Serial IRP_MJ_READ/IRP_MJ_WRITE operations
 *
 * PROGRAMMERS:     Hervé Poussineau (hpoussin@reactos.org)
 */

#include "serial.h"

#include <debug.h>

static IO_WORKITEM_ROUTINE SerialReadWorkItem;

static PVOID
SerialGetUserBuffer(IN PIRP Irp)
{
	ASSERT(Irp);

	if (Irp->MdlAddress)
		return Irp->MdlAddress;
	else
		return Irp->AssociatedIrp.SystemBuffer;
}

static VOID
ReadBytes(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp,
	PWORKITEM_DATA WorkItemData)
{
	PSERIAL_DEVICE_EXTENSION DeviceExtension;
	ULONG Length;
	PUCHAR Buffer;
	UCHAR ReceivedByte;
	KTIMER TotalTimeoutTimer;
	KIRQL Irql;
	ULONG ObjectCount;
	PVOID ObjectsArray[3];
	ULONG_PTR Information = 0;
	BOOLEAN Cancelled = FALSE;
	NTSTATUS Status;

	ASSERT(DeviceObject);
	ASSERT(WorkItemData);

	DeviceExtension = (PSERIAL_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	Length = IoGetCurrentIrpStackLocation(Irp)->Parameters.Read.Length;
	Buffer = SerialGetUserBuffer(Irp);

	INFO_(SERIAL, "UseIntervalTimeout = %s, IntervalTimeout = %lu\n",
		WorkItemData->UseIntervalTimeout ? "YES" : "NO",
		WorkItemData->UseIntervalTimeout ? WorkItemData->IntervalTimeout.QuadPart : 0);
	INFO_(SERIAL, "UseTotalTimeout = %s\n",
		WorkItemData->UseTotalTimeout ? "YES" : "NO");

	ObjectCount = 2;
	ObjectsArray[0] = &DeviceExtension->InputBufferNotEmpty;
	ObjectsArray[1] = &WorkItemData->CancelEvent;
	if (WorkItemData->UseTotalTimeout)
	{
		KeInitializeTimer(&TotalTimeoutTimer);
		KeSetTimer(&TotalTimeoutTimer, WorkItemData->TotalTimeoutTime, NULL);
		ObjectsArray[ObjectCount] = &TotalTimeoutTimer;
		ObjectCount++;
	}

	/* while buffer is not fully filled */
	while (TRUE)
	{
		/* read already received bytes from buffer */
		KeAcquireSpinLock(&DeviceExtension->InputBufferLock, &Irql);
		while (!IsCircularBufferEmpty(&DeviceExtension->InputBuffer)
			&& Length > 0)
		{
			PopCircularBufferEntry(&DeviceExtension->InputBuffer, &ReceivedByte);
			INFO_(SERIAL, "Reading byte from buffer: 0x%02x\n", ReceivedByte);

			Buffer[Information++] = ReceivedByte;
			Length--;
		}
		SerialUpdateReceiveFlow(DeviceExtension);
		KeClearEvent(&DeviceExtension->InputBufferNotEmpty);
		KeReleaseSpinLock(&DeviceExtension->InputBufferLock, Irql);

		if (Length == 0)
		{
			INFO_(SERIAL, "All bytes read\n");
			break;
		}

		if (WorkItemData->DontWait
			&& !(WorkItemData->ReadAtLeastOneByte && Information == 0))
		{
			INFO_(SERIAL, "Buffer empty. Don't wait more bytes\n");
			break;
		}

		Status = KeWaitForMultipleObjects(
			ObjectCount,
			ObjectsArray,
			WaitAny,
			Executive,
			KernelMode,
			FALSE,
			(WorkItemData->UseIntervalTimeout && Information > 0) ? &WorkItemData->IntervalTimeout : NULL,
			NULL);

		if (Status == STATUS_WAIT_1)
		{
			Cancelled = TRUE;
			break;
		}
		if (Status == STATUS_TIMEOUT /* interval timeout */
			|| Status == STATUS_WAIT_2) /* total timeout */
		{
			TRACE_(SERIAL, "Timeout when reading bytes. Status = 0x%08lx\n", Status);
			break;
		}
	}

	/* stop total timeout timer */
	if (WorkItemData->UseTotalTimeout)
		KeCancelTimer(&TotalTimeoutTimer);

	KeAcquireSpinLock(&DeviceExtension->InputBufferLock, &Irql);
	RemoveEntryList(&WorkItemData->ListEntry);
	KeReleaseSpinLock(&DeviceExtension->InputBufferLock, Irql);
	if (WorkItemData->Cancellable && !IoSetCancelRoutine(Irp, NULL))
		KeWaitForSingleObject(&WorkItemData->CancelDoneEvent, Executive, KernelMode, FALSE, NULL);

	Irp->IoStatus.Information = Information;
	if (Cancelled)
		Irp->IoStatus.Status = STATUS_CANCELLED;
	else if (Information == 0)
		Irp->IoStatus.Status = STATUS_TIMEOUT;
	else
		Irp->IoStatus.Status = STATUS_SUCCESS;
}

static VOID NTAPI
SerialCancelRead(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	PWORKITEM_DATA WorkItemData = (PWORKITEM_DATA)Irp->Tail.Overlay.DriverContext[0];

	IoReleaseCancelSpinLock(Irp->CancelIrql);
	KeSetEvent(&WorkItemData->CancelEvent, IO_NO_INCREMENT, FALSE);
	KeSetEvent(&WorkItemData->CancelDoneEvent, IO_NO_INCREMENT, FALSE);
}

VOID
SerialAbortReads(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension)
{
	PLIST_ENTRY Entry;
	KIRQL Irql;

	KeAcquireSpinLock(&DeviceExtension->InputBufferLock, &Irql);
	for (Entry = DeviceExtension->ReadList.Flink; Entry != &DeviceExtension->ReadList; Entry = Entry->Flink)
		KeSetEvent(&CONTAINING_RECORD(Entry, WORKITEM_DATA, ListEntry)->CancelEvent, IO_NO_INCREMENT, FALSE);
	KeReleaseSpinLock(&DeviceExtension->InputBufferLock, Irql);
}

static VOID NTAPI
SerialReadWorkItem(
	IN PDEVICE_OBJECT DeviceObject,
	IN PVOID pWorkItemData /* real type PWORKITEM_DATA */)
{
	PWORKITEM_DATA WorkItemData;
	PIRP Irp;

	TRACE_(SERIAL, "SerialReadWorkItem() called\n");

	WorkItemData = (PWORKITEM_DATA)pWorkItemData;
	Irp = WorkItemData->Irp;

	ReadBytes(DeviceObject, Irp, WorkItemData);

	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	IoReleaseRemoveLock(&((PSERIAL_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->RemoveLock, Irp);
	IoFreeWorkItem(WorkItemData->IoWorkItem);
	ExFreePoolWithTag(pWorkItemData, SERIAL_TAG);
}

NTSTATUS NTAPI
SerialRead(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	PIO_STACK_LOCATION Stack;
	PSERIAL_DEVICE_EXTENSION DeviceExtension;
	ULONG Length;
	PUCHAR Buffer;
	PWORKITEM_DATA WorkItemData;
	PIO_WORKITEM WorkItem;
	KIRQL Irql;
	NTSTATUS Status;

	TRACE_(SERIAL, "IRP_MJ_READ\n");

	Stack = IoGetCurrentIrpStackLocation(Irp);
	Length = Stack->Parameters.Read.Length;
	Buffer = SerialGetUserBuffer(Irp);
	DeviceExtension = (PSERIAL_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

	if (Length != 0 && Buffer == NULL)
	{
		Status = STATUS_INVALID_PARAMETER;
		goto ByeBye;
	}

	if (Length == 0)
	{
		Status = STATUS_SUCCESS;
		goto ByeBye;
	}

	/* Allocate memory for parameters */
	WorkItemData = ExAllocatePoolWithTag(NonPagedPool, sizeof(WORKITEM_DATA), SERIAL_TAG);
	if (!WorkItemData)
	{
		Status = STATUS_INSUFFICIENT_RESOURCES;
		goto ByeBye;
	}
	RtlZeroMemory(WorkItemData, sizeof(WORKITEM_DATA));
	WorkItemData->Irp = Irp;
	KeInitializeEvent(&WorkItemData->CancelEvent, NotificationEvent, FALSE);
	KeInitializeEvent(&WorkItemData->CancelDoneEvent, NotificationEvent, FALSE);
	KeAcquireSpinLock(&DeviceExtension->InputBufferLock, &Irql);
	InsertTailList(&DeviceExtension->ReadList, &WorkItemData->ListEntry);
	KeReleaseSpinLock(&DeviceExtension->InputBufferLock, Irql);

	/* Calculate time outs */
	if (DeviceExtension->SerialTimeOuts.ReadIntervalTimeout == INFINITE &&
		 DeviceExtension->SerialTimeOuts.ReadTotalTimeoutMultiplier == INFINITE &&
		 DeviceExtension->SerialTimeOuts.ReadTotalTimeoutConstant > 0 &&
		 DeviceExtension->SerialTimeOuts.ReadTotalTimeoutConstant < INFINITE)
	{
		/* read at least one byte, and at most bytes already received */
		WorkItemData->DontWait = TRUE;
		WorkItemData->ReadAtLeastOneByte = TRUE;
	}
	else if (DeviceExtension->SerialTimeOuts.ReadIntervalTimeout == INFINITE &&
	         DeviceExtension->SerialTimeOuts.ReadTotalTimeoutConstant == 0 &&
	         DeviceExtension->SerialTimeOuts.ReadTotalTimeoutMultiplier == 0)
	{
		/* read only bytes that are already in buffer */
		WorkItemData->DontWait = TRUE;
	}
	else
	{
		/* use timeouts */
		if (DeviceExtension->SerialTimeOuts.ReadIntervalTimeout != 0)
			{
				WorkItemData->UseIntervalTimeout = TRUE;
				WorkItemData->IntervalTimeout.QuadPart = -(LONGLONG)DeviceExtension->SerialTimeOuts.ReadIntervalTimeout * 10000;
		}
		if (DeviceExtension->SerialTimeOuts.ReadTotalTimeoutMultiplier != 0 ||
			 DeviceExtension->SerialTimeOuts.ReadTotalTimeoutConstant != 0)
		{
			ULONG TotalTimeout;
			LARGE_INTEGER SystemTime;

			WorkItemData->UseTotalTimeout = TRUE;
			TotalTimeout = DeviceExtension->SerialTimeOuts.ReadTotalTimeoutConstant +
				DeviceExtension->SerialTimeOuts.ReadTotalTimeoutMultiplier * Length;
			KeQuerySystemTime(&SystemTime);
			WorkItemData->TotalTimeoutTime.QuadPart = SystemTime.QuadPart +
				TotalTimeout * 10000;
		}
	}

	/* Pend IRP */
	WorkItem = IoAllocateWorkItem(DeviceObject);
	if (WorkItem)
	{
		Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
		if (!NT_SUCCESS(Status))
		{
			IoFreeWorkItem(WorkItem);
			KeAcquireSpinLock(&DeviceExtension->InputBufferLock, &Irql);
			RemoveEntryList(&WorkItemData->ListEntry);
			KeReleaseSpinLock(&DeviceExtension->InputBufferLock, Irql);
			ExFreePoolWithTag(WorkItemData, SERIAL_TAG);
			Irp->IoStatus.Status = Status;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			return Status;
		}
		WorkItemData->IoWorkItem = WorkItem;
		WorkItemData->Cancellable = TRUE;
		Irp->Tail.Overlay.DriverContext[0] = WorkItemData;
		IoMarkIrpPending(Irp);
		IoSetCancelRoutine(Irp, SerialCancelRead);
		if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
		{
			KeSetEvent(&WorkItemData->CancelEvent, IO_NO_INCREMENT, FALSE);
			KeSetEvent(&WorkItemData->CancelDoneEvent, IO_NO_INCREMENT, FALSE);
		}
		IoQueueWorkItem(WorkItem, SerialReadWorkItem, DelayedWorkQueue, WorkItemData);
		return STATUS_PENDING;
	}

	/* Insufficient resources, we can't pend the Irp */
	INFO_(SERIAL, "Insufficient resources\n");
	Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
	if (!NT_SUCCESS(Status))
	{
		KeAcquireSpinLock(&DeviceExtension->InputBufferLock, &Irql);
		RemoveEntryList(&WorkItemData->ListEntry);
		KeReleaseSpinLock(&DeviceExtension->InputBufferLock, Irql);
		ExFreePoolWithTag(WorkItemData, SERIAL_TAG);
		goto ByeBye;
	}
	ReadBytes(DeviceObject, Irp, WorkItemData);
	Status = Irp->IoStatus.Status;
	ExFreePoolWithTag(WorkItemData, SERIAL_TAG);

	IoReleaseRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));

ByeBye:
	Irp->IoStatus.Status = Status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return Status;
}

static VOID NTAPI
SerialCancelWrite(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	PSERIAL_DEVICE_EXTENSION DeviceExtension = (PSERIAL_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	KIRQL Irql;

	IoReleaseCancelSpinLock(Irp->CancelIrql);

	KeAcquireSpinLock(&DeviceExtension->OutputBufferLock, &Irql);
	RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
	if (DeviceExtension->CurrentWriteIrp == Irp)
	{
		DeviceExtension->CurrentWriteIrp = NULL;
		KeCancelTimer(&DeviceExtension->WriteTimer);
	}
	KeReleaseSpinLock(&DeviceExtension->OutputBufferLock, Irql);

	Irp->IoStatus.Status = STATUS_CANCELLED;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	KeInsertQueueDpc(&DeviceExtension->SendByteDpc, NULL, NULL);
}

static NTSTATUS
SerialQueueWrite(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension,
	IN PIRP Irp)
{
	KIRQL Irql;

	Irp->IoStatus.Information = 0;
	Irp->Tail.Overlay.DriverContext[0] = SerialGetUserBuffer(Irp);
	IoMarkIrpPending(Irp);

	KeAcquireSpinLock(&DeviceExtension->OutputBufferLock, &Irql);
	InsertTailList(&DeviceExtension->WriteQueue, &Irp->Tail.Overlay.ListEntry);
	IoSetCancelRoutine(Irp, SerialCancelWrite);
	if (Irp->Cancel && IoSetCancelRoutine(Irp, NULL))
	{
		RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
		KeReleaseSpinLock(&DeviceExtension->OutputBufferLock, Irql);
		Irp->IoStatus.Status = STATUS_CANCELLED;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_PENDING;
	}
	KeReleaseSpinLock(&DeviceExtension->OutputBufferLock, Irql);

	KeInsertQueueDpc(&DeviceExtension->SendByteDpc, NULL, NULL);
	return STATUS_PENDING;
}

static VOID
SerialArmWriteTimer(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension,
	IN ULONGLONG Timeout)
{
	LARGE_INTEGER DueTime;

	DueTime.QuadPart = -(LONGLONG)Timeout;
	KeSetTimer(&DeviceExtension->WriteTimer, DueTime, &DeviceExtension->WriteTimeoutDpc);
}

VOID
SerialFinishWrite(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension,
	IN PIRP Irp,
	IN NTSTATUS Status,
	IN OUT PLIST_ENTRY CompletedList)
{
	RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
	InitializeListHead(&Irp->Tail.Overlay.ListEntry);
	if (DeviceExtension->CurrentWriteIrp == Irp)
	{
		DeviceExtension->CurrentWriteIrp = NULL;
		KeCancelTimer(&DeviceExtension->WriteTimer);
	}
	if (IoSetCancelRoutine(Irp, NULL))
	{
		Irp->IoStatus.Status = Status;
		InsertTailList(CompletedList, &Irp->Tail.Overlay.ListEntry);
	}
}

PIRP
SerialCurrentWrite(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension,
	IN OUT PLIST_ENTRY CompletedList)
{
	PIO_STACK_LOCATION Stack;
	ULONGLONG Timeout;
	PIRP Irp;

	while (!DeviceExtension->CurrentWriteIrp && !IsListEmpty(&DeviceExtension->WriteQueue))
	{
		Irp = CONTAINING_RECORD(DeviceExtension->WriteQueue.Flink, IRP, Tail.Overlay.ListEntry);
		Stack = IoGetCurrentIrpStackLocation(Irp);
		if (Stack->MajorFunction != IRP_MJ_WRITE)
		{
			SerialFinishWrite(DeviceExtension, Irp, STATUS_SUCCESS, CompletedList);
			continue;
		}

		DeviceExtension->CurrentWriteIrp = Irp;
		DeviceExtension->WriteDeadline = 0;
		Timeout = (ULONGLONG)DeviceExtension->SerialTimeOuts.WriteTotalTimeoutMultiplier * Stack->Parameters.Write.Length
			+ DeviceExtension->SerialTimeOuts.WriteTotalTimeoutConstant;
		if (Timeout)
		{
			if (Timeout > MAXLONGLONG / 10000)
				Timeout = MAXLONGLONG / 10000;
			DeviceExtension->WriteDeadline = KeQueryInterruptTime() + Timeout * 10000;
			SerialArmWriteTimer(DeviceExtension, Timeout * 10000);
		}
	}
	return DeviceExtension->CurrentWriteIrp;
}

VOID
SerialCompleteWrites(
	IN PLIST_ENTRY CompletedList)
{
	PIRP Irp;

	while (!IsListEmpty(CompletedList))
	{
		Irp = CONTAINING_RECORD(RemoveHeadList(CompletedList), IRP, Tail.Overlay.ListEntry);
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
	}
}

VOID
SerialCancelWrites(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension)
{
	LIST_ENTRY CompletedList;
	KIRQL Irql;

	InitializeListHead(&CompletedList);
	KeAcquireSpinLock(&DeviceExtension->OutputBufferLock, &Irql);
	while (!IsListEmpty(&DeviceExtension->WriteQueue))
	{
		SerialFinishWrite(DeviceExtension,
			CONTAINING_RECORD(DeviceExtension->WriteQueue.Flink, IRP, Tail.Overlay.ListEntry),
			STATUS_CANCELLED, &CompletedList);
	}
	KeReleaseSpinLock(&DeviceExtension->OutputBufferLock, Irql);
	SerialCompleteWrites(&CompletedList);
}

ULONG
SerialQueuedWriteBytes(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension)
{
	PLIST_ENTRY Entry;
	PIO_STACK_LOCATION Stack;
	PIRP Irp;
	ULONG Bytes = 0;

	for (Entry = DeviceExtension->WriteQueue.Flink; Entry != &DeviceExtension->WriteQueue; Entry = Entry->Flink)
	{
		Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
		Stack = IoGetCurrentIrpStackLocation(Irp);
		if (Stack->MajorFunction == IRP_MJ_WRITE)
			Bytes += Stack->Parameters.Write.Length - (ULONG)Irp->IoStatus.Information;
	}
	return Bytes;
}

VOID NTAPI
SerialWriteTimeout(
	IN PKDPC Dpc,
	IN PVOID pDeviceExtension,
	IN PVOID Unused1,
	IN PVOID Unused2)
{
	PSERIAL_DEVICE_EXTENSION DeviceExtension = (PSERIAL_DEVICE_EXTENSION)pDeviceExtension;
	LIST_ENTRY CompletedList;
	ULONGLONG Now;
	KIRQL Irql;

	InitializeListHead(&CompletedList);
	KeAcquireSpinLock(&DeviceExtension->OutputBufferLock, &Irql);
	if (DeviceExtension->CurrentWriteIrp && DeviceExtension->WriteDeadline)
	{
		Now = KeQueryInterruptTime();
		if (Now >= DeviceExtension->WriteDeadline)
			SerialFinishWrite(DeviceExtension, DeviceExtension->CurrentWriteIrp, STATUS_TIMEOUT, &CompletedList);
		else if (KeReadStateTimer(&DeviceExtension->WriteTimer))
			SerialArmWriteTimer(DeviceExtension, DeviceExtension->WriteDeadline - Now);
	}
	KeReleaseSpinLock(&DeviceExtension->OutputBufferLock, Irql);

	SerialCompleteWrites(&CompletedList);
	KeInsertQueueDpc(&DeviceExtension->SendByteDpc, NULL, NULL);
}

NTSTATUS NTAPI
SerialWrite(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	PIO_STACK_LOCATION Stack;
	PSERIAL_DEVICE_EXTENSION DeviceExtension;
	NTSTATUS Status;

	TRACE_(SERIAL, "IRP_MJ_WRITE\n");

	Stack = IoGetCurrentIrpStackLocation(Irp);
	DeviceExtension = (PSERIAL_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

	if (Stack->Parameters.Write.Length == 0)
		Status = STATUS_SUCCESS;
	else if (SerialGetUserBuffer(Irp) == NULL)
		Status = STATUS_INVALID_PARAMETER;
	else
		return SerialQueueWrite(DeviceExtension, Irp);

	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = Status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return Status;
}

NTSTATUS NTAPI
SerialFlush(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	TRACE_(SERIAL, "IRP_MJ_FLUSH_BUFFERS\n");

	return SerialQueueWrite((PSERIAL_DEVICE_EXTENSION)DeviceObject->DeviceExtension, Irp);
}
