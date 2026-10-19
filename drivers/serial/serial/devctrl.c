/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         Serial port driver
 * FILE:            drivers/dd/serial/devctrl.c
 * PURPOSE:         Serial IRP_MJ_DEVICE_CONTROL operations
 *
 * PROGRAMMERS:     Hervé Poussineau (hpoussin@reactos.org)
 */

#include "serial.h"

#include <debug.h>

#define IO_METHOD_FROM_CTL_CODE(ctlCode) (ctlCode&0x00000003)

static VOID
SerialGetUserBuffers(
	IN PIRP Irp,
	IN ULONG IoControlCode,
	OUT PVOID* BufferIn,
	OUT PVOID* BufferOut)
{
	ASSERT(Irp);
	ASSERT(BufferIn);
	ASSERT(BufferOut);

	switch (IO_METHOD_FROM_CTL_CODE(IoControlCode))
	{
		case METHOD_BUFFERED:
			*BufferIn = *BufferOut = Irp->AssociatedIrp.SystemBuffer;
			break;
		case METHOD_IN_DIRECT:
		case METHOD_OUT_DIRECT:
			*BufferIn = Irp->AssociatedIrp.SystemBuffer;
			*BufferOut = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
			break;
		case METHOD_NEITHER:
			*BufferIn = IoGetCurrentIrpStackLocation(Irp)->Parameters.DeviceIoControl.Type3InputBuffer;
			*BufferOut = Irp->UserBuffer;
			break;
		default:
			/* Should never happen */
			*BufferIn = NULL;
			*BufferOut = NULL;
			break;
	}
}

static BOOLEAN
SerialGetDivisor(
	IN ULONG BaudRate,
	OUT PUSHORT Divisor)
{
	static const ULONG StandardRates[] =
	{
		75, 110, 134, 150, 300, 600, 1200, 1800, 2400, 4800, 7200, 9600,
		14400, 19200, 38400, 56000, 57600, 115200
	};
	ULONG Clock = BAUD_CLOCK / CLOCKS_PER_BIT;
	ULONGLONG Produced, Error;
	ULONG Value;
	ULONG Index;

	if (BaudRate == 0 || BaudRate > Clock)
		return FALSE;

	Value = (Clock + BaudRate / 2) / BaudRate;
	if (Value > MAXUSHORT)
		Value = MAXUSHORT;
	*Divisor = (USHORT)Value;

	for (Index = 0; Index < ARRAYSIZE(StandardRates); Index++)
	{
		if (StandardRates[Index] == BaudRate)
			return TRUE;
	}
	if (Value == MAXUSHORT)
		return TRUE;

	Produced = (ULONGLONG)Value * BaudRate;
	Error = Produced > Clock ? Produced - Clock : Clock - Produced;
	return Error * 1000 <= Produced * 11;
}

typedef struct _SERIAL_DIVISOR_UPDATE
{
	PUCHAR ComPortBase;
	USHORT Divisor;
} SERIAL_DIVISOR_UPDATE, *PSERIAL_DIVISOR_UPDATE;

static BOOLEAN NTAPI
SerialWriteDivisorSync(
	IN PVOID SynchronizeContext)
{
	PSERIAL_DIVISOR_UPDATE Update = (PSERIAL_DIVISOR_UPDATE)SynchronizeContext;
	UCHAR Lcr;

	Lcr = READ_PORT_UCHAR(SER_LCR(Update->ComPortBase));
	WRITE_PORT_UCHAR(SER_LCR(Update->ComPortBase), Lcr | SR_LCR_DLAB);
	WRITE_PORT_UCHAR(SER_DLL(Update->ComPortBase), Update->Divisor & 0xff);
	WRITE_PORT_UCHAR(SER_DLM(Update->ComPortBase), Update->Divisor >> 8);
	WRITE_PORT_UCHAR(SER_LCR(Update->ComPortBase), Lcr);
	return TRUE;
}

NTSTATUS NTAPI
SerialSetBaudRate(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension,
	IN ULONG NewBaudRate)
{
	USHORT divisor;
	PUCHAR ComPortBase = ULongToPtr(DeviceExtension->BaseAddress);
	NTSTATUS Status = STATUS_SUCCESS;

	if (!SerialGetDivisor(NewBaudRate, &divisor))
		return STATUS_INVALID_PARAMETER;

	Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
	if (NT_SUCCESS(Status))
	{
		SERIAL_DIVISOR_UPDATE Update;
		KIRQL Irql;
		TRACE_(SERIAL, "SerialSetBaudRate(COM%lu, %lu Bauds)\n", DeviceExtension->ComPort, NewBaudRate);
		Update.ComPortBase = ComPortBase;
		Update.Divisor = divisor;
		KeAcquireSpinLock(&DeviceExtension->InputBufferLock, &Irql);
		KeAcquireSpinLockAtDpcLevel(&DeviceExtension->OutputBufferLock);
		if (DeviceExtension->Interrupt)
			KeSynchronizeExecution(DeviceExtension->Interrupt, SerialWriteDivisorSync, &Update);
		else
			SerialWriteDivisorSync(&Update);
		KeReleaseSpinLockFromDpcLevel(&DeviceExtension->OutputBufferLock);
		KeReleaseSpinLock(&DeviceExtension->InputBufferLock, Irql);

		IoReleaseRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
	}

	if (NT_SUCCESS(Status))
		DeviceExtension->BaudRate = NewBaudRate;
	return Status;
}

