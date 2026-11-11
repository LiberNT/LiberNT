/*
 * PROJECT:     LiberNT PS/2 controller driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     PS/2 command packet completion
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "i8042prt.h"

#include <debug.h>

VOID
i8042FinishPacket(
    IN PPORT_DEVICE_EXTENSION DeviceExtension,
    IN NTSTATUS Result)
{
    PIRP Irp = DeviceExtension->CurrentIrp;
    PDEVICE_OBJECT DeviceObject = DeviceExtension->CurrentIrpDevice;

    ASSERT((Irp != NULL) == (DeviceObject != NULL));
    DeviceExtension->CurrentIrp = NULL;
    DeviceExtension->CurrentIrpDevice = NULL;
    KeCancelTimer(&DeviceExtension->PacketTimer);

    if (Irp != NULL)
    {
        Irp->IoStatus.Status = Result;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        IoStartNextPacket(DeviceObject, FALSE);
    }
}

VOID
i8042FailPacket(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN NTSTATUS Result)
{
    Irp->IoStatus.Status = Result;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    IoStartNextPacket(DeviceObject, FALSE);
}

/* A command byte the device never acknowledges would otherwise leave the request pending for good */
VOID
NTAPI
i8042PacketTimeoutDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2)
{
    PPORT_DEVICE_EXTENSION DeviceExtension = DeferredContext;
    BOOLEAN TimedOut = FALSE;
    UCHAR Command = 0;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (DeviceExtension->HighestDIRQLInterrupt == NULL)
        return;

    Irql = KeAcquireInterruptSpinLock(DeviceExtension->HighestDIRQLInterrupt);
    if (I8042CorePacketTimedOut(KeReadStateTimer(&DeviceExtension->PacketTimer),
                                DeviceExtension->Packet.State != Idle))
    {
        Command = DeviceExtension->Packet.Bytes[0];
        DeviceExtension->Packet.State = Idle;
        DeviceExtension->PacketResends = 0;
        TimedOut = TRUE;
    }
    KeReleaseInterruptSpinLock(DeviceExtension->HighestDIRQLInterrupt, Irql);

    if (!TimedOut)
        return;

    ERR_(I8042PRT, "No acknowledgement for command 0x%02x\n", Command);
    i8042FinishPacket(DeviceExtension, STATUS_IO_TIMEOUT);
}
