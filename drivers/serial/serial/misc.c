/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         Serial port driver
 * FILE:            drivers/dd/serial/misc.c
 * PURPOSE:         Miscellaneous operations
 *
 * PROGRAMMERS:     Hervé Poussineau (hpoussin@reactos.org)
 */
/* FIXME: call IoAcquireRemoveLock/IoReleaseRemoveLock around each I/O operation */

#include "serial.h"

#include <debug.h>


NTSTATUS NTAPI
ForwardIrpAndForget(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	PDEVICE_OBJECT LowerDevice = ((PSERIAL_DEVICE_EXTENSION)DeviceObject->DeviceExtension)->LowerDevice;

	ASSERT(LowerDevice);

	IoSkipCurrentIrpStackLocation(Irp);
	return IoCallDriver(LowerDevice, Irp);
}

typedef struct _SERIAL_IER_UPDATE
{
	PSERIAL_DEVICE_EXTENSION DeviceExtension;
	UCHAR Bits;
} SERIAL_IER_UPDATE, *PSERIAL_IER_UPDATE;

static BOOLEAN NTAPI
SerialEnableInterruptsSync(
	IN PVOID SynchronizeContext)
{
	PSERIAL_IER_UPDATE Update = (PSERIAL_IER_UPDATE)SynchronizeContext;
	PUCHAR ComPortBase = ULongToPtr(Update->DeviceExtension->BaseAddress);

	WRITE_PORT_UCHAR(SER_IER(ComPortBase), READ_PORT_UCHAR(SER_IER(ComPortBase)) | Update->Bits);
	return TRUE;
}

static VOID
SerialEnableInterrupts(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension,
	IN UCHAR Bits)
{
	SERIAL_IER_UPDATE Update;

	if (!DeviceExtension->Interrupt)
		return;
	Update.DeviceExtension = DeviceExtension;
	Update.Bits = Bits;
	KeSynchronizeExecution(DeviceExtension->Interrupt, SerialEnableInterruptsSync, &Update);
}

static BOOLEAN
SerialSynchronize(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension,
	IN PKSYNCHRONIZE_ROUTINE Routine,
	IN PVOID Context)
{
	if (!DeviceExtension->Interrupt)
		return Routine(Context);
	return KeSynchronizeExecution(DeviceExtension->Interrupt, Routine, Context);
}

typedef struct _SERIAL_WAIT_CONTEXT
{
	PSERIAL_DEVICE_EXTENSION DeviceExtension;
	PIRP Irp;
	ULONG Events;
	ULONG Mask;
	BOOLEAN Force;
	NTSTATUS Status;
} SERIAL_WAIT_CONTEXT, *PSERIAL_WAIT_CONTEXT;

static VOID
SerialRecordEvents(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension,
	IN ULONG Events)
{
	DeviceExtension->HistoryMask |= Events & DeviceExtension->WaitMask;
	if (DeviceExtension->HistoryMask && DeviceExtension->WaitOnMaskIrp)
		KeInsertQueueDpc(&DeviceExtension->CompleteIrpDpc, NULL, NULL);
}

static BOOLEAN NTAPI
SerialRecordEventsSync(
	IN PVOID SynchronizeContext)
{
	PSERIAL_WAIT_CONTEXT Context = (PSERIAL_WAIT_CONTEXT)SynchronizeContext;

	SerialRecordEvents(Context->DeviceExtension, Context->Events);
	return TRUE;
}

static VOID
SerialReportEvents(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension,
	IN ULONG Events)
{
	SERIAL_WAIT_CONTEXT Context;

	if (!(Events & DeviceExtension->WaitMask))
		return;
	Context.DeviceExtension = DeviceExtension;
	Context.Events = Events;
	SerialSynchronize(DeviceExtension, SerialRecordEventsSync, &Context);
}