static NTSTATUS
SerialSetHandFlow(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension,
	IN PSERIAL_HANDFLOW HandFlow)
{
	PUCHAR ComPortBase = ULongToPtr(DeviceExtension->BaseAddress);
	KIRQL Irql;
	NTSTATUS Status;

	if ((HandFlow->ControlHandShake & SERIAL_CONTROL_INVALID) ||
	    (HandFlow->FlowReplace & SERIAL_FLOW_INVALID) ||
	    (HandFlow->ControlHandShake & SERIAL_DTR_MASK) == SERIAL_DTR_MASK ||
	    HandFlow->XonLimit < 0 ||
	    HandFlow->XoffLimit < 0 ||
	    (ULONG)HandFlow->XonLimit > DeviceExtension->InputBuffer.Length ||
	    (ULONG)HandFlow->XoffLimit > DeviceExtension->InputBuffer.Length)
	{
		return STATUS_INVALID_PARAMETER;
	}

	Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
	if (!NT_SUCCESS(Status))
		return Status;

	KeAcquireSpinLock(&DeviceExtension->InputBufferLock, &Irql);
	DeviceExtension->HandFlow = *HandFlow;
	DeviceExtension->ReceiveHeld = FALSE;
	if (HandFlow->ControlHandShake & SERIAL_DTR_MASK)
		DeviceExtension->MCR |= SR_MCR_DTR;
	else
		DeviceExtension->MCR &= ~SR_MCR_DTR;
	switch (HandFlow->FlowReplace & SERIAL_RTS_MASK)
	{
		case SERIAL_RTS_CONTROL:
		case SERIAL_RTS_HANDSHAKE:
			DeviceExtension->MCR |= SR_MCR_RTS;
			break;
		default:
			DeviceExtension->MCR &= ~SR_MCR_RTS;
			break;
	}
	WRITE_PORT_UCHAR(SER_MCR(ComPortBase), DeviceExtension->MCR);
	SerialUpdateReceiveFlow(DeviceExtension);
	KeReleaseSpinLock(&DeviceExtension->InputBufferLock, Irql);
	IoReleaseRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));

	KeInsertQueueDpc(&DeviceExtension->SendByteDpc, NULL, NULL);
	return STATUS_SUCCESS;
}

static NTSTATUS
SerialTransmitImmediate(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension,
	IN UCHAR Character)
{
	PUCHAR ComPortBase = ULongToPtr(DeviceExtension->BaseAddress);
	KIRQL Irql;
	ULONG Wait;
	NTSTATUS Status;

	Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
	if (!NT_SUCCESS(Status))
		return Status;

	Status = STATUS_IO_TIMEOUT;
	for (Wait = 0; Wait < 100 && Status == STATUS_IO_TIMEOUT; Wait++)
	{
		KeAcquireSpinLock(&DeviceExtension->OutputBufferLock, &Irql);
		if (READ_PORT_UCHAR(SER_LSR(ComPortBase)) & SR_LSR_THR_EMPTY)
		{
			WRITE_PORT_UCHAR(SER_THR(ComPortBase), Character);
			DeviceExtension->SerialPerfStats.TransmittedCount++;
			DeviceExtension->TxEmptyPending = TRUE;
			Status = STATUS_SUCCESS;
		}
		KeReleaseSpinLock(&DeviceExtension->OutputBufferLock, Irql);
		if (Status == STATUS_IO_TIMEOUT)
		{
			LARGE_INTEGER Interval;
			Interval.QuadPart = -10000;
			KeDelayExecutionThread(KernelMode, FALSE, &Interval);
		}
	}

	IoReleaseRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
	return Status;
}

