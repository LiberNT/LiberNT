/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         Serial port driver
 * FILE:            drivers/dd/serial/cleanup.c
 * PURPOSE:         Serial IRP_MJ_CLEANUP operations
 *
 * PROGRAMMERS:     Hervé Poussineau (hpoussin@reactos.org)
 */

#include "serial.h"

#include <debug.h>

NTSTATUS NTAPI
SerialCleanup(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	TRACE_(SERIAL, "IRP_MJ_CLEANUP\n");
	SerialCancelWrites((PSERIAL_DEVICE_EXTENSION)DeviceObject->DeviceExtension);
	SerialAbortReads((PSERIAL_DEVICE_EXTENSION)DeviceObject->DeviceExtension);
	SerialCancelWaitOnMask((PSERIAL_DEVICE_EXTENSION)DeviceObject->DeviceExtension);
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}
