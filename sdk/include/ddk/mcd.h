/*
 * mcd.h
 *
 * Media changer driver interface
 *
 * This file is part of the w32api package.
 *
 * Contributors:
 *   Created by Casper S. Hornstrup <chorns@users.sourceforge.net>
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#ifndef __MCD_H
#define __MCD_H

#include "srb.h"
#include "scsi.h"
#include "ntddchgr.h"
#include <wmidata.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MCD_)
#define CHANGERAPI
#else
#define CHANGERAPI DECLSPEC_IMPORT
#endif

#ifdef DebugPrint
#undef DebugPrint
#endif

#if DBG
#define DebugPrint(x) ChangerClassDebugPrint x
#else
#define DebugPrint(x)
#endif

#define MAXIMUM_CHANGER_INQUIRY_DATA   252

CHANGERAPI
PVOID
NTAPI
ChangerClassAllocatePool(
  IN POOL_TYPE PoolType,
  IN ULONG NumberOfBytes);

VOID
ChangerClassDebugPrint(
  ULONG DebugPrintLevel,
  PCCHAR DebugMessage,
  ...);

CHANGERAPI
VOID
NTAPI
ChangerClassFreePool(
  IN PVOID PoolToFree);

CHANGERAPI
NTSTATUS
NTAPI
ChangerClassSendSrbSynchronous(
  IN PDEVICE_OBJECT DeviceObject,
  IN PSCSI_REQUEST_BLOCK Srb,
  IN PVOID Buffer,
  IN ULONG BufferSize,
  IN BOOLEAN WriteToDevice);


typedef NTSTATUS NTAPI
(*CHANGER_INITIALIZE)(
  IN PDEVICE_OBJECT DeviceObject);

typedef ULONG NTAPI
(*CHANGER_EXTENSION_SIZE)(
  VOID);

typedef VOID NTAPI
(*CHANGER_ERROR_ROUTINE)(
  PDEVICE_OBJECT DeviceObject,
  PSCSI_REQUEST_BLOCK Srb,
  NTSTATUS *Status,
  BOOLEAN *Retry);

typedef NTSTATUS NTAPI
(*CHANGER_COMMAND_ROUTINE)(
  IN PDEVICE_OBJECT DeviceObject,
  IN PIRP Irp);

typedef NTSTATUS NTAPI
(*CHANGER_PERFORM_DIAGNOSTICS)(
  IN PDEVICE_OBJECT DeviceObject,
  OUT PWMI_CHANGER_PROBLEM_DEVICE_ERROR ChangerDeviceError);

typedef struct _MCD_INIT_DATA {
  ULONG InitDataSize;
  CHANGER_EXTENSION_SIZE ChangerAdditionalExtensionSize;
  CHANGER_INITIALIZE ChangerInitialize;
  CHANGER_ERROR_ROUTINE ChangerError;
  CHANGER_PERFORM_DIAGNOSTICS ChangerPerformDiagnostics;
  CHANGER_COMMAND_ROUTINE ChangerGetParameters;
  CHANGER_COMMAND_ROUTINE ChangerGetStatus;
  CHANGER_COMMAND_ROUTINE ChangerGetProductData;
  CHANGER_COMMAND_ROUTINE ChangerSetAccess;
  CHANGER_COMMAND_ROUTINE ChangerGetElementStatus;
  CHANGER_COMMAND_ROUTINE ChangerInitializeElementStatus;
  CHANGER_COMMAND_ROUTINE ChangerSetPosition;
  CHANGER_COMMAND_ROUTINE ChangerExchangeMedium;
  CHANGER_COMMAND_ROUTINE ChangerMoveMedium;
  CHANGER_COMMAND_ROUTINE ChangerReinitializeUnit;
  CHANGER_COMMAND_ROUTINE ChangerQueryVolumeTags;
} MCD_INIT_DATA, *PMCD_INIT_DATA;

CHANGERAPI
NTSTATUS
NTAPI
ChangerClassInitialize(
  IN PDRIVER_OBJECT DriverObject,
  IN PUNICODE_STRING RegistryPath,
  IN PMCD_INIT_DATA MCDInitData);

#ifdef __cplusplus
}
#endif

#ifndef MODE_PAGE_DISPLAY
#define MODE_PAGE_DISPLAY 0x22
#endif

typedef struct _ELEMENT_DESCRIPTOR {
  UCHAR ElementAddress[2];
  UCHAR Full : 1;
  UCHAR Reserved1 : 1;
  UCHAR Exception : 1;
  UCHAR Accessible : 1;
  UCHAR Reserved2 : 4;
  UCHAR Reserved3;
  UCHAR AdditionalSenseCode;
  UCHAR AddSenseCodeQualifier;
  UCHAR Lun : 3;
  UCHAR Reserved4 : 1;
  UCHAR LunValid : 1;
  UCHAR IdValid : 1;
  UCHAR Reserved5 : 1;
  UCHAR NotThisBus : 1;
  UCHAR BusAddress;
  UCHAR Reserved6;
  UCHAR Reserved7 : 6;
  UCHAR Invert : 1;
  UCHAR SValid : 1;
  UCHAR SourceStorageElementAddress[2];
} ELEMENT_DESCRIPTOR, *PELEMENT_DESCRIPTOR;

typedef struct _ELEMENT_STATUS_HEADER {
  UCHAR FirstElementAddress[2];
  UCHAR NumberOfElements[2];
  UCHAR Reserved1;
  UCHAR ReportByteCount[3];
} ELEMENT_STATUS_HEADER, *PELEMENT_STATUS_HEADER;

typedef struct _ELEMENT_STATUS_PAGE {
  UCHAR ElementType;
  UCHAR Reserved1 : 6;
  UCHAR AVolTag : 1;
  UCHAR PVolTag : 1;
  UCHAR ElementDescriptorLength[2];
  UCHAR Reserved2;
  UCHAR DescriptorByteCount[3];
} ELEMENT_STATUS_PAGE, *PELEMENT_STATUS_PAGE;

typedef struct _MODE_DEVICE_CAPABILITIES_PAGE {
  UCHAR PageCode : 6;
  UCHAR Reserved1 : 1;
  UCHAR PS : 1;
  UCHAR PageLength;
  UCHAR MediumTransport : 1;
  UCHAR StorageLocation : 1;
  UCHAR IEPort : 1;
  UCHAR DataXFer : 1;
  UCHAR Reserved2 : 4;
  UCHAR Reserved3;
  UCHAR MTtoMT : 1;
  UCHAR MTtoST : 1;
  UCHAR MTtoIE : 1;
  UCHAR MTtoDT : 1;
  UCHAR Reserved4 : 4;
  UCHAR STtoMT : 1;
  UCHAR STtoST : 1;
  UCHAR STtoIE : 1;
  UCHAR STtoDT : 1;
  UCHAR Reserved5 : 4;
  UCHAR IEtoMT : 1;
  UCHAR IEtoST : 1;
  UCHAR IEtoIE : 1;
  UCHAR IEtoDT : 1;
  UCHAR Reserved6 : 4;
  UCHAR DTtoMT : 1;
  UCHAR DTtoST : 1;
  UCHAR DTtoIE : 1;
  UCHAR DTtoDT : 1;
  UCHAR Reserved7 : 4;
  UCHAR Reserved8[4];
  UCHAR XMTtoMT : 1;
  UCHAR XMTtoST : 1;
  UCHAR XMTtoIE : 1;
  UCHAR XMTtoDT : 1;
  UCHAR Reserved9 : 4;
  UCHAR XSTtoMT : 1;
  UCHAR XSTtoST : 1;
  UCHAR XSTtoIE : 1;
  UCHAR XSTtoDT : 1;
  UCHAR Reserved10 : 4;
  UCHAR XIEtoMT : 1;
  UCHAR XIEtoST : 1;
  UCHAR XIEtoIE : 1;
  UCHAR XIEtoDT : 1;
  UCHAR Reserved11 : 4;
  UCHAR XDTtoMT : 1;
  UCHAR XDTtoST : 1;
  UCHAR XDTtoIE : 1;
  UCHAR XDTtoDT : 1;
  UCHAR Reserved12 : 4;
} MODE_DEVICE_CAPABILITIES_PAGE, *PMODE_DEVICE_CAPABILITIES_PAGE;

typedef struct _MODE_ELEMENT_ADDRESS_PAGE {
  UCHAR PageCode : 6;
  UCHAR Reserved1 : 1;
  UCHAR PS : 1;
  UCHAR PageLength;
  UCHAR MediumTransportElementAddress[2];
  UCHAR NumberTransportElements[2];
  UCHAR FirstStorageElementAddress[2];
  UCHAR NumberStorageElements[2];
  UCHAR FirstIEPortElementAddress[2];
  UCHAR NumberIEPortElements[2];
  UCHAR FirstDataXFerElementAddress[2];
  UCHAR NumberDataXFerElements[2];
  UCHAR Reserved2[2];
} MODE_ELEMENT_ADDRESS_PAGE, *PMODE_ELEMENT_ADDRESS_PAGE;

typedef struct _MODE_TRANSPORT_GEOMETRY_PAGE {
  UCHAR PageCode : 6;
  UCHAR Reserved1 : 1;
  UCHAR PS : 1;
  UCHAR PageLength;
  UCHAR Flip : 1;
  UCHAR Reserved2 : 7;
  UCHAR TransportElementNumber;
} MODE_TRANSPORT_GEOMETRY_PAGE, *PMODE_TRANSPORT_GEOMETRY_PAGE;

#endif /* __MCD_H */
