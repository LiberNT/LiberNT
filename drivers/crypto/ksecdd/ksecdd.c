/*
 * PROJECT:     ReactOS Kernel Security Support Provider Interface Driver
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Driver entry point for ksecdd
 * COPYRIGHT:   Copyright 2014 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

/* INCLUDES *******************************************************************/

#include "ksecdd.h"
#include <wdmsec.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PDEVICE_OBJECT KsecDeviceObject;

static const UNICODE_STRING KsecDeviceSddl = RTL_CONSTANT_STRING(
    L"D:(A;;0x1201bf;;;WD)(A;;FA;;;SY)(A;;FA;;;BA)(A;;0x1200a9;;;RC)(A;;0x1201bf;;;AC)(A;;0x1201bf;;;S-1-15-2-2)");


/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\KsecDD");
    NTSTATUS Status;

    /* Create the KsecDD device */
    /* TODO: own the device by administrators and label it low integrity, as the reference security does */
    Status = IoCreateDeviceSecure(DriverObject,
                                  0,
                                  &DeviceName,
                                  FILE_DEVICE_KSEC,
                                  FILE_DEVICE_SECURE_OPEN,
                                  FALSE,
                                  &KsecDeviceSddl,
                                  NULL,
                                  &KsecDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create KsecDD device: 0x%lx\n", Status);
        return Status;
    }

    /* Set up dispatch table */
    DriverObject->MajorFunction[IRP_MJ_CREATE] = KsecDdDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = KsecDdDispatch;
    DriverObject->MajorFunction[IRP_MJ_READ] = KsecDdDispatch;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = KsecDdDispatch;
    DriverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION] = KsecDdDispatch;
    DriverObject->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = KsecDdDispatch;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = KsecDdDispatch;

    /* Initialize */
    KsecInitializeRandomSupport();
    KsecInitializeEncryptionSupport();
    Status = KsecInitializeBCrypt();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("KSECDD: kernel BCrypt self-test failed: 0x%08lx\n", Status);
        IoDeleteDevice(KsecDeviceObject);
        KsecDeviceObject = NULL;
        return Status;
    }

    return STATUS_SUCCESS;
}
