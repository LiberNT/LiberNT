/*
 * PROJECT:     LiberNT PS/2 controller driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     PS/2 command packet completion
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "i8042prt.h"

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