NTSTATUS NTAPI
SerialSetLineControl(
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension,
	IN PSERIAL_LINE_CONTROL NewSettings)
{
	PUCHAR ComPortBase;
	UCHAR Lcr = 0;
	NTSTATUS Status;

	ASSERT(DeviceExtension);
	ASSERT(NewSettings);

	TRACE_(SERIAL, "SerialSetLineControl(COM%lu, Settings { %lu %lu %lu })\n",
		DeviceExtension->ComPort, NewSettings->StopBits, NewSettings->Parity, NewSettings->WordLength);

	/* Verify parameters */
	switch (NewSettings->WordLength)
	{
		case 5: Lcr |= SR_LCR_CS5; break;
		case 6: Lcr |= SR_LCR_CS6; break;
		case 7: Lcr |= SR_LCR_CS7; break;
		case 8: Lcr |= SR_LCR_CS8; break;
		default: return STATUS_INVALID_PARAMETER;
	}

	if (NewSettings->WordLength < 5 || NewSettings->WordLength > 8)
		return STATUS_INVALID_PARAMETER;

	switch (NewSettings->Parity)
	{
		case NO_PARITY:    Lcr |= SR_LCR_PNO; break;
		case ODD_PARITY:   Lcr |= SR_LCR_POD; break;
		case EVEN_PARITY:  Lcr |= SR_LCR_PEV; break;
		case MARK_PARITY:  Lcr |= SR_LCR_PMK; break;
		case SPACE_PARITY: Lcr |= SR_LCR_PSP; break;
		default: return STATUS_INVALID_PARAMETER;
	}

	switch (NewSettings->StopBits)
	{
		case STOP_BIT_1:
			Lcr |= SR_LCR_ST1;
			break;
		case STOP_BITS_1_5:
			if (NewSettings->WordLength != 5)
				return STATUS_INVALID_PARAMETER;
			Lcr |= SR_LCR_ST2;
			break;
		case STOP_BITS_2:
			if (NewSettings->WordLength < 6 || NewSettings->WordLength > 8)
				return STATUS_INVALID_PARAMETER;
			Lcr |= SR_LCR_ST2;
			break;
		default:
			return STATUS_INVALID_PARAMETER;
	}

	/* Update current parameters */
	ComPortBase = ULongToPtr(DeviceExtension->BaseAddress);
	Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
	if (!NT_SUCCESS(Status))
		return Status;
	WRITE_PORT_UCHAR(SER_LCR(ComPortBase), Lcr);

	/* Read junk out of RBR */
	READ_PORT_UCHAR(SER_RBR(ComPortBase));
	IoReleaseRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));

	if (NT_SUCCESS(Status))
		DeviceExtension->SerialLineControl = *NewSettings;

	return Status;
}

static
BOOLEAN
NTAPI
SerialClearPerfStats(IN PVOID SynchronizeContext)
{
    PSERIAL_DEVICE_EXTENSION DeviceExtension = SynchronizeContext;
    ASSERT(DeviceExtension);
    RtlZeroMemory(&DeviceExtension->SerialPerfStats, sizeof(SERIALPERF_STATS));
    DeviceExtension->BreakInterruptErrorCount = 0;
    return TRUE;
}

static
BOOLEAN
NTAPI
SerialGetPerfStats(IN PVOID SynchronizeContext)
{
    PIRP pIrp = SynchronizeContext;
    PSERIAL_DEVICE_EXTENSION pDeviceExtension;

    ASSERT(pIrp);
    pDeviceExtension = IoGetCurrentIrpStackLocation(pIrp)->DeviceObject->DeviceExtension;

    /*
    * we assume buffer is big enough to hold SerialPerfStats structure
    * caller must verify this
    */
    RtlCopyMemory(pIrp->AssociatedIrp.SystemBuffer,
                  &pDeviceExtension->SerialPerfStats,
                  sizeof(SERIALPERF_STATS));
    return TRUE;
}