static VOID
SerialFinishWait(
	IN PIRP Irp,
	IN NTSTATUS Status,
	IN ULONG Events)
{
	if (!IoSetCancelRoutine(Irp, NULL))
		return;
	if (NT_SUCCESS(Status))
	{
		*(PULONG)Irp->AssociatedIrp.SystemBuffer = Events;
		Irp->IoStatus.Information = sizeof(ULONG);
	}
	else
		Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = Status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static BOOLEAN NTAPI
SerialRemoveWaitSync(
	IN PVOID SynchronizeContext)
{
	PSERIAL_WAIT_CONTEXT Context = (PSERIAL_WAIT_CONTEXT)SynchronizeContext;

	if (Context->DeviceExtension->WaitOnMaskIrp == Context->Irp)
		Context->DeviceExtension->WaitOnMaskIrp = NULL;
	return TRUE;
}

static VOID NTAPI
SerialCancelWait(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	SERIAL_WAIT_CONTEXT Context;

	IoReleaseCancelSpinLock(Irp->CancelIrql);

	Context.DeviceExtension = (PSERIAL_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	Context.Irp = Irp;
	SerialSynchronize(Context.DeviceExtension, SerialRemoveWaitSync, &Context);

	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_CANCELLED;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static BOOLEAN NTAPI
SerialStartWaitSync(
	IN PVOID SynchronizeContext)
{
	PSERIAL_WAIT_CONTEXT Context = (PSERIAL_WAIT_CONTEXT)SynchronizeContext;
	PSERIAL_DEVICE_EXTENSION DeviceExtension = Context->DeviceExtension;

	if (!DeviceExtension->WaitMask || DeviceExtension->WaitOnMaskIrp)
	{
		Context->Status = STATUS_INVALID_PARAMETER;
	}
	else if (DeviceExtension->HistoryMask)
	{
		Context->Events = DeviceExtension->HistoryMask;
		DeviceExtension->HistoryMask = 0;
		Context->Status = STATUS_SUCCESS;
	}
	else
	{
		IoMarkIrpPending(Context->Irp);
		IoSetCancelRoutine(Context->Irp, SerialCancelWait);
		if (Context->Irp->Cancel && IoSetCancelRoutine(Context->Irp, NULL))
		{
			Context->Status = STATUS_CANCELLED;
		}
		else
		{
			DeviceExtension->WaitOnMaskIrp = Context->Irp;
			Context->Status = STATUS_PENDING;
		}
	}
	return TRUE;
}

NTSTATUS
SerialStartWait(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension,
	IN PIRP Irp,
	OUT PULONG Events)
{
	SERIAL_WAIT_CONTEXT Context;

	Context.DeviceExtension = DeviceExtension;
	Context.Irp = Irp;
	Context.Events = 0;
	SerialSynchronize(DeviceExtension, SerialStartWaitSync, &Context);
	if (Context.Status == STATUS_CANCELLED)
	{
		Irp->IoStatus.Information = 0;
		Irp->IoStatus.Status = STATUS_CANCELLED;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_PENDING;
	}
	if (Context.Status == STATUS_PENDING)
		return STATUS_PENDING;
	if (!NT_SUCCESS(Context.Status))
		return Context.Status;

	*Events = Context.Events;
	IoMarkIrpPending(Irp);
	Irp->IoStatus.Information = sizeof(ULONG);
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_PENDING;
}

static BOOLEAN NTAPI
SerialSetWaitMaskSync(
	IN PVOID SynchronizeContext)
{
	PSERIAL_WAIT_CONTEXT Context = (PSERIAL_WAIT_CONTEXT)SynchronizeContext;
	PSERIAL_DEVICE_EXTENSION DeviceExtension = Context->DeviceExtension;

	Context->Irp = DeviceExtension->WaitOnMaskIrp;
	DeviceExtension->WaitOnMaskIrp = NULL;
	DeviceExtension->WaitMask = Context->Mask;
	DeviceExtension->HistoryMask &= Context->Mask;
	return TRUE;
}

VOID
SerialSetWaitMask(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension,
	IN ULONG Mask)
{
	SERIAL_WAIT_CONTEXT Context;

	Context.DeviceExtension = DeviceExtension;
	Context.Mask = Mask;
	SerialSynchronize(DeviceExtension, SerialSetWaitMaskSync, &Context);
	if (Context.Irp)
		SerialFinishWait(Context.Irp, STATUS_SUCCESS, 0);
}

static BOOLEAN NTAPI
SerialTakeWaitSync(
	IN PVOID SynchronizeContext)
{
	PSERIAL_WAIT_CONTEXT Context = (PSERIAL_WAIT_CONTEXT)SynchronizeContext;
	PSERIAL_DEVICE_EXTENSION DeviceExtension = Context->DeviceExtension;

	Context->Irp = NULL;
	if (DeviceExtension->WaitOnMaskIrp && (Context->Force || DeviceExtension->HistoryMask))
	{
		Context->Irp = DeviceExtension->WaitOnMaskIrp;
		Context->Events = DeviceExtension->HistoryMask;
		DeviceExtension->WaitOnMaskIrp = NULL;
		DeviceExtension->HistoryMask = 0;
	}
	return TRUE;
}

VOID
SerialCancelWaitOnMask(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension)
{
	SERIAL_WAIT_CONTEXT Context;

	Context.DeviceExtension = DeviceExtension;
	Context.Force = TRUE;
	SerialSynchronize(DeviceExtension, SerialTakeWaitSync, &Context);
	if (Context.Irp)
		SerialFinishWait(Context.Irp, STATUS_CANCELLED, 0);
}

VOID NTAPI
SerialReceiveByte(
	IN PKDPC Dpc,
	IN PVOID pDeviceExtension, // real type PSERIAL_DEVICE_EXTENSION
	IN PVOID Unused1,
	IN PVOID Unused2)
{
	PSERIAL_DEVICE_EXTENSION DeviceExtension;
	PUCHAR ComPortBase;
	UCHAR Byte;
	KIRQL Irql;
	ULONG Events = 0;
	ULONG Limit;
	NTSTATUS Status;

	DeviceExtension = (PSERIAL_DEVICE_EXTENSION)pDeviceExtension;
	ComPortBase = ULongToPtr(DeviceExtension->BaseAddress);

	KeAcquireSpinLock(&DeviceExtension->InputBufferLock, &Irql);
	Limit = DeviceExtension->InputBuffer.Length * 4;
	if (GetNumberOfElementsInCircularBuffer(&DeviceExtension->InputBuffer) * 5 >= Limit)
		Limit = MAXULONG;
	while (READ_PORT_UCHAR(SER_LSR(ComPortBase)) & SR_LSR_DATA_RECEIVED)
	{
		Byte = READ_PORT_UCHAR(SER_RBR(ComPortBase));
		INFO_(SERIAL, "Byte received on COM%lu: 0x%02x\n",
			DeviceExtension->ComPort, Byte);
		if (DeviceExtension->HandFlow.FlowReplace & SERIAL_AUTO_TRANSMIT)
		{
			if (Byte == DeviceExtension->SpecialChars.XoffChar)
			{
				DeviceExtension->TransmitHeld = TRUE;
				continue;
			}
			if (Byte == DeviceExtension->SpecialChars.XonChar)
			{
				DeviceExtension->TransmitHeld = FALSE;
				KeInsertQueueDpc(&DeviceExtension->SendByteDpc, NULL, NULL);
				continue;
			}
		}
		Status = PushCircularBufferEntry(&DeviceExtension->InputBuffer, Byte);
		if (NT_SUCCESS(Status))
		{
			DeviceExtension->SerialPerfStats.ReceivedCount++;
			Events |= SERIAL_EV_RXCHAR;
			if (Byte == DeviceExtension->SpecialChars.EventChar)
				Events |= SERIAL_EV_RXFLAG;
		}
		else
			DeviceExtension->SerialPerfStats.BufferOverrunErrorCount++;
	}
	if (Limit != MAXULONG && GetNumberOfElementsInCircularBuffer(&DeviceExtension->InputBuffer) * 5 >= Limit)
		Events |= SERIAL_EV_RX80FULL;
	SerialUpdateReceiveFlow(DeviceExtension);
	KeSetEvent(&DeviceExtension->InputBufferNotEmpty, 0, FALSE);
	KeReleaseSpinLock(&DeviceExtension->InputBufferLock, Irql);

	if (Events)
		SerialReportEvents(DeviceExtension, Events);

	/* allow new interrupts */
	SerialEnableInterrupts(DeviceExtension, SR_IER_DATA_RECEIVED);
}

VOID
SerialUpdateReceiveFlow(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension)
{
	PUCHAR ComPortBase = ULongToPtr(DeviceExtension->BaseAddress);
	ULONG Control = DeviceExtension->HandFlow.ControlHandShake;
	ULONG Flow = DeviceExtension->HandFlow.FlowReplace;
	ULONG Used = GetNumberOfElementsInCircularBuffer(&DeviceExtension->InputBuffer);
	ULONG Free = DeviceExtension->InputBuffer.Length - Used;
	BOOLEAN Hold;

	if ((Control & SERIAL_DTR_MASK) != SERIAL_DTR_HANDSHAKE &&
	    (Flow & SERIAL_RTS_MASK) != SERIAL_RTS_HANDSHAKE &&
	    !(Flow & SERIAL_AUTO_RECEIVE))
	{
		DeviceExtension->ReceiveHeld = FALSE;
		return;
	}

	if (!DeviceExtension->ReceiveHeld && Free <= (ULONG)DeviceExtension->HandFlow.XoffLimit)
		Hold = TRUE;
	else if (DeviceExtension->ReceiveHeld && Used <= (ULONG)DeviceExtension->HandFlow.XonLimit)
		Hold = FALSE;
	else
		return;

	DeviceExtension->ReceiveHeld = Hold;
	if ((Control & SERIAL_DTR_MASK) == SERIAL_DTR_HANDSHAKE)
	{
		if (Hold)
			DeviceExtension->MCR &= ~SR_MCR_DTR;
		else
			DeviceExtension->MCR |= SR_MCR_DTR;
	}
	if ((Flow & SERIAL_RTS_MASK) == SERIAL_RTS_HANDSHAKE)
	{
		if (Hold)
			DeviceExtension->MCR &= ~SR_MCR_RTS;
		else
			DeviceExtension->MCR |= SR_MCR_RTS;
	}
	WRITE_PORT_UCHAR(SER_MCR(ComPortBase), DeviceExtension->MCR);

	if (Flow & SERIAL_AUTO_RECEIVE)
	{
		InterlockedExchange(&DeviceExtension->PendingFlowChar,
		                    Hold ? DeviceExtension->SpecialChars.XoffChar : DeviceExtension->SpecialChars.XonChar);
		KeInsertQueueDpc(&DeviceExtension->SendByteDpc, NULL, NULL);
	}
}

ULONG
SerialGetHoldReasons(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension)
{
	ULONG Control = DeviceExtension->HandFlow.ControlHandShake;
	ULONG Flow = DeviceExtension->HandFlow.FlowReplace;
	ULONG HoldReasons = 0;

	if (DeviceExtension->TransmitHeld)
		HoldReasons |= SERIAL_TX_WAITING_FOR_XON;
	if ((Flow & SERIAL_AUTO_RECEIVE) && !(Flow & SERIAL_XOFF_CONTINUE) && DeviceExtension->ReceiveHeld)
		HoldReasons |= SERIAL_TX_WAITING_XOFF_SENT;
	if ((Control & SERIAL_CTS_HANDSHAKE) && !(DeviceExtension->MSR & SR_MSR_CTS))
		HoldReasons |= SERIAL_TX_WAITING_FOR_CTS;
	if ((Control & SERIAL_DSR_HANDSHAKE) && !(DeviceExtension->MSR & SR_MSR_DSR))
		HoldReasons |= SERIAL_TX_WAITING_FOR_DSR;
	if ((Control & SERIAL_DCD_HANDSHAKE) && !(DeviceExtension->MSR & SR_MSR_DCD))
		HoldReasons |= SERIAL_TX_WAITING_FOR_DCD;
	return HoldReasons;
}

static BOOLEAN
SerialTransmitBlocked(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension)
{
	return SerialGetHoldReasons(DeviceExtension) != 0;
}

VOID NTAPI
SerialSendByte(
	IN PKDPC Dpc,
	IN PVOID pDeviceExtension, // real type PSERIAL_DEVICE_EXTENSION
	IN PVOID Unused1,
	IN PVOID Unused2)
{
	PSERIAL_DEVICE_EXTENSION DeviceExtension;
	PUCHAR ComPortBase;
	UCHAR Byte;
	KIRQL Irql;
	LIST_ENTRY CompletedList;
	ULONG Events = 0;
	PIRP Irp;

	DeviceExtension = (PSERIAL_DEVICE_EXTENSION)pDeviceExtension;
	ComPortBase = ULongToPtr(DeviceExtension->BaseAddress);
	InitializeListHead(&CompletedList);

	KeAcquireSpinLock(&DeviceExtension->OutputBufferLock, &Irql);
	if (DeviceExtension->PendingFlowChar >= 0 &&
	    (READ_PORT_UCHAR(SER_LSR(ComPortBase)) & SR_LSR_THR_EMPTY))
	{
		LONG FlowChar = InterlockedExchange(&DeviceExtension->PendingFlowChar, -1);
		if (FlowChar >= 0)
			WRITE_PORT_UCHAR(SER_THR(ComPortBase), (UCHAR)FlowChar);
	}
	Irp = SerialCurrentWrite(DeviceExtension, &CompletedList);
	while (Irp && !SerialTransmitBlocked(DeviceExtension)
		&& READ_PORT_UCHAR(SER_LSR(ComPortBase)) & SR_LSR_THR_EMPTY)
	{
		Byte = ((PUCHAR)Irp->Tail.Overlay.DriverContext[0])[Irp->IoStatus.Information++];
		WRITE_PORT_UCHAR(SER_THR(ComPortBase), Byte);
		INFO_(SERIAL, "Byte sent to COM%lu: 0x%02x\n",
			DeviceExtension->ComPort, Byte);
		DeviceExtension->SerialPerfStats.TransmittedCount++;
		DeviceExtension->TxEmptyPending = TRUE;
		if (Irp->IoStatus.Information == IoGetCurrentIrpStackLocation(Irp)->Parameters.Write.Length)
		{
			SerialFinishWrite(DeviceExtension, Irp, STATUS_SUCCESS, &CompletedList);
			Irp = SerialCurrentWrite(DeviceExtension, &CompletedList);
		}
	}
	if (!Irp && DeviceExtension->TxEmptyPending
		&& READ_PORT_UCHAR(SER_LSR(ComPortBase)) & SR_LSR_THR_EMPTY)
	{
		DeviceExtension->TxEmptyPending = FALSE;
		Events = SERIAL_EV_TXEMPTY;
	}
	if ((Irp && !SerialTransmitBlocked(DeviceExtension)) ||
	    DeviceExtension->PendingFlowChar >= 0 || DeviceExtension->TxEmptyPending)
	{
		/* allow new interrupts */
		SerialEnableInterrupts(DeviceExtension, SR_IER_THR_EMPTY);
	}
	KeReleaseSpinLock(&DeviceExtension->OutputBufferLock, Irql);

	SerialCompleteWrites(&CompletedList);
	if (Events)
		SerialReportEvents(DeviceExtension, Events);
}

VOID NTAPI
SerialCompleteWait(
	IN PKDPC Dpc,
	IN PVOID pDeviceExtension, // real type PSERIAL_DEVICE_EXTENSION
	IN PVOID Unused1,
	IN PVOID Unused2)
{
	SERIAL_WAIT_CONTEXT Context;

	Context.DeviceExtension = (PSERIAL_DEVICE_EXTENSION)pDeviceExtension;
	Context.Force = FALSE;
	SerialSynchronize(Context.DeviceExtension, SerialTakeWaitSync, &Context);
	if (Context.Irp)
		SerialFinishWait(Context.Irp, STATUS_SUCCESS, Context.Events);
}

static ULONG
SerialServiceInterruptCause(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension,
	IN UCHAR Iir)
{
	PUCHAR ComPortBase = ULongToPtr(DeviceExtension->BaseAddress);
	ULONG Events = 0;

	switch (Iir & SR_IIR_ID_MASK)
	{
		case SR_IIR_MSR_CHANGE:
		{
			UCHAR MSR, IER;
			TRACE_(SERIAL, "SR_IIR_MSR_CHANGE\n");

			MSR = READ_PORT_UCHAR(SER_MSR(ComPortBase));
			DeviceExtension->MSR = MSR;
			KeInsertQueueDpc(&DeviceExtension->SendByteDpc, NULL, NULL);
			if (MSR & SR_MSR_CTS_CHANGED)
			{
				if (MSR & SR_MSR_CTS)
					KeInsertQueueDpc(&DeviceExtension->SendByteDpc, NULL, NULL);
				else
				{
					; /* FIXME: stop transmission */
				}
				Events |= SERIAL_EV_CTS;
			}
			if (MSR & SR_MSR_DSR_CHANGED)
			{
				if (MSR & SR_MSR_DSR)
					KeInsertQueueDpc(&DeviceExtension->ReceivedByteDpc, NULL, NULL);
				else
				{
					; /* FIXME: stop reception */
				}
				Events |= SERIAL_EV_DSR;
			}
			if (MSR & SR_MSR_RI_CHANGED)
			{
				INFO_(SERIAL, "SR_MSR_RI_CHANGED changed: now %d\n", MSR & SI_MSR_RI);
				Events |= SERIAL_EV_RING;
			}
			if (MSR & SR_MSR_DCD_CHANGED)
			{
				INFO_(SERIAL, "SR_MSR_DCD_CHANGED changed: now %d\n", MSR & SR_MSR_DCD);
				Events |= SERIAL_EV_RLSD;
			}
			IER = READ_PORT_UCHAR(SER_IER(ComPortBase));
			WRITE_PORT_UCHAR(SER_IER(ComPortBase), IER | SR_IER_MSR_CHANGE);

			break;
		}
		case SR_IIR_THR_EMPTY:
		{
			TRACE_(SERIAL, "SR_IIR_THR_EMPTY\n");

			KeInsertQueueDpc(&DeviceExtension->SendByteDpc, NULL, NULL);

			break;
		}
		case SR_IIR_DATA_RECEIVED:
		{
			UCHAR IER;
			TRACE_(SERIAL, "SR_IIR_DATA_RECEIVED\n");

			IER = READ_PORT_UCHAR(SER_IER(ComPortBase));
			WRITE_PORT_UCHAR(SER_IER(ComPortBase), IER & ~SR_IER_DATA_RECEIVED);
			KeInsertQueueDpc(&DeviceExtension->ReceivedByteDpc, NULL, NULL);

			break;
		}
		case SR_IIR_ERROR:
		{
			UCHAR LSR;
			TRACE_(SERIAL, "SR_IIR_ERROR\n");

			LSR = READ_PORT_UCHAR(SER_LSR(ComPortBase));
			if (LSR & SR_LSR_OVERRUN_ERROR)
			{
				InterlockedIncrement((PLONG)&DeviceExtension->SerialPerfStats.SerialOverrunErrorCount);
				Events |= SERIAL_EV_ERR;
			}
			if (LSR & SR_LSR_PARITY_ERROR)
			{
				InterlockedIncrement((PLONG)&DeviceExtension->SerialPerfStats.ParityErrorCount);
				Events |= SERIAL_EV_ERR;
			}
			if (LSR & SR_LSR_FRAMING_ERROR)
			{
				InterlockedIncrement((PLONG)&DeviceExtension->SerialPerfStats.FrameErrorCount);
				Events |= SERIAL_EV_ERR;
			}
			if (LSR & SR_LSR_BREAK_INT)
			{
				InterlockedIncrement((PLONG)&DeviceExtension->BreakInterruptErrorCount);
				Events |= SERIAL_EV_BREAK;
			}

			break;
		}
	}

	return Events;
}

BOOLEAN NTAPI
SerialInterruptService(
	IN PKINTERRUPT Interrupt,
	IN OUT PVOID ServiceContext)
{
	PDEVICE_OBJECT DeviceObject;
	PSERIAL_DEVICE_EXTENSION DeviceExtension;
	PUCHAR ComPortBase;
	UCHAR Iir;
	ULONG Events = 0;

	DeviceObject = (PDEVICE_OBJECT)ServiceContext;
	DeviceExtension = (PSERIAL_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	ComPortBase = ULongToPtr(DeviceExtension->BaseAddress);

	Iir = READ_PORT_UCHAR(SER_IIR(ComPortBase));
	if (Iir == 0xff)
		return TRUE;
	if (Iir & SR_IIR_NO_INTERRUPT)
		return FALSE;

	while (!(Iir & SR_IIR_NO_INTERRUPT))
	{
		Events |= SerialServiceInterruptCause(DeviceExtension, Iir);
		Iir = READ_PORT_UCHAR(SER_IIR(ComPortBase));
	}

	if (Events)
		SerialRecordEvents(DeviceExtension, Events);
	return TRUE;
}
