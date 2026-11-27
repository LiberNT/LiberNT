/** @file
  EFI Usb I/O Protocol as defined in UEFI specification.
  This protocol is used by code, typically drivers, running in the EFI
  boot services environment to access USB devices like USB keyboards,
  mice and mass storage devices. In particular, functions for managing devices
  on USB buses are defined here.

  Copyright (c) 2006 - 2018, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef __USB_IO_H__
#define __USB_IO_H__

//
// Global ID for the USB I/O Protocol
//
#define EFI_USB_IO_PROTOCOL_GUID \
  { \
    0x2B2F68D6, 0x0CD2, 0x44cf, {0x8E, 0x8B, 0xBB, 0xA2, 0x0B, 0x1B, 0x5B, 0x75 } \
  }

typedef struct _EFI_USB_IO_PROTOCOL EFI_USB_IO_PROTOCOL;

#pragma pack(1)

///
/// Standard Interface Descriptor
/// USB 2.0 spec, Section 9.6.5
///
typedef struct {
  UINT8    Length;
  UINT8    DescriptorType;
  UINT8    InterfaceNumber;
  UINT8    AlternateSetting;
  UINT8    NumEndpoints;
  UINT8    InterfaceClass;
  UINT8    InterfaceSubClass;
  UINT8    InterfaceProtocol;
  UINT8    Interface;
} USB_INTERFACE_DESCRIPTOR;

#pragma pack()

typedef USB_INTERFACE_DESCRIPTOR EFI_USB_INTERFACE_DESCRIPTOR;

typedef
EFI_STATUS
(EFIAPI *EFI_USB_IO_GET_INTERFACE_DESCRIPTOR)(
  IN EFI_USB_IO_PROTOCOL            *This,
  OUT EFI_USB_INTERFACE_DESCRIPTOR  *InterfaceDescriptor
  );

///
/// The EFI_USB_IO_PROTOCOL provides four basic transfers types described
/// in the USB 1.1 Specification. These include control transfer, interrupt
/// transfer, bulk transfer and isochronous transfer. The EFI_USB_IO_PROTOCOL
/// also provides some basic USB device/controller management and configuration
/// interfaces. A USB device driver uses the services of this protocol to manage USB devices.
///
struct _EFI_USB_IO_PROTOCOL {
  //
  // IO transfer
  //
  VOID                                     *UsbControlTransfer;
  VOID                                     *UsbBulkTransfer;
  VOID                                     *UsbAsyncInterruptTransfer;
  VOID                                     *UsbSyncInterruptTransfer;
  VOID                                     *UsbIsochronousTransfer;
  VOID                                     *UsbAsyncIsochronousTransfer;

  //
  // Common device request
  //
  VOID                                     *UsbGetDeviceDescriptor;
  VOID                                     *UsbGetConfigDescriptor;
  EFI_USB_IO_GET_INTERFACE_DESCRIPTOR      UsbGetInterfaceDescriptor;
  VOID                                     *UsbGetEndpointDescriptor;
  VOID                                     *UsbGetStringDescriptor;
  VOID                                     *UsbGetSupportedLanguages;

  //
  // Reset controller's parent port
  //
  VOID                                     *UsbPortReset;
};

#endif
