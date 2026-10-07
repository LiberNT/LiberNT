#ifndef _HIDCLASS_PCH_
#define _HIDCLASS_PCH_

#define _HIDPI_NO_FUNCTION_MACROS_
#include <wdm.h>
#include <hidpddi.h>
#include <stdio.h>
#include <hidport.h>
#include <pseh/pseh2.h>
#include <reactos/hidp_private.h>

#define HIDCLASS_TAG 'CdiH'

DEFINE_GUID(GUID_DEVINTERFACE_WINEXINPUT, 0x6c53d5fd, 0x6480, 0x440f, 0xb6, 0x18, 0x47, 0x67, 0x50, 0xc5, 0xe1, 0xa6);

typedef struct
{
    PDRIVER_OBJECT DriverObject;
    ULONG DeviceExtensionSize;
    BOOLEAN DevicesArePolled;
    PDRIVER_DISPATCH MajorFunction[IRP_MJ_MAXIMUM_FUNCTION + 1];
    PDRIVER_ADD_DEVICE AddDevice;
    PDRIVER_UNLOAD DriverUnload;
    KSPIN_LOCK Lock;

} HIDCLASS_DRIVER_EXTENSION, *PHIDCLASS_DRIVER_EXTENSION;

typedef struct
{
    //
    // hid device extension
    //
    HID_DEVICE_EXTENSION HidDeviceExtension;

    //
    // if it is a pdo
    //
    BOOLEAN IsFDO;

    //
    // driver extension
    //
    PHIDCLASS_DRIVER_EXTENSION DriverExtension;

    //
    // device description
    //
    HIDP_DEVICE_DESC DeviceDescription;

    //
    // hid attributes
    //
    HID_DEVICE_ATTRIBUTES Attributes;

} HIDCLASS_COMMON_DEVICE_EXTENSION, *PHIDCLASS_COMMON_DEVICE_EXTENSION;

typedef struct
{
    //
    // parts shared by fdo and pdo
    //
    HIDCLASS_COMMON_DEVICE_EXTENSION Common;

    //
    // device capabilities
    //
    DEVICE_CAPABILITIES Capabilities;

    //
    // hid descriptor
    //
    HID_DESCRIPTOR HidDescriptor;

    //
    // report descriptor
    //
    PUCHAR ReportDescriptor;

    //
    // device relations
    //
    PDEVICE_RELATIONS DeviceRelations;

    KSPIN_LOCK ReadLock;
    LIST_ENTRY OpenFileList;
    PIRP ReadIrp;
    PUCHAR ReadBuffer;
    ULONG ReadBufferLength;
    ULONG WaitingReads;
    BOOLEAN ReadActive;
    BOOLEAN ReadStopped;
    KEVENT ReadIdleEvent;
    KDPC ReadDpc;

} HIDCLASS_FDO_EXTENSION, *PHIDCLASS_FDO_EXTENSION;

typedef struct
{
    //
    // parts shared by fdo and pdo
    //
    HIDCLASS_COMMON_DEVICE_EXTENSION Common;

    //
    // device capabilities
    //
    DEVICE_CAPABILITIES Capabilities;

    //
    // collection index
    //
    ULONG CollectionNumber;

    //
    // device interface
    //
    UNICODE_STRING DeviceInterface;

    BOOLEAN IsXInput;

    //
    // FDO device object
    //
    PDEVICE_OBJECT FDODeviceObject;

    //
    // fdo device extension
    //
    PHIDCLASS_FDO_EXTENSION FDODeviceExtension;

} HIDCLASS_PDO_DEVICE_EXTENSION, *PHIDCLASS_PDO_DEVICE_EXTENSION;

#define HIDCLASS_QUEUED_REPORTS 32

typedef struct __HIDCLASS_FILEOP_CONTEXT__
{
    //
    // device extension
    //
    PHIDCLASS_PDO_DEVICE_EXTENSION DeviceExtension;

    LIST_ENTRY FdoListEntry;

    //
    // read irp pending list
    //
    LIST_ENTRY PendingReads;

    PUCHAR ReportQueue;
    ULONG ReportSlotLength;
    ULONG ReportQueueHead;
    ULONG ReportQueueCount;

    //
    // stop in progress indicator
    //
    BOOLEAN StopInProgress;

} HIDCLASS_FILEOP_CONTEXT, *PHIDCLASS_FILEOP_CONTEXT;

/* fdo.c */
NTSTATUS
HidClassFDO_PnP(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp);

NTSTATUS
HidClassFDO_DispatchRequest(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp);

NTSTATUS
HidClassFDO_DispatchRequestSynchronous(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp);

VOID
HidClassFDO_InitializeRead(
    IN PDEVICE_OBJECT FDODeviceObject);

NTSTATUS
HidClassFDO_StartRead(
    IN PDEVICE_OBJECT FDODeviceObject);

VOID
HidClassFDO_StopRead(
    IN PDEVICE_OBJECT FDODeviceObject,
    IN BOOLEAN Wait);

/* pdo.c */
NTSTATUS
HidClassPDO_CreatePDO(
    IN PDEVICE_OBJECT DeviceObject,
    OUT PDEVICE_RELATIONS *OutDeviceRelations);

NTSTATUS
HidClassPDO_PnP(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp);

PHIDP_COLLECTION_DESC
HidClassPDO_GetCollectionDescription(
    PHIDP_DEVICE_DESC DeviceDescription,
    ULONG CollectionNumber);

PHIDP_REPORT_IDS
HidClassPDO_GetReportDescription(
    PHIDP_DEVICE_DESC DeviceDescription,
    ULONG CollectionNumber);

PHIDP_REPORT_IDS
HidClassPDO_GetReportDescriptionByReportID(
    PHIDP_DEVICE_DESC DeviceDescription,
    UCHAR ReportID);

#endif /* _HIDCLASS_PCH_ */
