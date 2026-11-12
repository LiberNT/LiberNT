/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DMA alignment a PCI bus master gets from its DMA adapter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static const GUID UsbHostControllerInterface =
    { 0x3ABF6F2D, 0x71C4, 0x462A, { 0x8A, 0x92, 0x1E, 0x68, 0x61, 0xE6, 0xAF, 0x27 } };

START_TEST(HalDmaAlignment)
{
    PZZWSTR List = NULL;
    UNICODE_STRING Name;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT Pdo;
    DEVICE_DESCRIPTION Description;
    PDMA_ADAPTER Adapter;
    ULONG MapRegisters = 0;
    ULONG Alignment;
    NTSTATUS Status;

    Status = IoGetDeviceInterfaces(&UsbHostControllerInterface, NULL, 0, &List);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status) || !List || !*List)
    {
        skip(FALSE, "No USB host controller interface\n");
        if (List)
            ExFreePool(List);
        return;
    }

    RtlInitUnicodeString(&Name, List);
    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_ATTRIBUTES, &FileObject, &DeviceObject);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Pdo = IoGetDeviceAttachmentBaseRef(DeviceObject);
        ok(Pdo != NULL, "No physical device object under %wZ\n", &Name);
        if (Pdo)
        {
            RtlZeroMemory(&Description, sizeof(Description));
            Description.Version = DEVICE_DESCRIPTION_VERSION;
            Description.Master = TRUE;
            Description.ScatterGather = TRUE;
            Description.Dma64BitAddresses = TRUE;
            Description.InterfaceType = PCIBus;
            Description.MaximumLength = MAXULONG;
            Adapter = IoGetDmaAdapter(Pdo, &Description, &MapRegisters);
            ok(Adapter != NULL, "IoGetDmaAdapter failed\n");
            if (Adapter)
            {
                Alignment = Adapter->DmaOperations->GetDmaAlignment(Adapter);
                trace("GetDmaAlignment for %wZ: %lu, %lu map registers\n", &Name, Alignment, MapRegisters);
                ok_eq_ulong(Alignment, 1UL);
                Adapter->DmaOperations->PutDmaAdapter(Adapter);
            }
            ObDereferenceObject(Pdo);
        }
        ObDereferenceObject(FileObject);
    }
    ExFreePool(List);
}