static NTSTATUS
SerialGetCommProp(
	OUT PSERIAL_COMMPROP pCommProp,
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension)
{
	ASSERT(pCommProp);

	RtlZeroMemory(pCommProp, sizeof(SERIAL_COMMPROP));

	if (!(pCommProp->ProvSpec1 & COMMPROP_INITIALIZED))
		pCommProp->PacketLength = sizeof(SERIAL_COMMPROP);
	pCommProp->PacketVersion = 2;
	pCommProp->ServiceMask = SERIAL_SP_SERIALCOMM;
	pCommProp->CurrentRxQueue = DeviceExtension->InputBuffer.Length;
	pCommProp->ProvSubType = PST_RS232;
	pCommProp->ProvCapabilities = SERIAL_PCF_DTRDSR | SERIAL_PCF_INTTIMEOUTS | SERIAL_PCF_PARITY_CHECK
		| SERIAL_PCF_RTSCTS | SERIAL_PCF_CD | SERIAL_PCF_SETXCHAR | SERIAL_PCF_TOTALTIMEOUTS
		| SERIAL_PCF_XONXOFF;
	pCommProp->SettableParams = SERIAL_SP_BAUD | SERIAL_SP_DATABITS | SERIAL_SP_HANDSHAKING
		| SERIAL_SP_PARITY | SERIAL_SP_PARITY_CHECK | SERIAL_SP_CARRIER_DETECT | SERIAL_SP_STOPBITS;

	pCommProp->SettableBaud = SERIAL_BAUD_075 | SERIAL_BAUD_110 | SERIAL_BAUD_134_5
		| SERIAL_BAUD_150 | SERIAL_BAUD_300 | SERIAL_BAUD_600 | SERIAL_BAUD_1200
		| SERIAL_BAUD_1800 | SERIAL_BAUD_2400 | SERIAL_BAUD_4800 | SERIAL_BAUD_7200
		| SERIAL_BAUD_9600 | SERIAL_BAUD_14400 | SERIAL_BAUD_19200 | SERIAL_BAUD_38400
		| SERIAL_BAUD_56K | SERIAL_BAUD_57600 | SERIAL_BAUD_115200 | SERIAL_BAUD_USER;
	pCommProp->MaxBaud = SERIAL_BAUD_USER;

	pCommProp->SettableData = SERIAL_DATABITS_5 | SERIAL_DATABITS_6 | SERIAL_DATABITS_7 | SERIAL_DATABITS_8;
	pCommProp->SettableStopParity = SERIAL_STOPBITS_10 | SERIAL_STOPBITS_15 | SERIAL_STOPBITS_20
		| SERIAL_PARITY_NONE | SERIAL_PARITY_ODD | SERIAL_PARITY_EVEN | SERIAL_PARITY_MARK | SERIAL_PARITY_SPACE;

	pCommProp->ProvSpec2 = 0; /* Size of provider-specific data */

	return STATUS_SUCCESS;
}

static NTSTATUS
SerialGetCommStatus(
	OUT PSERIAL_STATUS pSerialStatus,
	IN PSERIAL_DEVICE_EXTENSION DeviceExtension)
{
	KIRQL Irql;

	ASSERT(pSerialStatus);
	RtlZeroMemory(pSerialStatus, sizeof(SERIAL_STATUS));

	pSerialStatus->Errors = 0;
	if (DeviceExtension->BreakInterruptErrorCount)
		pSerialStatus->Errors |= SERIAL_ERROR_BREAK;
	if (DeviceExtension->SerialPerfStats.FrameErrorCount)
		pSerialStatus->Errors |= SERIAL_ERROR_FRAMING;
	if (DeviceExtension->SerialPerfStats.SerialOverrunErrorCount)
		pSerialStatus->Errors |= SERIAL_ERROR_OVERRUN;
	if (DeviceExtension->SerialPerfStats.BufferOverrunErrorCount)
		pSerialStatus->Errors |= SERIAL_ERROR_QUEUEOVERRUN;
	if (DeviceExtension->SerialPerfStats.ParityErrorCount)
		pSerialStatus->Errors |= SERIAL_ERROR_PARITY;

	KeAcquireSpinLock(&DeviceExtension->InputBufferLock, &Irql);
	pSerialStatus->AmountInInQueue = (DeviceExtension->InputBuffer.WritePosition + DeviceExtension->InputBuffer.Length
		- DeviceExtension->InputBuffer.ReadPosition) % DeviceExtension->InputBuffer.Length;
	KeReleaseSpinLock(&DeviceExtension->InputBufferLock, Irql);

	KeAcquireSpinLock(&DeviceExtension->OutputBufferLock, &Irql);
	pSerialStatus->HoldReasons = SerialGetHoldReasons(DeviceExtension);
	pSerialStatus->AmountInOutQueue = SerialQueuedWriteBytes(DeviceExtension);
	KeReleaseSpinLock(&DeviceExtension->OutputBufferLock, Irql);

	pSerialStatus->EofReceived = FALSE; /* always FALSE */
	pSerialStatus->WaitForImmediate = FALSE; /* always FALSE */

	return STATUS_SUCCESS;
}

