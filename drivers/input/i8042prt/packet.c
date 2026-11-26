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
    PIRP Irp;
    PDEVICE_OBJECT DeviceObject;
    KIRQL OldIrql;

    KeAcquireSpinLock(&DeviceExtension->PacketTimerLock, &OldIrql);
    Irp = DeviceExtension->CurrentIrp;
    DeviceObject = DeviceExtension->CurrentIrpDevice;
    ASSERT((Irp != NULL) == (DeviceObject != NULL));
    DeviceExtension->CurrentIrp = NULL;
    DeviceExtension->CurrentIrpDevice = NULL;
    KeCancelTimer(&DeviceExtension->PacketTimer);
    KeReleaseSpinLock(&DeviceExtension->PacketTimerLock, OldIrql);

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
    KIRQL TimerIrql;
    BOOLEAN TimerSignaled;
    I8042_INTERRUPT_LOCK Lock;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    KeAcquireSpinLock(&DeviceExtension->PacketTimerLock, &TimerIrql);
    TimerSignaled = KeReadStateTimer(&DeviceExtension->PacketTimer);
    if (!i8042AcquireInterrupt(DeviceExtension, &Lock))
    {
        KeReleaseSpinLock(&DeviceExtension->PacketTimerLock, TimerIrql);
        return;
    }

    if (I8042CorePacketTimedOut(TimerSignaled,
                                DeviceExtension->Packet.State != Idle))
    {
        if (DeviceExtension->Packet.Bytes && DeviceExtension->Packet.ByteCount)
            Command = DeviceExtension->Packet.Bytes[0];
        DeviceExtension->Packet.State = Idle;
        DeviceExtension->PacketResends = 0;
        TimedOut = TRUE;
    }
    i8042ReleaseInterrupt(DeviceExtension, &Lock);
    KeReleaseSpinLock(&DeviceExtension->PacketTimerLock, TimerIrql);

    if (!TimedOut)
        return;

    ERR_(I8042PRT, "No acknowledgement for command 0x%02x\n", Command);
    i8042FinishPacket(DeviceExtension, STATUS_IO_TIMEOUT);
}