NTSTATUS NTAPI
SerialDeviceControl(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	PIO_STACK_LOCATION Stack;
	ULONG IoControlCode;
	PSERIAL_DEVICE_EXTENSION DeviceExtension;
	ULONG LengthIn, LengthOut;
	ULONG_PTR Information = 0;
	PVOID BufferIn, BufferOut;
	PUCHAR ComPortBase;
	NTSTATUS Status;

	TRACE_(SERIAL, "IRP_MJ_DEVICE_CONTROL dispatch\n");

	Stack = IoGetCurrentIrpStackLocation(Irp);
	LengthIn = Stack->Parameters.DeviceIoControl.InputBufferLength;
	LengthOut = Stack->Parameters.DeviceIoControl.OutputBufferLength;
	DeviceExtension = (PSERIAL_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
	ComPortBase = ULongToPtr(DeviceExtension->BaseAddress);
	IoControlCode = Stack->Parameters.DeviceIoControl.IoControlCode;
	SerialGetUserBuffers(Irp, IoControlCode, &BufferIn, &BufferOut);

	/* FIXME: need to probe buffers */
	/* FIXME: see https://web.archive.org/web/20101020230420/http://www.osronline.com/ddkx/serial/serref_61bm.htm */
	switch (IoControlCode)
	{
		case IOCTL_SERIAL_CLEAR_STATS:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_CLEAR_STATS\n");
			KeSynchronizeExecution(
				DeviceExtension->Interrupt,
				SerialClearPerfStats,
				DeviceExtension);
			Status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_CLR_DTR:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_CLR_DTR\n");
			if ((DeviceExtension->HandFlow.ControlHandShake & SERIAL_DTR_MASK) == SERIAL_DTR_HANDSHAKE)
			{
				Status = STATUS_INVALID_PARAMETER;
				break;
			}
			Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
			if (NT_SUCCESS(Status))
			{
				DeviceExtension->MCR &= ~SR_MCR_DTR;
				WRITE_PORT_UCHAR(SER_MCR(ComPortBase), DeviceExtension->MCR);
				IoReleaseRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
			}
			break;
		}
		case IOCTL_SERIAL_CLR_RTS:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_CLR_RTS\n");
			if ((DeviceExtension->HandFlow.FlowReplace & SERIAL_RTS_MASK) == SERIAL_RTS_HANDSHAKE)
			{
				Status = STATUS_INVALID_PARAMETER;
				break;
			}
			Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
			if (NT_SUCCESS(Status))
			{
				DeviceExtension->MCR &= ~SR_MCR_RTS;
				WRITE_PORT_UCHAR(SER_MCR(ComPortBase), DeviceExtension->MCR);
				IoReleaseRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
			}
			break;
		}
		case IOCTL_SERIAL_CONFIG_SIZE:
		{
			/* Obsolete on Microsoft Windows 2000+ */
			PULONG pConfigSize;
			TRACE_(SERIAL, "IOCTL_SERIAL_CONFIG_SIZE\n");
			if (LengthOut != sizeof(ULONG) || BufferOut == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				pConfigSize = (PULONG)BufferOut;
				*pConfigSize = 0;
				Information = sizeof(ULONG);
				Status = STATUS_SUCCESS;
			}
			break;
		}
		case IOCTL_SERIAL_GET_BAUD_RATE:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_GET_BAUD_RATE\n");
			if (LengthOut < sizeof(SERIAL_BAUD_RATE))
				Status = STATUS_BUFFER_TOO_SMALL;
			else if (BufferOut == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				((PSERIAL_BAUD_RATE)BufferOut)->BaudRate = DeviceExtension->BaudRate;
				Information = sizeof(SERIAL_BAUD_RATE);
				Status = STATUS_SUCCESS;
			}
			break;
		}
		case IOCTL_SERIAL_GET_CHARS:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_GET_CHARS\n");
			if (LengthOut < sizeof(SERIAL_CHARS))
				Status = STATUS_BUFFER_TOO_SMALL;
			else if (BufferOut == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				*(PSERIAL_CHARS)BufferOut = DeviceExtension->SpecialChars;
				Information = sizeof(SERIAL_CHARS);
				Status = STATUS_SUCCESS;
			}
			break;
		}
		case IOCTL_SERIAL_GET_COMMSTATUS:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_GET_COMMSTATUS\n");
			if (LengthOut < sizeof(SERIAL_STATUS))
				Status = STATUS_BUFFER_TOO_SMALL;
			else if (BufferOut == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				Status = SerialGetCommStatus((PSERIAL_STATUS)BufferOut, DeviceExtension);
				Information = sizeof(SERIAL_STATUS);
			}
			break;
		}
		case IOCTL_SERIAL_GET_DTRRTS:
		{
			PULONG pDtrRts;
			TRACE_(SERIAL, "IOCTL_SERIAL_GET_DTRRTS\n");
			if (LengthOut != sizeof(ULONG) || BufferOut == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				pDtrRts = (PULONG)BufferOut;
				*pDtrRts = 0;
				if (DeviceExtension->MCR & SR_MCR_DTR)
					*pDtrRts |= SERIAL_DTR_STATE;
				if (DeviceExtension->MCR & SR_MCR_RTS)
					*pDtrRts |= SERIAL_RTS_STATE;
				Information = sizeof(ULONG);
				Status = STATUS_SUCCESS;
			}
			break;
		}
		case IOCTL_SERIAL_GET_HANDFLOW:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_GET_HANDFLOW\n");
			if (LengthOut < sizeof(SERIAL_HANDFLOW))
				Status = STATUS_BUFFER_TOO_SMALL;
			else if (BufferOut == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				*(PSERIAL_HANDFLOW)BufferOut = DeviceExtension->HandFlow;
				Information = sizeof(SERIAL_HANDFLOW);
				Status = STATUS_SUCCESS;
			}
			break;
		}
		case IOCTL_SERIAL_GET_LINE_CONTROL:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_GET_LINE_CONTROL\n");
			if (LengthOut < sizeof(SERIAL_LINE_CONTROL))
				Status = STATUS_BUFFER_TOO_SMALL;
			else if (BufferOut == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				*((PSERIAL_LINE_CONTROL)BufferOut) = DeviceExtension->SerialLineControl;
				Information = sizeof(SERIAL_LINE_CONTROL);
				Status = STATUS_SUCCESS;
			}
			break;
		}
		case IOCTL_SERIAL_GET_MODEM_CONTROL:
		{
			PULONG pMCR;
			TRACE_(SERIAL, "IOCTL_SERIAL_GET_MODEM_CONTROL\n");
			if (LengthOut != sizeof(ULONG) || BufferOut == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				pMCR = (PULONG)BufferOut;
				*pMCR = DeviceExtension->MCR;
				Information = sizeof(ULONG);
				Status = STATUS_SUCCESS;
			}
			break;
		}
		case IOCTL_SERIAL_GET_MODEMSTATUS:
		{
			PULONG pMSR;
			TRACE_(SERIAL, "IOCTL_SERIAL_GET_MODEMSTATUS\n");
			if (LengthOut != sizeof(ULONG) || BufferOut == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				pMSR = (PULONG)BufferOut;
				*pMSR = DeviceExtension->MSR;
				Information = sizeof(ULONG);
				Status = STATUS_SUCCESS;
			}
			break;
		}
		case IOCTL_SERIAL_GET_PROPERTIES:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_GET_PROPERTIES\n");
			if (LengthOut < sizeof(SERIAL_COMMPROP))
				Status = STATUS_BUFFER_TOO_SMALL;
			else if (BufferOut == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				Status = SerialGetCommProp((PSERIAL_COMMPROP)BufferOut, DeviceExtension);
				Information = sizeof(SERIAL_COMMPROP);
			}
			break;
		}
		case IOCTL_SERIAL_GET_STATS:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_GET_STATS\n");
			if (LengthOut < sizeof(SERIALPERF_STATS))
				Status = STATUS_BUFFER_TOO_SMALL;
			else if (BufferOut == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				KeSynchronizeExecution(DeviceExtension->Interrupt,
					SerialGetPerfStats, Irp);
				Information = sizeof(SERIALPERF_STATS);
				Status = STATUS_SUCCESS;
			}
			break;
		}
		case IOCTL_SERIAL_GET_TIMEOUTS:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_GET_TIMEOUTS\n");
			if (LengthOut != sizeof(SERIAL_TIMEOUTS) || BufferOut == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				*(PSERIAL_TIMEOUTS)BufferOut = DeviceExtension->SerialTimeOuts;
				Information = sizeof(SERIAL_TIMEOUTS);
				Status = STATUS_SUCCESS;
			}
			break;
		}
		case IOCTL_SERIAL_GET_WAIT_MASK:
		{
			PULONG pWaitMask;
			TRACE_(SERIAL, "IOCTL_SERIAL_GET_WAIT_MASK\n");
			if (LengthOut != sizeof(ULONG) || BufferOut == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				pWaitMask = (PULONG)BufferOut;
				*pWaitMask = DeviceExtension->WaitMask;
				Information = sizeof(ULONG);
				Status = STATUS_SUCCESS;
			}
			break;
		}
		case IOCTL_SERIAL_IMMEDIATE_CHAR:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_IMMEDIATE_CHAR\n");
			if (LengthIn < sizeof(UCHAR) || BufferIn == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
				Status = SerialTransmitImmediate(DeviceExtension, *(PUCHAR)BufferIn);
			break;
		}
		case IOCTL_SERIAL_LSRMST_INSERT:
		{
			/* FIXME */
			ERR_(SERIAL, "IOCTL_SERIAL_LSRMST_INSERT not implemented.\n");
			Status = STATUS_NOT_IMPLEMENTED;
			break;
		}
		case IOCTL_SERIAL_PURGE:
		{
			KIRQL Irql;
			TRACE_(SERIAL, "IOCTL_SERIAL_PURGE\n");
			if (LengthIn != sizeof(ULONG) || BufferIn == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				ULONG PurgeMask = *(PULONG)BufferIn;

				Status = STATUS_SUCCESS;
				if (PurgeMask & SERIAL_PURGE_TXABORT)
					SerialCancelWrites(DeviceExtension);
				if (PurgeMask & SERIAL_PURGE_RXABORT)
					SerialAbortReads(DeviceExtension);
				if (PurgeMask & SERIAL_PURGE_RXCLEAR)
				{
					KeAcquireSpinLock(&DeviceExtension->InputBufferLock, &Irql);
					DeviceExtension->InputBuffer.ReadPosition = DeviceExtension->InputBuffer.WritePosition = 0;
					if (DeviceExtension->UartType >= Uart16550A)
					{
						/* Clear also Uart FIFO */
						Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
						if (NT_SUCCESS(Status))
						{
							WRITE_PORT_UCHAR(SER_FCR(ComPortBase), SR_FCR_CLEAR_RCVR);
							IoReleaseRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
						}
					}
					KeReleaseSpinLock(&DeviceExtension->InputBufferLock, Irql);
				}

				if (PurgeMask & SERIAL_PURGE_TXCLEAR)
				{
					KeAcquireSpinLock(&DeviceExtension->OutputBufferLock, &Irql);
					if (DeviceExtension->UartType >= Uart16550A)
					{
						/* Clear also Uart FIFO */
						Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
						if (NT_SUCCESS(Status))
						{
							WRITE_PORT_UCHAR(SER_FCR(ComPortBase), SR_FCR_CLEAR_XMIT);
							IoReleaseRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
						}
					}
					KeReleaseSpinLock(&DeviceExtension->OutputBufferLock, Irql);
				}
			}
			break;
		}
		case IOCTL_SERIAL_RESET_DEVICE:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_RESET_DEVICE\n");
			Status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_SET_BAUD_RATE:
		{
			PULONG pNewBaudRate;
			TRACE_(SERIAL, "IOCTL_SERIAL_SET_BAUD_RATE\n");
			if (LengthIn != sizeof(ULONG) || BufferIn == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				pNewBaudRate = (PULONG)BufferIn;
				Status = SerialSetBaudRate(DeviceExtension, *pNewBaudRate);
			}
			break;
		}
		case IOCTL_SERIAL_SET_BREAK_OFF:
		case IOCTL_SERIAL_SET_BREAK_ON:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_SET_BREAK_%s\n", IoControlCode == IOCTL_SERIAL_SET_BREAK_ON ? "ON" : "OFF");
			Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
			if (NT_SUCCESS(Status))
			{
				UCHAR Lcr = READ_PORT_UCHAR(SER_LCR(ComPortBase));
				if (IoControlCode == IOCTL_SERIAL_SET_BREAK_ON)
					Lcr |= SR_LCR_BRK;
				else
					Lcr &= ~SR_LCR_BRK;
				WRITE_PORT_UCHAR(SER_LCR(ComPortBase), Lcr);
				IoReleaseRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
			}
			break;
		}
		case IOCTL_SERIAL_SET_CHARS:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_SET_CHARS\n");
			if (LengthIn < sizeof(SERIAL_CHARS))
				Status = STATUS_BUFFER_TOO_SMALL;
			else if (BufferIn == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				DeviceExtension->SpecialChars = *(PSERIAL_CHARS)BufferIn;
				Status = STATUS_SUCCESS;
			}
			break;
		}
		case IOCTL_SERIAL_SET_DTR:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_SET_DTR\n");
			if ((DeviceExtension->HandFlow.ControlHandShake & SERIAL_DTR_MASK) == SERIAL_DTR_HANDSHAKE)
				Status = STATUS_INVALID_PARAMETER;
			else if (!(DeviceExtension->MCR & SR_MCR_DTR))
			{
				Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
				if (NT_SUCCESS(Status))
				{
					DeviceExtension->MCR |= SR_MCR_DTR;
					WRITE_PORT_UCHAR(SER_MCR(ComPortBase), DeviceExtension->MCR);
					IoReleaseRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
				}
			}
			else
				Status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_SET_FIFO_CONTROL:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_SET_FIFO_CONTROL\n");
			if (LengthIn != sizeof(ULONG) || BufferIn == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
				if (NT_SUCCESS(Status))
				{
					WRITE_PORT_UCHAR(SER_FCR(ComPortBase), (UCHAR)((*(PULONG)BufferIn) & 0xff));
					IoReleaseRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
				}
			}
			break;
		}
		case IOCTL_SERIAL_SET_HANDFLOW:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_SET_HANDFLOW\n");
			if (LengthIn < sizeof(SERIAL_HANDFLOW))
				Status = STATUS_BUFFER_TOO_SMALL;
			else if (BufferIn == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
				Status = SerialSetHandFlow(DeviceExtension, (PSERIAL_HANDFLOW)BufferIn);
			break;
		}
		case IOCTL_SERIAL_SET_LINE_CONTROL:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_SET_LINE_CONTROL\n");
			if (LengthIn < sizeof(SERIAL_LINE_CONTROL))
				Status = STATUS_BUFFER_TOO_SMALL;
			else if (BufferIn == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
				Status = SerialSetLineControl(DeviceExtension, (PSERIAL_LINE_CONTROL)BufferIn);
			break;
		}
		case IOCTL_SERIAL_SET_MODEM_CONTROL:
		{
			PULONG pMCR;
			TRACE_(SERIAL, "IOCTL_SERIAL_SET_MODEM_CONTROL\n");
			if (LengthIn != sizeof(ULONG) || BufferIn == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
				if (NT_SUCCESS(Status))
				{
					pMCR = (PULONG)BufferIn;
					DeviceExtension->MCR = (UCHAR)(*pMCR & 0xff);
					WRITE_PORT_UCHAR(SER_MCR(ComPortBase), DeviceExtension->MCR);
					IoReleaseRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
				}
			}
			break;
		}
		case IOCTL_SERIAL_SET_QUEUE_SIZE:
		{
			if (LengthIn < sizeof(SERIAL_QUEUE_SIZE ))
				return STATUS_BUFFER_TOO_SMALL;
			else if (BufferIn == NULL)
				return STATUS_INVALID_PARAMETER;
			else
			{
				KIRQL Irql;
				PSERIAL_QUEUE_SIZE NewQueueSize = (PSERIAL_QUEUE_SIZE)BufferIn;
				Status = STATUS_SUCCESS;
				if (NewQueueSize->InSize > DeviceExtension->InputBuffer.Length)
				{
					KeAcquireSpinLock(&DeviceExtension->InputBufferLock, &Irql);
					Status = IncreaseCircularBufferSize(&DeviceExtension->InputBuffer, NewQueueSize->InSize);
					KeReleaseSpinLock(&DeviceExtension->InputBufferLock, Irql);
				}
			}
			break;
		}
		case IOCTL_SERIAL_SET_RTS:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_SET_RTS\n");
			if ((DeviceExtension->HandFlow.FlowReplace & SERIAL_RTS_MASK) == SERIAL_RTS_HANDSHAKE)
				Status = STATUS_INVALID_PARAMETER;
			else if (!(DeviceExtension->MCR & SR_MCR_RTS))
			{
				Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
				if (NT_SUCCESS(Status))
				{
					DeviceExtension->MCR |= SR_MCR_RTS;
					WRITE_PORT_UCHAR(SER_MCR(ComPortBase), DeviceExtension->MCR);
					IoReleaseRemoveLock(&DeviceExtension->RemoveLock, ULongToPtr(DeviceExtension->ComPort));
				}
			}
			else
				Status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_SET_TIMEOUTS:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_SET_TIMEOUTS\n");
			if (LengthIn != sizeof(SERIAL_TIMEOUTS) || BufferIn == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				DeviceExtension->SerialTimeOuts = *(PSERIAL_TIMEOUTS)BufferIn;
				Status = STATUS_SUCCESS;
			}
			break;
		}
		case IOCTL_SERIAL_SET_WAIT_MASK:
		{
			PULONG pWaitMask = (PULONG)BufferIn;
			TRACE_(SERIAL, "IOCTL_SERIAL_SET_WAIT_MASK\n");

			if (LengthIn != sizeof(ULONG) || BufferIn == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				SerialSetWaitMask(DeviceExtension, *pWaitMask);
				Status = STATUS_SUCCESS;
			}
			break;
		}
		case IOCTL_SERIAL_SET_XOFF:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_SET_XOFF\n");
			DeviceExtension->TransmitHeld = TRUE;
			Status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_SET_XON:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_SET_XON\n");
			DeviceExtension->TransmitHeld = FALSE;
			KeInsertQueueDpc(&DeviceExtension->SendByteDpc, NULL, NULL);
			Status = STATUS_SUCCESS;
			break;
		}
		case IOCTL_SERIAL_WAIT_ON_MASK:
		{
			TRACE_(SERIAL, "IOCTL_SERIAL_WAIT_ON_MASK\n");

			if (LengthOut != sizeof(ULONG) || BufferOut == NULL)
				Status = STATUS_INVALID_PARAMETER;
			else
			{
				Status = SerialStartWait(DeviceExtension, Irp, (PULONG)BufferOut);
				if (Status == STATUS_PENDING)
					return STATUS_PENDING;
			}
			break;
		}
		case IOCTL_SERIAL_XOFF_COUNTER:
		{
			/* FIXME */
			ERR_(SERIAL, "IOCTL_SERIAL_XOFF_COUNTER not implemented.\n");
			Status = STATUS_NOT_IMPLEMENTED;
			break;
		}
		default:
		{
			/* Pass Irp to lower driver */
			TRACE_(SERIAL, "Unknown IOCTL code 0x%x\n", Stack->Parameters.DeviceIoControl.IoControlCode);
			IoSkipCurrentIrpStackLocation(Irp);
			return IoCallDriver(DeviceExtension->LowerDevice, Irp);
		}
	}

	Irp->IoStatus.Status = Status;
	if (Status == STATUS_PENDING)
	{
		IoMarkIrpPending(Irp);
	}
	else
	{
		Irp->IoStatus.Information = Information;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
	}
	return Status;
}
